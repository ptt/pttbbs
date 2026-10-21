SRCROOT?=	../..
include $(SRCROOT)/pttbbs.mk

ABS_SRCROOT:=	$(shell cd $(SRCROOT) && pwd)

GO_ENV=	CC="$(CC)" CGO_LDFLAGS_ALLOW="-Wl,--gc-sections" CGO_CFLAGS="-I$(ABS_SRCROOT)/include -DBBSHOME=\"$(BBSHOME)\"" CGO_LDFLAGS="-Wl,--gc-sections -L$(ABS_SRCROOT)/common/bbs -lcmbbs -L$(ABS_SRCROOT)/common/sys -lcmsys -L$(ABS_SRCROOT)/common/osdep -losdep"

.PHONY: all test install clean stat.svc stat.ctl

all: stat.svc stat.ctl

stat.svc:
	$(GO_ENV) go build -o stat.svc .

stat.ctl:
	$(GO_ENV) go build -o stat.ctl ./cmd/stat.ctl

test:
	$(GO_ENV) go test -ldflags="-linkmode=external" -v ./...

install: all
	install -d $(BBSHOME)/bin/
	install -c -m 755 stat.svc $(BBSHOME)/bin/
	install -c -m 755 stat.ctl $(BBSHOME)/bin/

clean:
	rm -f stat.svc stat.ctl
