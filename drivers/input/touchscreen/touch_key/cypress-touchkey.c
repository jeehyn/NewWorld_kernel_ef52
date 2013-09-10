/*
 * Driver for keys on GPIO lines capable of generating interrupts.
 *
 * Copyright 2005 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <asm/gpio.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/earlysuspend.h>
#include <asm/io.h>
#include <linux/spinlock.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/device.h>


#include "cypress-touchkey.h"


static DEFINE_SPINLOCK(touchkey_lock);
static int i2c_lock;



#if 0
// Panda Board
#define OMAP_MUX_MODE0      0
#define OMAP_MUX_MODE3      3
#define OMAP_PULL_ENA			(1 << 3)
#define OMAP_PULL_UP			(1 << 4)
#define OMAP_INPUT_EN			(1 << 8)
#define OMAP_PIN_INPUT_PULLUP		(OMAP_PULL_ENA | OMAP_INPUT_EN | OMAP_PULL_UP)
#define OMAP_PIN_INPUT_PULLDOWN		(OMAP_PULL_ENA | OMAP_INPUT_EN)

#define SET_TOUCH_I2C()				{omap4_ctrl_pad_writel((OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP)<<16, 0x012C); \
                                     omap4_ctrl_pad_writel((OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP), 0x0130); }

// No pull-up
#define SET_TOUCH_I2C_TO_GPIO()		{omap4_ctrl_pad_writel((OMAP_MUX_MODE3 | OMAP_INPUT_EN)<<16, 0x012C); \
                                     omap4_ctrl_pad_writel((OMAP_MUX_MODE3 | OMAP_INPUT_EN), 0x0130); }

#define SET_TOUCH_I2C_TO_PD()		{omap4_ctrl_pad_writel((OMAP_MUX_MODE3 | OMAP_PIN_INPUT_PULLDOWN)<<16, 0x012C); \
                                     omap4_ctrl_pad_writel((OMAP_MUX_MODE3 | OMAP_PIN_INPUT_PULLDOWN), 0x0130); }

#endif



#define TOUCH_FIRMWARE_VERSION      (0x4)
#define CURRENT_FIRMWARE_VERSION    (TOUCH_FIRMWARE_VERSION)


/*
cypress touchkey register
*/

#define KEYCODE_REG 0x00
#define FIRMWARE_VERSION 0x01
#define TOUCHKEY_MODULE_VERSION 0x02
#define CMD_REG 0x03
#define THRESHOLD_REG 0x04
#define SENS_REG 0x05
#define IDAC_REG 0x06
#define DIFF_DATA_REG 0x0A
#define RAW_DATA_REG 0x0E
#define BASELINE_REG 0x12

#define TOUCH_LED	0x90

/* Command for 0x00 */
#define LED_ON_CMD 0x10
#define LED_OFF_CMD 0x20

#define SENS_EN_CMD 0x40
#define AUTO_CAL_MODE_CMD 0x50
#define SLEEP_CMD 0x80

#define LED_AUTO	0x70

/* Command for 0x03 */
#define AUTO_CAL_EN_CMD 0x01

#define TOUCHKEY_ADDRESS	0x20

#define UPDOWN_EVENT_BIT 0x08
#define KEYCODE_BIT 0x07
#define COMMAND_BIT 0xF0

#define I2C_M_WR 0		/* for i2c */

#define DEVICE_NAME "cypress_touchkey"

static int touchkey_keycode[3] = { 0, KEY_MENU, KEY_BACK };

static u16 menu_sensitivity = 0;
static u16 back_sensitivity = 0;
static u16 raw_data0 = 0;
static u16 raw_data1 = 0;
static u8 idac0 = 0;
static u8 idac1 = 0;
static int touchkey_enable = 0;
//static int force_firm_update = 0;


struct i2c_touchkey_driver {
	struct  i2c_client *client;
	struct  input_dev *input_dev;
    struct  device *dev;
	struct  early_suspend early_suspend;

    struct workqueue_struct *touchkey_wq;
    struct work_struct touchkey_work;
    struct work_struct touch_update_work;
};
struct i2c_touchkey_driver *touchkey_driver = NULL;


