#!/bin/bash
export VITASDK=$HOME/vitasdk
export PATH=$VITASDK/bin:$PATH
yes | vdpm libvita2d libjpeg-turbo 2>&1 | tail -20
ls -l $VITASDK/arm-vita-eabi/include/vita2d.h $VITASDK/arm-vita-eabi/lib/libvita2d.a $VITASDK/arm-vita-eabi/lib/libjpeg.a
