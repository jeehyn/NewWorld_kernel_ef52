mkbootimg --cmdline " console=NULL,115200,n8 androidboot.hardware=qcom user_debug=31 msm_rtb.filter=0x3F ehci-hcd.park=3 maxcpus=2 loglevel=0 vmalloc=0x12c00000" --base 0x80200000 --pagesize 2048 --kernel ../build_ef52k/arch/arm/boot/zImage --ramdisk lgboot.img-ramdisk.gz  --ramdiskaddr 0x82200000 -o ../lgboot.img