static const struct i2c_device_id cypress_touchkey_id[] = {
	{"cypress_touchkey", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, cypress_touchkey_id);


static void init_hw(void);
static int i2c_touchkey_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int i2c_touchkey_remove(struct i2c_client *client);
void touchkey_update_func(struct work_struct *p);
static void touchkey_sysfile_init(void);

void touch_led_ioctl(void);

static u8 touchkey_led_status;

static int touch_version = 0;


struct i2c_driver touchkey_i2c_driver = {
    .probe = i2c_touchkey_probe,
    .remove = i2c_touchkey_remove,
    .driver = {
		   .name = "cypress_touchkey",
		   },
	.id_table = cypress_touchkey_id,
};

static int i2c_touchkey_read(u8 reg, u8 * val, unsigned int len)
{
	int err = 0;
	int retry = 10;

	struct i2c_msg msg[2];

	if ((touchkey_driver == NULL)) {
		printk(KERN_ERR "[TouchKey] touchkey is not enabled.R\n");
		return -ENODEV;
	}

	while (retry--) {
		msg[0].addr = touchkey_driver->client->addr;
		msg[0].flags = I2C_M_WR;
		msg[0].len = 1;
		msg[0].buf = &reg;

		msg[1].addr = touchkey_driver->client->addr;
		msg[1].flags = I2C_M_RD;
		msg[1].len = len;
		msg[1].buf = val;

		err = i2c_transfer(touchkey_driver->client->adapter, msg, 2);
		if (err >= 0) {
			return 0;
		}
		printk(KERN_ERR "[TouchKey] %s %d i2c transfer error\n", __func__, __LINE__);	/* add by inter.park */
		msleep(10);
	}
	return err;
}


static int i2c_touchkey_write(u8 reg, u8 * val, unsigned int len)
{
    int err = 0;
    struct i2c_msg msg[1];
    unsigned char data[len+1];
    int retry = 2;

    if ((touchkey_driver == NULL) || !(touchkey_enable == 1)) {
        //printk(KERN_ERR "[TouchKey] touchkey is not enabled.\n");
        return -ENODEV;
    }

    while (retry--) {
        data[0] = reg;
        memcpy(data + 1, val, len);
        msg->addr = touchkey_driver->client->addr;
        msg->flags = I2C_M_WR;
        msg->len = len+1;
        msg->buf = data;
        err = i2c_transfer(touchkey_driver->client->adapter, msg, 1);
        if (err >= 0) {
            return 0;
        }
        printk(KERN_DEBUG "[TouchKey] %s %d i2c transfer error\n",
        __func__, __LINE__);
        msleep(10);
    }
    return err;
}




#define AVDD_POWER	"cam2pwr"

extern int touch_ctrl_regulator(int on_off);

bool regulator_on = false;

int touch_ctrl_regulator(int on_off)
{
	gpio_set_value(TOUCH_1_8_POWER, on_off);
	gpio_set_value(GPIO_TOUCH_LED, on_off);	
	return 0;
}

static void init_hw(void)
{
	int ret = 0;

	ret = gpio_direction_input(GPIO_TOUCH_INT);
	if (ret) {
		printk( "%s: gpio_direction_output GPIO_TOUCH_INT : %d failed, rc=%d\n",__func__, GPIO_TOUCH_INT, ret);
	}

	ret = gpio_request(TOUCH_RESET, "touch_reset");
	if (ret) {
		gpio_free(TOUCH_RESET);
		ret = gpio_request(TOUCH_RESET, "touch_reset");
		if (ret) {
			printk("%s: gpio_request TOUCH_RESET : %d failed, rc=%d\n",__func__, TOUCH_RESET, ret);
		}
	}

	ret = gpio_direction_output(TOUCH_RESET, 1);
	if (ret) {
		printk( "%s: gpio_direction_output TOUCH_RESET : %d failed, rc=%d\n",__func__, TOUCH_RESET, ret);
	}

	msleep(10);
	gpio_set_value(TOUCH_RESET, 0);	

	ret = gpio_request(TOUCH_1_8_POWER, "touch_power");
	if (ret) {
		gpio_free(TOUCH_1_8_POWER);
		ret = gpio_request(TOUCH_1_8_POWER, "touch_power");
		if (ret) {
			printk("%s: gpio_request TOUCH_1_8_POWER : %d failed, rc=%d\n",__func__, TOUCH_1_8_POWER, ret);
		}
	}

	ret = gpio_direction_output(TOUCH_1_8_POWER, 1);
	if (ret) {
		printk("%s: gpio_direction_output TOUCH_1_8_POWER : %d failed, rc=%d\n",__func__, TOUCH_1_8_POWER, ret);
	}

	ret = gpio_request(GPIO_TOUCH_LED, "touch_led");
	if (ret) {
		gpio_free(GPIO_TOUCH_LED);
		ret = gpio_request(GPIO_TOUCH_LED, "touch_led");
		if (ret) {
			printk("%s: gpio_request GPIO_TOUCH_LED : %d failed, rc=%d\n",__func__, GPIO_TOUCH_LED, ret);
		}
	}

	ret = gpio_direction_output(GPIO_TOUCH_LED, 1);
	if (ret) {
		printk("%s: gpio_direction_output GPIO_TOUCH_LED : %d failed, rc=%d\n",__func__, GPIO_TOUCH_LED, ret);
	}
	
}

void touchkey_work_func(struct work_struct *p)
{
	u8 data;
	int ret;

	//if (!gpio_get_value(GPIO_TOUCH_INT))
    {
		while(1) {
			spin_lock(&touchkey_lock);
			if(i2c_lock == 0) {
				break;
			}
			spin_unlock(&touchkey_lock);
		};
		i2c_lock = 1;
		spin_unlock(&touchkey_lock);
		ret = i2c_touchkey_read(KEYCODE_REG, &data, 1);
		spin_lock(&touchkey_lock);
		i2c_lock = 0;
		spin_unlock(&touchkey_lock);

		/******************************************************************
		typedef struct I2CReg
		{
			unsigned char	 BtnStatus; 							 // 0 :
			unsigned char	 Version;								  // 1 :FW Version
			unsigned char	 PcbStatus; 							 // 2 :Module Version
			unsigned char	 Cmd;									  // 3 :
			unsigned char	 Chip_id;								  // 4 :0x55(DEFAULT_CHIP_ID) 0
			unsigned char	 Sens;									   // 5 :sensitivity grade(0x00(slow),0x01(mid),0x02(fast))
			unsigned char	 SetIdac[4];									   // 6~9
			WORD			 DiffData[CSD_TotalSensorCount];   // 10,11 - 12,13
			WORD			 RawData[CSD_TotalSensorCount];  // 14,15 - 16,17
			WORD			 Baseline[CSD_TotalSensorCount];   // 18,19 - 20,21
		}I2CReg;
		******************************************************************/

		if(data & COMMAND_BIT) 	{
			//printk(KERN_DEBUG "[TouchKey] CMD %#02x\n", data);
			dbg("[TouchKey] CMD %#02x\n", data);
			return;
		}

        if (data & UPDOWN_EVENT_BIT) {
            input_report_key(touchkey_driver->input_dev,
            touchkey_keycode[data &
            KEYCODE_BIT], 0);
            input_sync(touchkey_driver->input_dev);
        }
        else {
            input_report_key(touchkey_driver->input_dev,
            touchkey_keycode[data &
            KEYCODE_BIT], 1);
            input_sync(touchkey_driver->input_dev);
        }
		dbg("TOUCH KEY Value : %x\n ", touchkey_keycode[data & KEYCODE_BIT]);

	}
	enable_irq(IRQ_TOUCH_INT);
}

static irqreturn_t touchkey_interrupt(int irq, void *dummy)
{
	disable_irq_nosync(IRQ_TOUCH_INT);
	queue_work(touchkey_driver->touchkey_wq, &touchkey_driver->touchkey_work);

	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static int cypress_touchkey_early_suspend(struct early_suspend *h)
{
	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);
	touchkey_enable = 0;
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);
	printk(KERN_DEBUG "[TouchKey] cypress_touchkey_early_suspend\n");
	if (touchkey_enable < 0) {
		printk(KERN_DEBUG "[TouchKey] ---%s---touchkey_enable: %d\n",
		       __func__, touchkey_enable);
		return 0;
	}

	disable_irq(IRQ_TOUCH_INT);
	flush_workqueue(touchkey_driver->touchkey_wq);

    // Power Off
    touch_ctrl_regulator(0); // Power Off
    // LED Power Off

 //   SET_TOUCH_I2C_TO_PD();

	/* releae key */
	input_report_key(touchkey_driver->input_dev, touchkey_keycode[1], 0);
	input_report_key(touchkey_driver->input_dev, touchkey_keycode[2], 0);
    input_sync(touchkey_driver->input_dev);

	return 0;
}

