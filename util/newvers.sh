#!/bin/sh
# $Id: newvers.sh,v 1.2 2003/06/22 14:45:17 in2 Exp $

t=`date`

cat << EOF > vers.c
char    *compile_time = "${t}";
EOF
