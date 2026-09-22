#!/usr/bin/env python3
"""Preprocess and transcode Big5 C/C++ source to UTF-8 execution charset for Clang.

Can be used in two ways:
1. As a stdin -> stdout filter in a pipeline:
   clang -E -frewrite-includes foo.c | ./util/clang_utf8.py | clang -x c -c - -o foo.o
2. As a compiler wrapper in Makefiles:
   CC  = $(abspath $(SRCROOT)/util/clang_utf8.py) clang
   CXX = $(abspath $(SRCROOT)/util/clang_utf8.py) clang++
"""

import os
import subprocess
import sys


def big5_to_utf8_c(big5_data: bytes) -> bytes:
    """Convert Big5 preprocessed C/C++ source to UTF-8 bytes, unescaping 0x5c trail bytes."""
    out = bytearray()
    i = 0
    n = len(big5_data)
    while i < n:
        b = big5_data[i]
        if 0x81 <= b <= 0xFE and i + 1 < n:
            b2 = big5_data[i + 1]
            if (0x40 <= b2 <= 0x7E) or (0xA1 <= b2 <= 0xFE):
                out.append(b)
                out.append(b2)
                if b2 == 0x5C and i + 2 < n and big5_data[i + 2] == 0x5C:
                    i += 3
                else:
                    i += 2
                continue
        out.append(b)
        i += 1

    raw = bytes(out)
    try:
        return raw.decode("big5-hkscs").encode("utf-8")
    except UnicodeDecodeError:
        lines = []
        for line in raw.splitlines(keepends=True):
            try:
                lines.append(line.decode("big5-hkscs").encode("utf-8"))
            except UnicodeDecodeError:
                lines.append(line.decode("utf-8", errors="replace").encode("utf-8"))
        return b"".join(lines)


def is_source_file(arg: str) -> bool:
    return not arg.startswith("-") and arg.endswith((".c", ".cc", ".cpp"))


def main() -> int:
    # Filter mode: stdin -> stdout
    if len(sys.argv) <= 1 or sys.argv[1] == "-":
        sys.stdout.buffer.write(big5_to_utf8_c(sys.stdin.buffer.read()))
        return 0

    if sys.argv[1].startswith("-"):
        cc_prefix = ["clang"]
        raw_compiler = "clang"
        args = sys.argv[1:]
    elif sys.argv[1] == "ccache" and len(sys.argv) >= 3:
        cc_prefix = [sys.argv[1], sys.argv[2], "-Wno-macro-redefined"]
        raw_compiler = sys.argv[2]
        args = sys.argv[3:]
    else:
        cc_prefix = [sys.argv[1], "-Wno-macro-redefined"]
        raw_compiler = sys.argv[1]
        args = sys.argv[2:]

    # If preprocessing only (-E / -M / -MM), invoke compiler directly.
    if any(a in ("-E", "-M", "-MM") for a in args):
        os.execvp(cc_prefix[0], cc_prefix + args)

    src_indices = [idx for idx, a in enumerate(args) if is_source_file(a)]
    if not src_indices:
        os.execvp(cc_prefix[0], cc_prefix + args)

    # Extract preprocessor/compiler flags for the `-E` step.
    pp_flags = []
    skip_next = False
    for a in args:
        if skip_next:
            skip_next = False
            continue
        if a == "-o":
            skip_next = True
            continue
        if a.startswith("-o"):
            continue
        if a == "-c":
            continue
        if a.startswith(("-l", "-L", "-Wl,")):
            continue
        if not a.startswith("-") and a.endswith((".o", ".a", ".so")):
            continue
        if is_source_file(a):
            continue
        pp_flags.append(a)

    src_idx = src_indices[0]
    src_file = args[src_idx]
    is_cxx = src_file.endswith((".cc", ".cpp")) or raw_compiler.endswith("++")
    src_lang = "c++" if is_cxx else "c"

    # Step 1: Run preprocessor (clang -E -frewrite-includes)
    pp_cmd = [
        raw_compiler,
        "-E",
        "-frewrite-includes",
        "-Wno-invalid-source-encoding",
        "-Wno-macro-redefined",
    ] + pp_flags + [src_file]
    pp_proc = subprocess.run(pp_cmd, capture_output=True)
    if pp_proc.returncode != 0:
        sys.stderr.buffer.write(pp_proc.stderr)
        return pp_proc.returncode

    # Step 2: Transcode Big5 -> UTF-8 (with 0x5c trail byte unescaping)
    utf8_code = big5_to_utf8_c(pp_proc.stdout)

    # Step 3: Feed preprocessed UTF-8 stream back to clang
    tail = args[src_idx + 1 :]
    has_more_inputs = any(
        not a.startswith("-") and tail[i - 1] != "-o"
        for i, a in enumerate(tail)
        if i > 0 or not a.startswith("-")
    )
    mid = ["-x", src_lang, "-"] + (["-x", "none"] if has_more_inputs else [])
    if "-c" in args and not any(a == "-o" or a.startswith("-o") for a in args):
        default_obj = os.path.splitext(os.path.basename(src_file))[0] + ".o"
        mid = ["-o", default_obj] + mid
    compile_args = args[:src_idx] + mid + tail
    cc_proc = subprocess.run(cc_prefix + compile_args, input=utf8_code)
    return cc_proc.returncode


if __name__ == "__main__":
    sys.exit(main())
