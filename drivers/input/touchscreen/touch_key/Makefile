#
# Makefile for the earjack drivers.
#

obj-$(CONFIG_TOUCHSCREEN_CYTTSP_TOUCHKEY) += cypress-touchkey.o issp_driver_routines.o issp_main.o issp_routines.o
#obj-m += cypress-touchkey.o issp_driver_routines.o issp_main.o issp_routines.o

ANDROID_ROOT_DIR:=../../../../../
KDIR := $(ANDROID_ROOT_DIR)out/target/product/msm8960/obj/KERNEL_OBJ/
PWD := $(shell pwd)
ARCH:=arm
CROSS_COMPILE:=$(ANDROID_ROOT_DIR)../prebuilt/linux-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-

#EXTRA_CFLAGS += -include ../../../../../../../BOARD_REV.h

CYTTSP_TOUCHKEY_DEBUG=n

ifeq ($(CYTTSP_TOUCHKEY_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
EXTRA_CFLAGS += -DVERBOSE_DEBUG#VERBOSE_DEBUG
endif

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	rm -f -r *.o *.ko .*cmd *.mod.c .tmp* core *.i modules.order Module.symvers modules.builtin
