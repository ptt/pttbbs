#!/bin/sh

# prevent localized logs
LC_ALL=C
export LC_ALL

build_remote="$(git config --get remote.origin.url 2>/dev/null)"
build_origin="$(git rev-parse --short origin/master 2>/dev/null)"
build_hash="$(git rev-parse --short HEAD 2>/dev/null)"
[ "${build_hash}" = "${build_origin}" ] && build_origin=""
if ! git diff --quiet 2>/dev/null; then
  build_hash="${build_hash} M"
fi
build_time="$(date)"

cat >vers.c <<EOF
const char *build_remote = "${build_remote}";
const char *build_origin = "${build_origin}";
const char *build_hash = "${build_hash}";
const char *build_time = "${build_time}";
EOF
