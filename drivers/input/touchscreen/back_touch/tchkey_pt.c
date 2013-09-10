/*
 * Copyright (c) 2010 Pantech Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <mach/vreg.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/wakelock.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/regulator/consumer.h>//jhseo test for ldo control
#include <linux/err.h>//jhseo test for ldo control
#include <linux/gpio.h>//jhseo test for ldo control

#include <linux/fcntl.h>

#include <linux/miscdevice.h>
#include <linux/completion.h>
#include "tchkey_pt.h"

//#include "Pantech_Back_Touch_EF51_Rev0R0_WS02_PDN.h"		//ver.0.0
//#include "Pantech_Back_Touch_EF51_Rev0R1_WS02.h"			//ver0.1
//#include "Pantech_Back_Touch_EF51_Rev0R2_WS02.h"		//ver 0.2	 / WS20
//#include"Pantech_Back_Touch_EF51_Rev0R3_WS02.h"	//ver0.3 true / tuning side and liniarity and exception / TP10
//#include "REV0R3_DELAY.h"	//temp - decrease time before up gesture;
//#include"Pantech_Back_Touch_EF51_Rev0R4_TP1O.h"	//ver0.4 / modifiy exception : 7 channel -> 9 channel / waketime 70ms -> 10ms
//#include"Pantech_Back_Touch_EF51_Rev0R5_TP1O.h"	//ver0.5 / delete exceptioiin for double tapexception / delete release when down two point.
//#include"Pantech_Back_Touch_EF51_Rev0R6_TP1O.h" //ver0.6 / modify x,y axis range 
#include"Pantech_Back_Touch_EF51_Rev0R9_TP1O.h" // 0.7 & 0.8 skip , ver 0.9 - reduce time after HW_reset / reduce time at change Mode



#define TCHKEYPT_DRV_NAME	"tchkeypt"
#define DRIVER_VERSION		"1.1.0"	// for INVENSENSE

#define TOUCHPAD_RST				62

#define USE_TCHPAD_WORKQUEUE
#define USE_FILE_ATTR

#define SIZE_100x100

#define USE_TOUCHIC					0x02
#define TOUCHPAD_MODE				USE_TOUCHIC

#define MAX_NUM_FINGER				1

#define SINGLE_TOUCH
//#define MULTI_TOUCH

//#define TUNING_POINTER_TEST		//for use POINTER
#define CONFIG_BACKTOUCH_DBG

/* -------------------------------------------------------------------- */
/* debug option */
/* -------------------------------------------------------------------- */
//#define SENSOR_TCHKEYPT_DBG_ENABLE
#ifdef SENSOR_TCHKEYPT_DBG_ENABLE
#define dbg(fmt, args...)   printk("[TCHKEYPT] " fmt, ##args)
#else
#define dbg(fmt, args...)
#endif
#define dbg_func_in()       dbg("[FUNC_IN] %s\n", __func__)
#define dbg_func_out()      dbg("[FUNC_OUT] %s\n", __func__)
#define dbg_line()         dbg("[LINE] %d(%s)\n", __LINE__, __func__)
/* -------------------------------------------------------------------- */

/* -------------------------------------------------------------------- */
/* SKY BOARD FEATURE */
/* -------------------------------------------------------------------- */
#define TCHKEYPT_PS_INT_N						72
#define TCHKEYPT_PS_IRQ						gpio_to_irq(TCHKEYPT_PS_INT_N)

#define TCHKEYPT_REG_BASE						0x0000
#define TCHKEYPT_SEQUENCE_KEY_REG			(TCHKEYPT_REG_BASE+0x0000)

//operatioin Mode
#define NORMAL_MODE								0x00
#define POWER_DOWN_MODE							0x01
#define HISTGRAM_MODE							0x02
#define CAP_SENSING_MODE						0x03
#define SELF_TEST_MODE							0x04



// I2C Done Status
#define I2C_DONE_ADDR						0xFFFF
#define I2C_DONE_VALUE							0x01
#define IC_RESET_VALUE							0x02

// MODE (EEPROM)
#define PROGRAM_MODE							0x01
#define IC_EEPROM_PROGRAM_MODE			0x80
#define IC_EEPROM_NORMAL_MODE				0x00


//Firmware Status
#define INITIAL_PARAMETER_REQUEST			0xFF

/************************************************************************************
  TCHKEYPT STATUS MODE
 1. Status Interrupt Mode
 2. Status change mode
 3. Firmware mode
 4. Check FW Version
 5. Change Operation Parameter Mode

 10. I2C fail mode
*************************************************************************************/
#define STATUS_INTERRUPT_MODE				0x01
#define STATUS_CHANGE_MODE					0x02
#define STATUS_FIRMWARE_MODE				0x03
#define STATUS_CHECK_FW_VER					0x04
#define STATUS_CHANGE_PARAMETER_MODE		0x05

//NOT MERGE
#define STATUS_I2C_FAIL_MODE				0x10


// I2C mode in Status change mode
#define STATUS_CHANGE_MODE_WRITE			0x01
#define STATUS_CHANGE_MODE_READ				0x02

//I2C mode in Status check fw ver mode
#define STATUS_CHECK_FW_VER_WRITE			0x01
#define STATUS_CHECK_FW_VER_READ			0x02

// Event Type
#define ABSOLUTE_POINT						1
#define KEY_EVENT							2
#define GESTURE_EVENT						4
#define RELATIVE_POINT						8
#define HISTOL_MODE							64
#define CAP_MODE							128

//LCD resolution 
#ifdef TUNING_POINTER_TEST
#define RESOLUTION_X						1080
#define RESOLUTION_Y						1920
#else
#define RESOLUTION_X						200
#define RESOLUTION_Y						200
#endif

// Back touch status
#define ONE_FINGER_RELEASE_STATUS			0x00
#define ONE_FINGER_PRESS_STATUS				0x02


#ifdef SIZE_100x100
#define TCHPAD_MIN_X							350
#define TCHPAD_MIN_Y							590
#define TCHPAD_RANGE_X							100
#define TCHPAD_RANGE_Y							100
#define TCHPAD_MAX_X							(TCHPAD_MIN_X+TCHPAD_RANGE_X)
#define TCHPAD_MAX_Y							(TCHPAD_MIN_Y+TCHPAD_RANGE_Y)
#define UP_BAR									30
#define MENU_BAR								30
#define LEFT_SIDE								25
#define RIGHT_SIDE								25
#endif

/* -------------------------------------------------------------------- */
/* Debug Option */
/* -------------------------------------------------------------------- */
#define CONFIG_BACKTOUCH_DEBUG

/* All printk off - Default Level */
#define BTOUCH_DBG_LVL_0	0

/* Notice Each Function Entered and Successed  */
#define BTOUCH_DBG_LVL_1	1

/* Show Function flow  */
#define BTOUCH_DBG_LVL_2	2

// NOT used
#define BTOUCH_DBG_LVL_3	3

// enable disable irq
#define BTOUCH_DBG_LVL_4	4


/* -------------------------------------------------------------------- */
/* Firmware  */
/* -------------------------------------------------------------------- */

// File size (bytes)
#define FILE_BUF_SIZE	32768
#define READ_BUF_SIZE	512


#define FIRMWARE_ENABLE

//#define CHECK_FINISH_FW_TIMER

static int tchkeypt_fwupdate_array(void);
static int tchkeypt_fwupdate_start_array(void);


#ifdef CHECK_FINISH_FW_TIMER
static struct timer_list check_finish_fw_enable_delayed_timer;
struct workqueue_struct *check_fw_wq;

static void tchkeypt_chg_mode_start_func(unsigned long data);
static void tchkeypt_status_change_mode_func(struct work_struct * p);
#endif


/* -------------------------------------------------------------------- */
/* Structure */
/* -------------------------------------------------------------------- */
/* File IO */
static int open(struct inode *inode, struct file *file);
static ssize_t write(struct file *file, const char *buf, size_t count, loff_t *ppos);
static long ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

static struct i2c_driver tchkeypt_driver;
static struct tchkeyptdata_t *tchkeyptdata;

/* -------------------------------------------------------------------- */
/* Debug Option */
/* -------------------------------------------------------------------- */

/* -------------------------------------------------------------------- */
/* Function Proto type */
/* -------------------------------------------------------------------- */
#ifdef CONFIG_HAS_EARLYSUSPEND
static void tchkey_early_suspend(struct early_suspend *handler);
static void tchkey_late_resume(struct early_suspend *handler);
#endif

static int tchkeypt_interrupt_only_wave(void);

static int tchkeypt_i2c_read_done(u16 reg, u8 *buf, int count);
static int tchkeypt_i2c_read(u16 reg, u8 *buf, int count);

static int tchkeypt_i2c_write_done(u16 reg, u8 *data, int len);	//include i2c done
static int tchkeypt_i2c_only_write(u16 reg, const u8 *buf, int count);	//not interrupt wave and i2c done

static int tchkeypt_hwreset(void);
static void tchkeypt_struct_initial(void);

static int tchkeypt_status_change_mode(int Mode);


static int tchkeypt_check_firmware_ver(void);
static int tchkeypt_compare_fw_fwfile_ver(void);

static int tchkeypt_normal_mode_polling(void);
static int tchkeypt_power_down_mode_polling(void);
static int tchkeypt_status_change_mode_polling_front_touch_reset(void);


static irqreturn_t tchkeypt_irq_handler(int irq, void *dev_id);
static void tchkeypt_work_f(struct work_struct *work);