static int cypress_touchkey_late_resume(struct early_suspend *h)
{
	printk(KERN_DEBUG "[TouchKey] cypress_touchkey_late_resume\n");

    // Power On
    touch_ctrl_regulator(1);
    // LED Power On

//	SET_TOUCH_I2C();


	if (touchkey_enable < 0) {
		printk(KERN_DEBUG "[TouchKey] ---%s---touchkey_enable: %d\n",
		       __func__, touchkey_enable);
		return 0;
	}

	enable_irq(IRQ_TOUCH_INT);
	touchkey_enable = 1;

	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);

	msleep(50);

	if(touchkey_led_status == LED_ON_CMD) {
		i2c_touchkey_write(KEYCODE_REG, &touchkey_led_status, 1);
		printk("LED returned on\n");
	}
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);

	return 0;
}

#endif

/*
void touch_key_ctrl(int flag)
{
	if(flag == 0)
		touchkey_led_status = 0x20;
	else
		touchkey_led_status = 0x10;

		i2c_touchkey_write(KEYCODE_REG, &touchkey_led_status, 1); 

	return;
}
*/

static int i2c_touchkey_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct input_dev *input_dev;
	int err = 0;
	u8 data[3] = { 0, };
    int ret = 0;

	printk(KERN_DEBUG "[TouchKey] cypress i2c_touchkey_probe\n");

    ret = gpio_request(GPIO_TOUCH_INT, "GPIO_TOUCH_INT");
    if (ret < 0)
    {
        dbg("failed to request GPIO. ret = %d\n", ret);
    }

	touchkey_driver = kzalloc(sizeof(struct i2c_touchkey_driver), GFP_KERNEL);
	if (touchkey_driver == NULL) {
		dev_err(dev, "failed to create our state\n");
		return -ENOMEM;
	}

    touchkey_driver->dev = dev;

    touchkey_driver->client = client;
    i2c_set_clientdata(client, touchkey_driver);

	touchkey_driver->client = client;
	touchkey_driver->client->irq = IRQ_TOUCH_INT;
	strlcpy(touchkey_driver->client->name, "cypress-touchkey", I2C_NAME_SIZE);
	touchkey_driver->client->dev.init_name = DEVICE_NAME;

	input_dev = input_allocate_device();

	if (!input_dev) {
		return -ENOMEM;
	}

	touchkey_driver->input_dev = input_dev;

	input_dev->name = DEVICE_NAME;
	input_dev->phys = "cypress-touchkey/input0";
	input_dev->id.bustype = BUS_HOST;

	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(touchkey_keycode[1], input_dev->keybit);
	set_bit(touchkey_keycode[2], input_dev->keybit);

	err = input_register_device(input_dev);
	if (err) {
		input_free_device(input_dev);
		return err;
	}

    touchkey_driver->touchkey_wq = create_singlethread_workqueue("cypress_touchkey_wq");
    if (!touchkey_driver->touchkey_wq) {
        dbg("failed to touchkey workqueue\n");
        return -ENOMEM;
    }

    INIT_WORK(&touchkey_driver->touchkey_work, touchkey_work_func);
    INIT_WORK(&touchkey_driver->touch_update_work, touchkey_update_func);

    // Add sysfs file
    touchkey_sysfile_init();

	init_hw();

	msleep(50);

 // 	irq_set_irq_type(IRQ_TOUCH_INT, IRQ_TYPE_EDGE_FALLING);
	if (request_irq(IRQ_TOUCH_INT, touchkey_interrupt, /*IRQF_DISABLED*/IRQF_TRIGGER_FALLING, DEVICE_NAME, touchkey_driver)) {
		printk(KERN_ERR "[TouchKey] %s Can't allocate irq ..\n", __func__);
		return -EBUSY;
	}

	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);
	err = i2c_touchkey_read(KEYCODE_REG, data, 3);
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);

    dbg("Read Data - data[0] : %x, data[1] : %x, data[2] : %x\n",data[0], data[1], data[2] );

	if (err) {
		printk(KERN_ERR "[TouchKey] touch key is not connected\n");
		input_free_device(input_dev);
		return err;
	}

	touchkey_enable = 1;

