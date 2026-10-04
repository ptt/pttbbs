SRCROOT?=	../..
include $(SRCROOT)/pttbbs.mk

ABS_SRCROOT:=	$(shell cd $(SRCROOT) && pwd)

GO_ENV=	CC="$(CC)" CGO_LDFLAGS_ALLOW="-Wl,--gc-sections" CGO_CFLAGS="-I$(ABS_SRCROOT)/include -DBBSHOME=\"$(BBSHOME)\"" CGO_LDFLAGS="-Wl,--gc-sections -L$(ABS_SRCROOT)/common/bbs -lcmbbs -L$(ABS_SRCROOT)/common/sys -lcmsys -L$(ABS_SRCROOT)/common/osdep -losdep"

.PHONY: all test install clean post.svc post.ctl

all: post.svc post.ctl

post.svc:
	$(GO_ENV) go build -o post.svc .

post.ctl:
	$(GO_ENV) go build -o post.ctl ./cmd/post.ctl

test:
	$(GO_ENV) go test -v ./...

install: all
	install -d $(BBSHOME)/bin/
	install -c -m 755 post.svc $(BBSHOME)/bin/
	install -c -m 755 post.ctl $(BBSHOME)/bin/

clean:
	rm -f post.svc post.ctl