#define tchkeypt_get_reg(reg,data)	tchkeypt_i2c_read_done(reg,data,12)



#ifdef USE_TCHPAD_WORKQUEUE
struct workqueue_struct *tchpad_wq;
#endif

#ifdef USE_FILE_ATTR
static struct class *touch_pad_class;
struct device *ts_pad_dev;
#endif


//FILE IO
static struct file_operations fops = 
{
	.owner =    THIS_MODULE,
	.write =    write,
	.open =     open,
	.unlocked_ioctl = ioctl,
};

static struct miscdevice touch_io = 
{
	.minor =    MISC_DYNAMIC_MINOR,
	.name =     "tchkey_pt",
	.fops =     &fops
};

/* -------------------------------------------------------------------- */
/* External Functions */
/* -------------------------------------------------------------------- */
/* -------------------------------------------------------------------- */
/* Internal Functions */
/* -------------------------------------------------------------------- */
#ifdef USE_FILE_ATTR
static ssize_t setup_rear_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int enable;

	dbg_func_in();

	enable = atomic_read(&tchkeyptdata->enable);

	dbg_func_out();

	return sprintf(buf, "%d\n", enable);
}

static ssize_t setup_rear_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long enable = simple_strtoul(buf, NULL, 10);

	dbg_func_in();

	if(!client)
	{
		printk("[TCHKEYPT] %s: i2c_client failed \n",__func__);
		enable=0xff;
	}

	if ((enable == 0) || (enable == 1))
	{
		if(enable == 0)
		{
			/* disable worker */
			disable_irq_nosync(client->irq);

		}
		else// if(enable == 1)
		{
			enable_irq(client->irq);
		}

		atomic_set(&tchkeyptdata->enable, enable);
	}

	dbg_func_out();

	return size;
}


static DEVICE_ATTR(setup, S_IRUGO | S_IWUSR, setup_rear_show, setup_rear_store);
#endif


/*******************************************/
/* ------------- I2C Interface ---------------*/
/*******************************************/

static int tchkeypt_i2c_read_done(u16 reg, u8 *buf, int count)
{
	int rc1, rc2;
	int ret = 0; 
	u8 cmd[2] = {0,};

	dbg_func_in();


	if ( tchkeyptdata->client == NULL ) {
		printk("[TCHKEYPT]%s : touch power key data.client is NULL\n", __func__);
		return -1;
	}

	buf[1] = reg & 0xFF;
	buf[0] = ( reg >> 8 ) & 0xFF;
	
	rc1 = i2c_master_send(tchkeyptdata->client,  buf, 2);

	rc2 = i2c_master_recv(tchkeyptdata->client, buf, count);

	if ( (rc1 != 2) || (rc2 != count ) ) {
		printk("[TCHKEYPT][%s] FAILED: read of register %x(rc1=%d/rc2=%d)\n",__func__, reg, rc1,rc2);
		return -1;
	}

	cmd[0] = I2C_DONE_VALUE;
	
	ret = tchkeypt_i2c_only_write(I2C_DONE_ADDR,&cmd[0],1);
	if(ret < 0)
	{
		printk("[TCHKEYPT][%s] tchkeypt_i2c_only_write is failed\n",__func__);
		return -1;
	}

	dbg_func_out();

	return ret;
}

static int tchkeypt_i2c_read(u16 reg, u8 *buf, int count)
{
	int rc1, rc2;
	int ret = 0; 

	dbg_func_in();

	if ( tchkeyptdata->client == NULL ) {
		printk("[TCHKEYPT]%s : touch power key data.client is NULL\n", __func__);
		return -1;
	}

	buf[1] = reg & 0xFF;
	buf[0] = ( reg >> 8 ) & 0xFF;
	
	rc1 = i2c_master_send(tchkeyptdata->client,  buf, 2);

	rc2 = i2c_master_recv(tchkeyptdata->client, buf, count);

	if ( (rc1 != 2) || (rc2 != count ) ) {
		printk("[TCHKEYPT][%s] FAILED: read of register %x(rc1=%d/rc2=%d)\n",__func__, reg, rc1,rc2);
		return -1;	}

	dbg_func_out();

	return ret;
}

static int tchkeypt_i2c_write_done(u16 reg, u8 *data, int len)
{
	u8	buf[128] = {0,};
	int rc;
	int ret = 0;
	int i;
	u8 cmd[2] = {0,};
	
	dbg_func_in();

	if ( tchkeyptdata->client == NULL ) {
		printk("[TCHKEYPT]%s : tchkeyptdata->client is NULL\n", __func__);
		return -ENODEV;
	}

	buf[1] = reg & 0xFF;
	buf[0] = ( reg >> 8 ) & 0xFF;

	if (len > 128) {
		printk( "tchkeypt_i2c_write_done FAILED: buffer size is limitted(20)\n");
		return -1;
	}
	for( i=0 ; i<len; i++ ) buf[i+2] = data[i];

	//printk("[TCHKEYPT]Reg : %x\tbuf[2] : %d\t count:%d\n",reg,buf[2],len);
	rc = i2c_master_send(tchkeyptdata->client, buf, len+2);

	if (rc != len+2) {
		printk( "[%s] FAILED: writing to reg %x\n",__func__, reg);
		return -1;
	}

	cmd[0] = I2C_DONE_VALUE;
	
	if(tchkeypt_i2c_only_write(I2C_DONE_ADDR,&cmd[0],1))
	{
		printk("[TCHKEYPT][%s] tchkeypt_i2c_only_write is failed\n",__func__);
		return -1;
	}

	dbg_func_out();

	return ret;

}


// To do I2C Done - write  value(1) at 0XFFFF
static int tchkeypt_i2c_only_write(u16 reg, const u8 *data, int count){
	u8	buf[256] = {0,};
	int rc;
	int ret = 0;
	int i = 0;
	
	dbg_func_in();

	if ( tchkeyptdata->client == NULL ) {
		printk("[TCHKEYPT]%s : tchkeyptdata->client is NULL\n", __func__);
		return -ENODEV;
	}
	
	buf[1] = reg & 0xFF;
	buf[0] = ( reg >> 8 ) & 0xFF;

	//buf[2] = data[0];
	if (count > 128) {
		printk( "tchkeypt_i2c_only_write FAILED: buffer size is limitted(128)\n");
		return -1;
	}	
	for( i=0 ; i<count; i++ )
		buf[i+2] = data[i];


	//printk("[TCHKEYPT]Reg : %x\tbuf[2] : %d\t count:%d\n",reg,buf[2],count);
	
	rc = i2c_master_send(tchkeyptdata->client, buf, count+2);

	if (rc != count+2) {
		printk( "[%s] FAILED: writing to reg %x\n",__func__, reg);	
		return -1;
	}
#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >=BTOUCH_DBG_LVL_4)
		printk("[TCHKEYPT][%s]WRITE_DONE\n",__func__);
#endif
	dbg_func_out();
	return ret;
}

/* -------------------------------------------------------------------- */
// Interrupt pin wave
/* -------------------------------------------------------------------- */
static int tchkeypt_interrupt_only_wave(void){
	int rc = 0;
	dbg_func_in();

#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
		printk("[TCHKEYPT][%s] start\n",__func__);
#endif
	//Added tchkeypt interrupt gpio value (output pin setting(low) -> 100us driving -> output pin setting(high) -> Input Mode)
	rc = gpio_direction_output(TCHKEYPT_PS_INT_N, 0);
	if (rc) {
		printk("[TCHKEYPT]gpio_direction_output TCHKEYPT_PS_INT_N 0 : %d failed, rc=%d\n",TCHKEYPT_PS_INT_N, rc);
		return -EINVAL;
	}	

	udelay(100); // low driving time

	rc = gpio_direction_output(TCHKEYPT_PS_INT_N, 1);

	if (rc) {
		printk("[TCHKEYPT]gpio_direction_output TCHKEYPT_PS_INT_N 1 : %d failed, rc=%d\n",TCHKEYPT_PS_INT_N, rc);
		return -EINVAL;
	}

	rc  = gpio_direction_input(TCHKEYPT_PS_INT_N);
	if (rc) {
		printk("[TCHKEYPT]gpio_direction_input TCHKEYPT_PS_INT_N : %d failed, rc=%d\n",TCHKEYPT_PS_INT_N, rc);
		return -EINVAL;
	}
	// end Added
#ifdef CONFIG_BACKTOUCH_DBG
		if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
			printk("[TCHKEYPT][%s] end\n",__func__);
#endif
	dbg_func_out();	
	return 0;
}


static int tchkeypt_hwreset(void){

	int rc = 0;
	
#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_0)
		printk("[TCHKEYPT][%s] HW RESET \n",__func__);

#endif
	rc = gpio_direction_output(TOUCHPAD_RST,0);
	if (rc) {
		printk("[TCHKEYPT]gpio_direction_output LOW TCHKEYPT_RESET failed, rc=%d\n",rc);
		return -EINVAL;
	}
	
	mdelay(10);
	
	rc = gpio_direction_output(TOUCHPAD_RST,1);
	
	if (rc) {
		printk("[TCHKEYPT]gpio_direction_output HIGH TCHKEYPT_RESET failed, rc=%d\n",rc);
		return -EINVAL;
	}
	
	mdelay(50);

	return rc;
}


