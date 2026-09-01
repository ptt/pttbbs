SRCROOT?=	../..
include $(SRCROOT)/pttbbs.mk

ABS_SRCROOT:=	$(shell cd $(SRCROOT) && pwd)

GO_ENV=	CC="$(CC)" CGO_LDFLAGS_ALLOW="-Wl,--gc-sections" CGO_CFLAGS="-I$(ABS_SRCROOT)/include -DBBSHOME=\"$(BBSHOME)\"" CGO_LDFLAGS="-Wl,--gc-sections -L$(ABS_SRCROOT)/common/bbs -lcmbbs -L$(ABS_SRCROOT)/common/sys -lcmsys -L$(ABS_SRCROOT)/common/osdep -losdep"

ifdef WITHOUT_UAO
GOTAGS=	-tags without_uao
endif

.PHONY: all clean test

all: viewdir

viewdir:
	$(GO_ENV) go build $(GOTAGS) -o viewdir .

clean:
	rm -f viewdir

test:
	$(GO_ENV) go test $(GOTAGS) -v ./...
