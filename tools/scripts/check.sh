#!/bin/bash
L=$HOME/vitasdk/arm-vita-eabi/lib
for f in $L/*_stub.a; do
  if $HOME/vitasdk/bin/arm-vita-eabi-nm -g --defined-only "$f" 2>/dev/null | grep -q -E ' sceSharedFbBegin$'; then echo "$f"; fi
done
