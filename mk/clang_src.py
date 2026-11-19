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

import hashlib
import os
import re
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


def big5_to_big5_c(big5_data: bytes) -> bytes:
    """Ensure all Big5 0x5C trail bytes are escaped with backslash for Clang ASCII parsing."""
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
                if b2 == 0x5C:
                    out.append(0x5C)
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


def compile_copy(cc_prefix: list[str], args: list[str], src_idx: int,
                 code: bytes) -> int:
    """Compile code in place of the source args[src_idx], from a copy:
    beside the source, as the debug info names the copy's directory (and,
    with -main-file-name, the source); with the same extension; named after
    the output, as ccache hashes the input's path; and as old as the source,
    as ccache distrusts files changed just now."""
    src = args[src_idx]
    head = args[:src_idx]
    out = output_path(args)
    if out is None:
        # What the compiler would name it after the source.
        stem = os.path.splitext(os.path.basename(src))[0]
        out = stem + ".o" if "-c" in args else stem + ".s" if "-S" in args else "a.out"
        head = head + ["-o", out]
    # Unique to the output, as sources can share a directory and outputs a
    # name (cgo's), and the same each build.
    tag = hashlib.sha1(os.path.relpath(out, os.path.dirname(src) or ".").encode())
    name = os.path.basename(out)
    if not name.startswith(".clang_src."):    # compile_then_link's object
        name = ".clang_src." + name
    name += "." + tag.hexdigest()[:8] + os.path.splitext(src)[1]
    try:
        f = open(os.path.join(os.path.dirname(src), name), "wb")
    except OSError:
        # A read-only source (Go's runtime/cgo, say): next to the output,
        # finding the source's #include "..." with -iquote.
        f = open(os.path.join(os.path.dirname(out), name), "wb")
        head = head + ["-iquote", os.path.dirname(src) or "."]
    copy = f.name
    with f:
        f.write(code)
    st = os.stat(src)
    os.utime(copy, ns=(st.st_atime_ns, st.st_mtime_ns))
    try:
        return subprocess.run(
            cc_prefix + head +
            ["-Xclang", "-main-file-name", "-Xclang", os.path.basename(src), copy] +
            args[src_idx + 1:]).returncode
    finally:
        os.unlink(copy)


def is_source_file(arg: str) -> bool:
    return not arg.startswith("-") and arg.endswith((".c", ".cc", ".cpp"))


def fix_stdin_line_markers(code: bytes, src_file: str) -> bytes:
    """Replace '<stdin>' in preprocessor line directives with src_file."""
    pattern = re.compile(rb'(?m)^#(\s*line)?\s+(\d+)\s+"<stdin>"')
    escaped_src = src_file.replace("\\", "\\\\").replace('"', '\\"').encode()

    def repl(m: re.Match[bytes]) -> bytes:
        prefix = m.group(1) or b""
        line_num = m.group(2)
        return b"#" + prefix + b" " + line_num + b' "' + escaped_src + b'"'

    return pattern.sub(repl, code)


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

    # ccache can't cache compiling and linking in one go (as util/ builds its
    # programs): compile to an object through this wrapper, then link that.
    if cc_prefix[0] == "ccache" and "-c" not in args and len(src_indices) == 1:
        rc = compile_then_link(args, src_indices[0], raw_compiler,
                               input_charset, exec_charset)
        if rc is not None:
            return rc

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
    # When input_charset is big5, pre-escape Big5 0x5C trail bytes before feeding
    # into Clang's preprocessor, to prevent Clang from mistaking Big5 trail 0x5C
    # preceding a quote (e.g. "氣功\"") as an escaped quote.
    src_dir = os.path.dirname(src_file) or "."

    if input_charset == "big5":
        with open(src_file, "rb") as f:
            src_bytes = f.read()
        pp_input = big5_to_big5_c(src_bytes)
        pp_cmd = [
            raw_compiler,
            "-E",
            "-frewrite-includes",
            "-Wno-invalid-source-encoding",
            "-Wno-macro-redefined",
            "-iquote",
            src_dir,
        ] + pp_flags + ["-x", src_lang, "-"]
    else:
        pp_input = None
        pp_cmd = [
            raw_compiler,
            "-E",
            "-frewrite-includes",
            "-Wno-invalid-source-encoding",
            "-Wno-macro-redefined",
        ] + pp_flags + [src_file]

    pp_proc = subprocess.run(pp_cmd, input=pp_input, capture_output=True)
    if pp_proc.returncode != 0:
        sys.stderr.buffer.write(pp_proc.stderr.replace(b"<stdin>", src_file.encode()))
        return pp_proc.returncode

    # Step 2: Transcode source according to input & exec charset
    if input_charset == "big5" and exec_charset == "big5":
        # Already pre-escaped for Clang in Step 1
        transcoded_code = pp_proc.stdout
    elif input_charset == "big5" and exec_charset == "utf8":
        # Unescape the extra backslash and convert to UTF-8
        transcoded_code = big5_to_utf8_c(pp_proc.stdout)
    elif input_charset == "utf8" and exec_charset == "big5":
        transcoded_code = utf8_to_big5_c(pp_proc.stdout)
    else:
        transcoded_code = pp_proc.stdout

    transcoded_code = fix_stdin_line_markers(transcoded_code, src_file)

    # Step 3: Feed preprocessed stream back to clang
    return compile_copy(cc_prefix, args, src_idx, transcoded_code)


