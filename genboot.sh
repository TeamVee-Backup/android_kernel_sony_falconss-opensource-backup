#!/bin/bash
# Commands for Build boot.img of Xperia E1 by mpersano <mpr@fzort.org>
# RAMDisk is from Xperia E1 Single
# This a force boot.img, i can't make a live boot changer like in LG, because bootloader

rm -rf boot-creator/*.img

if [ "$1" == "-b" ]
    then PREBUILT="--dt_dir arch/arm/boot"
    else PREBUILT="--qcdt boot-creator/dtimg/$VARIANT-dt"
fi

makeqcdtbootimg(){
cp boot-creator/ramdisk/$1-ramdisk boot-creator/ramdisk/$1-ramdisk.gz
./boot-creator/tool/mkqcdtbootimg \
    --kernel arch/arm/boot/zImage \
    --ramdisk boot-creator/ramdisk/$1-ramdisk \
    $PREBUILT \
    --cmdline "`cat boot-creator/ramdisk/cmdline`" \
    --base 0x00000000 \
    --ramdisk_offset 0x2000000 \
    --kernel_offset 0x10000 \
    --tags_offset 0x01e00000 \
    --pagesize 2048 \
    -o boot-creator/$1-boot.img
}

makeqcdtbootimg single

makeqcdtbootimg dual

makeqcdtbootimg tv
