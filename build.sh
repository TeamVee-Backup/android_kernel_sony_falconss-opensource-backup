#!/bin/bash
# Original Live by cybojenix <anthonydking@gmail.com>
# New Live/Menu by Caio Oliveira aka Caio99BR <caiooliveirafarias0@gmail.com>
# Colors by Aidas Lukošius aka aidasaidas75 <aidaslukosius75@yahoo.com>
# Toolchains by Suhail aka skyinfo <sh.skyinfo@gmail.com>
# Commands for Build of Xperia E1 by mpersano <mpr@fzort.org>
# And the internet for filling in else where

# You need to download https://github.com/TeamVee/android_prebuilt_toolchains
# Clone in the same folder as the kernel to choose a toolchain and not specify a location

# Main Process - Start

maindevice() {
defconfig="cyanogenmod_falconss_defconfig"
name="XperiaE1"
make $defconfig &> /dev/null | echo "$x - $name, setting..."
unset buildprocesscheck defconfigcheck
}

maintoolchain() {
clear
echo "-Toolchain choice-"
echo
if [ -f ../android_prebuilt_toolchains/aptess.sh ]
	then . ../android_prebuilt_toolchains/aptess.sh
else
	if [ -d ../android_prebuilt_toolchains ]; then
		echo "You not have APTESS Script in Android Prebuilt Toolchain folder"
		echo "Check the folder, using Manual Method now"
	else
		echo "-You don't have TeamVee Prebuilt Toolchains-"
	fi
	echo ""
	echo "Please specify a location"
	echo "and the prefix of the chosen toolchain at the end"
	echo "GCC 4.6 ex. ../arm-eabi-4.6/bin/arm-eabi-"
	echo
	echo "Stay blank if you want to exit"
	echo
	read -p "Place: " CROSS_COMPILE
	if ! [ "$CROSS_COMPILE" == "" ]
		then ToolchainCompile=$CROSS_COMPILE
	fi
fi
if ! [ "$CROSS_COMPILE" == "" ]
	then unset buildprocesscheck
fi
}

# Main Process - End

# Build Process - Start

buildprocess() {
if [ -f .config ]; then
	echo "$x - Building $customkernel"

	if [ -f arch/$ARCH/boot/zImage ]
		then rm -rf arch/$ARCH/boot/zImage | echo "Removing old kernel image before build"
	fi

	NR_CPUS=$(($(grep -c ^processor /proc/cpuinfo) + 1))
	echo "${bldblu}Building $customkernel with $NR_CPUS jobs at once${txtrst}"

	START=$(date +"%s")
	if [ "$buildoutput" == "OFF" ]
		then make zImage msm8610-v2-mtp.dtb modules -j${NR_CPUS} &>/dev/null | loop
		else make zImage msm8610-v2-mtp.dtb modules -j${NR_CPUS}
	fi
	END=$(date +"%s")
	BUILDTIME=$(($END - $START))

	if [ -f arch/$ARCH/boot/zImage ]; then
		buildprocesscheck="Already Done!"
		. genboot.sh
	else
		buildprocesscheck="Something goes wrong"
	fi
else
	ops
fi
}

loop() {
LEND=$(date +"%s")
LBUILDTIME=$(($LEND - $START))
echo -ne "\r\033[K"
echo -ne "${bldgrn}Build Time: $(($LBUILDTIME / 60)) minutes and $(($LBUILDTIME % 60)) seconds.${txtrst}"
if ! [ -f arch/$ARCH/boot/zImage ]; then
	sleep 1
	loop
fi
}

updatedefconfig(){
if [ -f .config ]; then
	clear
	echo "-${bldgrn}Updating defconfig${txtrst}-"
	echo
	if [ `cat arch/$ARCH/configs/$defconfig | grep "Automatically" | wc -l` -ge 1 ]
		then defconfigformat="Usual copy of .config format  | Complete"
		else defconfigformat="Default Linux Kernel format   | Small"
	fi
	echo "The actual defconfig is a:"
	echo "--$defconfigformat--"
	echo
	echo "This can update defconfig to:"
	echo "1) Default Linux Kernel format  | Small"
	echo "2) Usual copy of .config format | Complete"
	echo
	echo "e) Exit"
	echo
	read -p "Choice: " -n 1 -s x
	case "$x" in
		1 ) echo "Wait..."; make savedefconfig &>/dev/null; mv defconfig arch/$ARCH/configs/$defconfig;;
		2 ) cp .config arch/$ARCH/configs/$defconfig;;
		e ) ;;
		* ) ops;;
	esac
else
	ops
