#!/usr/bin/env python3
"""Pull Castle Crashers' text out of your own castle.exe into strings files.

The game's scripts show text by number (TextField.ntext = 742). castle.exe
holds the words as UTF-16 in a table of records: a u32 string number, then
one pointer per language (en, de, fr, es, it, ja, ko, zh-Hant, pt, zh-Hans).
This finds that table by its shape (no fixed offsets, so it works on any build
that keeps the layout) and writes one strings_<lang>.txt per language in the
format strtab.c reads:

    742 = <the words>

Run it on the PC, against a COPY of castle.exe (never the Steam folder):

    python extract_strings.py --exe "<working folder>/Steam PC copy/original/castle.exe" --out <folder>

The output is your game's text: keep it on your PC and Vita, never share it.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

LANGUAGES = ["en", "de", "fr", "es", "it", "ja", "ko", "zh-Hant", "pt", "zh-Hans"]
MIN_RECORDS = 100   # a real table has hundreds; shorter runs are coincidence


class Pe:
    def __init__(self, data: bytes):
        if data[:2] != b"MZ":
            raise ValueError("not a Windows executable")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("not a PE file")
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        opt_size = struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        if struct.unpack_from("<H", data, opt)[0] != 0x10B:
            raise ValueError("expected a 32-bit executable")
        self.base = struct.unpack_from("<I", data, opt + 28)[0]
        self.sections = []
        for i in range(nsec):
            s = opt + opt_size + i * 40
            vsize, va, rsize, raw = struct.unpack_from("<IIII", data, s + 8)
            self.sections.append((va, vsize, rsize, raw))
        self.data = data

    def offset(self, va: int) -> int | None:
        rva = va - self.base
        for sva, vsize, rsize, raw in self.sections:
            if sva <= rva < sva + rsize:
                return raw + rva - sva
        return None


def read_utf16(pe: Pe, va: int, limit: int = 4096) -> str | None:
    off = pe.offset(va)
    if off is None or off % 2:
        return None
    end = off
    data = pe.data
    while end + 1 < len(data) and data[end:end + 2] != b"\0\0":
        end += 2
        if end - off > limit * 2:
            return None
    try:
        return data[off:end].decode("utf-16-le")
    except UnicodeDecodeError:
        return None


def find_table(pe: Pe, nlang: int) -> tuple[int, int]:
    """Start offset and record count of the longest run of string records."""
    rec = 4 + 4 * nlang
    data = pe.data
    cache: dict[int, bool] = {}

    def is_record(pos: int) -> bool:
        if pos in cache:
            return cache[pos]
        ok = False
        if pos + rec <= len(data):
            fields = struct.unpack_from(f"<{1 + nlang}I", data, pos)
            ok = fields[0] < 0x10000 and all(read_utf16(pe, p, 2048) is not None for p in fields[1:])
        cache[pos] = ok
        return ok

    best = (0, 0)
    pos = 0
    while pos + rec <= len(data):
        if is_record(pos):
            n = 1
            while is_record(pos + n * rec):
                n += 1
            # step back too, in case we landed mid-table
            start = pos
            while start - rec >= 0 and is_record(start - rec):
                start -= rec
                n += 1
            if n > best[1]:
                best = (start, n)
            pos = start + n * rec
        else:
            pos += 4
    return best


def escape(text: str) -> str:
    text = text.replace("\r", "")
    text = text.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")
    text = text.replace("{", "{{")   # textlayout reads {tokens}
    if text[:1] == " ":
        text = "\\s" + text[1:]
    if text[-1:] == " ":
        text = text[:-1] + "\\s"
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    pe = Pe(args.exe.read_bytes())
    # The 2012 build has 10 languages; the 2015 Remastered update added
    # Russian. Try a few widths and keep the longest table.
    best = (0, 0, 0)
    for nlang in (10, 11, 12, 9, 13):
        start, count = find_table(pe, nlang)
        if count > best[1]:
            best = (start, count, nlang)
    start, count, nlang = best
    langs = LANGUAGES[:nlang] + [f"lang{i}" for i in range(len(LANGUAGES), nlang)]
    if nlang != len(LANGUAGES):
        print(f"Note: {nlang} languages, not 10. This castle.exe is a different build "
              "(11 suggests the 2015 Remastered update, which added Russian). Extra "
              "languages are named lang10 and so on; check which is which.", file=sys.stderr)
    if count < MIN_RECORDS:
        print(f"No string table found (longest run: {count} records). "
              "This build may lay its text out differently.", file=sys.stderr)
        return 1

    # Sanity check before writing anything: the menu table holds sentences
    # and words like "Resume Game", not identifiers. On Jah's Remastered
    # castle.exe (2026-10-14) the longest run was a colour/item name table
    # ("aliceblue", "lilac"), because that build keeps its menu-word table
    # in a form this tool can't read. Refuse rather than write wrong text.
    rec0 = 4 + 4 * nlang
    english = []
    for i in range(count):
        p = struct.unpack_from("<I", pe.data, start + i * rec0 + 4)[0]
        english.append(read_utf16(pe, p) or "")
    wordy = sum(1 for t in english if " " in t.strip() or (t[:1].isupper() and t[1:].islower()))
    if wordy < count * 0.5:
        print(f"Found a {count}-entry table, but it doesn't look like menu text "
              f"(only {wordy} entries look like words or sentences; first few: "
              f"{english[:5]}). Nothing written. This castle.exe build needs the "
              "menu words matched by hand instead.", file=sys.stderr)
        return 2

    rec = 4 + 4 * nlang
    out = {lang: [] for lang in langs}
    for i in range(count):
        number, *ptrs = struct.unpack_from(f"<{1 + nlang}I", pe.data, start + i * rec)
        for lang, p in zip(langs, ptrs):
            out[lang].append(f"{number} = {escape(read_utf16(pe, p) or '')}")

    args.out.mkdir(parents=True, exist_ok=True)
    for lang, lines in out.items():
        header = (f"# Castle Crashers text ({lang}), from your castle.exe by extract_strings.py.\n"
                  "# Your own game's words: keep on your PC and Vita, never share.\n")
        (args.out / f"strings_{lang}.txt").write_text(header + "\n".join(lines) + "\n", encoding="utf-8")
    print(f"{count} strings in {nlang} languages (table at file offset 0x{start:X}) -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
