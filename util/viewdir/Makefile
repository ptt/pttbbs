SRCROOT?=	../..
include $(SRCROOT)/pttbbs.mk

.PHONY: all clean test

all: viewdir

# With CONFIG_NEST the .DIR of boards is the cache of nest.svc:
# L sets modes through nest.svc (see nestBuild).
VIEWDIR_LDFLAGS:=	$(if $(call DEF_CHECK,CONFIG_NEST),-X main.nestBuild=YES)

viewdir:
	go build -ldflags "$(VIEWDIR_LDFLAGS)" -o viewdir .

clean:
	rm -f viewdir

test:
	go test -v ./...
