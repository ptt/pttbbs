SRCROOT:=	../..
.include "$(SRCROOT)/pttbbs.mk"

SRCS:=	verifydb.fbs

.for SRC in $(SRCS)
$(SRC).h: $(SRC)
	flatc --cpp $(SRC) --filename-suffix .fbs

HDRS+=	$(SRC).h
.endfor

install:

all: $(HDRS)

clean:
	@rm -f $(HDRS)
