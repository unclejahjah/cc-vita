#!/bin/bash
export VITASDK=$HOME/vitasdk
export PATH=$VITASDK/bin:$PATH
cd "/mnt/c/Users/Alex_/Documents/VITA Casle Crashers/cc-vita/build"
arm-vita-eabi-readelf -lW cc_vita | head -20
arm-vita-eabi-readelf -SW cc_vita | grep -E "sce_gap|eh_frame|init_array"
