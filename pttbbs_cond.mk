#######################################################################
#
# Require: SRCROOT is specified, to accurately access pttbbs.conf
#
#######################################################################

#######################################################################
# conditional configurations and optional modules
#######################################################################
BBSCONF:=       $(SRCROOT)/pttbbs.conf
DEF_PATTERN:=   ^[ \t]*\#[ \t]*define[ \t]*
DEF_CMD:=       grep -Ewq "${DEF_PATTERN}"
DEF_YES:=       && echo "YES" || echo ""
USE_BBSLUA!=    sh -c '${DEF_CMD}"USE_BBSLUA" ${BBSCONF} ${DEF_YES}'
USE_PFTERM!=    sh -c '${DEF_CMD}"USE_PFTERM" ${BBSCONF} ${DEF_YES}'
USE_NIOS!=      sh -c '${DEF_CMD}"USE_NIOS"   ${BBSCONF} ${DEF_YES}'
USE_CONVERT!=   sh -c '${DEF_CMD}"CONVERT"    ${BBSCONF} ${DEF_YES}'
USE_PTTUI_THREAD!=	sh -c '${DEF_CMD}"PTTUI_THREAD"    ${BBSCONF} ${DEF_YES}'