//	touchkey_led_status = LED_ON_CMD;
//	i2c_touchkey_write(KEYCODE_REG, &touchkey_led_status, 1); 

	touch_version = data[1];

	printk("[touchkey] FW ver = 0x%x\n", touch_version);
	if(touch_version < CURRENT_FIRMWARE_VERSION || touch_version == 0xFF) {
		printk("[touchkey] Force firmware update\n");
		queue_work(touchkey_driver->touchkey_wq, &touchkey_driver->touch_update_work);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	touchkey_driver->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 2;
	touchkey_driver->early_suspend.suspend = (void*) cypress_touchkey_early_suspend;
	touchkey_driver->early_suspend.resume = (void*) cypress_touchkey_late_resume;
	register_early_suspend(&touchkey_driver->early_suspend);
#endif				/* CONFIG_HAS_EARLYSUSPEND */

	return ret;
}



static int i2c_touchkey_remove(struct i2c_client *client)
{
	unregister_early_suspend(&touchkey_driver->early_suspend);
    if (client->irq){
        free_irq(client->irq, touchkey_driver);
    }
	input_unregister_device(touchkey_driver->input_dev);
	kfree(touchkey_driver);
	return 0;
}


extern int ISSP_main(void);

static int touchkey_update_status = 0;

void touchkey_update_func(struct work_struct *p)
{
	int retry = 5;

	touchkey_update_status = 1;
	printk(KERN_DEBUG "[TouchKey] %s start\n", __func__);
	touchkey_enable = 0;

//	SET_TOUCH_I2C_TO_GPIO();
	gpio_request(GPIO_TOUCH_SDA, "I2C_SDA");
	gpio_request(GPIO_TOUCH_SCL, "I2C_SCL");

	disable_irq(IRQ_TOUCH_INT);

	while (retry--) {
		if (ISSP_main() == 0)
        {
            msleep(100);
            touchkey_enable = 1;

            gpio_free(GPIO_TOUCH_SDA);
            gpio_free(GPIO_TOUCH_SCL);

//            SET_TOUCH_I2C();
            msleep(100);

            touchkey_update_status = 0;

            printk(KERN_DEBUG
            "[TouchKey] touchkey_update succeeded\n");

            goto exit;
        }
	}

	gpio_free(GPIO_TOUCH_SDA);
	gpio_free(GPIO_TOUCH_SCL);
//	SET_TOUCH_I2C();
	msleep(100);

	touchkey_update_status = -1;
	printk(KERN_DEBUG "[TouchKey] touchkey_update failed\n");

exit:
    enable_irq(IRQ_TOUCH_INT);
    return;
}