static void tchkeypt_struct_initial(void){

	// struct tchkeyptdata is initialize
	tchkeyptdata->PAD_FUNCTION=0x00;
	tchkeyptdata->status_mode = 0x00;
	
	tchkeyptdata->in_status_chg_mode = 0;
	tchkeyptdata->in_status_check_fw_ver_mode = 0;
	tchkeyptdata->in_status_chg_parameter_mode = 0;

	tchkeyptdata->status_cmd[0] = 0;
	tchkeyptdata->status_cmd[1] = 0;
	
	tchkeyptdata->fw_ver[0] = 0;
	tchkeyptdata->fw_ver[1] = 0;

	tchkeyptdata->change_reg = 0;

	tchkeyptdata->setting_mode = 0;
	
	tchkeyptdata->change_parameter_value[0] = 0;
	tchkeyptdata->change_parameter_value[1] = 0;
	
	tchkeyptdata->check_ver_flag = 0;
	tchkeyptdata->check_i2c_fw = 0;
	tchkeyptdata->timer_count = 0;

	tchkeyptdata->dbg_op = 0;	//default debug level

}
#ifdef CHECK_FINISH_FW_TIMER

/***********************************************************************/
// Delay Function - after firmware upgrade
/***********************************************************************/

static void tchkeypt_status_change_mode_func(struct work_struct * p){

#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
		printk("[TCHKEYPT][%s] After Firmware upgraded\n",__func__);
	
#endif
/*	disable_irq(tchkeyptdata->client->irq);
	tchkeypt_normal_mode_polling();
	enable_irq(tchkeyptdata->client->irq);*/

	
	del_timer(&check_finish_fw_enable_delayed_timer);


}
#endif

/***********************************************************************/
// BY Polling method
/***********************************************************************/


#define MAX_MODE_CHECK_COUNT		5
#define MAX_POWER_CHECK_COUNT		3

#define MAX_MODE_CHECK_COUNT_CHG_FW_VER		10
#define MAX_POWER_CHECK_COUNT_CHG_FW_VER	5


/***********************************************************************/
// Mode change flag read flag => check function
/***********************************************************************/
static int tchkeypt_check_chg_mode_write_done(void)
{
	int status = -1;
	int resume_count = 0;	
	u8 check_buf[2]={0,};
	int ret = 0;

	do
	{
		status = gpio_get_value(TCHKEYPT_PS_INT_N);
		if(!status)
		{
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s]IN read detected\n",__func__);
#endif		
			ret = tchkeypt_i2c_read_done(0x0100, check_buf,1);
			if(ret < 0)
				printk("[TCHKEYPT][%s][tchkeypt_i2c_read_done] 3. Mode change Ack, Verify Touch flag is failed\n",__func__);

			if(check_buf[0] == 0xAA)
			{
#ifdef CONFIG_BACKTOUCH_DBG
				if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_0)
					printk("[TCHKEYPT][%s]Change Mode is Successed\n",__func__);
#endif
				return 0;
			}
			else{
				
#ifdef CONFIG_BACKTOUCH_DBG
				if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_0)
					printk("[TCHKEYPT][%s]Change Mode is failed\tbuf[0]=%02x\n",__func__,check_buf[0]);
#endif

			}
		}

		mdelay(5);
	}while(resume_count++ < MAX_MODE_CHECK_COUNT);	

	return -1;

}

/* -------------------------------------------------------------------- */
/* Tchkey_pt ON /Normal Mode Setting - by polling method */
/* -------------------------------------------------------------------- */
// State : Normal Mode

static int tchkeypt_normal_mode_polling(void)
{
	
	int resume_count = 0;
	int status = -1;
	int ret =0;
	u8 t_buf[2] = {0,};
	int power_reset_count = MAX_POWER_CHECK_COUNT;
	
	dbg_func_in();
	
#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
		printk("[TCHKEYPT][%s] start\n",__func__);
#endif

	t_buf[0] = 0x01;
	t_buf[1] = 0x00;	//Normal mode

	ret = tchkeypt_interrupt_only_wave();

	if(ret < 0)
	{
		printk("[TCHKEYPT] [%s][tchkeypt_interrupt_only_wave] INT pin failed\n",__func__);
		return -1;	
	}

	do{
		status = gpio_get_value(TCHKEYPT_PS_INT_N);

		if(!status)
		{
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s] IN write detected\n",__func__);
#endif			
			ret = tchkeypt_i2c_write_done(0x007F, t_buf, 2);
			if(ret < 0)
				printk("[TCHKEYPT] [%s][tchkeypt_i2c_write_done] 2. Check Host mode change Flag Area is failed\n",__func__);
			
			break;
		}

		if(resume_count > power_reset_count){
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
				printk("[TCHKEYPT][%s] chip power on check count is over\n",__func__);
#endif			

			power_reset_count = MAX_POWER_CHECK_COUNT*2;

			ret = tchkeypt_hwreset();
			if(ret < 0)
			{
				printk("[TCHKEYPT] [%s][tchkeypt_hwreset] hw_reset failed\n",__func__);
			}
			
			ret = tchkeypt_interrupt_only_wave();
			
			if(ret < 0)
			{
				printk("[TCHKEYPT] [%s][tchkeypt_interrupt_only_wave] INT pin failed\n",__func__);
				return -1;	
			}
		}
		resume_count++;
		mdelay(10);
	}while(resume_count < (MAX_MODE_CHECK_COUNT+MAX_POWER_CHECK_COUNT));

	if(resume_count >= (MAX_MODE_CHECK_COUNT+MAX_POWER_CHECK_COUNT))
	{
		printk("[TCHKEYPT][%s] mode change write flag fail\n",__func__);
		// Need chip reboot?
		return -1;
	}

	mdelay(1);

	status = tchkeypt_check_chg_mode_write_done();

	if(status != 0){
		printk("[TCHKEYPT][%s] resume wake up write fail!\n",__func__);		
		tchkeypt_hwreset();
		return -1;
	}

	return 0;
}


/* -------------------------------------------------------------------- */
/* Tchkey_pt OFF /Power down  Mode Setting - by polling method */
/* -------------------------------------------------------------------- */
// State : Power Downl Mode (Deep sleep mode)

static int tchkeypt_power_down_mode_polling(void)
{
	
	int resume_count = 0;
	int status = -1;
	int ret = 0;
	u8 t_buf[2] = {0,};
	
	dbg_func_in();

	
#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
		printk("[TCHKEYPT][%s] start\n",__func__);
#endif

	t_buf[0] = 0x01;
	t_buf[1] = 0x01;	//power down mode

	ret = tchkeypt_interrupt_only_wave();
	if(ret < 0)
	{
		printk("[TCHKEYPT] [%s][tchkeypt_interrupt_only_wave] INT pin failed\n",__func__);
		return -1;	
	}
	do{

		status = gpio_get_value(TCHKEYPT_PS_INT_N);

		if(!status)
		{
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s] IN write detected\n",__func__);
#endif			
			ret = tchkeypt_i2c_write_done(0x007F, t_buf, 2);
			if(ret < 0)
				printk("[TCHKEYPT] [%s][tchkeypt_i2c_write_done] 2. Check Host mode change Flag Area is failed\n",__func__);
			
			break;
		}

		resume_count++;
		mdelay(10);
	}while(resume_count <MAX_MODE_CHECK_COUNT);

	if(resume_count >= MAX_MODE_CHECK_COUNT)
	{
		printk("[TCHKEYPT][%s] mode change write flag fail\n",__func__);
		tchkeypt_hwreset();
		// Need Chip power off?
	}

	mdelay(1);

	status = tchkeypt_check_chg_mode_write_done();

	if(status != 0)
		printk("[TCHKEYPT][%s] tchkeypt_power_down_mode_polling write ack fail!\n",__func__);	
	
	return 0;
}

/* -------------------------------------------------------------------- */
/* for Backtouch ON after power on / off */
/* because Front touch often entered int bootloader mode */
/* -------------------------------------------------------------------- */
// State : Power down Mode & Normal Mode
void tchkeypt_status_change_mode_front_touch_reset(void){
	int ret = 0;

	//disable_irq(tchkeyptdata->client->irq);
	//disable_irq_nosync(tchkeyptdata->client->irq);
	//cancel_work_sync(&tchkeyptdata->work);	
	mutex_lock(&tchkeyptdata->i2clock); 

	ret = tchkeypt_status_change_mode_polling_front_touch_reset();
	if(ret < 0)
		printk("[TCHKEYPT] [%s][tchkeypt_status_change_mode_polling_front_touch_reset] failed\n",__func__);

	enable_irq(tchkeyptdata->client->irq);

	mutex_unlock(&tchkeyptdata->i2clock); 
}

