#!/bin/sh

# prevent localized logs
LC_ALL=C
export LC_ALL

t=`date`

# are we working in CVS?
if [ -d ".svn" ] ; then

    #determine branch
    branch=`svn info | grep '^URL: ' | sed 's/.*\/pttbbs\/\([a-zA-Z0-9_\-\.]*\)\/pttbbs\/.*/\1/'`
    branch="$branch "

    #determine rev
    rev=`svn info | grep Revision | sed 's/Revision: /r/'`

    if [ "$rev" != "" ]
    then
	t="$t, $branch$rev"
    fi

fi

cat << EOF > vers.c
const char * const compile_time = "${t}";
EOF