static ssize_t set_touchkey_firm_update_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
    printk(KERN_DEBUG "[TouchKey] touchkey firmware update \n");

    if (*buf == 'S' || *buf =='F') {
        if ((*buf !='F' && touch_version >= CURRENT_FIRMWARE_VERSION) && touch_version != 0xFF)  {
            touchkey_update_status = 0;
            printk(KERN_DEBUG "[TouchKey] already updated latest version\n");
            return size;
        }
        queue_work(touchkey_driver->touchkey_wq, &touchkey_driver->touch_update_work);
    }
    return size;
}
static ssize_t touch_led_control(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
    int data;
    int errnum;
    if (sscanf(buf, "%d\n", &data) == 1) {
        printk(KERN_DEBUG "[TouchKey] touch_led_control: %d \n", data);
        data = data<<4;
        while(1) {
            spin_lock(&touchkey_lock);
            if(i2c_lock == 0) {
                break;
            }
            spin_unlock(&touchkey_lock);
        };
        i2c_lock = 1;
        spin_unlock(&touchkey_lock);
        errnum = i2c_touchkey_write(KEYCODE_REG, (u8 *)&data, 1);
        spin_lock(&touchkey_lock);
        i2c_lock = 0;
        spin_unlock(&touchkey_lock);
        touchkey_led_status = data;
    }
    else {
        printk(KERN_DEBUG "[TouchKey] touch_led_control Error\n");
    }

    return size;
}


