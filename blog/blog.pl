#!/usr/bin/perl
# $Id$
use CGI qw/:standard/;
use lib qw/./;
use LocalVars;
use DB_File;
use strict;
use Data::Dumper;
use Date::Calc qw(:all);
use Template;
use OurNet::FuzzyIndex;
use DBI;
use DBD::mysql;
use POSIX;
use MD5;
use Mail::Sender;
use Data::Serializer;
use Encode;

use vars qw/@emonth @cnumber %config %attr %article %th $dbh $brdname/;

sub main
{
    my($fn, $y, $m, $d, $ofn);
    my($tmpl);

    $dbh = undef;
    @emonth = ('', 'January', 'February', 'March', 'April', 'May',
	       'June', 'July', 'August', 'September', 'October',
	       'November', 'December');
    @cnumber = ('零', '一', '二', '三', '四', '五', '六',
		'七', '八', '九', '十', '十一', '十二');
    
    if( $brdname = param('searchboard') ){
        dodbi(sub {
            my($dbh) = @_;
            my($sth);
            $sth = $dbh->prepare("select k from counter where k='$brdname'");
            $sth->execute();
	    $brdname = (($sth = $sth->fetchrow_hashref()) ?
			$sth->{k} : 'Blog');
        });
        return redirect("/blog.pl/$brdname/");
    }
    
    if( !$ENV{PATH_INFO} ){
        print header(-status => 400);
        return;
    }
    if( !(($brdname, $ofn) = $ENV{PATH_INFO} =~ m|^/([\w\-]+?)/([\.,\w]*)$|) ||
	!( ($fn, $y, $m, $d) = parsefn($ofn) )                            ||
	!(-e "$BLOGDATA/$brdname/$fn")                                    ||
	!(tie %config, 'DB_File',
	  "$BLOGDATA/$brdname/config.db", O_RDONLY, 0666, $DB_HASH)       ||
	!(tie %attr, 'DB_File',
	  "$BLOGDATA/$brdname/attr.db", O_RDONLY, 0666, $DB_HASH)      ){
	return redirect("/blog.pl/$1/")
	    if( $ENV{PATH_INFO} =~ m|^/([\w\-]+?)$| );
	print header(-status => 404);
	return;
    }

    charset('');
    print header(-type => GetType($fn));
    $fn ||= 'index.html';

    # first, import all settings in %config
    %th = %config;
    $th{BOARDNAME} = $brdname;
    $th{key} = $y * 10000 + $m * 100 + $d;

    # loadBlog ---------------------------------------------------------------
    tie %article, 'DB_File', "$BLOGDATA/$brdname.db", O_RDONLY, 0666, $DB_HASH;
    if( $attr{"$fn.loadBlog"} =~ /article/i ){
	AddArticle('blog', $attr{"$fn.loadBlogFields"}, packdate($y, $m, $d));
    }
    elsif( $attr{"$fn.loadBlog"} =~ /monthly/i ){
	my($s, $y1, $m1, $d1);
	for( ($y1, $m1, $d1) = ($y, $m, 32) ; $d1 > 0 ; --$d1 ){
	    AddArticle('blog', $attr{"$fn.loadBlogFields"},
		       packdate($y1, $m1, $d1));
	}
    }
    elsif( $attr{"$fn.loadBlog"} =~ /^last(\d+)/i ){
	my($ptr, $i);
	for( $ptr = $article{last}, $i = 0 ;
	     $ptr && $i < $1               ;
	     $ptr = $article{"$ptr.prev"}, ++$i    ){
	    AddArticle('blog', $attr{"$fn.loadBlogFields"},
		       $ptr);
	}
    }
    elsif( $attr{"$fn.loadBlog"} =~ /FuzzySearch/i ){
	my $idx = OurNet::FuzzyIndex->new("$BLOGDATA/$brdname.idx");
	my %result = $idx->query($th{SearchKey} = param('SearchKey'),
				 MATCH_FUZZY);
	foreach my $t (sort { $result{$b} <=> $result{$a} } keys(%result)) {
	    AddArticle('blog', $attr{"$fn.loadBlogFields"},
		       $idx->getkey($t), sprintf("%5.1f", $result{$t} / 10));
	}
    }

    if( $attr{"$fn.loadBlogPrevNext"} ){
	my $s = packdate($y, $m, $d);
	AddArticle('next', $attr{"$fn.loadBlogPrevNext"},
		   $article{"$s.next"});
	AddArticle('prev', $attr{"$fn.loadBlogPrevNext"},
		   $article{"$s.prev"});
    }

    # loadArchives -----------------------------------------------------------
    if( $attr{"$fn.loadArchives"} =~ /^monthly/i ){
	# 找尋 +-1 year 內有資料的月份
	my($c, $y1, $m1);
	for( $c = 0, ($y1, $m1) = ($y + 1, $m) ;
	     $c < 48                           ;
	     ++$c, --$m1                         ) {

	    if( $m1 == 0 ){ $m1 = 12; --$y1; }
	    if( $article{ sprintf('%04d%02d', $y1, $m1) } ){
		push @{$th{Archives}}, {year   => $y1, month  => $m1,
					emonth => $emonth[$m1],
					cmonth => $cnumber[$m1],
					key    => packdate($y1, $m1, 1)};
	    }
	}
    }

    # loadRecentEntries ------------------------------------------------------
    if( $attr{"$fn.loadRecentEntries"} ){
	my($i, $ptr, $y, $m, $d);
	print $attr{"$fn.loadRecentEntries:"};
	for( $i = 0, $ptr = $article{'last'}             ;
	     $ptr && $i < $attr{"$fn.loadRecentEntries"} ;
	     ++$i, $ptr = $article{"$ptr.prev"}            ){
	    ($y, $m, $d) = unpackdate($ptr);
	    push @{$th{RecentEntries}}, {year => $y, month => $m,
					 emonth => $emonth[$m],
					 cmonth => $cnumber[$m],
					 title  => $article{"$ptr.title"},
					 key => $ptr};
	}
    }

    # topBlogs
    my($t);
    foreach $t ( ['loadTopBlogs', 'v', 'topBlogs', 'counter'],
		 ['loadTopWeekBlogs', 'v', 'topWeekBlogs', 'wcounter'],
		 ['loadRandomBlogs', 'RAND()', 'randomBlogs', 'counter'],
		 ){
	if( $attr{"$fn.$t->[0]"} ){
	    dodbi(sub {
		my($dbh) = @_;
		my($sth);
		$sth = $dbh->prepare("select k, v from $t->[3] ".
				     "order by $t->[1] desc ".
				     ($attr{"$fn.$t->[0]"} eq 'all' ? '' :
				      'limit 0,'. $attr{"$fn.$t->[0]"}));
		$sth->execute();
		while( $_  = $sth->fetchrow_hashref() ){
		    push @{$th{$t->[2]}}, {brdname => $_->{k},
					   counter => $_->{v}};
		}
	    });
	}
    }

    # Counter ----------------------------------------------------------------
    if( $attr{"$fn.loadCounter"} ){
	$th{counter} = dodbi(sub {
	    my($dbh) = @_;
	    my($sth, $t, $time);
	    $time = time();
	    $dbh->do("update counter set v = v + 1, mtime = $time ".
		     "where k = '$brdname' && mtime < ". ($time - 2));
	    $dbh->do("update wcounter set v = v + 1, mtime = $time ".
		     "where k = '$brdname' && mtime < ". ($time - 2));
	    $sth = $dbh->prepare("select v from counter where k='$brdname'");
	    $sth->execute();
	    $t = $sth->fetchrow_hashref();
	    return $t->{v} if( $t->{v} );

	    $dbh->do("insert into counter (k, v) values ('$brdname', 1)");
	    $dbh->do("insert into wcounter (k, v) values ('$brdname', 1)");
	    return 1;
	});
    }

    # Calendar ---------------------------------------------------------------
    if( $attr{"$fn.loadCalendar"} ){
	# 沒有合適的 module , 自己寫一個 |||b
	my($c, $week, $day, $t, $link, $newtr);
	$c = ("<table border=\"0\" cellspacing=\"4\" cellpadding=\"0\">\n".
	      "<caption class=\"calendarhead\">$emonth[$m] $y</caption>\n".
	      "<tr>\n");
	$c .= ("<th abbr=\"$_->[0]\" align=\"center\">".
	       "<span class=\"calendar\">$_->[1]</span></th>\n")
	    foreach( ['Sunday', 'Sun'], ['Monday', 'Mon'],
		     ['Tuesday', 'Tue'], ['Wednesday', 'Wed'],
		     ['Thursday', 'Thu'], ['Friday', 'Fri'],
		     ['Saturday', 'Sat'] );

	$week = Day_of_Week($y, $m, 1);
	$c .= "</tr>\n<tr>\n";

	if( $week == 7 ){
	    $week = 0;
	}
	else{
	    $c .= ("<th abbr=\"null\" align=\"center\"><span class=\"calendar\">".
		   "&nbsp;</span></th>\n")
		foreach( 1..$week );
	}
	foreach( 1..31 ){
	    last if( !check_date($y, $m, $_) );
	    $c .= "<tr>\n" if( $newtr );
	    $c .= "<th abbr=\"$_\" align=\"center\"><span class=\"calendar\">";

	    $t = packdate($y, $m, $_);
	    if( !$article{"$t.title"} ){
		$c .= "$_";
	    }
	    else{
		my $link = $attr{"$fn.loadCalendar"};
		$link =~ s/\[\% key \%\]/$t/g;
		$c .= "<a href=\"$link\">$_</a>";
	    }
		   
	    $c .= "</span></th>\n";
	    if( ++$week == 7 ){
		$c .= "</tr>\n\n";
		$week = 0;
		$newtr = 1;
	    }
	    else{
		$newtr = 0;
	    }
	}

	$c .= "</tr>\n" if( !$newtr );
	$c .= "</table>\n";
	$th{calendar} = $c;
    }

    # Comments ---------------------------------------------------------------
    if( $attr{"$fn.loadRecentComments"} ){
	dodbi(sub {
	    my($dbh) = @_;
	    my($sth, $t);
	    $sth = $dbh->prepare("select artid,name,mail,mtime ".
				 "from comment ".
				 "where brdname='$brdname' ".
				 "order by mtime desc ".
				 "LIMIT 0,". $attr{"$fn.loadRecentComments"});
	    $sth->execute();
	    while( $t = $sth->fetchrow_hashref() ){
		$t->{title} = $article{"$t->{artid}.title"};
		$t->{key} = $t->{artid};
		$t->{time} = POSIX::strftime('%D', localtime($t->{mtime}));
		push @{$th{RecentComments}}, $t;
	    }
	});
    }

    if( $attr{"$fn.loadComments"} ){
	my($name, $mail, $comment) = (param('name'),
				      param('mail'), param('comment'));

	if( $name && $comment ){
	    if( $attr{"$fn.loadComments"} =~ /\@/ ){
		my $sr = new Mail::Sender{smtp => 'localhost'};
		$sr->MailMsg({from    => '批踢踢部落格 <blog@ptt.cc>',
			      to      => $attr{"$fn.loadComments"},
			      subject => "您的部落格收到 $name 給您的迴響",
			      charset => 'big5',
			      msg     =>  "
您的部落格 http://blog.ptt2.cc/blog.pl/$brdname/$ofn
剛才收到來自 $name <$mail> 給您的迴響
--------------------------------------------------------------------
$comment
--------------------------------------------------------------------
 (這封信件是由程式自動發出, 請不要直接回複這封信^^)
",
			  });
	    }
	    dodbi(sub {
		my($dbh) = @_;
		my($t, $hash);
		$t = time();
		$name = $dbh->quote($name);
		$mail = $dbh->quote($mail);
		$comment = $dbh->quote($comment);
		$hash = MD5->hexhash("$t$th{key}$name$mail$comment");
		$dbh->do('insert into comment '.
			 '(brdname, artid, name, mail, content, mtime, hash) '.
			 "values ('$brdname', '$th{key}', $name, $mail, ".
			 "$comment, '$t', '$hash')");
	    });
	}

	dodbi(sub {
	    my($dbh) = @_;
	    my($sth, $t);
	    $sth = $dbh->prepare("select mtime,name,mail,content,hash ".
				 "from comment ".
				 "where brdname='$brdname'&&artid='$th{key}' ".
				 "order by mtime desc");
	    $sth->execute();
	    while( $t = $sth->fetchrow_hashref() ){
		$t->{time} = POSIX::ctime($t->{mtime});
		$t->{content} = applyfilter($t->{content},
					    $config{outputfilter});
		push @{$th{comment}}, $t;
	    }
	});
    }

    # serialized -------------------------------------------------------------
    if( $attr{"$fn.loadSerialized"} ){
	my($obj, %h, $str);
	$obj = Data::Serializer->new(serializer => 'Storable',
				     digester   => 'MD5',
				     compress   => 0,
				     );
	open FH, '<'.$attr{"$fn.loadSerialized"};
	FH->read($str, -s $attr{"$fn.loadSerialized"});
	close FH;
	%h = %{$obj->deserialize($str)};
	$th{$_} = $h{$_} foreach( keys %h );
    }

    # 用 Template Toolkit 輸出
    $th{LANG} =~ s/zh_TW/zh-TW/;
    mkdir "$BLOGCACHE/$brdname";
    $tmpl = Template->new({INCLUDE_PATH => '.',
			   ABSOLUTE => 0,
			   RELATIVE => 0,
			   RECURSION => 0,
			   EVAL_PERL => 0,
			   COMPILE_EXT => '.pl',
			   COMPILE_DIR => "$BLOGCACHE/$brdname/",
		       });
    chdir "$BLOGDATA/$brdname/";
    $tmpl->process($fn, \%th) ||
	print "<pre>template error: ". $tmpl->error();
    $dbh->disconnect() if( $dbh );

    untie %attr if( %attr );
    untie %config if( %config );
    untie %article if( %article );
    undef $tmpl;
}

