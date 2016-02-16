#!/bin/bash
# Commands for Build boot.img of Xperia E1 by mpersano <mpr@fzort.org>
# RAMDisk is from Xperia E1 Single
# This a force boot.img, i can't make a live boot changer like in LG, because bootloader

rm -rf boot-creator/boot.img

./boot-creator/tool/mkqcdtbootimg \
    --kernel arch/arm/boot/zImage \
    --ramdisk boot-creator/ramdisk/ramdisk.gz \
    --dt_dir arch/arm/boot/ \
    --cmdline "`cat boot-creator/ramdisk/cmdline`" \
    --base 0x00000000 \
    --ramdisk_offset 0x2008000 \
    --kernel_offset 0x10000 \
    --tags_offset 0x1e08000 \
    --pagesize 2048 \
    -o boot-creator/boot.img
