#ifndef __CYPRESS_TOUCH_H__
#define __CYPRESS_TOUCH_H__

#include <linux/irq.h>
#include <linux/mfd/pm8xxx/pm8921.h>

#define PM8921_GPIO_BASE                NR_GPIO_IRQS
#define PM8921_GPIO_PM_TO_SYS(pm_gpio)  (pm_gpio - 1 + PM8921_GPIO_BASE)
#define PM8921_MPP_BASE                 (PM8921_GPIO_BASE + PM8921_NR_GPIOS)
#define PM8921_MPP_PM_TO_SYS(pm_gpio)   (pm_gpio - 1 + PM8921_MPP_BASE)
#define PM8921_IRQ_BASE                 (NR_MSM_IRQS + NR_GPIO_IRQS)

#if (CONFIG_BOARD_VER < CONFIG_WS10)
#define GPIO_TOUCH_SCL      (28)
#else
#define GPIO_TOUCH_SCL      (62)
#endif

#define GPIO_TOUCH_SDA      (6)
#define GPIO_TOUCH_INT      (59)
#define IRQ_TOUCH_INT	    gpio_to_irq(GPIO_TOUCH_INT) //OMAP_GPIO_IRQ(GPIO_TOUCH_INT)
#define TOUCH_RESET			86

#define GPIO_TOUCH_LED		(33)

#if (CONFIG_BOARD_VER < CONFIG_WS10)
#define TOUCH_1_8_POWER	62
#else
#define TOUCH_1_8_POWER		PM8921_GPIO_PM_TO_SYS(12)
#endif

#define CONFIG_CYPRESS_TOUCHKEY_DEBUG

#ifdef CONFIG_CYPRESS_TOUCHKEY_DEBUG
#define dbg(args...)	{	\
				printk("[touchkey] %s: ", __FUNCTION__);	\
				printk(args);	\
			}
#else
#define dbg(args...)
#endif


#endif // __CYPRESS_TOUCH_H__