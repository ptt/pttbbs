#!/bin/sh

# Modify Makefiles to work with PMake v2.
# PMake v2 used '#if' to replace '.if'.

TESTFN=pttbbs.mk

echo "Checking directory..."

if [ ! -f $TESTFN -a -f ../$TESTFN ] ; then
    cd .. # more chance to locate it
fi

if [ ! -f $TESTFN ] ; then
    echo "Please prepare your $TESTFN first and run this script in"
    echo "same level directory with $TESTFN"
    exit 1
fi 

echo "Found $TESTFN, check pmake version..."

if ( pmake -h 2>/dev/null >/dev/null ) ; then
    echo "OK, you have pmake v2."
else
    echo "I can't find pmake v2. You are using v1 or pmake not installed."
    echo "This script only works for pmake v2."
    exit 2
fi

for X in `find . -name "Makefile"` `find . -name "*.mk"`
do
    echo "$X -> $X.orig"
    mv -f $X $X.orig
    # I'm not sure if very old sed has multiple command supporting,
    # and what about their regex, so let's be stupid here.
    cat $X.orig | sed 's/^\.if/#if/' \
	| sed 's/^\.el/#el/'| sed 's/^\.end/#end/' \
	| sed 's/^\.include/#include/' \
	> $X
done

echo "Complete. Now invoke pmake and try."