static int tchkeypt_status_change_mode_polling_front_touch_reset(void){

	int resume_count = 0;
	int status = -1;
	int ret =0;
	int power_reset_count = MAX_POWER_CHECK_COUNT;
	u8 t_buf[2] = {0,};

	dbg_func_in();

	
#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_0)
		printk("[TCHKEYPT][%s] start\n",__func__);
#endif

	t_buf[0] = 0x01;
	t_buf[1] = 0x00; //Normal mode
	
	tchkeypt_hwreset();
	ret = tchkeypt_interrupt_only_wave();
	mdelay(10);

	if(ret < 0)
	{
		printk("[TCHKEYPT] [%s][tchkeypt_interrupt_only_wave] INT pin failed\n",__func__);
		return -1;	
	}

	do{
		status = gpio_get_value(TCHKEYPT_PS_INT_N);

		if(!status)
		{
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s] IN write detected\tcount : %d\n",__func__,resume_count);
#endif			
			ret = tchkeypt_i2c_write_done(0x007F, t_buf, 2);
			if(ret < 0)
				printk("[TCHKEYPT] [%s][tchkeypt_i2c_write_done] 2. Check Host mode change Flag Area is failed\n",__func__);
			
			break;
		}
		printk("[TCHKEYPT][%s] resume_count = %d \t status=%d\n",__func__,resume_count,status);
		if(resume_count > power_reset_count){
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
				printk("[TCHKEYPT][%s] chip power on check count is over\n",__func__);
#endif			

			power_reset_count = MAX_POWER_CHECK_COUNT*2;

			tchkeypt_hwreset();

			ret = tchkeypt_interrupt_only_wave();
			
			if(ret < 0)
			{
				printk("[TCHKEYPT] [%s][tchkeypt_interrupt_only_wave] INT pin failed\n",__func__);
				return -1;	
			}
		}

		resume_count++;
		mdelay(10);
	}while(resume_count < (MAX_POWER_CHECK_COUNT*2));

	if(resume_count >= (MAX_POWER_CHECK_COUNT*2))
	{
		printk("[TCHKEYPT][%s] mode change write flag fail\n",__func__);
		// Need chip reboot?
		return -1;
	}

	mdelay(1);

	status = tchkeypt_check_chg_mode_write_done();

	if(status != 0){
		printk("[TCHKEYPT][%s] resume wake up write fail!\n",__func__); 	
		tchkeypt_hwreset();
		return -1;
	}

	return 0;
}

void tchkeyptdata_disable_irq(void){
	
#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
		printk("[TCHKEYPT][%s] \n",__func__);
#endif			

	disable_irq(tchkeyptdata->client->irq);
	cancel_work_sync(&tchkeyptdata->work);
}


EXPORT_SYMBOL(tchkeypt_status_change_mode_front_touch_reset);
EXPORT_SYMBOL(tchkeyptdata_disable_irq);


/* -------------------------------------------------------------------- */
/* Tchkey_pt ON /OFF Mode Setting */
/* ON  = 1 / OFF = 2 */
/* -------------------------------------------------------------------- */
// State : Power down Mode & Normal Mode
static int tchkeypt_status_change_mode(int Mode){
	int ret = 0;

	dbg_func_in();

#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
		printk("[TCHKEYPT][%s] Change Mode START\n",__func__);
		
#endif

	disable_irq(tchkeyptdata->client->irq);

	tchkeyptdata->status_mode = STATUS_CHANGE_MODE;

	tchkeypt_interrupt_only_wave();

	tchkeyptdata->status_cmd[0] = 0x01;

	if(Mode == 1)	//Normal Mode
	{	
		// 2. check Host mode change flag Area ->1. Normal Mode writting Reg
		tchkeyptdata->status_cmd[1] = NORMAL_MODE;
		
#ifdef CONFIG_BACKTOUCH_DBG
		if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
			printk("[TCHKEYPT][%s] Normal Mode\n",__func__);
#endif
		
	}//end Setting Normal Mode

	if(Mode == 2)	//PowerDown  Mode
	{
		// 2. check Host mode change flag Area ->1. Normal Mode writting Reg
		tchkeyptdata->status_cmd[1] = POWER_DOWN_MODE;
#ifdef CONFIG_BACKTOUCH_DBG
		if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
			printk("[TCHKEYPT][%s] Deep Sleep Mode\n",__func__);
#endif
	}
	
	//Setting flag before i2c write(ISR)
	tchkeyptdata->in_status_chg_mode = STATUS_CHANGE_MODE_WRITE;

#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
		printk("[TCHKEYPT][%s]enable irq after i2c write\n",__func__);
#endif

	enable_irq(tchkeyptdata->client->irq);	// detecting interrupt pin low level by ISR : 
	
	dbg_func_out();
	return ret;	
}


/* -------------------------------------------------------------------- */
/* Frimware Update */
/* -------------------------------------------------------------------- */

static int tchkeypt_check_firmware_ver(void)
{

	int ret = 0;
#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
		printk("[TCHKEYPT][%s] START \n",__func__);
#endif


	dbg_func_in();
	disable_irq(tchkeyptdata->client->irq);

	tchkeyptdata->status_mode = STATUS_CHECK_FW_VER;

	tchkeyptdata->status_cmd[0] = 0xC0;

	ret = tchkeypt_interrupt_only_wave();
	if(ret < 0){
		printk("[TCHKEYPT][%s] tchkeypt_interrupt_only_wave failed \n",__func__);		
		ret = -1;
	}
	
	tchkeyptdata->in_status_check_fw_ver_mode = STATUS_CHECK_FW_VER_WRITE;

#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
		printk("[TCHKEYPT][%s]enable irq after i2c write\n",__func__);
#endif


	enable_irq(tchkeyptdata->client->irq);

	dbg_func_out();
	return ret;


}

static int tchkeypt_compare_fw_fwfile_ver(void)
{
	u8 fwfile_ver[2] ={0,};

	fwfile_ver[0] = rawData[32528];	// Major version
	fwfile_ver[1] = rawData[32529];	// Minor version

#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_0)
		printk("[TCHKEYPT][Fwfile_Major_ver : %d] [Fwfile_Minor_ver : %d]\n",fwfile_ver[0],fwfile_ver[1]);
#endif


	if(tchkeyptdata->fw_ver[0] < fwfile_ver[0])
	{
		printk("[TCHKEYPT][%s] Fw ver is not latest ver\n",__func__);
		
		tchkeypt_fwupdate_array();
	}
	
	else if(tchkeyptdata->fw_ver[0] == fwfile_ver[0])
	{
		if(tchkeyptdata->fw_ver[1] < fwfile_ver[1])
		{
			printk("[TCHKEYPT][%s] Fw upgrade is not latest ver\n",__func__);

			tchkeypt_fwupdate_array();
		}
		else
		{
			printk("[TCHKEYPT] Fw Ver is Latest Ver\n");
/*			disable_irq(tchkeyptdata->client->irq);
			tchkeypt_normal_mode_polling();
			enable_irq(tchkeyptdata->client->irq);*/
		}
	}
	else
	{
		printk("[TCHKEYPT] Fw Ver is Latest Ver,  normal mode start\n");
/*		disable_irq(tchkeyptdata->client->irq);
		tchkeypt_normal_mode_polling();
		enable_irq(tchkeyptdata->client->irq);*/
	}
	return 0;
}

static int tchkeypt_fwupdate_array()
{
#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
		printk("[TCHKEYPT]Tchkeypt_ARRAY_START & Setting Flag(check fw flag)\n");
#endif

#ifdef CHECK_FINISH_FW_TIMER
		del_timer(&check_finish_fw_enable_delayed_timer);
		mod_timer(&check_finish_fw_enable_delayed_timer, jiffies + msecs_to_jiffies(9000)); 
#endif 

	tchkeyptdata->check_i2c_fw = 1;

	disable_irq(tchkeyptdata->client->irq);

	tchkeyptdata->status_mode = STATUS_FIRMWARE_MODE;

	tchkeypt_interrupt_only_wave();

#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
		printk("[TCHKEYPT][%s]enable irq after i2c write\n",__func__);
#endif

	enable_irq( tchkeyptdata->client->irq);

	return 0;
}