enum led
{
	TOUCH_LED_OFF = 0x00,
	TOUCH_LED_RED = 0x01,	
	TOUCH_LED_GREEN = 0x02,
	TOUCH_LED_BLUE = 0x03,
	TOUCH_LED_MAGENTA = 0x04,
	TOUCH_LED_YELLOW = 0x05,	
	TOUCH_LED_CYAN = 0x06,
	TOUCH_LED_WHITE = 0x07,		
};

static int led_count =0;

void touch_led_ioctl(void)
{
	u8 data[4];
	int err = 0, flag =0;

	if(led_count >= 8)
		led_count %= 8;

	led_count++;

	printk("touch_led_ioctl : %d\n",led_count);

	switch(led_count)
	{
		case 0 :
			flag = TOUCH_LED_OFF;
			break;
		case 1 :
			flag = TOUCH_LED_RED;
			break;	
		case 2 :
			flag = TOUCH_LED_GREEN;
			break;	
		case 3 :
			flag = TOUCH_LED_BLUE;
			break;	
		case 4 :
			flag = TOUCH_LED_MAGENTA;
			break;	
		case 5 :
			flag = TOUCH_LED_YELLOW;
			break;	
		case 6 :
			flag = TOUCH_LED_CYAN;
			break;	
		case 7 :
			flag = TOUCH_LED_WHITE;
			break;				
	}

	data[0] = TOUCH_LED;
	data[1] = 0x00;
	data[2] = 0x00;
	data[3] = flag;
	
	err = i2c_touchkey_write(KEYCODE_REG, data, 4);

	return;
}

static ssize_t touchkey_menu_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    u8 data[2];

    printk("called %s \n",__func__);
    while(1) {
        spin_lock(&touchkey_lock);
        if(i2c_lock == 0) {
            break;
        }
        spin_unlock(&touchkey_lock);
    };
    i2c_lock = 1;
    spin_unlock(&touchkey_lock);
    i2c_touchkey_read(DIFF_DATA_REG, data, 2);
    spin_lock(&touchkey_lock);
    i2c_lock = 0;
    spin_unlock(&touchkey_lock);
    printk("called %s menu sens = %d\n",__func__, menu_sensitivity);
    menu_sensitivity = ((0x00FF&data[0])<<8)|data[1];
    return sprintf(buf,"%d\n",menu_sensitivity);
}

static ssize_t touchkey_back_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	u8 data[2];

	printk("called %s \n",__func__);
	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);
	i2c_touchkey_read(DIFF_DATA_REG+2, data, 2);
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);
	printk("called %s back sens = %d\n",__func__, back_sensitivity);
	back_sensitivity = ((0x00FF&data[0])<<8)|data[1];
	return sprintf(buf,"%d\n",back_sensitivity);
}

static ssize_t touchkey_raw_data0_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	u8 data[2];

	printk("called %s \n",__func__);
	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);
	i2c_touchkey_read(RAW_DATA_REG, data, 2);
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);
	printk("called %s raw_data0 = %d\n",__func__, raw_data0);
	raw_data0 = ((0x00FF&data[0])<<8)|data[1];
	return sprintf(buf,"%d\n",raw_data0);
}


static ssize_t touchkey_raw_data1_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	u8 data[2];

	printk("called %s \n",__func__);
	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);
	i2c_touchkey_read(RAW_DATA_REG+2, data, 2);
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);
	printk("called %s raw_data1 = %d\n",__func__, raw_data1);
	raw_data1 = ((0x00FF&data[0])<<8)|data[1];
	return sprintf(buf,"%d\n",raw_data1);
}

