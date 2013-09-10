#ifndef __ARCH_ARM_MACH_MSM_SKY_SYS_RESET_H
#define __ARCH_ARM_MACH_MSM_SKY_SYS_RESET_H

#ifdef CONFIG_PANTECH_RESET_REASON

#define QCOM_RESTART_REASON_ADDR   (MSM_IMEM_BASE + 0x65C)
#define PANTECH_RESTART_REASON_OFFSET 0x68 //0x08//0x04
#define PANTECH_RESTART_REASON_ADDR   (QCOM_RESTART_REASON_ADDR + PANTECH_RESTART_REASON_OFFSET)


/*******************************************************************************
**  RESET REASON DEFINE (Must Have vender cust_pantech.h == kernel sky_sys_reset) START
*******************************************************************************/
#define SYS_RESET_REASON_MASK                      0xDEAD0000
#define SYS_RESET_BACKLIGHT_OFF_FLAG               0x20000000
	
#define SYS_RESET_REASON_LINUX_MASK                0xDEAD1100
#define SYS_RESET_REASON_LINUX                     0xDEAD11E1
#define SYS_RESET_REASON_USERDATA_FS               0xDEAD11E2
	
#define SYS_RESET_REASON_WATCHDOG_MASK             0xDEAD2200
#define SYS_RESET_REASON_WATCHDOG                  0xDEAD22E1
#define SYS_RESET_REASON_WATCHDOG_XPU              0xDEAD22E2
	
#define SYS_RESET_REASON_ABNORMAL_MASK             0xDEAD3300
#define SYS_RESET_REASON_ABNORMAL                  0xDEAD33E1
	
#define SYS_RESET_REASON_MDM_MASK                  0xDEAD4400
#define SYS_RESET_REASON_MDM                       0xDEAD44E1
	
#define SYS_RESET_REASON_LPASS_MASK                0xDEAD5500
#define SYS_RESET_REASON_LPASS                     0xDEAD55E1
	
#define SYS_RESET_REASON_DSPS_MASK                 0xDEAD6600
#define SYS_RESET_REASON_DSPS                      0xDEAD66E1
	
#define SYS_RESET_REASON_RIVA_MASK                 0xDEAD7700
#define SYS_RESET_REASON_RIVA                      0xDEAD77E1
	
#define SYS_RESET_REASON_RPM_MASK                  0xDEAD8800
#define SYS_RESET_REASON_RPM_DOGBARK               0xDEAD88E1
#define SYS_RESET_REASON_RPM_ERRFATAL              0xDEAD88E2
	
#define SYS_RESET_REASON_NORMAL_MASK               0xDEAD9900
#define SYS_RESET_REASON_NORMAL                    0xDEAD99E1
	
#define IS_SYS_RESET_N_REBOOT                      (((*(unsigned int *)PANTECH_RESTART_REASON_ADDR)& ~SYS_RESET_BACKLIGHT_OFF_FLAG) & ~(0xFFFF)) == (SYS_RESET_REASON_MASK)
#define IS_SYS_RESET                               ((((*(unsigned int *)PANTECH_RESTART_REASON_ADDR)& ~SYS_RESET_BACKLIGHT_OFF_FLAG) & ~(0xFFFF)) == (SYS_RESET_REASON_MASK)) && \
                                                   ((((*(unsigned int *)PANTECH_RESTART_REASON_ADDR)& ~SYS_RESET_BACKLIGHT_OFF_FLAG) & ~(0xFF)) != (SYS_RESET_REASON_NORMAL_MASK))
#define IS_BACKLIGHT_OFF_FLAG                      (((*(unsigned int *)PANTECH_RESTART_REASON_ADDR)& SYS_RESET_BACKLIGHT_OFF_FLAG)) == (SYS_RESET_BACKLIGHT_OFF_FLAG)
#define WHAT_SYS_RESET_GROUP                       ((*(unsigned int *)PANTECH_RESTART_REASON_ADDR)& ~SYS_RESET_BACKLIGHT_OFF_FLAG) & ~(0xFF)
#define WHAT_SYS_RESET                             ((*(unsigned int *)PANTECH_RESTART_REASON_ADDR)& ~SYS_RESET_BACKLIGHT_OFF_FLAG)
/*******************************************************************************
**  RESET REASON DEFINE (Must Have vender cust_pantech.h == kernel sky_sys_reset) END
*******************************************************************************/

extern int sky_reset_reason;
extern void sky_sys_rst_init_reboot_info(void);
#ifdef CONFIG_F_SKYDISP_SILENT_BOOT
extern uint8_t sky_sys_rst_get_silent_boot_mode(void);
extern uint8_t sky_sys_rst_get_silent_boot_backlight(void);
extern void  sky_sys_rst_set_silent_boot_backlight(int backlight);
#endif
extern void sky_sys_rst_set_reboot_info(int reset_reason);

#endif
#endif
// CONFIG_PANTECH_RESET_REASON

