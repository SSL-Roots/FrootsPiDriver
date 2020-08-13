#!/bin/bash -vxe

dir=$(dirname $0)/../

cd $dir/src/dts/
dtc -@ -I dts -O dtb -o mygpio.dtbo mygpio-overlay.dts
sudo cp mygpio.dtbo /boot/firmware/overlays/

echo "dtboを反映するため再起動してください"
