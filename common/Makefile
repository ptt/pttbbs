SRCROOT=        ..
.include "$(SRCROOT)/pttbbs.mk"

SUBDIR:=	bbs osdep sys

.if $(USE_MBBSD_CXX)
SUBDIR+=	fbs
.endif

.include <bsd.subdir.mk>
