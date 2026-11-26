SRCROOT?=	../..
include $(SRCROOT)/pttbbs.mk

ABS_SRCROOT:=	$(shell cd $(SRCROOT) && pwd)

GO_ENV=	CC="$(CC)" CGO_LDFLAGS_ALLOW="-Wl,--gc-sections" CGO_CFLAGS="-I$(ABS_SRCROOT)/include -DBBSHOME=\"$(BBSHOME)\"" CGO_LDFLAGS="-Wl,--gc-sections -L$(ABS_SRCROOT)/common/bbs -lcmbbs -L$(ABS_SRCROOT)/common/sys -lcmsys -L$(ABS_SRCROOT)/common/osdep -losdep"

.PHONY: all test install clean gem.svc gem.ctl

all: gem.svc gem.ctl

gem.svc:
	$(GO_ENV) go build -o gem.svc .

gem.ctl:
	$(GO_ENV) go build -o gem.ctl ./cmd/gem.ctl

test:
	$(GO_ENV) go test -v ./...

install: all
	install -d $(BBSHOME)/bin/
	install -c -m 755 gem.svc $(BBSHOME)/bin/
	install -c -m 755 gem.ctl $(BBSHOME)/bin/

clean:
	rm -f gem.svc gem.ctl
