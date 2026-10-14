#!/bin/bash
# fmain.sh KEYWORD [swf-name-pattern]: which game SWFs contain KEYWORD; disassemble its functions
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
cd "/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
find . -iname "${2:-*}.swf" | while read f; do
  if grep -q -a "$1" "$f"; then
    echo "### $f"
    python3 -I $S/avm1.py $S/probe.py "$f" "$1"
  fi
done
