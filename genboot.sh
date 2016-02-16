#!/bin/bash
# Commands for Build boot.img of Xperia E1 by mpersano <mpr@fzort.org>
# RAMDisk is from Xperia E1 Single
# This a force boot.img, i can't make a live boot changer like in LG, because bootloader

rm -rf boot-creator/*.img

if [ "$1" == "-p" ]; then
    PREBUILT="--qcdt boot-creator/dtimg/$VARIANT-dt"
else
    PREBUILT="--dt_dir arch/arm/boot"
fi

bootcreator(){
./boot-creator/tool/mkqcdtbootimg \
    --kernel arch/arm/boot/zImage \
    --ramdisk boot-creator/ramdisk/$VARIANT-ramdisk \
    $PREBUILT \
    --cmdline "`cat boot-creator/ramdisk/cmdline`" \
    --base 0x00000000 \
    --ramdisk_offset 0x2008000 \
    --kernel_offset 0x10000 \
    --tags_offset 0x1e08000 \
    --pagesize 2048 \
    -o boot-creator/$VARIANT-boot.img
}



VARIANT="single"
cp boot-creator/ramdisk/$VARIANT-ramdisk boot-creator/ramdisk/$VARIANT-ramdisk.gz
bootcreator

VARIANT="dual"
cp boot-creator/ramdisk/$VARIANT-ramdisk boot-creator/ramdisk/$VARIANT-ramdisk.gz
bootcreator

VARIANT="tv"
cp boot-creator/ramdisk/$VARIANT-ramdisk boot-creator/ramdisk/$VARIANT-ramdisk.gz
bootcreator
