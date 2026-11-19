#!/bin/bash
# usage: phone-editor-shot.sh <tag> [ENV=VAL ...]   -> scratchpad/phone-dumps/<tag>.png
ADB="${ADB:-adb}"
S="${VG_SHOTS:-/tmp/vg-phone}"; mkdir -p "$S/phone-dumps"
tag=$1; shift
LIB=$($ADB shell pm path com.vstghost | tr -d '\r' | sed 's#package:##; s#/base.apk#/lib/arm64#')
$ADB shell "run-as com.vstghost env LIBDIR=$LIB $* sh files/phone-run.sh -all,err+all /data/user/0/com.vstghost/files/rootfs/host/vstghost-host.exe editor 'Z:/data/user/0/com.vstghost/files/plugins/${PLUG:-Dexed}.vst3/Contents/x86_64-win/${PLUG:-Dexed}.vst3' --seconds ${SECS:-20} --shot 'Z:/data/user/0/com.vstghost/files/shot-$tag.bmp'" 2>&1 | grep -v "^\[BOX64\] Using" | grep -E "^\{|editor:|err:|SIG|free\(\)" | head -8
$ADB exec-out run-as com.vstghost cat files/shot-$tag.bmp > "$S/phone-dumps/$tag.bmp" && python -I "$(dirname "$0")/bmp2png.py" "$S/phone-dumps/$tag.bmp" "$S/phone-dumps/$tag.png" && ls -la "$S/phone-dumps/$tag.png"
