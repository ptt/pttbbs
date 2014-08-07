SUBDIR=	common mbbsd util trans

.include <bsd.subdir.mk>

.ORDER: all-common all-mbbsd
.ORDER: all-common all-util
.ORDER: all-common all-trans
