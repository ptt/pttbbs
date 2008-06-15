SUBDIR=	common mbbsd util innbbsd trans

.include <bsd.subdir.mk>

.ORDER: all-common all-mbbsd
.ORDER: all-common all-util
.ORDER: all-common all-innbbsd
.ORDER: all-common all-trans
