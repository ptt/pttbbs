#!/usr/bin/perl
# $Id: blog.pl,v 1.13 2003/06/02 02:09:38 in2 Exp $
use CGI qw/:standard/;
use LocalVars;
use DB_File;
use strict;
use Data::Dumper;
use Date::Calc qw(:all);
use Template;
use HTML::Calendar::Simple;

use vars qw/@emonth @cnumber %config %attr %article %th/;

sub main
{
    my($brdname, $fn, $y, $m, $d);
    my($tmpl);

    @emonth = ('', 'January', 'February', 'March', 'April', 'May',
	       'June', 'July', 'August', 'September', 'October',
	       'November', 'December');
    @cnumber = ('零', '一', '二', '三', '四', '五', '六',
		'七', '八', '九', '十', '十一', '十二');
    
    if( !$ENV{PATH_INFO} ){
        print header(-status => 400);
        return;
    }
    if( !(($brdname, $fn) = $ENV{PATH_INFO} =~ m|^/([\w\-]+?)/([\.,\w]*)$|) ||
	!( ($fn, $y, $m, $d) = parsefn($fn) )                             ||
	!(-e "$BLOGDATA/$brdname/$fn")                                    ||
	!(tie %config, 'DB_File',
	  "$BLOGDATA/$brdname/config.db", O_RDONLY, 0666, $DB_HASH)       ||
	!(tie %attr, 'DB_File',
	  "$BLOGDATA/$brdname/attr.db", O_RDONLY, 0666, $DB_HASH)      ){
	return redirect("http://blog.ptt2.cc/blog.pl/$1/")
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
	     $ptr = $article{"$ptr.prev"}    ){
	    AddArticle('blog', $attr{"$fn.loadBlogFields"},
		       $ptr);
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
    if( $attr{"$fn.loadTopBlogs"} ){
	dodbi(sub {
	    my($dbh) = @_;
	    my($sth);
	    $sth = $dbh->prepare("select k, v from counter order by v desc ".
				 "limit 0,". $attr{"$fn.loadTopBlogs"});
	    $sth->execute();
	    while( $_  = $sth->fetchrow_hashref() ){
		push @{$th{topBlogs}}, {brdname => $_->{k},
					counter => $_->{v}};
	    }
	});
    }

    # Counter ----------------------------------------------------------------
    if( $attr{"$fn.loadCounter"} ){
	$th{counter} = dodbi(sub {
	    my($dbh) = @_;
	    my($sth, $t);
	    $dbh->do("update counter set v = v + 1 where k = '$brdname'");
	    $sth = $dbh->prepare("select v from counter where k='$brdname'");
	    $sth->execute();
	    $t = $sth->fetchrow_hashref();
	    return $t->{v} if( $t->{v} );

	    $dbh->do("insert into counter (k, v) ".
		     "values ('$brdname', 1)");
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
	#my $cal = HTML::Calendar::Simple->new({month => $m, year => $y});
	#$th{calendar} = $cal->calendar_month;
    }

    # 用 Template Toolkit 輸出
    $tmpl = Template->new({INCLUDE_PATH => '.',
			   ABSOLUTE => 0,
			   RELATIVE => 0,
			   RECURSION => 0,
			   EVAL_PERL => 0,
		       });
    chdir "$BLOGDATA/$brdname/";
    $tmpl->process($fn, \%th) ||
	print "<pre>template error: ". $tmpl->error();
}

sub AddArticle($$$)
{
    my($cl, $fields, $s) = @_;
    my($content, $short) = ();
    $content = applyfilter($article{"$s.content"}, $config{outputfilter})
	if( $fields =~ /content/i );

    $short = applyfilter($article{"$s.short"}, $config{outputfilter})
	if( $fields =~ /short/i );

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
		   }
        if( $article{"$s.title"} );
}

sub applyfilter($$)
{
    my($c, $filter) = @_;
    foreach( split(',', $filter) ){
	if( /^generic$/i ){
	    #$c =~ s/\</&lt;/gs;
	    #$c =~ s/\>/&gt;/gs;
	    #$c =~ s/\"/&quot;/gs;
	    $c =~ s/\n/<br \/>\n/gs;
	}
	elsif( /^ubb$/i ){
	    $c =~ s|\[url\](.*?)\[/url\]|<a href='\1'>\1</a>|gsi;
	    $c =~ s|\[url=(.*?)\](.*?)\[/url\]|<a href='\1'>\2</a>|gsi;
	    $c =~ s|\[email\](.*?)\[/email\]|<a href='mailto:\1'>\1</a>|gsi;
	    $c =~ s|\[b\](.*?)\[/b\]|<b>\1</b>|gsi;
	    $c =~ s|\[i\](.*?)\[/i\]|<i>\1</i>|gsi;
	    $c =~ s|\[img\](.*?)\[/img\]|<img src='\1'>|gsi;
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
    use DBI;
    use DBD::mysql;
    my $dbh = DBI->connect("DBI:mysql:database=blog;".
			   "host=localhost",
			   'root',
			   '',
			   {'RaiseError' => 1});
    eval {
	$ret = &{$func}($dbh);
    };
    $dbh->disconnect();
    print "SQL: $@\n" if( $@ );
    return $ret;
}

main();
1;

