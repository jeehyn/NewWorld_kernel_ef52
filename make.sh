#!/bin/bash

ODIR=../build_ef52k
image_filename="$ODIR/arch/arm/boot/zImage"
TOOLCHAIN=../linaro/bin/arm-eabi-

export ARCH=arm
export CROSS_COMPILE=../linaro/bin/arm-eabi-
KERNEL_DEFCONFIG=oc_qsound_defconfig

if [ ! -d $ODIR ]; then
  mkdir $ODIR
  chmod 755 $ODIR
fi

if [ ! -f $ODIR/.config ]; then
  echo "load defconfig"
  make O=$ODIR ARCH=arm CROSS_COMPILE=$TOOLCHAIN $KERNEL_DEFCONFIG 
fi

if [ "$1" = "" ]; then
  make CONFIG_NO_ERROR_ON_MISMATCH=y CONFIG_DEBUG_SECTION_MISMATCH=y -j32 O=$ODIR ARCH=arm CROSS_COMPILE=$TOOLCHAIN zImage
elif [ "$1" == "testbuild" ]; then
./make.sh && ./make.sh curzip oc_qsound

elif [ "$1" == "curzip" ]; then
if [ "$2" == "" ]; then
echo "Error : You must enter zImage's name"
echo "Usage : ./make.sh curzip [zImage name]"
exit
fi
echo "zImage setting is $2..."
	./zimage.sh	
	cd ..
	./irongetmod.sh
mv -f zImage out/ef52/zImage/$2
  mv -f cfg80211.ko out/ef52/cfg80211.ko
  mv -f exfat_fs.ko out/ef52/exfat_fs.ko
  mv -f wlan.ko out/ef52/wlan.ko
  mv -f WCNSS_cfg.dat out/ef52/WCNSS_cfg.dat
  cd out/ef52/
  zip -r "package.zip" "META-INF" "tool" "zImage" *.*
  mv -f package.zip ../../../shared/package.zip
echo "Success :D"

elif [ "$1" == "fullbuild" ]; then
if [ "$2" == "" ]; then
echo "Error : You must enter zImage's name"
echo "Usage : ./make.sh fullbuild [zImage name]"
exit
fi
	./make.sh
	./make.sh modules
# for modpost kernel modules...
	./make.sh
	./make.sh curzip $2
elif [ "$1" == "modules" ]; then
  make CONFIG_NO_ERROR_ON_MISMATCH=y CONFIG_DEBUG_SECTION_MISMATCH=y -j16 O=$ODIR ARCH=arm CROSS_COMPILE=$TOOLCHAIN modules
  cd ..
  ./irongetmod.sh
elif [ "$1" == "bootimg" ]; then
	./make.sh
  echo "   make boot.img"
  ./ktmake.sh
else
  make -j16 O=$ODIR ARCH=arm CROSS_COMPILE=$TOOLCHAIN $1 $2 $3
fi