# Options whose value is the next argument: for the compiler (passed to
# both steps), or for the linker only. Others make compile_then_link decline.
CC_VALUE_OPTS = ("-I", "-D", "-U", "-include", "-imacros", "-isystem", "-iquote",
                 "-idirafter", "-x", "-Xclang", "-target", "-arch", "-isysroot",
                 "--sysroot", "-MF", "-MT", "-MQ")
LD_VALUE_OPTS = ("-o", "-Xlinker", "-z", "-framework", "-T", "-u", "-L", "-l")
LD_ONLY = ("-shared", "-static", "-rdynamic", "-pie", "-no-pie")
NOT_LINKING = ("-S", "-E", "-M", "-MM", "-fsyntax-only")


def split_compile_link(args: list[str], src_idx: int):
    """For a command that compiles args[src_idx] and links it: the arguments
    to compile it alone, and to link (None for the object); None if the
    command is not plainly that (an argument it doesn't know, say)."""
    cc, ld, i = [], [], 0
    while i < len(args):
        a = args[i]
        if i == src_idx:
            ld.append(None)
        elif a in NOT_LINKING or is_source_file(a):
            return None
        elif a in LD_VALUE_OPTS or a in CC_VALUE_OPTS:
            if i + 1 == len(args):
                return None
            if a in CC_VALUE_OPTS:
                cc += [a, args[i + 1]]
            ld += [a, args[i + 1]]
            i += 1
        elif a.startswith(("-o", "-l", "-L", "-Wl,")) or a in LD_ONLY:
            ld.append(a)
        elif a.startswith("-"):
            cc.append(a)
            ld.append(a)
        elif re.search(r"\.(o|a|so(\.[0-9]+)*|dylib)$", a):
            ld.append(a)
        else:
            return None
        i += 1
    return cc, ld


def compile_then_link(args: list[str], src_idx: int, raw_compiler: str,
                      input_charset: str, exec_charset: str) -> int | None:
    parts = split_compile_link(args, src_idx)
    if parts is None:
        return None
    cc, ld = parts
    out = output_path(args) or "a.out"
    obj = os.path.join(os.path.dirname(out), ".clang_src." + os.path.basename(out) + ".o")
    try:
        rc = subprocess.run(
            [sys.executable, os.path.abspath(__file__),
             f"-finput-charset={input_charset}", f"-fexec-charset={exec_charset}",
             "ccache", raw_compiler] + cc + ["-c", "-o", obj, args[src_idx]]).returncode
        if rc:
            return rc
        link, seen_x = [], False
        for a in ld:
            if a is None:
                # An earlier -x would apply to the object too.
                link += ["-x", "none", obj] if seen_x else [obj]
            else:
                link.append(a)
                seen_x = seen_x or a == "-x"
        return subprocess.run([raw_compiler] + link).returncode
    finally:
        if os.path.exists(obj):
            os.unlink(obj)


def output_path(args: list[str]) -> str | None:
    for i, a in enumerate(args):
        if a == "-o" and i + 1 < len(args):
            return args[i + 1]
        if a.startswith("-o") and len(a) > 2:
            return a[2:]
    return None


if __name__ == "__main__":
    sys.exit(main())
