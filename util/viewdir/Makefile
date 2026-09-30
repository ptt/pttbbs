SRCROOT?=	../..
include $(SRCROOT)/pttbbs.mk

.PHONY: all clean test

all: viewdir

viewdir:
	go build -o viewdir .

clean:
	rm -f viewdir

test:
	go test -v ./...
