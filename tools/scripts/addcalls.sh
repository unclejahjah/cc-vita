#!/bin/bash
cd "/mnt/c/Users/Jah/Documents/VITA Casle Crashers/Raw Game Files-20261001T162609Z-1-001/Raw Game Files/levels"
cat LEVEL*.swf | grep -a -o 'f_Add[A-Za-z0-9_]*\|f_Register[A-Za-z0-9_]*\|f_Depth[A-Za-z]*' | sort | uniq -c | sort -rn | head -30
