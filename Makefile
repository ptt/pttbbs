SUBDIR=	common mbbsd util service

.include <bsd.subdir.mk>

.ORDER: all-common all-mbbsd
.ORDER: all-common all-util
.ORDER: all-common all-service
