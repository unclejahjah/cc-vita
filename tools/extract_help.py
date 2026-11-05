#!/usr/bin/env python3
"""Help & Options' words from your own castle.exe, for the Vita's How to Play, Controls and
Settings books (src/help_book.c).

The executable fills those pages itself, from UTF-16 strings it keeps outside the numbered
text table that extract_strings.py reads. This finds each one by a hash of its English
text (so no game text lives in this repository), applies the few Vita wording changes
(the R button instead of the right trigger) and writes them as help.* keys:

    python extract_help.py --exe "<working folder>/Steam PC copy/original/castle.exe" --out strings_help_en.txt

Copy the output to ux0:data/CastleCrashers/strings_help_en.txt. It is your game's text:
keep it on your PC and Vita, never share it.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

# key, first 12 hex digits of sha1(UTF-8 English text)
KEYS = [
    ("help.howto.1.title", "400679f12013"),
    ("help.howto.1.txt1", "6c4a232ce507"),
    ("help.howto.1.txt2", "518d9b0f8f11"),
    ("help.howto.1.txt3", "0688d0d0b6bf"),
    ("help.howto.1.txt4", "4ee0ca2e434f"),
    ("help.howto.1.txt5", "20d22c296b94"),
    ("help.howto.1.txt6", "8b6ea4963e96"),
    ("help.howto.1.txt7", "67c0270ae024"),
    ("help.howto.1.txt8", "3598ca094813"),
    ("help.howto.1.txt9", "58d9ecc5b8ab"),
    ("help.howto.2.txt1", "aa5dba4af16d"),
    ("help.howto.2.txt2", "cf6508baf959"),
    ("help.howto.2.txt3", "f0344d78528b"),
    ("help.howto.2.txt4", "0d1bb0655b1e"),
    ("help.howto.2.txt5", "598d472a47d2"),
    ("help.howto.2.txt6", "ac331e6c605f"),
    ("help.howto.2.txt7", "e926c58a4dbd"),
    ("help.howto.2.txt8", "5ffd04bb906e"),
    ("help.howto.3.title", "534421625645"),
    ("help.howto.3.txt1", "1bd3bbf2121c"),
    ("help.howto.3.txt2", "55760a263ed3"),
    ("help.howto.3.txt3", "9a694e07bcac"),
    ("help.howto.3.txt4", "32e4b77be1a3"),
    ("help.howto.3.txt5", "91ed2fd3ef93"),
    ("help.howto.3.txt6", "acd8ef932801"),
    ("help.howto.3.txt7", "3b7092faf7b8"),
    ("help.howto.3.txt8", "1a452f6f571d"),
    ("help.howto.3.txt9", "1509d35d75f2"),
    ("help.howto.3.txt10", "21cfdd5f6f5f"),
    ("help.howto.4.title", "e5fc0a68533a"),
    ("help.howto.4.txt1", "146ee7c42283"),
    ("help.howto.4.txt2", "f43bb660c6fa"),
    ("help.howto.4.txt3", "0a73f1beee12"),
    ("help.howto.4.txt4", "ca299b485ba8"),
    ("help.howto.4.txt5", "5af68289fc69"),
    ("help.howto.4.txt6", "882d70b8c69e"),
    ("help.howto.4.txt7", "e341ca155410"),
    ("help.howto.4.txt8", "c73277375239"),
    ("help.howto.4.txt9", "35a086d1c4f3"),
    ("help.howto.4.txt10", "3e8248e32edf"),
    ("help.howto.4.txt11", "b454c37c10e2"),
    ("help.howto.5.title", "f32d0645a833"),
    ("help.howto.5.txt1", "2f67d2c59a71"),
    ("help.howto.5.txt2", "61a2758a0a4b"),
    ("help.howto.5.txt3", "568a6d707c0f"),
    ("help.howto.5.txt4", "87ba1b123629"),
    ("help.howto.5.txt5", "bade4f29a8fa"),
    ("help.howto.5.txt6", "e448607e817a"),
    ("help.howto.5.txt7", "e64f2ee680f1"),
    ("help.howto.5.txt8", "1d2fb6cf94db"),
    ("help.howto.5.txt9", "7d17765f84c9"),
    ("help.howto.5.txt10", "26f9a7ac961f"),
    ("help.howto.5.txt11", "27f367f0cd9f"),
    ("help.howto.5.txt12", "7d206e7683eb"),
    ("help.howto.5.txt13", "8e4b35bee430"),
    ("help.howto.5.txt14", "51031a623246"),
    ("help.howto.6.title", "973b6cd2dd77"),
    ("help.howto.6.txt1", "15adcc338ba0"),
    ("help.howto.6.txt2", "186869b46418"),
    ("help.howto.6.txt3", "3483addba8fa"),
    ("help.howto.6.txt4", "34c1c617209c"),
    ("help.howto.6.txt5", "350bc8a5763d"),
    ("help.howto.6.txt6", "d183dbcf8a79"),
    ("help.howto.7.title", "f718846620df"),
    ("help.howto.7.txt1", "b493b4a09313"),
    ("help.howto.7.txt2", "9a55cbd26b3b"),
    ("help.howto.7.txt3", "e2fcdf765266"),
    ("help.howto.7.txt4", "0faef4f6e68f"),
    ("help.howto.7.txt5", "06dc5b73b6b0"),
    ("help.title.howto", "9686238f5aa8"),
    ("help.title.controls", "bee75ca77f75"),
    ("help.title.settings", "c7f73bb54d92"),
    ("help.settings.music", "3d6052382b67"),
    ("help.settings.sfx", "729429a60311"),
    ("help.controls.block", "82dd2cdf36f9"),
    ("help.controls.move", "76cdb9507216"),
    ("help.controls.jump", "1eba140fdd9c"),
    ("help.controls.heavy", "878e65de2fd8"),
    ("help.controls.light", "edd1a31d906e"),
    ("help.controls.item", "ebf61ab9c1d9"),
    ("help.controls.select", "2aaac16a0c10"),
    ("help.prev", "859d455b8ebd"),
    ("help.next", "edee9402d198"),
    ("help.howto.2.title", "e6791be7eed7"),
    ("help.howto.3.strength", "24d3e4fdd9a8"),
    ("help.howto.3.defense", "3c284a10d010"),
    ("help.howto.3.magic", "e6791be7eed7"),
    ("help.howto.3.agility", "0fc9a6edb3ce"),
    ("help.controls.pause", "781961bc81c2"),
    ("help.title.credits", "bfac50d6424b"),
]

# the Vita has an R button where the pads had a right trigger
VITA_WORDING = [("right trigger", "R button")]


def strings_in(data: bytes) -> dict[str, str]:
    """Every NUL-terminated UTF-16LE string of 2+ printable characters, by hash."""
    found: dict[str, str] = {}
    for m in re.finditer(rb"(?:(?:[\x20-\x7e\x80-\xff][\x00-\x04\x20])|(?:\x26\x20)){2,}\x00\x00", data):
        if m.start() % 2:
            continue
        try:
            s = m.group()[:-2].decode("utf-16le")
        except UnicodeDecodeError:
            continue
        found.setdefault(hashlib.sha1(s.encode("utf-8")).hexdigest()[:12], s)
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    found = strings_in(a.exe.read_bytes())
    lines = ["# Castle Crashers Help & Options words (made by extract_help.py from your castle.exe). Not for git."]
    missing = []
    for key, h in KEYS:
        s = found.get(h)
        if s is None:
            missing.append(key)
            continue
        for old, new in VITA_WORDING:
            s = s.replace(old, new)
        lines.append(f"{key} = {s}")
    a.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {len(KEYS) - len(missing)} of {len(KEYS)} to {a.out}")
    if missing:
        print("not found (a different build?): " + ", ".join(missing), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
