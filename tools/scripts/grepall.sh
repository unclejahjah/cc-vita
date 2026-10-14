#!/bin/bash
# grepall.sh STRING: count of STRING in each game SWF
cd "/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files"
find . -iname "*.swf" | while read f; do
  n=$(grep -a -o -i "$1" "$f" | wc -l)
  [ "$n" -gt 0 ] && echo "$n $f"
done
