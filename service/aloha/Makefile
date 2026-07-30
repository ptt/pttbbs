SRCROOT?=	../..
.include "$(SRCROOT)/pttbbs.mk"

ABS_SRCROOT!=	cd $(SRCROOT) && pwd

GO_ENV=	CC="$(CC)" CGO_LDFLAGS_ALLOW="-Wl,--gc-sections" CGO_CFLAGS="-I$(ABS_SRCROOT)/include -DBBSHOME=\"$(BBSHOME)\"" CGO_LDFLAGS="-Wl,--gc-sections -L$(ABS_SRCROOT)/common/bbs -lcmbbs -L$(ABS_SRCROOT)/common/sys -lcmsys -L$(ABS_SRCROOT)/common/osdep -losdep"

all: aloha.svc aloha.ctl

aloha.svc:
	$(GO_ENV) go build -o aloha.svc .

aloha.ctl:
	$(GO_ENV) go build -o aloha.ctl ./cmd/aloha.ctl

test:
	$(GO_ENV) go test -v ./...

install: all
	install -d $(BBSHOME)/bin/
	install -c -m 755 aloha.svc $(BBSHOME)/bin/
	install -c -m 755 aloha.ctl $(BBSHOME)/bin/

clean:
	rm -f aloha.svc aloha.ctl
