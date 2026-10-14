#!/bin/bash
# clipev.sh SWF-relative-path offset len
S=/mnt/c/Users/Alex_/AppData/Local/Temp/claude/C--Users-Alex--Documents-VITA-Casle-Crashers/4f3d2ef3-6525-4f67-9fbd-27c5e47494d3/scratchpad
G="/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
python3 -I $S/clipev.py $S/avm1.py $S/probe.py "$G/$1" $2 $3
