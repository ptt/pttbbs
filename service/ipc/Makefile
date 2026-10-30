SRCROOT?=	../..
include $(SRCROOT)/pttbbs.mk

ABS_SRCROOT:=	$(shell cd $(SRCROOT) && pwd)

GO_ENV=	CC="$(CC)" CGO_LDFLAGS_ALLOW="-Wl,--gc-sections" CGO_CFLAGS="-I$(ABS_SRCROOT)/include -DBBSHOME=\"$(BBSHOME)\"" CGO_LDFLAGS="-Wl,--gc-sections -L$(ABS_SRCROOT)/common/bbs -lcmbbs -L$(ABS_SRCROOT)/common/sys -lcmsys -L$(ABS_SRCROOT)/common/osdep -losdep"

.PHONY: all test install clean ipc.svc ipc.ctl test/test_ipc_c_client

all: ipc.svc ipc.ctl test/test_ipc_c_client

ipc.svc:
	$(GO_ENV) go build -o ipc.svc .

ipc.ctl:
	$(GO_ENV) go build -o ipc.ctl ./cmd/ipc.ctl

test/test_ipc_c_client: test/test_ipc_c_client.c
	$(CC) $(CFLAGS) -Wno-invalid-source-encoding -DBBSHOME=\"$(BBSHOME)\" -I$(ABS_SRCROOT)/include -I$(ABS_SRCROOT)/common/bbs -L$(ABS_SRCROOT)/common/bbs -L$(ABS_SRCROOT)/common/sys -L$(ABS_SRCROOT)/common/osdep $< -o $@ -lcmbbs -lcmsys -losdep -lrt -pthread

test: ipc.svc ipc.ctl test/test_ipc_c_client
	$(GO_ENV) go test -v ./...
	@echo "Running C client & daemon integration test..."
	@./ipc.svc -D -v > /tmp/ipc_test.log 2>&1 & \
	PID=$$! ; \
	sleep 1 ; \
	./test/test_ipc_c_client ; \
	RES=$$? ; \
	./ipc.ctl ping >/dev/null 2>&1 ; \
	kill $$PID 2>/dev/null ; \
	wait $$PID 2>/dev/null ; \
	exit $$RES

install: all
	install -d $(BBSHOME)/bin/
	install -c -m 755 ipc.svc $(BBSHOME)/bin/
	install -c -m 755 ipc.ctl $(BBSHOME)/bin/

clean:
	rm -f ipc.svc ipc.ctl test/test_ipc_c_client
