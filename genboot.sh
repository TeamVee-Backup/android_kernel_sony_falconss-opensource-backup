#!/bin/bash
# Commands for Build boot.img of Xperia E1 by mpersano <mpr@fzort.org>

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
