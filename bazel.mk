BAZEL?=	bazelisk

.include "pttbbs.mk"

MACHINETYPE!=	uname -m

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

.if ${MACHINETYPE} == "aarch64" || ${MACHINETYPE} == "arm64"
ARCH="arm64"
.else
ARCH="amd64"
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

ORIG_PWD!= pwd

bbsuser:
	groupadd --gid 99 bbs && \
	useradd -m -g bbs -s /bin/bash --uid 9999 bbs && \
	mkdir -p /home/bbs && \
	chown -R bbs:bbs /home/bbs

pre-bazel:
	apt update && \
	apt install -y unzip wget bmake gcc g++ clang ccache libc6-dev libevent-dev pkg-config gnupg libflatbuffers-dev flatbuffers-compiler-dev liblua5.1-0-dev python libsqlite3-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc libgrpc++-dev libgrpc-dev libgflags-dev libmaxminddb-dev libgeoip-dev && \
	wget https://github.com/bazelbuild/bazelisk/releases/download/v1.11.0/bazelisk-linux-$(ARCH) -O /usr/local/bin/bazelisk && \
	chmod 755 /usr/local/bin/bazelisk && \
	bazelisk info

regmaild:
	cd daemon/regmaild; BBSHOME="/home/bbs" pmake clean all install; cd ../..

bazelbuild:
	CC=$(CC) $(BAZEL) build --sandbox_debug --subcommands --define BBSHOME="$(BBSHOME)" ...

$(BAZELPROG): bazelbuild

bazelinstall: $(BAZELPROG)
	install -d $(BBSHOME)/bin/
	install -s $(BAZELPROG) $(BBSHOME)/bin/mbbsd.bazel.$(DATETIME)
	objcopy --only-keep-debug --compress-debug-sections $(BAZELPROG) $(BBSHOME)/bin/mbbsd.bazel.$(DATETIME).debug
	@cd $(BBSHOME)/bin && objcopy --add-gnu-debuglink=mbbsd.bazel.$(DATETIME).debug mbbsd.bazel.$(DATETIME)
	@ln -sfv mbbsd.bazel.$(DATETIME) $(BBSHOME)/bin/mbbsd

pre-bazeltest:
	ipcrm -M 0x000004cc; echo "ORIG_PWD: ${ORIG_PWD}"; cd /home/bbs; ${ORIG_PWD}/util/uhash_loader; ${ORIG_PWD}/daemon/regmaild/initemaildb verifydb /home/bbs/emaildb.db; /home/bbs/bin/regmaild -i /home/bbs/localhost:5678; cd "${ORIG_PWD}"; ipcs; ps ax|grep regmaild; pwd

bazeltest:
	CC=$(CC) $(BAZEL) test --define BBSHOME="testhome" --test_output=errors ...

bazelretest:
	CC=$(CC) $(BAZEL) test --cache_test_results=no --define BBSHOME="testhome" --test_output=errors ...

bazeltestall:
	CC=$(CC) $(BAZEL) test --define BBSHOME="testhome" --test_output=all ...

bazelclean:
	$(BAZEL) clean

post-bazeltest:
	kill -9 `cat /home/bbs/run/regmaild.pid` && rm /home/bbs/run/regmaild.pid; \
	ipcrm -M 0x000004cc; \
	pwd
