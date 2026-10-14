#!/bin/bash
cd "/mnt/c/Users/Jah/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files/levels"
for f in LEVEL4.swf LEVEL3.swf LEVEL7.swf; do
  echo "$f: AddObject $(grep -a -o 'f_AddObject' $f | wc -l), f_Depth $(grep -a -o 'f_Depth' $f | wc -l), swapDepths $(grep -a -o 'swapDepths' $f | wc -l), depth_mod $(grep -a -o 'depth_mod' $f | wc -l), onClipEvent-ish $(grep -a -o 'f_ObjectInit\|f_AddEnemy\|f_AddProp' $f | wc -l)"
done
