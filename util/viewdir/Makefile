SRCROOT?=	../..
include $(SRCROOT)/pttbbs.mk

.PHONY: all clean test

all: viewdir

# With CONFIG_POST_SVC the .DIR of boards is the cache of post.svc:
# viewdir is read-only by default (see postSvcBuild).
VIEWDIR_LDFLAGS:=	$(if $(call DEF_CHECK,CONFIG_POST_SVC),-X main.postSvcBuild=YES)

viewdir:
	go build -ldflags "$(VIEWDIR_LDFLAGS)" -o viewdir .

clean:
	rm -f viewdir

test:
	go test -v ./...
