#!/usr/bin/perl
package BBSFileHeader;
use strict;
use IO::Handle;
use Data::Dumper;

use fields qw/dir fh cache/;

sub TIEHASH
{
    my($class, $dir) = @_;
    my $self = fields::new($class);

    open $self->{fh}, "<$dir/.DIR";
    return undef if( !$self->{fh} );

    $self->{dir} = $dir;
    return $self;
}

sub FETCH
{   
    my($self, $k) = @_;

    return $self->{dir} if( $k eq 'dir' );
    return ((-s "$self->{dir}/.DIR") / 128) if( $k eq 'num' );

    my($num, $key) = $k =~ /(.*)\.(.*)/;
    my($t, %h);

    $num += $self->FETCH('num')	if( $num < 0 );

    return $self->{cache}{$num}{$key} if( $self->{cache}{$num}{$key} );

    seek($self->{fh}, $num * 128, 0);
    $self->{fh}->read($t, 128);

    if( $key eq 'isdir' ){
	my $fn = "$self->{dir}/" . $self->FETCH("$num.filename");
	return (-d $fn);
    }
    elsif( $key eq 'content' ){
	my $fn = "$self->{dir}/" . $self->FETCH("$num.filename");
	return `/bin/cat $fn`;
    }
    else{
	($h{filename}, $h{recommend}, $h{owner}, $h{date}, $h{title}) =
	    unpack('Z33cZ14Z6Z65', $t);
	$h{title} = substr($h{title}, 3);
	$self->{cache}{$num}{$_} = $h{$_}
	    foreach( 'filename', 'owner', 'date', 'title' );
	return $h{$key};
    }
}

1;
