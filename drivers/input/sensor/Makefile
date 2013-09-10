#
# Makefile for the sensors drivers.
#

module_build=n

ifeq (${module_build},n)
obj-$(CONFIG_INPUT_IR_SENSOR) += pac7620.o pac7672_als.o
else
obj-m += pac7620.o pac7672_als.o

ANDROID_ROOT_DIR:=/home/p11456/APQ8064/EF52K/APQ8064_JB/
KDIR := $(ANDROID_ROOT_DIR)out/target/product/msm8960/obj/KERNEL_OBJ/
PWD := $(shell pwd)
ARCH:=arm
CROSS_COMPILE:=$(ANDROID_ROOT_DIR)prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-

#EXTRA_CFLAGS += -include ../../../../../../../BOARD_REV.h

all:
	@echo $(PWD)
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	rm -f -r *.o *.ko .*cmd *.mod.c .tmp* core *.i modules.order Module.symvers modules.builtin

endif
