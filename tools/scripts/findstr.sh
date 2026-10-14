#!/bin/bash
# findstr.sh FILE STRING : which tags contain STRING
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
for off in $(grep -a -o -b "$2" "$1" | cut -d: -f1 | head -5); do
  echo "offset $off:"
  python3 -I $S/whichtag.py $S/probe.py "$1" $off
done
