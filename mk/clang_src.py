#!/usr/bin/env python3
"""Preprocess and transcode C/C++ source charsets for Clang.

Supports:
  -finput-charset=big5 | utf8
  -fexec-charset=big5 | utf8

Usage:
1. As a compiler wrapper in Makefiles:
   CC  = $(abspath $(SRCROOT)/mk/clang_src.py) clang
   CXX = $(abspath $(SRCROOT)/mk/clang_src.py) clang++
2. As a filter (stdin -> stdout):
   clang_src.py -finput-charset=big5 -fexec-charset=utf8 - < in.c > out.c
"""

import os
import subprocess
import sys


def normalize_charset(cs: str) -> str:
    cs = cs.lower().replace("-", "")
    if cs in ("utf8", "utf_8"):
        return "utf8"
    if cs in ("big5", "big_5", "big5hkscs"):
        return "big5"
    return cs


def parse_and_strip_charset_flags(args: list[str]) -> tuple[list[str], str, str]:
    """Extract and remove -finput-charset and -fexec-charset flags from compiler args."""
    new_args = []
    input_charset = "big5"
    exec_charset = "utf8"

    i = 0
    n = len(args)
    while i < n:
        arg = args[i]
        if arg.startswith("-finput-charset="):
            input_charset = normalize_charset(arg.split("=", 1)[1])
            i += 1
        elif arg == "-finput-charset" and i + 1 < n:
            input_charset = normalize_charset(args[i + 1])
            i += 2
        elif arg.startswith("-fexec-charset="):
            exec_charset = normalize_charset(arg.split("=", 1)[1])
            i += 1
        elif arg == "-fexec-charset" and i + 1 < n:
            exec_charset = normalize_charset(args[i + 1])
            i += 2
        else:
            new_args.append(arg)
            i += 1

    return new_args, input_charset, exec_charset


def big5_to_utf8_c(big5_data: bytes) -> bytes:
    """Convert Big5 C/C++ source to UTF-8 bytes, unescaping 0x5c trail bytes."""
    out = bytearray()
    i = 0
    n = len(big5_data)
    c_escape_chars = set(b"ntrabfv01234567xXuU?")

    while i < n:
        b = big5_data[i]
        if 0x81 <= b <= 0xFE and i + 1 < n:
            b2 = big5_data[i + 1]
            if (0x40 <= b2 <= 0x7E) or (0xA1 <= b2 <= 0xFE):
                out.append(b)
                out.append(b2)
                if b2 == 0x5C and i + 2 < n and big5_data[i + 2] == 0x5C:
                    c3 = big5_data[i + 3] if i + 3 < n else None
                    if c3 == 0x5C:
                        # 3+ consecutive backslashes (e.g. 許\\ or manual 許\"):
                        # Eat the Big5 escape backslash, keep remaining backslashes.
                        i += 3
                    elif c3 in c_escape_chars:
                        # Followed by standard C escape like \n, \t, \r, \0: keep the backslash!
                        i += 2
                    elif c3 in (0x22, 0x27):  # \" or \'
                        # Determine if this quote is closing the literal or an escaped quote inside:
                        j = i + 4
                        while j < n and big5_data[j] in b" \t\r\n":
                            j += 1
                        # If followed by non-ASCII (e.g. Chinese) or identifier character,
                        # this is an escaped quote inside the string (e.g. "許\"多\"").
                        if j < n and (
                            big5_data[j] >= 0x80
                            or chr(big5_data[j]).isalnum()
                            or big5_data[j] == ord("_")
                        ):
                            i += 2  # Keep the backslash for \"
                        else:
                            i += 3  # Closing quote with Big5 escape (e.g. "許\"), eat it
                    else:
                        # Regular character (e.g. 閱\讀, 功\能): eat the Big5 escape backslash
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