static int tchkeypt_fwupdate_start_array()
{
	u8 cmd[2] = {0,};
	
#if 1
#else
	u8 rom_status;
#endif
	int page_num;
	int byte_num;
	int ret = 0;
	int i,j,k = 0;

	u16 write_addr;
	u16 read_page;
	u8 read_buf[128] = {0,};
	page_num = FILE_BUF_SIZE;

	
	dbg_func_in();

#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_0)
		printk("[TCHKEYPT][%s] FW START\n",__func__);

	// 0. Change Mode (EEPROM)

	// Enter EEPROM PROGRAM MODE
	cmd[0] = IC_EEPROM_PROGRAM_MODE;
	if(tchkeypt_i2c_only_write(0xFFFF, cmd, 1))
	{
		printk("[TCHKEYPT] [tchkeypt_i2c_only_write] 0. Change Mode (EEPROM) is failed\n");
		return -1;
	}
		
	// 1. Password Register Setting
	cmd[0] = 0x6B;
	if(tchkeypt_i2c_only_write(0xFFFC, cmd, 1))
	{
		printk("[TCHKEYPT] [%s][tchkeypt_i2c_only_write] 1. Password Register Setting  is failed\n",__func__);
		return -1;
	}
	
	cmd[0] = 0xD2;
	if(	tchkeypt_i2c_only_write(0xFFFD, cmd, 1))
	{
		printk("[TCHKEYPT] [%s][tchkeypt_i2c_only_write] 1. Password Register Setting  is failed\n",__func__);
		return -1;
	}
 
	// 3. Page Register Setting
	page_num = 256;
	write_addr = 0x0000;
	k = 0;
	byte_num = 128;

	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
		printk("[TCHKEYPT][%s] Pagenum 1 ~ 256 write START\n",__func__);

	for(i=0	; i<page_num; i++)
	{
		// a. Set to write at Page Number.
		cmd[0] = i;
		if(	tchkeypt_i2c_only_write(0xFFF9, cmd, 1))
		{
			printk("[TCHKEYPT] [%s][tchkeypt_i2c_only_write]  a. Set to write at Page Number.  is failed\n",__func__);
			return -1;
		}

		// b. Page Buffer Reset
		cmd[0] = 0x01;
		if(tchkeypt_i2c_only_write(0xFFFE, cmd, 1))
		{
			printk("[TCHKEYPT] [%s][tchkeypt_i2c_only_write]  b. Page Buffer Reset is failed\n",__func__);
			return -1;
		}

		// c. Fill Buffer Load Enable(128 byte)
		cmd[0] = 0x02;
		if(tchkeypt_i2c_only_write(0xFFFE, cmd, 1))
		{
			printk("[TCHKEYPT] [%s][tchkeypt_i2c_only_write] c. Fill Buffer Load Enable(128 byte) is failed\n",__func__);
			return -1;
		}

		// d. Fill Page Buffer (128 byte)
		if(tchkeypt_i2c_only_write(0x0000,&rawData[k],128))
		{
			printk("[TCHKEYPT] [%s][tchkeypt_i2c_only_write]  d. Fill Page Buffer (128 byte) is failed\n",__func__);
			return -1;
		}
		k = k + 128;

		// -------------------------------------------------------------------- 
		// Erase & wait 
		// -------------------------------------------------------------------- 

		cmd[0] = 0x03;
		if(tchkeypt_i2c_only_write(0xFFFE, cmd, 1))
		{
			printk("[TCHKEYPT] [%s][tchkeypt_i2c_only_write] Erase & wait   is failed\n",__func__);
			return -1;
		}
#if 1		// Use the Delay because HW bug
		mdelay(3);
#else
		while(1)
		{
			if(tchkeypt_i2c_read_done(0xFFFE,&rom_status,1))
				return -1;
			if(rom_status == 0)
				break;
		}
#endif

		// -------------------------------------------------------------------- 
		// Program & wait 
		// -------------------------------------------------------------------- 
		cmd[0] = 0x04;
		if(tchkeypt_i2c_only_write(0xFFFE, cmd, 1))
		{
			printk("[TCHKEYPT] [%s][tchkeypt_i2c_only_write] Program & wait   is failed\n",__func__);
			return -1;
		}
#if 1	// Use the Delay because HW bug
		mdelay(3);
#else


		while(1)
		{
			if(tchkeypt_i2c_read_done(0xFFFE,&rom_status,1))
				return -1;
			if(rom_status == 0)
				break;
		}
		printk("[TCHKEYPT]Tchkey_pt Firmware Finished\n");
#endif
	}
	if(tchkeyptdata->dbg_op >= 2)
		printk("[TCHKEYPT]Write EEPROM finish and Read & Verify\n");
	// -------------------------------------------------------------------- 
	// Read * Verify 
	// -------------------------------------------------------------------- 
	
	// Setting Read Mode	
	cmd[0] = 0x06;
	if(tchkeypt_i2c_only_write(0xFFFE, cmd, 1))
	{
		printk("[TCHKEYPT] [%s][tchkeypt_i2c_only_write] Setting Read Mode is failed\n",__func__);
		return -1;
	}
	read_page = 0x0000;
	k=0;
	for(i=0;i < 256; i++){
		if(	tchkeypt_i2c_read(read_page,&read_buf[0],128))
		{
			printk("[TCHKEYPT] [%s][%d][tchkeypt_i2c_read] reading failed\n",__func__,i);
			return -1;
		}
		// Compare
		for(j=0;j<128;j++){
			//k = 128*i+j;
			k = read_page + j; // ������ ��ŭ�� Offset�� ��������... Aiden...
			
			if( rawData[k] != read_buf[j]){
				printk("[TCHKEYPT]rawData[%d]: %d\t read_buf[%d]:%d\n",k,rawData[k],j,read_buf[j]);
				printk("[TCHKEYPT][%s] diff Firmware Data\n",__func__);
				return -1;
			}
				
		}	
		read_page = read_page + 128;

	}

	mdelay(10); //delay for verify 

	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
		printk("[TCHKEYPT]Read & Compare END &  Clear Password START\n");

	// -------------------------------------------------------------------- 
	// Exit EEPROM Program Mode
	// -------------------------------------------------------------------- 
	// 1. Clear Password... Aiden
	cmd[0] = 0x00;
	if(tchkeypt_i2c_only_write(0xFFFC, cmd, 1))
	{
		printk("[TCHKEYPT][%s]Clear Password 1 \n",__func__);
		return -1;
	}

	cmd[0] = 0x00;
	if(tchkeypt_i2c_only_write(0xFFFD, cmd, 1))
	{
		printk("[TCHKEYPT][%s]Clear Password 2 \n",__func__);
		return -1;
	}

	/////////////////////////////////////////////////
	// 2. EEPROM Normal Mode Setting
	cmd[0] = IC_EEPROM_NORMAL_MODE;

	if(tchkeypt_i2c_only_write(0xFFFF, cmd, 1))
	{
		printk("[TCHKEYPT][%s] 1. EEPROM Normal Mode Setting is failed\n",__func__);
		return -1;
	}

	tchkeypt_hwreset();
	dbg_func_out();
	
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_0)
		printk("[TCHKEYPT][%s]Tchkey_pt Firmware Finished\n",__func__);

	tchkeyptdata->check_i2c_fw = 0;	//check fw i2c flag
#endif
	return ret;
		
}	// end tchkeypth_fwupdate_start


#ifdef CHECK_FINISH_FW_TIMER
static void tchkeypt_chg_mode_start_func(unsigned long data)
{
#ifdef CONFIG_BACKTOUCH_DBG
	if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
		printk("[TCHKEYPT][%s] TIMER START\n",__func__);

	if(!tchkeyptdata->check_i2c_fw)	//Firmware Finished.
	{
		queue_work(check_fw_wq, &tchkeyptdata->work_chg_mode_start);
	}

	else
	{
		del_timer(&check_finish_fw_enable_delayed_timer);
		mod_timer(&check_finish_fw_enable_delayed_timer, jiffies + msecs_to_jiffies(2000)); 
		if(tchkeyptdata->dbg_op >= 2)
			printk("[TCHKEYPT]Current status is Firmware upgrading & Timercount : %d\n",tchkeyptdata->timer_count);
		tchkeyptdata->timer_count++;
		if((tchkeyptdata->timer_count)>6)	//protect long time for firmupgrade.
			tchkeyptdata->check_i2c_fw = 0;
	}
#endif
}

#endif


/* ------------- Interrupt and Handler ---------------*/
static irqreturn_t tchkeypt_irq_handler(int irq, void *dev_id)
{
	dbg_func_in();

	disable_irq_nosync(tchkeyptdata->client->irq);

#ifdef USE_TCHPAD_WORKQUEUE
	queue_work(tchpad_wq, &tchkeyptdata->work);
#else
	schedule_work(&tchkeyptdata->work);
#endif

	dbg_func_out();
	return IRQ_HANDLED;
}

static void tchkeypt_work_f(struct work_struct *work)
{
	int ret = 0;
	u8 data[12] = {0,};
	int temp_x = 0;
	int temp_x1 = 0;

	int temp_y = 0;
	int temp_y1 = 0;
	u8 buf[2]={0,};

#ifdef MULTI_TOUCH
	int id = 0;
#endif

	dbg_func_in();
	
	if(tchkeyptdata->status_mode == STATUS_CHANGE_MODE)
	{
		
		//printk("[TCHKEYPT][%s]STATUS_CHANGE_MODE\n",__func__);
		if(tchkeyptdata->in_status_chg_mode == STATUS_CHANGE_MODE_WRITE)
		{
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >=BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s]STATUS_CHANGE_MODE_WRITE\n",__func__);
#endif
			if(tchkeypt_i2c_write_done(0x007F, tchkeyptdata->status_cmd, 2))
			{
				printk("[TCHKEYPT] [%s][tchkeypt_i2c_write_done] 2. Check Host mode change Flag Area is failed\n",__func__);
				ret = -1;
			} 	
			//printk("[TCHKEYPT][%s]Setting STATUS_CHANGE_MODE_READ\n",__func__);
			tchkeyptdata->in_status_chg_mode = STATUS_CHANGE_MODE_READ;
			
			enable_irq(tchkeyptdata->client->irq);
		}

		else	//STATUS_CHANGE_MODE_READ
		{
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s]STATUS_CHANGE_MODE_READ\n",__func__);
#endif

			// 3. Mode change Ack, Verify Touch flag
			if(tchkeypt_i2c_read_done(0x0100, buf,1))
			{
				printk("[TCHKEYPT][%s][tchkeypt_i2c_read_done] 3. Mode change Ack, Verify Touch flag is failed\n",__func__);
				ret = -1;
			}
			
			if(buf[0] == 0xAA)
			{
#ifdef CONFIG_BACKTOUCH_DBG
				if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_0)
					printk("[TCHKEYPT][%s]Change Mode is Successed\n",__func__);
#endif
			}
			
			else
			{
				printk("[TCHKEYPT][%s]Change Mode is Failed\n",__func__);
				ret = -1;
			}
			
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
				printk("[TCHKEYPT][%s]Setting STATUS_INTERRUPT_MODE\n",__func__);
