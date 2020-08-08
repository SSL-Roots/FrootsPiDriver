#!/bin/bash -vxe

dir=$(dirname $0)/../

if [ -e "/sys/class/frootspi_hello" ]; then
    sudo rmmod frootspi
fi

echo $dir
cd $dir/src/drivers/
#make clean
make
sudo insmod frootspi.ko
sleep 1
sudo chmod 666 /dev/frootspi*