static ssize_t touchkey_idac0_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	u8 data;

	printk("called %s \n",__func__);
	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);
	i2c_touchkey_read(SENS_REG, &data, 1);
	printk("called %s sens = 0x%x\n",__func__,data);
	if(!(data & 0x80)) {
		spin_lock(&touchkey_lock);
		i2c_lock = 0;
		spin_unlock(&touchkey_lock);
		printk("called %s idac0 = %d\n",__func__,data);
		return sprintf(buf, "\n");
	}
	i2c_touchkey_read(IDAC_REG, &data, 1);
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);
	printk("called %s idac0 = %d\n",__func__,data);
	idac0 = data;
	return sprintf(buf,"%d\n",idac0);
}


static ssize_t touchkey_idac1_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	u8 data;

	printk("called %s \n",__func__);
	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);
	i2c_touchkey_read(SENS_REG, &data, 1);
	printk("called %s sens = 0x%x\n",__func__,data);
	if(!(data & 0x80)) {
		spin_lock(&touchkey_lock);
		i2c_lock = 0;
		spin_unlock(&touchkey_lock);
		printk("called %s idac0 = %d\n",__func__,data);
		return sprintf(buf, "\n");
	}
	i2c_touchkey_read(IDAC_REG+1, &data, 1);
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);
	printk("called %s idac1 = %d\n",__func__,data);
	idac1 = data;
	return sprintf(buf,"%d\n",idac1);
}

static ssize_t touch_sensitivity_control(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	unsigned char data = SENS_EN_CMD;
	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);
	i2c_touchkey_write(KEYCODE_REG, &data, 1);
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);
	return size;
}

static ssize_t set_touchkey_firm_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    /*TO DO IT */
	int count;
	count = sprintf(buf, "0x%X\n", CURRENT_FIRMWARE_VERSION);

	return count;
}

static ssize_t set_touchkey_firm_version_read_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char data[3] = { 0, };
	int count;
	while(1) {
		spin_lock(&touchkey_lock);
		if(i2c_lock == 0) {
			break;
		}
		spin_unlock(&touchkey_lock);
	};
	i2c_lock = 1;
	spin_unlock(&touchkey_lock);
	i2c_touchkey_read(KEYCODE_REG, data, 3);
	spin_lock(&touchkey_lock);
	i2c_lock = 0;
	spin_unlock(&touchkey_lock);

	count = sprintf(buf, "0x%X\n", data[1]);
	touch_version = data[1];
	printk(KERN_DEBUG "[TouchKey] touch_version_read 0x%X\n", data[1]);
	return count;
}

static ssize_t set_touchkey_firm_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;

	printk(KERN_DEBUG
	       "[TouchKey] touch_update_read: touchkey_update_status %d\n",
	       touchkey_update_status);

	if (touchkey_update_status == 0) {
		count = sprintf(buf, "PASS\n");
	} else if (touchkey_update_status == 1) {
		count = sprintf(buf, "DOWNLOADING\n");
	} else if (touchkey_update_status == -1) {
		count = sprintf(buf, "FAIL\n");
	}

	return count;
}

static DEVICE_ATTR(touchled_control, S_IRUGO | S_IWUSR | S_IWGRP, NULL, touch_led_control);
static DEVICE_ATTR(touchkey_menu, S_IRUGO, touchkey_menu_show, NULL);
static DEVICE_ATTR(touchkey_back, S_IRUGO, touchkey_back_show, NULL);
static DEVICE_ATTR(touchkey_raw_data0, S_IRUGO, touchkey_raw_data0_show, NULL);
static DEVICE_ATTR(touchkey_raw_data1, S_IRUGO, touchkey_raw_data1_show, NULL);
static DEVICE_ATTR(touchkey_idac0, S_IRUGO, touchkey_idac0_show, NULL);
static DEVICE_ATTR(touchkey_idac1, S_IRUGO, touchkey_idac1_show, NULL);
static DEVICE_ATTR(touch_sensitivity, S_IRUGO | S_IWUSR | S_IWGRP, NULL, touch_sensitivity_control);


