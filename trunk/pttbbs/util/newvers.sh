#!/bin/sh
# $Id: newvers.sh,v 1.1 2003/06/22 04:32:38 in2 Exp $

t=`date`

cat << EOF > vers.c
char    compile_time[] = "${t}";
EOF