def big5_to_big5_c(big5_data: bytes) -> bytes:
    """Ensure all Big5 0x5C trail bytes are escaped with backslash for Clang ASCII parsing."""
    out = bytearray()
    i = 0
    n = len(big5_data)
    c_escape_chars = set(b"ntrabfv01234567xXuU?")

    while i < n:
        b = big5_data[i]
        if 0x81 <= b <= 0xFE and i + 1 < n:
            b2 = big5_data[i + 1]
            if (0x40 <= b2 <= 0x7E) or (0xA1 <= b2 <= 0xFE):
                out.append(b)
                out.append(b2)
                if b2 == 0x5C:
                    c3 = big5_data[i + 2] if i + 2 < n else None
                    if c3 == 0x5C:
                        # Already has a backslash after b2 (b1 0x5c 0x5c)
                        out.append(0x5C)
                        c4 = big5_data[i + 3] if i + 3 < n else None
                        if c4 in c_escape_chars:
                            # e.g. "許\n" (b1 0x5c 0x5c n): add extra \ so Clang sees \\ followed by \n
                            out.append(0x5C)
                            i += 3
                            continue
                        elif c4 in (0x22, 0x27):
                            # e.g. "許\"多\""
                            j = i + 4
                            while j < n and big5_data[j] in b" \t\r\n":
                                j += 1
                            if j < n and (
                                big5_data[j] >= 0x80
                                or chr(big5_data[j]).isalnum()
                                or big5_data[j] == ord("_")
                            ):
                                # Internal escaped quote: add extra \ so Clang sees \\ followed by \"
                                out.append(0x5C)
                                i += 3
                                continue
                            else:
                                # Closing quote: already b1 0x5c 0x5c ", Clang sees \\ then closing quote
                                i += 3
                                continue
                        else:
                            # e.g. legacy "閱\讀" (b1 0x5c 0x5c c5): keep as-is
                            i += 3
                            continue
                    else:
                        # Clean Big5 (not followed by \): insert \ so Clang sees \\
                        out.append(0x5C)
                        i += 2
                        continue
                else:
                    i += 2
                    continue
        out.append(b)
        i += 1
    return bytes(out)


def utf8_to_big5_c(data: bytes) -> bytes:
    """Convert UTF-8 C source to Big5 bytes and escape 0x5C trail bytes for Clang."""
    try:
        big5_raw = data.decode("utf-8").encode("big5-hkscs")
    except (UnicodeDecodeError, UnicodeEncodeError):
        lines = []
        for line in data.splitlines(keepends=True):
            try:
                lines.append(line.decode("utf-8").encode("big5-hkscs"))
            except (UnicodeDecodeError, UnicodeEncodeError):
                lines.append(line)
        big5_raw = b"".join(lines)
    return big5_to_big5_c(big5_raw)


def transcode_c(data: bytes, input_charset: str, exec_charset: str) -> bytes:
    """Transcode preprocessed C/C++ source from input_charset to exec_charset for Clang."""
    if input_charset == "big5" and exec_charset == "utf8":
        return big5_to_utf8_c(data)
    elif input_charset == "big5" and exec_charset == "big5":
        return big5_to_big5_c(data)
    elif input_charset == "utf8" and exec_charset == "big5":
        return utf8_to_big5_c(data)
    elif input_charset == "utf8" and exec_charset == "utf8":
        return data
    else:
        return data


def is_source_file(arg: str) -> bool:
    return not arg.startswith("-") and arg.endswith((".c", ".cc", ".cpp"))


def main() -> int:
    args, input_charset, exec_charset = parse_and_strip_charset_flags(sys.argv[1:])

    # Filter mode: stdin -> stdout
    if len(args) <= 1 and (len(args) == 0 or args[0] == "-"):
        sys.stdout.buffer.write(
            transcode_c(sys.stdin.buffer.read(), input_charset, exec_charset)
        )
        return 0

    if args[0].startswith("-"):
        cc_prefix = ["clang"]
        raw_compiler = "clang"
    elif args[0] == "ccache" and len(args) >= 3:
        cc_prefix = [args[0], args[1], "-Wno-macro-redefined"]
        raw_compiler = args[1]
        args = args[2:]
    else:
        cc_prefix = [args[0], "-Wno-macro-redefined"]
        raw_compiler = args[0]
        args = args[1:]

    # Pass-through if input and exec charsets are both utf8 (Clang handles UTF-8 natively)
    if input_charset == "utf8" and exec_charset == "utf8":
        os.execvp(cc_prefix[0], cc_prefix + args)

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

    # Step 2: Transcode source according to input & exec charset
    transcoded_code = transcode_c(pp_proc.stdout, input_charset, exec_charset)

    # Step 3: Feed preprocessed stream back to clang
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
    cc_proc = subprocess.run(cc_prefix + compile_args, input=transcoded_code)
    return cc_proc.returncode


if __name__ == "__main__":
    sys.exit(main())
