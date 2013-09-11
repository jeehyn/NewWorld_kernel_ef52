#!/bin/bash
#######################################################
#  PANTECH defined Environment                        #
#######################################################
# export TARGET_BUILD_SKY_MODEL_NAME=IM-A750K
# export TARGET_BUILD_SKY_MODEL_ID=MODEL_EF32K
# export TARGET_BUILD_SKY_BOARD_VER=EF32K_ES20
# export TARGET_BUILD_SKY_FIRMWARE_VER=S0832143
# export TARGET_BUILD_SKY_CUST_INCLUDE=$PWD/include/CUST_SKY.h
# export TARGET_BUILD_SKY_CUST_INCLUDE_DIR=$PWD/include
#######################################################

ODIR=../build_ef52k
image_filename="$ODIR/arch/arm/boot/zImage"
TOOLCHAIN=../../../linaro/bin/arm-eabi-

export ARCH=arm
export toolchain=../../../linaro/bin/arm-eabi-
KERNEL_DEFCONFIG=msm8960_ef52k_tp20_perf_defconfig

if [ ! -d $ODIR ]; then
  mkdir $ODIR
  chmod 755 $ODIR
fi

if [ ! -f $ODIR/.config ]; then
  echo "load defconfig"
  make O=$ODIR ARCH=arm CROSS_COMPILE=$TOOLCHAIN $KERNEL_DEFCONFIG 
fi

if [ "$1" = "" ]; then
  make CONFIG_NO_ERROR_ON_MISMATCH=y CONFIG_DEBUG_SECTION_MISMATCH=y -j16 O=$ODIR ARCH=arm CROSS_COMPILE=$TOOLCHAIN zImage
elif [ "$1" == "modules" ]; then
  make CONFIG_NO_ERROR_ON_MISMATCH=y CONFIG_DEBUG_SECTION_MISMATCH=y -j16 O=$ODIR ARCH=arm CROSS_COMPILE=$TOOLCHAIN modules
  cd ..
  ./getmod.sh
else
  make -j16 O=$ODIR ARCH=arm CROSS_COMPILE=$TOOLCHAIN $1 $2 $3
fi

if [ -f $image_filename ]; then
  echo "   make boot.img"
  ./ktmake.sh
fi


