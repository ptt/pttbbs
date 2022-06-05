BAZEL?=	bazelisk

.include "pttbbs.mk"

# overwrite and remove ccache in CC and CXX
# https://github.com/bazelbuild/bazel/issues/12124
CC:= gcc
CXX:= g++

CLANG!=		sh -c 'type clang >/dev/null 2>&1 && echo 1 || echo 0'

.if defined(WITHOUT_CLANG)
CLANG:=
.elif $(CLANG)
CC:=		clang
CXX:=		clang++
.endif

#######################################################################
# common modules
#######################################################################

BAZELPROG=	bazel-bin/mbbsd/mbbsd

#######################################################################
# variables
#######################################################################

DATETIME!=	date '+%Y%m%d%H%M'

#######################################################################
# Make Rules
#######################################################################

bbsuser:
	groupadd --gid 99 bbs && \
	useradd -m -g bbs -s /bin/bash --uid 9999 bbs && \
	mkdir -p /home/bbs && \
	chown -R bbs:bbs /home/bbs

pre-bazel:
	apt update && \
	apt install -y wget bmake gcc g++ clang ccache libc6-dev libevent-dev pkg-config gnupg libflatbuffers-dev flatbuffers-compiler-dev liblua5.1-0-dev python && \
	wget https://github.com/bazelbuild/bazelisk/releases/download/v1.11.0/bazelisk-linux-amd64 -O /usr/local/bin/bazelisk && \
	chmod 755 /usr/local/bin/bazelisk && \
	bazelisk info

bazelbuild:
	CC=$(CC) $(BAZEL) build --sandbox_debug --subcommands --define BBSHOME="$(BBSHOME)" ...

$(BAZELPROG): bazelbuild

bazelinstall: $(BAZELPROG)
	install -d $(BBSHOME)/bin/
	install -s $(BAZELPROG) $(BBSHOME)/bin/mbbsd.bazel.$(DATETIME)
	objcopy --only-keep-debug --compress-debug-sections $(BAZELPROG) $(BBSHOME)/bin/mbbsd.bazel.$(DATETIME).debug
	@cd $(BBSHOME)/bin && objcopy --add-gnu-debuglink=mbbsd.bazel.$(DATETIME).debug mbbsd.bazel.$(DATETIME)
	@ln -sfv mbbsd.bazel.$(DATETIME) $(BBSHOME)/bin/mbbsd

bazeltest:
	CC=$(CC) $(BAZEL) test --define BBSHOME="testhome" --test_output=all ...

bazelclean:
	$(BAZEL) clean
