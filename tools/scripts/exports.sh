#!/bin/bash
# which game SWFs export the given symbol names
cd "/mnt/c/Users/Jah/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files/game"
for n in "$@"; do
  echo "$n: $(grep -l -a "$n" *.swf | tr '\n' ' ')"
done
