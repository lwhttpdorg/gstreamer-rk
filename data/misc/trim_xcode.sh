#!/bin/bash

if [[ -z $1 ]]; then
    echo "Usage: $0 Xcode_*.xip"
    exit 1
fi

STEM=${1%.*}
VER=${STEM##*_}

xip -x "$1"
find Xcode.app/ -mindepth 1 -not -path 'Xcode.app/Contents/Developer/Platforms/MacOSX.platform/*' -not -path 'Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/*' -delete
rm -rf Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/*swift*
rm -rf Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/*swift*/
if type -P xz; then
    echo "Creating trimmed xcode-$VER.tar.xz (parallel compression)"
    tar -c --use-compress-program='xz -T0' -f "xcode-$VER.tar.xz" Xcode.app
else
    echo "Creating trimmed xcode-$VER.tar.xz (single-threaded compression)"
    tar -cJf "xcode-$VER.tar.xz" Xcode.app
fi