static DEVICE_ATTR(touchkey_firm_update, S_IWUSR | S_IWGRP, NULL, set_touchkey_firm_update_store);		/* firmware update */
static DEVICE_ATTR(touchkey_firm_update_status, S_IRUGO, set_touchkey_firm_status_show, NULL);	/* firmware update status return */
static DEVICE_ATTR(touchkey_firm_version_phone, S_IRUGO, set_touchkey_firm_version_show, NULL);/* PHONE*/	/* firmware version resturn in phone driver version */
static DEVICE_ATTR(touchkey_firm_version_panel, S_IRUGO, set_touchkey_firm_version_read_show, NULL);/*PART*/	/* firmware version resturn in touchkey panel version */


static void touchkey_sysfile_init(void)
{
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_firm_update)< 0)
    {
        printk(KERN_ERR "Failed to create device file(%s)!\n", dev_attr_touchkey_firm_update.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_firm_update_status)< 0)
    {
        printk(KERN_ERR "Failed to create device file(%s)!\n", dev_attr_touchkey_firm_update_status.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_firm_version_phone)< 0)
    {
        printk(KERN_ERR "Failed to create device file(%s)!\n", dev_attr_touchkey_firm_version_phone.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_firm_version_panel)< 0)
    {
        printk(KERN_ERR "Failed to create device file(%s)!\n", dev_attr_touchkey_firm_version_panel.attr.name);
    }

    if (device_create_file(touchkey_driver->dev, &dev_attr_touchled_control) < 0) {
        printk(KERN_ERR "[TouchKey] %s device_create_file fail dev_attr_touch_update\n", __func__);
        pr_err("Failed to create device file(%s)!\n", dev_attr_touchled_control.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_menu) < 0) {
        printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_menu\n", __func__);
        pr_err("Failed to create device file(%s)!\n", dev_attr_touchkey_menu.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_back) < 0) {
        printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_back\n", __func__);
        pr_err("Failed to create device file(%s)!\n", dev_attr_touchkey_back.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_raw_data0) < 0) {
        printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_raw_data0\n", __func__);
        pr_err("Failed to create device file(%s)!\n", dev_attr_touchkey_raw_data0.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_raw_data1) < 0) {
        printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_raw_data1\n", __func__);
        pr_err("Failed to create device file(%s)!\n", dev_attr_touchkey_raw_data1.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_idac0) < 0) {
        printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_idac0\n", __func__);
        pr_err("Failed to create device file(%s)!\n", dev_attr_touchkey_idac0.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touchkey_idac1) < 0) {
        printk(KERN_ERR "%s device_create_file fail dev_attr_touchkey_idac1\n", __func__);
        pr_err("Failed to create device file(%s)!\n", dev_attr_touchkey_idac1.attr.name);
    }
    if (device_create_file(touchkey_driver->dev, &dev_attr_touch_sensitivity) < 0) {
        printk("%s device_create_file fail dev_attr_touch_sensitivity\n", __func__);
        pr_err("Failed to create device file(%s)!\n", dev_attr_touch_sensitivity.attr.name);
    }
}



static int __init touchkey_init(void)
{
    int ret = 0;

    ret = i2c_add_driver(&touchkey_i2c_driver);

    if (ret) {
        printk(KERN_ERR
        "[TouchKey] cypress touch keypad registration failed, module not inserted.ret= %d\n",
        ret);
    }
    return ret;
}

static void __exit touchkey_exit(void)
{
	printk(KERN_DEBUG "[TouchKey] %s \n", __func__);
	i2c_del_driver(&touchkey_i2c_driver);

	if (touchkey_driver->touchkey_wq) {
		destroy_workqueue(touchkey_driver->touchkey_wq);
	}

	gpio_free(GPIO_TOUCH_INT);
}

//late_initcall(touchkey_init);
module_init(touchkey_init);
module_exit(touchkey_exit);


MODULE_AUTHOR("CYPRESS");
MODULE_DESCRIPTION("cypress touch keypad");
MODULE_LICENSE("GPL");
