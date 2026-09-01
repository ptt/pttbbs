GMAKE!= command -v gmake 2>/dev/null || command -v gnumake 2>/dev/null || true

.if !empty(GMAKE)
.MAIN: all

.DEFAULT:
	@${GMAKE} ${.TARGETS}
.else
.error PttBBS requires GNU Make (gmake). Please install 'gmake' and try again.
.endif
