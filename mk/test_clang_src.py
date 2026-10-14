#!/usr/bin/env python3
"""Unit tests for clang_src.py transcoding, flag parsing, and Big5/UTF8 escape handling."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from mk.clang_src import (
    big5_to_big5_c,
    big5_to_utf8_c,
    parse_and_strip_charset_flags,
    transcode_c,
    utf8_to_big5_c,
)


class TestFlagParsing(unittest.TestCase):
    def test_default_charsets(self):
        args = ["clang", "-c", "foo.c", "-o", "foo.o"]
        cleaned, inc, exc = parse_and_strip_charset_flags(args)
        self.assertEqual(cleaned, args)
        self.assertEqual(inc, "big5")
        self.assertEqual(exc, "utf8")

    def test_strip_finput_and_fexec_charset_equals(self):
        args = [
            "clang",
            "-c",
            "foo.c",
            "-finput-charset=big5",
            "-fexec-charset=big5",
            "-o",
            "foo.o",
        ]
        cleaned, inc, exc = parse_and_strip_charset_flags(args)
        self.assertEqual(cleaned, ["clang", "-c", "foo.c", "-o", "foo.o"])
        self.assertEqual(inc, "big5")
        self.assertEqual(exc, "big5")

    def test_strip_finput_and_fexec_charset_spaces(self):
        args = [
            "-finput-charset",
            "utf-8",
            "-fexec-charset",
            "utf-8",
            "clang",
            "-c",
            "foo.c",
        ]
        cleaned, inc, exc = parse_and_strip_charset_flags(args)
        self.assertEqual(cleaned, ["clang", "-c", "foo.c"])
        self.assertEqual(inc, "utf8")
        self.assertEqual(exc, "utf8")


class TestBig5ToUtf8(unittest.TestCase):
    def test_escaped_quote_in_string(self):
        # Clean "許\"多\"" pre-escaped by big5_to_big5_c -> transcode to UTF-8
        clean = bytes([0x22, 0xB3, 0x5C, 0x5C, 0x22, 0xA6, 0x68, 0x5C, 0x22, 0x22])
        escaped = big5_to_big5_c(clean)
        out = big5_to_utf8_c(escaped)
        self.assertEqual(out.decode("utf-8"), '"許\\"多\\""')

    def test_c_escape_newline(self):
        # Clean "許\n" pre-escaped by big5_to_big5_c -> transcode to UTF-8
        clean = bytes([0x22, 0xB3, 0x5C, 0x5C, ord("n"), 0x22])
        escaped = big5_to_big5_c(clean)
        out = big5_to_utf8_c(escaped)
        self.assertEqual(out.decode("utf-8"), '"許\\n"')

    def test_c_escape_tab(self):
        # Clean "許\t" pre-escaped by big5_to_big5_c -> transcode to UTF-8
        clean = bytes([0x22, 0xB3, 0x5C, 0x5C, ord("t"), 0x22])
        escaped = big5_to_big5_c(clean)
        out = big5_to_utf8_c(escaped)
        self.assertEqual(out.decode("utf-8"), '"許\\t"')

    def test_clean_big5_middle(self):
        # "閱讀" -> should transcode to "閱讀"
        clean = bytes([0x22, 0xBE, 0x5C, 0xC5, 0xAA, 0x22])
        escaped = big5_to_big5_c(clean)
        out = big5_to_utf8_c(escaped)
        self.assertEqual(out.decode("utf-8"), '"閱讀"')

    def test_string_ending_with_big5(self):
        # "氣功" ending with 0x5c trail byte before quote -> "氣功"
        clean = bytes([0x22, 0xAE, 0xF0, 0xA5, 0x5C, 0x22])
        escaped = big5_to_big5_c(clean)
        out = big5_to_utf8_c(escaped)
        self.assertEqual(out.decode("utf-8"), '"氣功"')

    def test_clean_string_ending_with_big5(self):
        # "許" (clean Big5 string literal ending with 0x5c trail byte) -> "許"
        clean = bytes([0x22, 0xB3, 0x5C, 0x22])
        escaped = big5_to_big5_c(clean)
        out = big5_to_utf8_c(escaped)
        self.assertEqual(out.decode("utf-8"), '"許"')


class TestBig5ToBig5(unittest.TestCase):
    def test_clean_big5_reading(self):
        # Clean "閱讀" -> inserts backslash so Clang sees \\ and emits single 0x5c
        raw = bytes([0x22, 0xBE, 0x5C, 0xC5, 0xAA, 0x22])
        out = big5_to_big5_c(raw)
        self.assertEqual(out, bytes([0x22, 0xBE, 0x5C, 0x5C, 0xC5, 0xAA, 0x22]))

    def test_clean_string_ending_with_0x5c(self):
        # Clean "許" -> inserts backslash so closing quote is not eaten
        raw = bytes([0x22, 0xB3, 0x5C, 0x22])
        out = big5_to_big5_c(raw)
        self.assertEqual(out, bytes([0x22, 0xB3, 0x5C, 0x5C, 0x22]))

    def test_quote_in_middle(self):
        # "許\"多\"" -> adds \ before quote escape so Clang sees \\\"
        raw = bytes([0x22, 0xB3, 0x5C, 0x5C, 0x22, 0xA6, 0x68, 0x5C, 0x22, 0x22])
        out = big5_to_big5_c(raw)
        self.assertEqual(
            out,
            bytes(
                [
                    0x22,
                    0xB3,
                    0x5C,
                    0x5C,
                    0x5C,
                    0x22,
                    0xA6,
                    0x68,
                    0x5C,
                    0x22,
                    0x22,
                ]
            ),
        )

    def test_newline_in_middle(self):
        # "許\n" -> adds \ before newline escape so Clang sees \\\n
        raw = bytes([0x22, 0xB3, 0x5C, 0x5C, ord("n"), 0x22])
        out = big5_to_big5_c(raw)
        self.assertEqual(
            out, bytes([0x22, 0xB3, 0x5C, 0x5C, 0x5C, ord("n"), 0x22])
        )


class TestTranscodeDispatcher(unittest.TestCase):
    def test_big5_to_utf8(self):
        raw = bytes([0x22, 0xBE, 0x5C, 0xC5, 0xAA, 0x22])
        out = transcode_c(raw, "big5", "utf8")
        self.assertEqual(out.decode("utf-8"), '"閱讀"')

    def test_big5_to_big5(self):
        raw = bytes([0x22, 0xBE, 0x5C, 0xC5, 0xAA, 0x22])
        out = transcode_c(raw, "big5", "big5")
        self.assertEqual(out, bytes([0x22, 0xBE, 0x5C, 0x5C, 0xC5, 0xAA, 0x22]))

    def test_utf8_to_utf8(self):
        raw = b'"\xe9\x96\xb1\xe8\xae\x80"'
        out = transcode_c(raw, "utf8", "utf8")
        self.assertEqual(out, raw)

    def test_utf8_to_big5(self):
        raw = b'"\xe9\x96\xb1\xe8\xae\x80"'
        out = transcode_c(raw, "utf8", "big5")
        self.assertEqual(out, bytes([0x22, 0xBE, 0x5C, 0x5C, 0xC5, 0xAA, 0x22]))


class TestCompilerWrapper(unittest.TestCase):
    def test_compile_clean_big5_trailing_0x5c_quote(self):
        import shutil
        import subprocess
        import tempfile

        if not shutil.which("clang"):
            self.skipTest("clang not found")

        wrapper = os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "clang_src.py"
        )
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "test_bokan.c")
            obj = os.path.join(td, "test_bokan.o")
            # Big5 "氣功" followed immediately by quote: [..., 0xAE, 0xF0, 0xA5, 0x5C, 0x22, ...]
            with open(src, "wb") as f:
                f.write(
                    bytes(
                        [
                            0x63, 0x6F, 0x6E, 0x73, 0x74, 0x20, 0x63, 0x68,
                            0x61, 0x72, 0x20, 0x2A, 0x73, 0x20, 0x3D, 0x20,
                            0x22, 0xAE, 0xF0, 0xA5, 0x5C, 0x22, 0x3B, 0x0A,
                        ]
                    )
                )

            # Test big5 -> big5
            cmd = [
                sys.executable,
                wrapper,
                "clang",
                "-finput-charset=big5",
                "-fexec-charset=big5",
                "-c",
                src,
                "-o",
                obj,
            ]
            p = subprocess.run(cmd, capture_output=True)
            self.assertEqual(p.returncode, 0, p.stderr.decode("latin1"))
            with open(obj, "rb") as f:
                d = f.read()
            self.assertIn(bytes([0xAE, 0xF0, 0xA5, 0x5C]), d)

            # Test big5 -> utf8
            cmd_utf8 = [
                sys.executable,
                wrapper,
                "clang",
                "-finput-charset=big5",
                "-fexec-charset=utf8",
                "-c",
                src,
                "-o",
                obj,
            ]
            p_utf8 = subprocess.run(cmd_utf8, capture_output=True)
            self.assertEqual(p_utf8.returncode, 0, p_utf8.stderr.decode("latin1"))
            with open(obj, "rb") as f:
                d = f.read()
            self.assertIn("氣功".encode("utf-8"), d)


if __name__ == "__main__":
    unittest.main()
