#!/bin/bash
export VITASDK=$HOME/vitasdk
export PATH=$VITASDK/bin:$PATH
echo "--- sdk libs"
ls $VITASDK/arm-vita-eabi/lib | grep -i -E 'vita2d|png|jpeg|freetype|libz\.a'
cd "/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/cc-vita" || exit 1
mkdir -p build && cd build && cmake .. && make 2>&1
echo "EXIT=$?"
ls -l *.vpk 2>/dev/null
