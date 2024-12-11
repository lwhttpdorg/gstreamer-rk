#! /bin/bash

builddir="$1"
output_dir="test_videos"
tool_args="--mp4 --mp4-rand-size --safestart"
generation_args="--small --ignore-edge-intra-pred --ignore-ipcm --config config/default.json"

if [[ -z "$builddir" ]]; then
  echo "Usage: build.sh <build_directory>"
  exit 1
fi

date -R
cd $builddir
if [ ! -d "h26forge" ]; then
  git clone https://github.com/h26forge/h26forge.git
fi

cd h26forge
chmod +x make.sh
./make.sh

mkdir -p $output_dir
for i in $(seq -f "%04g" 0 99); do
  ./h26forge $tool_args generate $generation_args -o $output_dir/video$i.264
  gst-launch-1.0 filesrc location=$output_dir/video$i.264.mp4 ! parsebin ! fakesink
done

date -R

