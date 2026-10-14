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
        # "許\"多\"" -> must preserve \" and not break into "許"多\""
        raw = bytes([0x22, 0xB3, 0x5C, 0x5C, 0x22, 0xA6, 0x68, 0x5C, 0x22, 0x22])
        out = big5_to_utf8_c(raw)
        self.assertEqual(out.decode("utf-8"), '"許\\"多\\""')

    def test_c_escape_newline(self):
        # "許\n" -> must preserve \n and not turn into "許n"
        raw = bytes([0x22, 0xB3, 0x5C, 0x5C, ord("n"), 0x22])
        out = big5_to_utf8_c(raw)
        self.assertEqual(out.decode("utf-8"), '"許\\n"')

    def test_c_escape_tab(self):
        # "許\t" -> must preserve \t and not turn into "許t"
        raw = bytes([0x22, 0xB3, 0x5C, 0x5C, ord("t"), 0x22])
        out = big5_to_utf8_c(raw)
        self.assertEqual(out.decode("utf-8"), '"許\\t"')

    def test_manual_big5_escape_middle(self):
        # "閱\讀" (with 0x5c escape) -> should unescape to "閱讀"
        raw = bytes([0x22, 0xBE, 0x5C, 0x5C, 0xC5, 0xAA, 0x22])
        out = big5_to_utf8_c(raw)
        self.assertEqual(out.decode("utf-8"), '"閱讀"')

    def test_clean_big5_middle(self):
        # "閱讀" (clean Big5 without 0x5c escape) -> should transcode to "閱讀"
        raw = bytes([0x22, 0xBE, 0x5C, 0xC5, 0xAA, 0x22])
        out = big5_to_utf8_c(raw)
        self.assertEqual(out.decode("utf-8"), '"閱讀"')

    def test_string_ending_with_big5_escape(self):
        # "氣功\" (with trailing \ to prevent string termination collision) -> "氣功",
        raw = bytes([0x22, 0xAE, 0xF0, 0xA5, 0x5C, 0x5C, 0x22, ord(",")])
        out = big5_to_utf8_c(raw)
        self.assertEqual(out.decode("utf-8"), '"氣功",')

    def test_clean_string_ending_with_big5(self):
        # "許" (clean Big5 string literal ending with 0x5c trail byte) -> "許"
        raw = bytes([0x22, 0xB3, 0x5C, 0x22])
        out = big5_to_utf8_c(raw)
        self.assertEqual(out.decode("utf-8"), '"許"')


class TestBig5ToBig5(unittest.TestCase):
    def test_clean_big5_reading(self):
        # Clean "閱讀" -> inserts backslash so Clang sees \\ and emits single 0x5c
        raw = bytes([0x22, 0xBE, 0x5C, 0xC5, 0xAA, 0x22])
        out = big5_to_big5_c(raw)
        self.assertEqual(out, bytes([0x22, 0xBE, 0x5C, 0x5C, 0xC5, 0xAA, 0x22]))

    def test_legacy_big5_reading(self):
        # Legacy "閱\讀" -> already has \\, keeps single \\
        raw = bytes([0x22, 0xBE, 0x5C, 0x5C, 0xC5, 0xAA, 0x22])
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


if __name__ == "__main__":
    unittest.main()