fi
}

# Build Process - End

# Menu - Start

buildsh() {
clear
echo "Simple Linux Kernel Build Script ($(date +%d"/"%m"/"%Y))"
echo "$customkernel $kernelversion.$kernelpatchlevel.$kernelsublevel - $kernelname"
echo "-${bldred}Clean${txtrst}-"
echo "1) Kernel | ${bldred}$cleankernelcheck${txtrst}"
echo "-${bldgrn}Main Process${txtrst}-"
echo "2) Device Choice    | ${bldgrn}$name${txtrst}"
echo "3) Toolchain Choice | ${bldgrn}$ToolchainCompile${txtrst}"
echo "-${bldyel}Build Process${txtrst}-"
echo "4) Build Kernel | ${bldyel}$buildprocesscheck${txtrst}"
if ! [ "$BUILDTIME" == "" ]
	then echo "   Build Time   | ${bldcya}$(($BUILDTIME / 60))m$(($BUILDTIME % 60))s${txtrst}"
fi
echo "-${bldblu}Special Menu${txtrst}-"
echo "5) Update Defconfig | ${bldblu}$defconfigcheck${txtrst}"
echo "-${bldmag}Options${txtrst}-"
echo "o) View Build Output | $buildoutput"
echo "g) Git Gui  |  k) GitK  |  s) Git Push  |  l) Git Pull"
echo "q) Quit"
echo
read -n 1 -p "${txtbld}Choice: ${txtrst}" -s x
case $x in
	1) echo "$x - Cleaning Kernel"; make clean mrproper &> /dev/null; unset buildprocesscheck name defconfig BUILDTIME;;
	2) maindevice;;
	3) maintoolchain;;
	4) buildprocess;;
	5) updatedefconfig;;
	o) if [ "$buildoutput" == "OFF" ]; then unset buildoutput; else buildoutput="OFF"; fi;;
	q) echo "$x - Ok, Bye!"; break;;
	g) echo "$x - Opening Git Gui"; git gui;;
	k) echo "$x - Opening GitK"; gitk;;
	s) echo "$x - Pushing to remote repo"; git push --verbose --all; sleep 3;;
	l) echo "$x - Pushing to local repo"; git pull --verbose --all; sleep 3;;
	*) ops;;
esac
}

# Menu - End

# The core of script is here!

ops() {
echo "$x - This option is not valid"; sleep 1
}

if [ ! "$BASH_VERSION" ]
	then echo "Please do not use sh to run this script, just use . build.sh"
elif [ -e build.sh ]; then
	# Stock Color
	txtrst=$(tput sgr0)
	# Bold Colors
	txtbld=$(tput bold) # Bold
	bldred=${txtbld}$(tput setaf 1) # red
	bldgrn=${txtbld}$(tput setaf 2) # green
	bldyel=${txtbld}$(tput setaf 3) # yellow
	bldblu=${txtbld}$(tput setaf 4) # blue
	bldmag=${txtbld}$(tput setaf 5) # magenta
	bldcya=${txtbld}$(tput setaf 6) # cyan
	bldwhi=${txtbld}$(tput setaf 7) # white

	customkernel=FalconSSKernel
	export ARCH=arm

	while true; do
		if [ "$buildoutput" == "" ]
			then buildoutput="${bldmag}ON${txtrst}"
		fi
		if [ "$buildprocesscheck" == "" ]
			then buildprocesscheck="Ready to do!"
		fi
		if [ "$CROSS_COMPILE" == "" ]
			then buildprocesscheck="Use 3 first"
		fi
		if [ "$defconfig" == "" ]; then
			buildprocesscheck="Use 2 first"
			defconfigcheck="Use 2 first"
		else
			defconfigcheck="Ready to do!"
		fi
		if [ -f .config ]
			then unset cleankernelcheck
			else cleankernelcheck="Already Done!"
		fi
		kernelversion=`cat Makefile | grep VERSION | cut -c 11- | head -1`
		kernelpatchlevel=`cat Makefile | grep PATCHLEVEL | cut -c 14- | head -1`
		kernelsublevel=`cat Makefile | grep SUBLEVEL | cut -c 12- | head -1`
		kernelname=`cat Makefile | grep NAME | cut -c 8- | head -1`
		release=$(date +%d""%m""%Y)

		buildsh
	done
else
	echo
	echo "Ensure you run this file from the SAME folder as where it was,"
	echo "otherwise the script will have problems running the commands."
	echo "After you 'cd' to the correct folder, start the build script"
	echo "with the . build.sh command, NOT with any other command!"
	echo
fi
