#!/bin/bash
export PATH=$HOME/vitasdk/bin:$PATH
arm-vita-eabi-nm $HOME/vitasdk/arm-vita-eabi/lib/libvita2d.a 2>/dev/null | grep -E " [BDCT] _?vita2d|_vita2d" | grep -i -E "color|ortho|wvp|matrix|context|pool|draw_array" | sort -u
echo "--- flags"
grep -n -E "FLAGS|march|mfpu|mfloat" $HOME/vitasdk/share/vita.toolchain.cmake | head
