SRCROOT?=	../..
include $(SRCROOT)/pttbbs.mk

ABS_SRCROOT:=	$(shell cd $(SRCROOT) && pwd)

GO_ENV=	CC="$(CC)" CGO_LDFLAGS_ALLOW="-Wl,--gc-sections" CGO_CFLAGS="-I$(ABS_SRCROOT)/include -DBBSHOME=\"$(BBSHOME)\"" CGO_LDFLAGS="-Wl,--gc-sections -L$(ABS_SRCROOT)/common/bbs -lcmbbs -L$(ABS_SRCROOT)/common/sys -lcmsys -L$(ABS_SRCROOT)/common/osdep -losdep"

.PHONY: all test install clean nest.svc nest.ctl

all: nest.svc nest.ctl

nest.svc:
	$(GO_ENV) go build -o nest.svc .

nest.ctl:
	$(GO_ENV) go build -o nest.ctl ./cmd/nest.ctl

test:
	$(GO_ENV) go test -v ./...

install: all
	install -d $(BBSHOME)/bin/
	install -c -m 755 nest.svc $(BBSHOME)/bin/
	install -c -m 755 nest.ctl $(BBSHOME)/bin/

clean:
	rm -f nest.svc nest.ctl
