#!/bin/bash
# py.sh SCRIPT.py GAMEFILE args...: run a scratch script that takes probe.py and a game file (path relative to the game dir)
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
G="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
script=$1; file=$2; shift 2
case "$script" in
  spriteact.py|rootcode.py) python3 -I $S/$script $S/avm1.py $S/probe.py "$G/$file" "$@" ;;
  *) python3 -I $S/$script $S/probe.py "$G/$file" "$@" ;;
esac
