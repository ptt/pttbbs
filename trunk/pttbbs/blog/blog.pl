#!/usr/bin/perl
use CGI qw/:standard/;
use LocalVars;
use DB_File;
use strict;
use Data::Dumper;
use Date::Calc qw(:all);
use Template;

sub main
{
    my($brdname, $fn, %config, %attr, %article, $y, $m, $d);
    my($tmpl, %th);

    my @emonth = ('', 'January', 'February', 'March', 'April', 'May',
		  'June', 'July', 'August', 'September', 'October',
		  'November', 'December');
    my @cnumber = ('零', '一', '二', '三', '四', '五', '六',
		   '七', '八', '九', '十', '十一', '十二');

    if( !$ENV{PATH_INFO} ){
        print header(-status => 400);
        return;
    }
    if( !(($brdname, $fn) = $ENV{PATH_INFO} =~ m|/(\w+?)/([\.,\w]*)|) ||
	!( ($fn, $y, $m, $d) = parsefn($fn) )                         ||
	!(-e "$BLOGDATA/$brdname/$fn")                                ||
	!(tie %config, 'DB_File',
	  "$BLOGDATA/$brdname/config.db", O_RDONLY, 0666, $DB_HASH)   ||
	!(tie %attr, 'DB_File',
	  "$BLOGDATA/$brdname/attr.db", O_RDONLY, 0666, $DB_HASH)      ){
	print header(-status => 404);
	return;
    }

    charset('');
    print header(-type => GetType($fn));
    $fn ||= 'index.html';

    # first, import all settings in %config
    %th = %config;

    # loadBlog ---------------------------------------------------------------
    tie %article, 'DB_File', "$BLOGDATA/$brdname.db", O_RDONLY, 0666, $DB_HASH;
    if( $attr{"$fn.loadBlog"} =~ /month/i ){
	my($s, $y1, $m1, $d1);
	for( ($y1, $m1, $d1) = ($y, $m, 32) ; $d1 > 0 ; --$d1 ){
	    AddArticle('blog', $y1, $m1, $d1,
		       \%th, \%article, \@emonth, \@cnumber);
	}
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

    # output
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

sub AddArticle($$$$$$$$)
{
    my($cl, $y, $m, $d, $th, $article, $emonth, $cnumber) = @_;
    my $s = packdate($y, $m, $d);
    push @{$th->{$cl}}, {year   => $y,
		       month  => $m,
		       emonth => $emonth->[$m],
		       cmonth => $cnumber->[$m],
		       day    => $d,
		       key    => $s,
		       title  => $article->{"$s.title"},
		       body   => $article->{"$s.content"},
		       author => $article->{"$s.author"}}
    if( $article->{"$s.title"} );
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

main();
1;