#endif
			tchkeyptdata->status_mode = STATUS_INTERRUPT_MODE;
			
			enable_irq(tchkeyptdata->client->irq);

			//complete(&(tchkeyptdata->chg_mode_completion));			
			
		}
		
	}

	else if(tchkeyptdata->status_mode == STATUS_FIRMWARE_MODE)
	{
		ret = tchkeypt_fwupdate_start_array();
		
		if(ret < 0)
			printk("[TCHKEYPT][%s]Firmware upgrade Failed\n",__func__);
		
		tchkeyptdata->status_mode = STATUS_INTERRUPT_MODE;
		
#ifdef CONFIG_BACKTOUCH_DBG
		if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
			printk("[TCHKEYPT][%s]Setting STATUS_INTERRUPT_MODE\n",__func__);
#endif
		enable_irq(tchkeyptdata->client->irq);
	}

	else if(tchkeyptdata->status_mode == STATUS_CHECK_FW_VER)
	{
		if(tchkeyptdata->in_status_check_fw_ver_mode == STATUS_CHECK_FW_VER_WRITE)
		{
		
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s]STATUS_CHECK_FW_VER_WRITE\n",__func__);
#endif

			// 1. I2C Extend Command Area Setting
			if(tchkeypt_i2c_write_done(0x007B, tchkeyptdata->status_cmd, 1))
			{
				printk("[TCHKEYPT] [%s] 1. I2C Extend Command Area Setting Reg is failed\n",__func__);
				ret = -1;
			}

			tchkeyptdata->in_status_check_fw_ver_mode = STATUS_CHECK_FW_VER_READ;
			
			enable_irq(tchkeyptdata->client->irq);
		}

		else	//STATUS_CHECK_FW_VER_READ
		{
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s]STATUS_CHECK_FW_VER_READ\n",__func__);
#endif
			// 2. Read Device Info (version)
			if(tchkeypt_i2c_read_done(0x0112, tchkeyptdata->fw_ver,2))
			{
				printk("[TCHKEYPT] [%s] 2. Read Device Info (version) is failed\n",__func__);
				ret = -1;
			}
			else
				tchkeyptdata->check_ver_flag = 1;
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_0)
				printk("[TCHKEYPT][Major_ver: %d][Minor_ver: %d]\n",tchkeyptdata->fw_ver[0],tchkeyptdata->fw_ver[1]);
#endif
			tchkeyptdata->status_mode = STATUS_INTERRUPT_MODE;
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s]Setting STATUS_INTERRUPT_MODE\n",__func__);
#endif

			enable_irq(tchkeyptdata->client->irq);

	complete(&tchkeyptdata->check_ver_completion);
			
		}
	}

	
	else if(tchkeyptdata->status_mode == STATUS_CHANGE_PARAMETER_MODE)
	{
		
		//printk("[TCHKEYPT][%s]STATUS_CHANGE_PARAMETER_MODE\n",__func__);
		if(tchkeyptdata->in_status_chg_parameter_mode == STATUS_CHANGE_MODE_WRITE)
		{			
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s]STATUS_CHANGE_PARAMETER_MODE_WRITE\n",__func__);
#endif
			if(tchkeypt_i2c_write_done(tchkeyptdata->change_reg, &(tchkeyptdata->change_parameter_value[0]), 2))
			{
				printk("[TCHKEYPT] [%s][tchkeypt_i2c_write_done] [write value for change parameter Failed\n",__func__);
				ret = -1;
			} 	

			if(tchkeypt_i2c_write_done(0x007F, tchkeyptdata->status_cmd, 1))
			{
				printk("[TCHKEYPT] [%s][tchkeypt_i2c_write_done] [CHG_PAR_Host mode change Flag Area is failed]\n",__func__);
				ret = -1;
			} 	

			tchkeyptdata->in_status_chg_parameter_mode = STATUS_CHANGE_MODE_READ;
			
			enable_irq(tchkeyptdata->client->irq);
		}

		else	//STATUS_CHANGE_MODE_READ
		{
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_2)
				printk("[TCHKEYPT][%s]STATUS_CHANGE_PARAMETER_MODE_READ\n",__func__);
#endif

			// 3. Mode change Ack, Verify Touch flag
			if(tchkeypt_i2c_read_done(0x0100, buf,1))
			{
				printk("[TCHKEYPT][%s][tchkeypt_i2c_read_done] [CHG_PAR Mode change Ack, Verify Touch flag is failed]\n",__func__);
				ret = -1;
			}
			
			if(buf[0] == 0xAA)
			{			
#ifdef CONFIG_BACKTOUCH_DBG
				if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
					printk("[TCHKEYPT][%s]CHG_PAR_MODE is Successed\n",__func__);
#endif
			}
			
			else
			{
				printk("[TCHKEYPT][%s][CHG_PAR_MODE is Failed]\n",__func__);
				ret = -1;
			}
			
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
				printk("[TCHKEYPT][%s]Setting STATUS_INTERRUPT_MODE\n",__func__);
#endif
			tchkeyptdata->status_mode = STATUS_INTERRUPT_MODE;
			
			enable_irq(tchkeyptdata->client->irq);
			
		}
	}



	else
	{
		mutex_lock(&tchkeyptdata->i2clock);  // remove for maintouch mutex

		ret |= tchkeypt_get_reg(TCHKEYPT_SEQUENCE_KEY_REG, &data[0]);

		if(ret) {
			dbg("%s : can't get tchkey value \n",__func__);
			goto err_work_exit;
		}

		/************************************************************************************************************
		* data 0 : Firmware Status
		* data 1 : Event Type (Cap Mode, Histo Mode, Reserved, Reserved, Relative Point, Gesture Event, Key Event, Absolute Point )
		* data 2 : Gesture Data
		* data 3 : Valid Key
		* data 4 : Key Data(3 byte)
		* data 7 : Vaild Point ( bit enable - 1point 0x01, 2point 0x03, 3point 0x07 etc )
		* data 8 : Point X0(2 byte)
		* data10: Point Y0(2 byte)
		* data12: Point X1(2 byte)
		* data14: Point Y1(2 byte)
		* data16: Point X2(2 byte)
		* data18: Point Y2(2 byte)
		* data20: Point X3(2 byte)
		* data22: Point Y3(2 byte)
		* data24: Point X4(2 byte)
		* data26: Point Y4(2 byte)
		**************************************************************************************************************/	

		if(data[1]==ABSOLUTE_POINT && data[7]==0x01)//pad trackball gesture by one finger touch
		{
			dbg("[TCHKEYPT]One Finger Press Down Status\n");

			temp_x1 = data[8];	//b			

			temp_x = data[9];

			temp_y1 = data[10];
			
			temp_y = data[11];

			temp_x = (temp_x1<<8) | (temp_x&0x00FF);

			temp_y = (temp_y1<<8) | (temp_y&0x00FF);
			

#ifdef TUNING_POINTER_TEST
			temp_x = temp_x ;
			temp_y = temp_y ;
#else
			temp_x = temp_x * 2;
			temp_y = temp_y * 2;
#endif
			
			
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
				printk("[TCHKEYPT][%s]Down\tX position = %d\t Y Position = %d\n",__func__,temp_x,temp_y);
#endif

			temp_x = temp_x * 4;

			temp_y = temp_y * 4;


			//temp_x = (int)((temp_x * RESOLUTION_X)/TCHPAD_RANGE_X +0.5);
			//temp_y = (int)((temp_y * RESOLUTION_Y)/TCHPAD_RANGE_Y +0.5);

			
			//dbg("X position = %d\t Y Position = %d\n",temp_x,temp_y);
			
			//printk("X position = %d\t Y Position = %d\n",temp_x,temp_y);

			//temp_x = (int)((temp_x * RESOLUTION_X)/TCHPAD_RANGE_X +0.5);
			//temp_y = (int)((temp_y * RESOLUTION_Y)/TCHPAD_RANGE_Y +0.5);

			
			//dbg("X position = %d\t Y Position = %d\n",temp_x,temp_y);
			
			//printk("X position = %d\t Y Position = %d\n",temp_x,temp_y);

#ifdef MULTI_TOUCH
			id = input_mt_new_trkid(tchkeyptdata->tchkeypt);
			input_mt_slot(tchkeyptdata->tchkeypt, 0);
			input_report_abs(tchkeyptdata->tchkeypt, ABS_MT_TRACKING_ID, id);
			input_report_abs(tchkeyptdata->tchkeypt, ABS_MT_POSITION_X, temp_x);
			input_report_abs(tchkeyptdata->tchkeypt, ABS_MT_POSITION_Y, temp_y);
			input_report_key(tchkeyptdata->tchkeypt, BTN_TOUCH, 1);
			input_sync(tchkeyptdata->tchkeypt);
#endif

#ifdef SINGLE_TOUCH
			input_report_abs(tchkeyptdata->tchkeypt, ABS_X, temp_x);
			input_report_abs(tchkeyptdata->tchkeypt, ABS_Y, temp_y);
#ifndef TUNING_POINTER_TEST
			input_report_abs(tchkeyptdata->tchkeypt, ABS_Z, 255);
#endif
      	       //+US1-CF1
   	       //feature: FW_VENDOR_SUPPORT_REAR_TOUCH
		input_report_abs(tchkeyptdata->tchkeypt, ABS_Z, 0);
      	       //-US1-CF1
			input_report_key(tchkeyptdata->tchkeypt, BTN_TOUCH, 1);
			input_sync(tchkeyptdata->tchkeypt);
#endif
			
			tchkeyptdata->PAD_FUNCTION=ONE_FINGER_PRESS_STATUS;
			
			
		}
		if(data[7]==0x00 && data[8]==0xFF && data[9]==0xFF && data[10]==0xFF && data[11]==0xFF)//pad release event by no touch
		{
		
#ifdef CONFIG_BACKTOUCH_DBG
			if(tchkeyptdata->dbg_op >= BTOUCH_DBG_LVL_1)
				printk("[TCHKEYPT]One Finger Release Status\n");
#endif			
			if(tchkeyptdata->PAD_FUNCTION==ONE_FINGER_PRESS_STATUS)
			{			
#ifdef MULTI_TOUCH
			input_mt_slot(tchkeyptdata->tchkeypt, 0);
	        input_report_abs(tchkeyptdata->tchkeypt, ABS_MT_TRACKING_ID, -1);
			input_report_key(tchkeyptdata->tchkeypt, BTN_TOUCH, 0);
#endif

#ifdef SINGLE_TOUCH
			input_report_abs(tchkeyptdata->tchkeypt, ABS_X, temp_x);
			input_report_abs(tchkeyptdata->tchkeypt, ABS_Y, temp_y);
#ifndef TUNING_POINTER_TEST
			input_report_abs(tchkeyptdata->tchkeypt, ABS_Z, 255);
#endif
			input_report_key(tchkeyptdata->tchkeypt, BTN_TOUCH, 0);

#endif

			input_sync(tchkeyptdata->tchkeypt);

			}
			tchkeyptdata->PAD_FUNCTION=ONE_FINGER_RELEASE_STATUS;
			tchkeyptdata->PAD_TEMP_FUNCTION=ONE_FINGER_RELEASE_STATUS;
		}

		
err_work_exit:
		enable_irq(tchkeyptdata->client->irq);
		mutex_unlock(&tchkeyptdata->i2clock); // remove for maintouch mutex
	}

	dbg_func_out();
}


