SRCROOT=        ..
.include "$(SRCROOT)/pttbbs.mk"

SUBDIR:=	bbs osdep sys

.if $(USE_MBBSD_CXX)
SUBDIR+=	fbs
.endif

all:

install: all
	@true

.include <bsd.subdir.mk>