sub utf8dump($;$)
{
    my($str, $prefix) = @_;
    my $ret = $prefix || '';
    my $ostr = $str;
    Encode::from_to($str, 'big5', 'utf-8');
    $ret .= '%'. sprintf('%x', ord($_))
	foreach( split(//, $str) );
    return "<a href=\"$ret\">$ostr</a>";
}

sub AddArticle($$$;$)
{
    my($cl, $fields, $s, $score) = @_;
    my($content, $short, $nComments) = ();
    $content = applyfilter($article{"$s.content"}, $config{outputfilter})
	if( $fields =~ /content/i );

    $short = applyfilter($article{"$s.short"}, $config{outputfilter})
	if( $fields =~ /short/i );

    if( $fields =~ /nComments/i ){
	$nComments = dodbi(sub {
	    my($dbh) = @_;
	    my $sth = $dbh->prepare("select count(*) from comment ".
				    "where brdname='$brdname'&&artid='$s'");
	    $sth->execute();
	    return $sth->fetchrow_hashref()->{'count(*)'};
	}) || 0;
    }

    my($y, $m, $d) = unpackdate($s);
    push @{$th{$cl}}, {year   => $y,
		       month  => $m,
		       emonth => $emonth[$m],
		       cmonth => $cnumber[$m],
		       day    => $d,
		       key    => $s,
		       title  => (($fields !~ /title/i) ? '' :
				  $article{"$s.title"}),
		       content=> $content,
		       author => (($fields !~ /author/i) ? '' :
				  $article{"$s.author"}),
		       short  => $short,
		       score  => $score,
		       nComments => $nComments,
		   }
        if( $article{"$s.title"} );
}

sub applyfilter($$)
{
    my($c, $filter) = @_;
    foreach( split(',', $filter) ){
	if( /^generic$/i ){
	    $c =~ s/\n/<br \/>\n/gs;
	}
	elsif( /^strict$/i ){
	    $c =~ s/\</&lt;/gs;
	    $c =~ s/\>/&gt;/gs;
	    $c =~ s/\"/&quot;/gs;
	    $c =~ s/\'/&apos;/gs;
#	    $c =~ s/ /&nbsp;/gs;
	}
	elsif( /^ubb$/i ){
	    $c =~ s|\[url\](.*?)\[/url\]|<a href="$1">$1</a>|gsi;
	    $c =~ s|\[url=(.*?)\](.*?)\[/url\]|<a href="$1">$2</a>|gsi;
	    $c =~ s|\[email\](.*?)\[/email\]|<a href="mailto:$1">$1</a>|gsi;
	    $c =~ s|\[b\](.*?)\[/b\]|<b>$1</b>|gsi;
	    $c =~ s|\[i\](.*?)\[/i\]|<i>$1</i>|gsi;
	    $c =~ s|\[img\](.*?)\[/img\]|<img src="$1" alt="(null)" style="border:0;" />|gsi;
	}
	elsif( /^wiki$/i ){
	    my $t;
	    $c =~ s|\[(http://\S+) (.*?)\]|\[ <a href=\"$1\">$2</a> \]|gi;
	    $c =~ s|([^\>\"])(http://\S+\.(:?jpg\|gif\|png\|bmp))|$1<a href=\"$2\"><img src=\"$2\" alt="$2" style="border:0;"></a>|gsi;
	    $c =~ s|([^\>\"])(http://\S+)|$1<a href=\"$2\">$2</a>|gsi;
	    $c =~ s|\(\((.*?)\)\)|utf8dump($1, $th{wikibase})|gsie;
	    $c =~ s|^\-{4,}$|<hr />|gm;
	}
    }
    return $c;
}

sub parsefn($)
{
    my($fs) = @_;
    return ("$1.$3", unpackdate($2))
	if( $fs =~ /^(.*),(\w+)\.(.*)$/ );
    return ($fs, Today());
}

sub GetType($)
{
    my($f) = @_;
    return 'text/css' if( $f =~ /.css$/i );
    return 'text/html';
}

sub packdate($$$)
{
    return $_[0] * 10000 + $_[1] * 100 + $_[2];
}

sub unpackdate($)
{
    return (int($_[0] / 10000),
	    (int($_[0] / 100)) % 100,
	    $_[0] % 100);
}

sub dodbi
{
    my($func) = @_;
    my($ret);
    my $dbh = DBI->connect("DBI:mysql:database=$BLOGdbname;".
			   "host=$BLOGdbhost",
			   $BLOGdbuser, $BLOGdbpasswd,
			   {'RaiseError' => 1})
	if( !$dbh );
    eval {
	$ret = &{$func}($dbh);
    };
    print "SQL: $@\n" if( $@ );
    return $ret;
}

main();
1;