/* ------------- Register ---------------*/
/* -------------------------------------------------------------------- */
/* Driver */
/* -------------------------------------------------------------------- */

#ifdef CONFIG_HAS_EARLYSUSPEND
static void tchkey_early_suspend(struct early_suspend *handler)
{
	dbg_func_in();

	disable_irq(tchkeyptdata->client->irq);

	cancel_work_sync(&tchkeyptdata->work);	

	if(tchkeyptdata->setting_mode == 1 || tchkeyptdata->setting_mode == 0)	//setting off
		tchkeypt_power_down_mode_polling();

#ifdef CHECK_FINISH_FW_TIMER
	del_timer(&check_finish_fw_enable_delayed_timer);
#endif
	
	dbg_func_out();
}

static void tchkey_late_resume(struct early_suspend *handler)
{

	dbg_func_in();

	if(tchkeyptdata->setting_mode == 1)	//setting on
		tchkeypt_normal_mode_polling();

	enable_irq(tchkeyptdata->client->irq);

	dbg_func_out();
}
#endif

static int __devinit tchkeypt_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err=0, rc= 0;
	int error = 0;
	dbg_func_in();
	printk("[%s] START\n",__func__);
	
	tchkeyptdata = kzalloc (sizeof(struct tchkeyptdata_t),GFP_KERNEL);

	if (tchkeyptdata == NULL) {
		dbg("err kzalloc for tchkey\n");
		err = -ENOMEM;
	}

#ifdef USE_TCHPAD_WORKQUEUE
	tchpad_wq = create_singlethread_workqueue("tchpad_wq");
	if (!tchpad_wq)
	{
		dbg("create_singlethread_workqueue(tchpad_wq) error.\n");
		goto err_exit;//jhseo test
	}
#endif
	

	rc = gpio_request(TOUCHPAD_RST, "backtouch_rst");
	if (rc) {
		pr_err("gpio_request TOUCHPAD_RST : %d failed, rc=%d\n",TOUCHPAD_RST, rc);
		return -EINVAL;
	}
	
	// HW reset 
	rc = tchkeypt_hwreset();

	rc = gpio_request(TCHKEYPT_PS_INT_N, "backtouch_chg");
	if (rc) {
	 	printk("[TCHKEYPT]gpio_request TCHKEYPT_PS_INT_N : %d failed, rc=%d\n",TCHKEYPT_PS_INT_N, rc);
		return -EINVAL;
	}  

	rc = gpio_direction_input(TCHKEYPT_PS_INT_N);
	if (rc) {
		printk("[TCHKEYPT]gpio_direction_input TCHKEYPT_PS_INT_N : %d failed, rc=%d\n",TCHKEYPT_PS_INT_N, rc);
		return -EINVAL;
	}
	
	
#ifdef USE_FILE_ATTR
	if(!touch_pad_class)
		touch_pad_class=class_create(THIS_MODULE, "touch_rear");

	ts_pad_dev = device_create(touch_pad_class, NULL, 0, NULL, "ts_rear");
	if (IS_ERR(ts_pad_dev))
		pr_err("Failed to create device(ts)!\n");
	if (device_create_file(ts_pad_dev, &dev_attr_setup) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_setup.attr.name);
#endif

	// 1. initialize mutex
	mutex_init(&tchkeyptdata->i2clock);
	
	// 2. assign i2c client
	tchkeyptdata->client = client;
	i2c_set_clientdata(client, &tchkeyptdata);

	// 3. check available
	if(tchkeyptdata->client == NULL) {
		printk("[TCHKEYPT][ERR] %s : i2c client is NULL\n", __func__);
		goto err_exit;
	}

	tchkeyptdata->tchkeypt = input_allocate_device();
	if (!tchkeyptdata->tchkeypt) {
		dbg("[%s] err input allocate device\n",__func__);
		err = -ENOMEM;
		goto err_exit;
	}

	tchkeyptdata->tchkeypt = tchkeyptdata->tchkeypt;
	tchkeyptdata->tchkeypt->name = TCHKEYPT_DRV_NAME;
	tchkeyptdata->tchkeypt->dev.parent = &client->dev;

	INIT_WORK(&tchkeyptdata->work, tchkeypt_work_f);
	
#ifdef CHECK_FINISH_FW_TIMER
	INIT_WORK(&tchkeyptdata->work_chg_mode_start,tchkeypt_status_change_mode_func);

	check_fw_wq  = create_singlethread_workqueue("check_fw_wq");
	if(!check_fw_wq)
	{
		printk("[TCHKEYPT]create_singlethread_workqueue(check_fw_wq) ERROR \n");
		goto err_exit;
	}

	init_timer(&check_finish_fw_enable_delayed_timer);
	check_finish_fw_enable_delayed_timer.function = tchkeypt_chg_mode_start_func;
	check_finish_fw_enable_delayed_timer.data = 0;
	check_finish_fw_enable_delayed_timer.expires = 0;	
#endif

	set_bit(EV_KEY, tchkeyptdata->tchkeypt->evbit);
	set_bit(EV_ABS, tchkeyptdata->tchkeypt->evbit);
    set_bit(EV_SYN, tchkeyptdata->tchkeypt->evbit);
#ifndef TUNING_POINTER_TEST
	set_bit(INPUT_PROP_DIRECT, tchkeyptdata->tchkeypt->propbit);	//for divide back touch device
#endif
	set_bit(BTN_TOUCH, tchkeyptdata->tchkeypt->keybit);


#ifdef MULTI_TOUCH
	input_mt_init_slots(tchkeyptdata->tchkeypt, MAX_NUM_FINGER);
	input_set_abs_params(tchkeyptdata->tchkeypt, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(tchkeyptdata->tchkeypt, ABS_TOOL_WIDTH, 0, 15, 0, 0);
	input_set_abs_params(tchkeyptdata->tchkeypt, ABS_MT_POSITION_X, 0, RESOLUTION_X-1, 0, 0);
	input_set_abs_params(tchkeyptdata->tchkeypt, ABS_MT_POSITION_Y, 0, RESOLUTION_Y-1, 0, 0);
	input_set_abs_params(tchkeyptdata->tchkeypt, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(tchkeyptdata->tchkeypt, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);
#endif

#ifdef SINGLE_TOUCH
	input_set_abs_params(tchkeyptdata->tchkeypt, ABS_X, 0, RESOLUTION_X-1, 0, 0);
	input_set_abs_params(tchkeyptdata->tchkeypt, ABS_Y, 0, RESOLUTION_Y-1, 0, 0);
#ifndef TUNING_POINTER_TEST
	input_set_abs_params(tchkeyptdata->tchkeypt, ABS_Z, 0, 255, 0, 0);	//for divide back touch device 
#endif	
#endif


	error = input_register_device(tchkeyptdata->tchkeypt);
	if (error) {
		dbg("[%s] tchkeypt : Failed to register input device\n",__func__);
		err = -ENOMEM;
		goto err_exit;	}

	input_set_drvdata(tchkeyptdata->tchkeypt, tchkeyptdata);

	
	rc = misc_register(&touch_io);
	if (rc) 
	{
		pr_err("::::::::: can''t register qt602240 misc\n");
	}

	tchkeyptdata->client->irq = TCHKEYPT_PS_IRQ;

	//error = request_irq (tchkeyptdata->client->irq,tchkeypt_irq_handler,IRQF_TRIGGER_FALLING,"tchkeypt_ps_irq", tchkeyptdata);
	error = request_irq (tchkeyptdata->client->irq,tchkeypt_irq_handler,IRQF_TRIGGER_LOW,"tchkeypt_ps_irq", tchkeyptdata);

	if (error) {
		dbg("[%s] irq request error \n", __func__);
		err = -ENOMEM;
		goto err_exit;
	}

	tchkeypt_struct_initial();	// tchkeyptdata_t structure Initial

#ifdef FIRMWARE_ENABLE	
	init_completion(&tchkeyptdata->check_ver_completion);

	rc = tchkeypt_check_firmware_ver();

	if(rc < 0 ){
		printk("[TCHKEYPT][%s] tchkeypt_check_firmware_ver failed\n",__func__);
	}
	
	rc = wait_for_completion_interruptible_timeout(&tchkeyptdata->check_ver_completion, msecs_to_jiffies(2000));

	if(tchkeyptdata->check_ver_flag)	//firmware version check success
	{	
		tchkeypt_compare_fw_fwfile_ver();
		printk("[TCHKEYPT]Check Version is Success\n");
	}
	else
	{	
		//printk("[TCHKEYPT]Check Version is Failed and tchkeypt_status_change_mode\n");
		printk("[TCHKEYPT]Check Version is Failed and RESET\n");
		tchkeypt_hwreset();
/*		msleep(300);
		disable_irq(tchkeyptdata->client->irq);
		tchkeypt_normal_mode_polling();
		enable_irq(tchkeyptdata->client->irq);*/
	}
#else
	// change mode PDN mode(Deep sleep mode) -> Normal Mode
/*	disable_irq(tchkeyptdata->client->irq);
	tchkeypt_normal_mode_polling();
	enable_irq(tchkeyptdata->client->irq);*/
#endif



#ifdef CONFIG_HAS_EARLYSUSPEND
	//printk("[TCHKEYPT]CONFIG_HAS_EARLYSUSPEND\n");

//	tchkeyptdata->	pend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1; //front touch
//	tchkeyptdata->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 2;
	tchkeyptdata->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN -1;	// 49 
	tchkeyptdata->early_suspend.suspend = tchkey_early_suspend;
	tchkeyptdata->early_suspend.resume = tchkey_late_resume;
	register_early_suspend(&tchkeyptdata->early_suspend);
#endif

	printk("[%s] COMPLETE\n",__func__);

	dbg_func_out();
	
	return err;

err_exit:
	if (tchkeyptdata->tchkeypt) {
		input_free_device(tchkeyptdata->tchkeypt);
	}

	if (tchkeyptdata != NULL) {
		kfree(tchkeyptdata);
	}

	return -EIO;
}

static int __devexit tchkeypt_remove(struct i2c_client *client)
{
	int rc = 0;
	dbg_func_in();

	if(client != NULL) kfree(i2c_get_clientdata(client));

	
	mutex_destroy(&tchkeyptdata->i2clock);	

#ifdef CHECK_FINISH_FW_TIMER
	del_timer(&check_finish_fw_enable_delayed_timer);
	if (check_fw_wq)
		destroy_workqueue(check_fw_wq);
#endif

#ifdef USE_TCHPAD_WORKQUEUE
	if (tchpad_wq)
		destroy_workqueue(tchpad_wq);
#endif



	rc = misc_register(&touch_io);
	if (rc) 
	{
		pr_err("::::::::: can''t register qt602240 misc\n");
	}

	dbg_func_out();

	return 0;
}



// FILE IO
typedef enum {	
	BACKTOUCH_IOCTL_SELF_TEST_REF=4001,	
	BACKTOUCH_IOCTL_SELF_TEST_OPEN,	
	BACKTOUCH_IOCTL_SELF_TEST_SHORT,	
	BACKTOUCH_IOCTL_TURN_ON,	
	BACKTOUCH_IOCTL_TURN_OFF
} BACHTOUCH_IOCTL_CMD;

static int open(struct inode *inode, struct file *file) 
{
	file->private_data = tchkeyptdata;
	return 0; 
}

static ssize_t write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	int nBufSize=0;
	u8 cmd[2] = {0,};
	if((size_t)(*ppos) > 0) 
		return 0;

	if(buf!=NULL)
	{
		nBufSize=strlen(buf);
		if(strncmp(buf, "queue", 5)==0)
		{
			queue_work(tchpad_wq, &tchkeyptdata->work);
		}
		if(strncmp(buf, "tchkeypton", 10)==0)	// Normal mode
		{			
			tchkeypt_status_change_mode(1);
		}
		if(strncmp(buf, "tchkeyptoff", 11)==0)	//power down mode
		{			
			tchkeypt_status_change_mode(2);			
		}

		if(strncmp(buf, "done", 4)==0)
		{	
			cmd[0] = I2C_DONE_VALUE;
			
			tchkeypt_i2c_only_write(I2C_DONE_ADDR,&cmd[0],1);
		}

		if(strncmp(buf, "checkfw", 7)==0)
		{			
			tchkeypt_check_firmware_ver();
		}

		if(strncmp(buf, "fwfilecheck", 11)==0)
		{
			
			tchkeypt_compare_fw_fwfile_ver();
		}


		if(strncmp(buf, "hwreset",7) ==0)
		{
			tchkeypt_hwreset();
		}

		if(strncmp(buf, "fwarray", 7) == 0)
		{
			tchkeypt_fwupdate_array();
		
		}

		if(strncmp(buf, "enableirq", 9) == 0)
		{
			enable_irq(tchkeyptdata->client->irq);
		
		}

		if(strncmp(buf, "disableirq", 10) == 0)
		{
			disable_irq(tchkeyptdata->client->irq);
		
		}

		if(strncmp(buf, "lcdon", 5)==0)	// Normal mode
		{	
		
			tchkeypt_hwreset();
			tchkeypt_status_change_mode(1);
		}

		
		if(strncmp(buf, "lcdoff", 6)==0)	// Normal mode
		{	
		
			tchkeypt_hwreset();
			tchkeypt_status_change_mode(2);
		}
		if(strncmp(buf, "plcdon", 6)==0)	// polling check fw in phone
		{	
		
			disable_irq(tchkeyptdata->client->irq);
			tchkeypt_hwreset();
			tchkeypt_normal_mode_polling();
			enable_irq(tchkeyptdata->client->irq);
		}
		if(strncmp(buf, "ptchkeyptoff", 12)==0)	// polling check fw in phone
		{	
			disable_irq(tchkeyptdata->client->irq);
			tchkeypt_power_down_mode_polling();
			enable_irq(tchkeyptdata->client->irq);
		}

		
		if(strncmp(buf, "ptchkeypton", 11)==0)	// polling check fw in phone
		{	
			disable_irq(tchkeyptdata->client->irq);
			tchkeypt_normal_mode_polling();
			enable_irq(tchkeyptdata->client->irq);
		}

		if(strncmp(buf, "initialstruct", 11)==0)	// polling check fw in phone
		{	
			tchkeypt_struct_initial();
		}
		/*************************************************/
		// debug option Setting
		/*************************************************/
		
		if(strncmp(buf,"defaultdbgLVL",13) ==0)
		{
			tchkeyptdata->dbg_op = 0;
		}

		
		if(strncmp(buf,"1dbgLVL",7) ==0)
		{
			tchkeyptdata->dbg_op = 1;
		}


		if(strncmp(buf,"2dbgLVL",7) ==0)
		{
			tchkeyptdata->dbg_op = 2;
		}


		if(strncmp(buf,"3dbgLVL",7) ==0)
		{
			tchkeyptdata->dbg_op = 3;
		}



	
	}
	*ppos +=nBufSize;
	return nBufSize;
}

static long ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
//	void __user *argp = (void __user *)arg;
	printk("[TCHKEYPT] ioctl(%d, %d) \n",(int)cmd,(int)arg);

	switch (cmd) 
	{
		case BACKTOUCH_IOCTL_SELF_TEST_REF:
			
			//tchkeypt_status_change_mode(3);
			//return tchkeyptdata->self.result_reference;
			break;
			
		case BACKTOUCH_IOCTL_SELF_TEST_OPEN:
			//tchkeypt_status_change_mode(3);
			//return tchkeyptdata->self.result_open;
			break;
			
		case BACKTOUCH_IOCTL_SELF_TEST_SHORT:
			//tchkeypt_status_change_mode(3);
			//return tchkeyptdata->self.result_short;
			break;

		case BACKTOUCH_IOCTL_TURN_ON:
			mutex_lock(&tchkeyptdata->i2clock); 
            if(tchkeyptdata->setting_mode != 1) {
			printk("CMD Turn ON\n");			
			disable_irq(tchkeyptdata->client->irq);
			ret = tchkeypt_normal_mode_polling();
			enable_irq(tchkeyptdata->client->irq);
			if(ret >= 0)
				tchkeyptdata->setting_mode = 1;
            }
            mutex_unlock(&tchkeyptdata->i2clock); 			
			break;
		case BACKTOUCH_IOCTL_TURN_OFF:		
            mutex_lock(&tchkeyptdata->i2clock); 
            if(tchkeyptdata->setting_mode != 2) {
			printk("CMD Turn OFF\n");			
			disable_irq(tchkeyptdata->client->irq);
			ret = tchkeypt_power_down_mode_polling();
			enable_irq(tchkeyptdata->client->irq);
			if(ret >= 0)
				tchkeyptdata->setting_mode = 2;
            }
            mutex_unlock(&tchkeyptdata->i2clock); 			
			break;
		

		default:
			break;
	}

	return 0;
}

static const struct i2c_device_id tchkeypt_id[] = {
	{ "tchkeypt", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, tchkeypt_id);


static struct i2c_driver tchkeypt_driver = {
	.driver = {
		.name	= TCHKEYPT_DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	= tchkeypt_probe,
	.remove	= __devexit_p(tchkeypt_remove),
	.id_table = tchkeypt_id,
};

static int __init tchkeypt_init(void)
{
	dbg_func_in();
#if 1
	return i2c_add_driver(&tchkeypt_driver);
#endif
}

static void __exit tchkeypt_exit(void)
{
#if 1
	i2c_del_driver(&tchkeypt_driver);
#endif
}

/* -------------------------------------------------------------------- */

MODULE_AUTHOR("Seo JunHyuk <seo.junhyuk@pantech.com>");
MODULE_DESCRIPTION("TCHKEYPT proximity sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(tchkeypt_init);

module_exit(tchkeypt_exit);

/* -------------------------------------------------------------------- */
