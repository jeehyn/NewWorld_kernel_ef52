
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <mach/gpio.h>
#include <linux/gpio.h>
#include <mach/gpiomux.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#include "pac7620.h"

#ifdef IRMOTION_TRACE_LOG
#include <linux/time.h>
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/syscalls.h>
#endif

#define PAC7620_INT_GPIO 15

static int DbgMsgON = 0;
static int PollingON = 1;
static int GesLogON = 0;
static int LogON = 0;

#define PAC7620_MSG(fmt, args...) if(DbgMsgON) printk(fmt, ##args)

typedef struct input_sysfs_data{
	atomic_t enabled;
	//atomic_t delay;
}input_sysfs_data;

typedef struct {
	struct i2c_client	*client;
	struct input_dev *proximity_input_dev;
	struct input_dev *irmotion_input_dev;
	struct delayed_work work_irmotion;
	struct delayed_work work_proximity;

	struct input_sysfs_data proximity_data;
	struct input_sysfs_data irmotion_data;
	int irq;
	state_e state;
	mode_e mode;
	mode_e last_mode;
	bank_e bank;
} pac7620_data_t;

static  pac7620_data_t pac7620data;

/****************************************************************************************************
 *										TRACE LOG FILE										 	  	*
 ****************************************************************************************************/
#ifdef IRMOTION_TRACE_LOG

#define IRMOTION_LOGFILE "/data/motionlog.txt"

#define INT_DIGITS 19       /* enough for 64 bit integer */

struct file* filp = NULL;

static int TraceLogON = 0;

char *itoa(int i)
{
	/* Room for INT_DIGITS digits, - and '\0' */
	static char buf[INT_DIGITS + 2];
	char *p = buf + INT_DIGITS + 1;   /* points to terminating '\0' */
	if (i >= 0) {
		do {
			*--p = '0' + (i % 10);
			i /= 10;
		} while (i != 0);
		return p;
	}
	else {            /* i < 0 */
		do {
			*--p = '0' - (i % 10);
			i /= 10;
		} while (i != 0);
		*--p = '-';
	}
	return p;
}


struct file* file_open(const char* path, int flags, int rights) {
	struct file* filp = NULL;
	mm_segment_t oldfs;
	int err = 0;

	oldfs = get_fs();
	set_fs(get_ds());
	filp = filp_open(path, flags, rights);
	set_fs(oldfs);
	if(IS_ERR(filp)) {
		err = PTR_ERR(filp);
		return NULL;
	}
	return filp;
}

void file_close(struct file* file) {
	filp_close(file, NULL);
}

int file_write(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_write(file, data, size, &offset);

	set_fs(oldfs);
	return ret;
}
#endif
/****************************************************************************************************/

static int pac7672_pm(bool enable)
{
    int nRet;
    struct regulator *vreg_lvs4_1p8;

    vreg_lvs4_1p8 = regulator_get(NULL, "8921_lvs4");
    if(IS_ERR(vreg_lvs4_1p8)) {
        nRet = PTR_ERR(vreg_lvs4_1p8);
        return -EIO;
    }
    if(enable)
        nRet = regulator_enable(vreg_lvs4_1p8);
    else
        nRet = regulator_disable(vreg_lvs4_1p8);
    msleep(50);
    if(nRet<0) {
        return -EIO;
    }
    regulator_put(vreg_lvs4_1p8);

    return nRet;
}

/****************************************************************************************************
 *											pac7620											 	  	*
 ****************************************************************************************************/

static struct gpiomux_setting irmotion_active = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_UP,
	.dir = GPIOMUX_IN,
	
};


static struct gpiomux_setting irmotion_suspended = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_UP,
	.dir = GPIOMUX_IN,
};

static struct msm_gpiomux_config irmotion_gpio_configs[]  = {
	{ 
		.gpio      = PAC7620_INT_GPIO,
		.settings = {
			[GPIOMUX_ACTIVE]    = &irmotion_active,
			[GPIOMUX_SUSPENDED] = &irmotion_suspended,
		},
	},
};

static int pac7620_i2c_write(u8 reg, u8 *data, int len)
{
	u8  buf[20];
	int rc;
	int ret = 0;
	int i;

	buf[0] = reg;
	if (len >= 20) {
		printk(KERN_ERR "%s (%d) : FAILED: buffer size is limitted(20) %d\n", __func__, __LINE__, len);
		return -1;
	}

	for( i=0 ; i<len; i++ ) {
		buf[i+1] = data[i];
	}

	rc = i2c_master_send(pac7620data.client, buf, len+1);

	if (rc != len+1) {
		printk(KERN_ERR "%s (%d) : FAILED: writing to reg 0x%x    rc = %d\n", __func__, __LINE__, reg, rc);
		ret = -1;
	}

	return ret;
}


static int pac7620_i2c_read(u8 reg)
{
	int value;

	value = i2c_smbus_read_byte_data(pac7620data.client, reg);

	if (value < 0) {
		return 0;
	}	

	return value;
}

static int pac7620_set_reg(u8 reg, u8 data)
{
	int ret = pac7620_i2c_write(reg, &data, 1);

	return  ret;
}


static int pac7620_init_reg(void)
{
	int i=0;

	for(i = 0; i < INIT_REG_ARRAY_MAX_SIZE;i++){
		pac7620_set_reg(init_register_array[i][0],init_register_array[i][1]);
	}

	printk(KERN_INFO "%s (%d) : pac7620 initialize register.\n", __func__, __LINE__);

	return 0;
}

static int pac7620_init_ges_reg(void)
{
	int i=0;

	for(i = 0; i < GES_REG_ARRAY_MAX_SIZE;i++){
		pac7620_set_reg(ges_register_array[i][0],ges_register_array[i][1]);
	}

	printk(KERN_INFO "%s (%d) : pac7620 initialize gesture register.\n", __func__, __LINE__);

	return 0;
}

static int pac7620_init_prox_reg(void)
{
	int i = 0;
	for(i = 0; i < PROX_REG_ARRAY_MAX_SIZE;i++){
		pac7620_set_reg(prox_register_array[i][0],prox_register_array[i][1]);
	}

	printk(KERN_INFO "%s (%d) : pac7620 initialize proximity register.\n", __func__, __LINE__);

	return 0;
}

static int pac7620_reset_reg(void)
{
	pac7620_set_reg(PAC7620_ADDR_REG_BANK_RESET,1);

	printk(KERN_INFO "%s (%d) : register bank reset.\n", __func__, __LINE__);

	return 0;
}

static int pac7620_bank_select(bank_e bank)
{
	int ret = 0;
	switch(bank){
		case BANK0:
			ret = pac7620_set_reg(PAC7620_REGITER_BANK_SEL, PAC7620_BANK0);
			break;
		case BANK1:
			ret = pac7620_set_reg(PAC7620_REGITER_BANK_SEL, PAC7620_BANK1);
			break;
		default:
			break;
	}

	pac7620data.bank = bank;

	return ret;
}

static int pac7620_change_state(state_e state)
{
	switch(state){
		case SUSPEND_STATE:
			pac7620_bank_select(BANK1);
			pac7620_set_reg(PAC7620_ADDR_OPERATION_ENABLE, PAC7620_DISABLE);
			pac7620_bank_select(BANK0);
			pac7620_set_reg(PAC7620_ADDR_SUSPEND_CMD, PAC7620_I2C_SUSPEND);
			break;

		case RESUME_STATE:
			pac7620_i2c_read(0x00);
			pac7620_i2c_read(0x00);
			pac7620_i2c_read(0x00);
			pac7620_bank_select(BANK1);
			pac7620_set_reg(PAC7620_ADDR_OPERATION_ENABLE, PAC7620_ENABLE);			
			break;

		case STANDBY_STATE:
			//disable pac7620
			pac7620_bank_select(BANK1);
			pac7620_set_reg(PAC7620_ADDR_OPERATION_ENABLE, PAC7620_DISABLE);
			break;

		case WORKING_STATE:
			//enable
			pac7620_bank_select(BANK1);
			pac7620_set_reg(PAC7620_ADDR_OPERATION_ENABLE, PAC7620_ENABLE);
			break;

		default:
			//STANDBY1_STATE,
			//STANDBY2_STATE,	
			break;
	}

	pac7620data.state = state;

	return 0;
}

static int pac7620_register_mode(mode_e mode)
{
	if(pac7620data.state == SUSPEND_STATE){
		printk(KERN_DEBUG "%s (%d) : Can't set mode Coz PAC7620 is Suspend state\n", __func__, __LINE__);
	}

	switch(mode){
		case PROXIMITY_MODE:
			pac7620_init_prox_reg();
			break;
		case IRMOTION_MODE:
			pac7620_init_ges_reg();	
			break;
		case INITIAL_MODE:
			pac7620_init_reg();
			break;
		case RESET_MODE:
			pac7620_reset_reg();
			break;
		default:
			break;
	}
	pac7620data.mode = mode;

	return 0;
}


/****************************************************************************************************
 *											Debug											 	  	*
 ****************************************************************************************************/
static int pac7620_register_debug(void)
{
	int value =0;

	pac7620_bank_select(BANK0);

	value = pac7620_i2c_read(PAC7620_ADDR_GES_PS_DET_MASK_0);
	printk(KERN_DEBUG "%s (%d) : PAC7620_ADDR_GES_PS_DET_MASK_0 value : %d\n", __func__, __LINE__, value);

	value = pac7620_i2c_read(PAC7620_ADDR_GES_PS_DET_MASK_1);
	printk(KERN_DEBUG "%s (%d) : PAC7620_ADDR_GES_PS_DET_MASK_1 value : %d\n", __func__, __LINE__, value);

	value = pac7620_i2c_read(PAC7620_ADDR_STATE_INDICATOR);
	printk(KERN_DEBUG "%s (%d) : PAC7620_ADDR_STATE_INDICATOR value : %d\n", __func__, __LINE__, value);

	value = pac7620_i2c_read(PAC7620_ADDR_PS_HIGH_THRESHOLD);
	printk(KERN_DEBUG "%s (%d) : PAC7620_ADDR_PS_HIGH_THRESHOLD value : %d\n", __func__, __LINE__, value);

	value = pac7620_i2c_read(PAC7620_ADDR_PS_LOW_THRESHOLD);
	printk(KERN_DEBUG "%s (%d) : PAC7620_ADDR_PS_LOW_THRESHOLD value : %d\n", __func__, __LINE__, value);

	value = pac7620_i2c_read(PAC7620_ADDR_PS_APPROACH_STATE);
	printk(KERN_DEBUG "%s (%d) : PAC7620_ADDR_PS_APPROACH_STATE value : %d\n", __func__, __LINE__, value);

	printk(KERN_DEBUG "%s (%d) : name value : %s\n", __func__, __LINE__, pac7620data.proximity_input_dev->name);
	

	return 0;
}

static int pac7620_data_debug(void)
{
	pac7620_bank_select(BANK0);	

	printk(KERN_DEBUG "%s (%d) : name value : %s\n", __func__, __LINE__, pac7620data.proximity_input_dev->name);

	printk(KERN_DEBUG "%s (%d) : irq  value : %d\n", __func__, __LINE__, pac7620data.irq);

	printk(KERN_DEBUG "%s (%d) : proximity  enable value : %d\n", __func__, __LINE__, atomic_read(&pac7620data.proximity_data.enabled));
	
	printk(KERN_DEBUG "%s (%d) : irmotion  enable value : %d\n", __func__, __LINE__, atomic_read(&pac7620data.irmotion_data.enabled));

 	printk(KERN_DEBUG "%s (%d) : mode  value : %s\n", __func__, __LINE__,
 		(pac7620data.mode == PROXIMITY_MODE) ? "PROXIMITY_MODE" : (pac7620data.mode == IRMOTION_MODE) ? "IRMOTION_MODE" : 
 		(pac7620data.mode == INITIAL_MODE) ? "INITIAL_MODE" : (pac7620data.mode == RESET_MODE) ? "RESET_MODE" : "Unknown");
	
	printk(KERN_DEBUG "%s (%d) : last_mode  value : %s\n", __func__, __LINE__,
 		(pac7620data.last_mode == PROXIMITY_MODE) ? "PROXIMITY_MODE" : (pac7620data.last_mode == IRMOTION_MODE) ? "IRMOTION_MODE" : 
 		(pac7620data.last_mode == INITIAL_MODE) ? "INITIAL_MODE" : (pac7620data.last_mode == RESET_MODE) ? "RESET_MODE" : "Unknown");

	printk(KERN_DEBUG "%s (%d) : state  value : %s\n", __func__, __LINE__,
 		(pac7620data.state == SUSPEND_STATE) ? "SUSPEND_STATE" : (pac7620data.state == RESUME_STATE) ? "RESUME_STATE" : 
 		(pac7620data.state == STANDBY_STATE) ? "STANDBY_STATE" : (pac7620data.state == WORKING_STATE) ? "WORKING_STATE" : "Unknown");


	return 0;
}

static ssize_t pac7620_debug_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	if (sysfs_streq(buf, "data"))
		pac7620_data_debug();
	else if (sysfs_streq(buf, "reg"))
		pac7620_register_debug();
	else if(sysfs_streq(buf, "reset"))
		pac7620_register_mode(RESET_MODE);
	else if(sysfs_streq(buf, "msgon"))
		DbgMsgON = 1;
	else if(sysfs_streq(buf, "msgoff"))
		DbgMsgON = 0;
	else if(sysfs_streq(buf, "gpiore"))
		gpio_request(PAC7620_INT_GPIO,"pac7620_int_gpio");	
	else if(sysfs_streq(buf, "gpiofr"))
		gpio_free(PAC7620_INT_GPIO);
	else if(sysfs_streq(buf, "pull"))
		gpio_tlmm_config(GPIO_CFG(PAC7620_INT_GPIO, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
	else if(sysfs_streq(buf, "pollon"))
		PollingON = 1;
	else if(sysfs_streq(buf, "polloff"))
		PollingON = 0;	
	else if(sysfs_streq(buf, "logon"))
		LogON = 1;
	else if(sysfs_streq(buf, "logoff"))
		LogON = 0;
	else if(sysfs_streq(buf, "geson"))
		GesLogON = 1;
	else if(sysfs_streq(buf, "gesoff"))
		GesLogON = 0;	
	else {
		printk(KERN_DEBUG "%s (%d) : invalid value %d\n", __func__, __LINE__,*buf);		
		return 0;
	}

	return size;
}


static ssize_t pac7620_debug_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	//int val = atomic_read(&pac7620data.irmotion_data.enabled);
	//return sprintf(buf, "%d\n", val);
	return 0;
}


/****************************************************************************************************
 *											Gesture											 	  	*
 ****************************************************************************************************/
static void pac7620_irmotion_enable(void)
{
	if(pac7620data.mode == PROXIMITY_MODE){
		pac7620data.last_mode = IRMOTION_MODE;
		return;
	}

	if (!atomic_cmpxchg(&pac7620data.irmotion_data.enabled, 0, 1)) {
		unsigned short ges = 0;
		pac7620_register_mode(IRMOTION_MODE);
		pac7620_change_state(WORKING_STATE);	
		pac7620_bank_select(BANK0);
		GET_GESTURE(ges,GES_PS_DET_FLAG); //dummy gesture read

		schedule_delayed_work(&pac7620data.work_irmotion, 200);
	}
}

static void pac7620_irmotion_disable(void)
{
	if(pac7620data.mode == PROXIMITY_MODE){
		pac7620data.last_mode = INITIAL_MODE;
		return;
	}else{
		pac7620data.mode = INITIAL_MODE;
	}	

	if (atomic_cmpxchg(&pac7620data.irmotion_data.enabled, 1, 0)) {
		pac7620_change_state(STANDBY_STATE);
		cancel_delayed_work_sync(&pac7620data.work_irmotion);
	}
}

static ssize_t irmotion_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int value;

	if (sysfs_streq(buf, "1"))
		value = true;
	else if (sysfs_streq(buf, "0"))
		value = false;
	else {
		printk(KERN_ERR "%s (%d) : invalid value %d\n", __func__, __LINE__,*buf);		
		return size;
	}

	if(value == 1){
		printk(KERN_INFO "%s (%d) : gesture enable\n", __func__, __LINE__);
		
#ifdef IRMOTION_TRACE_LOG
		if(LogON == 1){
			filp = file_open(IRMOTION_LOGFILE,O_CREAT | O_RDWR | O_APPEND | O_LARGEFILE,0777);		
			if(filp != NULL){
				TraceLogON = 1;
			}
		}
#endif

		pac7620_irmotion_enable();
	}else if(value == 0){
		printk(KERN_INFO "%s (%d) : gesture disable\n", __func__, __LINE__);
		pac7620_irmotion_disable();

#ifdef IRMOTION_TRACE_LOG	
		if(LogON == 1){
			file_write(filp, filp->f_pos, "========================================\n", 41);		
			file_close(filp);
	}
		TraceLogON = 0;
#endif
	}

	return size;
}


static ssize_t irmotion_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = atomic_read(&pac7620data.irmotion_data.enabled);
	return sprintf(buf, "%d\n", val);
	return 0;
}


static void pac7620_irmotion_work_func(struct work_struct *work)
{	
	unsigned short ges = 0;
	unsigned short size = 0;
	unsigned char brightness = 0;
	static unsigned int count = 0;

	pac7620_bank_select(BANK0);

	GET_GESTURE(ges,GES_PS_DET_FLAG);
	GET_OBJECT_SIZE(size,OBJECT_SIZE);
	GET_BRIGHTNESS(brightness);

	//printk("%s (%d) : size -> %d(0x%x), brite -> %d(0x%x)\n", __func__, __LINE__,size,size,brightness,brightness);

	count++;

	if(count >= 10000){
		count=0;
	}
	

	switch(ges){
		case GES_RIGHT_FLAG:
			
			PAC7620_MSG("%s (%d) : gesture -> RIGHT size -> 0x%x\n", __func__, __LINE__,size);
			break;
		case GES_LEFT_FLAG:
			
			PAC7620_MSG("%s (%d) : gesture -> LEFT size -> 0x%x\n", __func__, __LINE__,size);
			break;

		case GES_UP_FLAG:
			
			PAC7620_MSG("%s (%d) : gesture -> UP size -> 0x%x\n", __func__, __LINE__,size);
			break;

		case GES_DOWN_FLAG:
			
			PAC7620_MSG("%s (%d) : gesture -> DOWN size -> 0x%x\n", __func__, __LINE__,size);
			break;

		case GES_FORWARD_FLAG:
			
			PAC7620_MSG("%s (%d) : gesture -> FORWARD size -> 0x%x\n", __func__, __LINE__,size);
			break;

		case GES_BACKWARD_FLAG:
			
			PAC7620_MSG("%s (%d) : gesture -> BACKWORD size -> 0x%x\n", __func__, __LINE__,size);
			break;

		case GES_CLOCKWISE_FLAG:
			
			PAC7620_MSG("%s (%d) : gesture -> CLOCKWISE size -> 0x%x\n", __func__, __LINE__,size);
			break;

		case GES_COUNT_CLOCKWISE_FLAG:
			
			PAC7620_MSG("%s (%d) : gesture -> COUNTER CLOCKWISE size -> 0x%x\n", __func__, __LINE__,size);
			break;
			
		case GES_WAVE_FLAG:
			
			PAC7620_MSG("%s (%d) : gesture -> WAVE size -> 0x%x\n", __func__, __LINE__,size);
			break;
			
		default:
			break;
	}

	if(GesLogON == 1){
		printk("[%s] size -> %d  brightness-> %d\n", (ges == GES_RIGHT_FLAG) ? "RI" : (ges == GES_LEFT_FLAG) ? "RE" : \
			(ges == GES_UP_FLAG) ? "UP" : (ges == GES_DOWN_FLAG) ? "DO" :	\
			(ges == GES_FORWARD_FLAG) ? "FW" : (ges == GES_BACKWARD_FLAG) ? "BW" :	\
			(ges == GES_CLOCKWISE_FLAG) ? "CW" : (ges == GES_COUNT_CLOCKWISE_FLAG) ? "CC" :\
			(ges == GES_WAVE_FLAG) ? "WA" : "NN",  size, brightness);
	}
	
#ifdef IRMOTION_TRACE_LOG	
	if((TraceLogON == 1) && (LogON == 1)){
		char sz[2]={0,};
		char br[2]={0,};
		char *s,*b = NULL;
		s = sz; b = br;		
		
		file_write(filp, filp->f_pos, "Gesture=", 8);
		
		file_write(filp, filp->f_pos, (ges == GES_RIGHT_FLAG) ? "RI" : (ges == GES_LEFT_FLAG) ? "RE" : \
			(ges == GES_UP_FLAG) ? "UP" : (ges == GES_DOWN_FLAG) ? "DO" :	\
			(ges == GES_FORWARD_FLAG) ? "FW" : (ges == GES_BACKWARD_FLAG) ? "BW" :	\
			(ges == GES_CLOCKWISE_FLAG) ? "CW" : (ges == GES_COUNT_CLOCKWISE_FLAG) ? "CC" :\
			(ges == GES_WAVE_FLAG) ? "WA" : "NN", 2);

		file_write(filp, filp->f_pos, "  size=", 7);
		file_write(filp, filp->f_pos, itoa(size), sizeof(4));

		file_write(filp, filp->f_pos, "  brightness=",13);
		file_write(filp, filp->f_pos, itoa(brightness), sizeof(4));

		file_write(filp, filp->f_pos, "\n",1);
	}
#endif
	
	input_report_abs(pac7620data.irmotion_input_dev, ABS_X, ges);
	input_report_abs(pac7620data.irmotion_input_dev, ABS_Y, size);
	input_report_abs(pac7620data.irmotion_input_dev, ABS_Z, count);
	
	input_sync(pac7620data.irmotion_input_dev);

#if defined(CONFIG_COVER_IN_ALL_DIRECTION)
	schedule_delayed_work(&pac7620data.work_irmotion, 25);	// 25 * HZ
#else	
	schedule_delayed_work(&pac7620data.work_irmotion, 50);	// 50 * HZ	
#endif	// CONFIG_COVER_IN_ALL_DIRECTION
}


static DEVICE_ATTR(ir_enable, S_IRUGO | S_IWUSR | S_IWGRP | S_IROTH | S_IWOTH, irmotion_enable_show, irmotion_enable_store);

static DEVICE_ATTR(debug, S_IRUGO | S_IWUSR | S_IWGRP, pac7620_debug_show, pac7620_debug_store);


static struct attribute *irmotion_sysfs_attrs[] = {
	&dev_attr_ir_enable.attr,
	&dev_attr_debug.attr,
	NULL
};

static struct attribute_group irmotion_attribute_group = {
	.attrs = irmotion_sysfs_attrs,
};


/****************************************************************************************************
 *											Proximity										 	  	*
 ****************************************************************************************************/
  

irqreturn_t pac7620_irq_thread_fn(int irq, void *data)
{
	disable_irq_nosync(pac7620data.irq);

	schedule_delayed_work(&pac7620data.work_proximity, 0);

	//enable_irq(pac7620data.irq);	
	return IRQ_HANDLED;
}



static void pac7620_proximity_dummy_event(void)
{	

	input_report_abs(pac7620data.proximity_input_dev, ABS_X, 5);
	input_report_abs(pac7620data.proximity_input_dev, ABS_Y, 0);
	input_report_abs(pac7620data.proximity_input_dev, ABS_Z, 0);

	input_sync(pac7620data.proximity_input_dev);	
}


static void pac7620_proximity_enable(void)
{
	int ret = 0;
	if(pac7620data.mode == IRMOTION_MODE){
		pac7620data.last_mode = IRMOTION_MODE;
		pac7620_irmotion_disable();
	}	

	if (!atomic_cmpxchg(&pac7620data.proximity_data.enabled, 0, 1)) {

		// we will be remove disable_irq/enable_irq Coz of IRQF_ONESHOT
		if(PollingON == 0){
			ret = request_threaded_irq(pac7620data.irq, NULL,
						pac7620_irq_thread_fn,
						IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
						"pac7620_irq", &pac7620data);

			if (ret < 0) {
				printk(KERN_ERR "%s (%d) : request_irq failed for gpio %d (%d)\n", __func__, __LINE__, pac7620data.irq , ret);
			}	
		}else{
			schedule_delayed_work(&pac7620data.work_proximity, 20);
		}


		pac7620_register_mode(PROXIMITY_MODE);
		pac7620_change_state(WORKING_STATE);		


		//enable_irq(pac7620data.irq);	
	}
}

static void pac7620_proximity_disable(void)
{
	if (atomic_cmpxchg(&pac7620data.proximity_data.enabled, 1, 0)) {
		//cancel_work_sync(&pac7620data.work_proximity);
		cancel_delayed_work_sync(&pac7620data.work_proximity);

		if(PollingON == 0){
			disable_irq_nosync(pac7620data.irq);

			free_irq(pac7620data.irq, &pac7620data);
		}

		pac7620_proximity_dummy_event();

		pac7620_change_state(STANDBY_STATE);

		if(pac7620data.last_mode == IRMOTION_MODE){
			pac7620data.mode = INITIAL_MODE;
			pac7620data.last_mode = INITIAL_MODE;
			pac7620_irmotion_enable();
		}else{
			pac7620data.mode = INITIAL_MODE;
			pac7620data.last_mode = INITIAL_MODE;
		}	
	}
}

static ssize_t proximity_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int value = 0;

	if (sysfs_streq(buf, "1"))
		value = true;
	else if (sysfs_streq(buf, "0"))
		value = false;
	else {
		printk(KERN_ERR "%s (%d) : invalid value %d\n", __func__, __LINE__,*buf);		
		return size;
	}

	if(gpio_get_value(PAC7620_INT_GPIO) == 0){
		gpio_tlmm_config(GPIO_CFG(PAC7620_INT_GPIO, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
		pac7620_bank_select(BANK0);
		pac7620_i2c_read(PAC7620_ADDR_GES_PS_DET_FLAG_1); //booting time just once run
		pac7620_i2c_read(PAC7620_ADDR_GES_PS_DET_FLAG_1); //booting time just once run
	}

	if(value == 1){		
		printk(KERN_INFO "%s (%d) : proximity enable\n", __func__, __LINE__);
		pac7620_proximity_enable();
	}else if(value == 0){
		printk(KERN_INFO "%s (%d) : proximity disable\n", __func__, __LINE__);
		pac7620_proximity_disable();
	}

	return size;
}


static ssize_t proximity_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = atomic_read(&pac7620data.proximity_data.enabled);
	return sprintf(buf, "%d\n", val);
}

static void pac7620_proximity_work_func(struct work_struct *work)
{	
	int ret = 0;
	unsigned char res_val =0, detect = 0, raw_data = 0;

	ret = pac7620_bank_select(BANK0);
	
	if(ret < 0){
		printk(KERN_ERR "%s (%d) : Bank selection failed \n", __func__, __LINE__);	
		schedule_delayed_work(&pac7620data.work_proximity, 20);
		return;
	}
	
	res_val = pac7620_i2c_read(PAC7620_ADDR_GES_PS_DET_FLAG_1);
	detect = pac7620_i2c_read(PAC7620_ADDR_PS_APPROACH_STATE);
	raw_data = pac7620_i2c_read(PAC7620_ADDR_PS_RAW_DATA);

	//report value
	input_report_abs(pac7620data.proximity_input_dev, ABS_X, ((int)detect == 1) ? 0 : 5);
	input_report_abs(pac7620data.proximity_input_dev, ABS_Y, (int)raw_data);
	input_report_abs(pac7620data.proximity_input_dev, ABS_Z, (int)res_val);
	input_sync(pac7620data.proximity_input_dev);

	PAC7620_MSG(KERN_INFO "%s PROXIMITY detect (0x%x), (%d), (%d)\n", __func__, detect, raw_data, res_val);
	
	if(PollingON == 0){
		enable_irq(pac7620data.irq);
	}else{
		schedule_delayed_work(&pac7620data.work_proximity, 20);
	}
}

static DEVICE_ATTR(prox_enable, S_IRUGO | S_IWUSR | S_IWGRP | S_IROTH | S_IWOTH, proximity_enable_show, proximity_enable_store);

static struct attribute *proximity_sysfs_attrs[] = {
	&dev_attr_prox_enable.attr,
	NULL
};

static struct attribute_group proximity_attribute_group = {
	.attrs = proximity_sysfs_attrs,
};


/****************************************************************************************************/


static int pac7620_init_interrupt(void)
{
	int ret = 0;
	
	printk(KERN_INFO "%s (%d) : initialize interrupt\n", __func__, __LINE__);

	msm_gpiomux_install(irmotion_gpio_configs, ARRAY_SIZE(irmotion_gpio_configs));

	gpio_tlmm_config(GPIO_CFG(PAC7620_INT_GPIO, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA),GPIO_CFG_ENABLE);

	gpio_request(PAC7620_INT_GPIO,"pac7620_int_gpio");

	gpio_direction_input(PAC7620_INT_GPIO);

	pac7620data.irq = gpio_to_irq(PAC7620_INT_GPIO);
/*
	ret = request_threaded_irq(pac7620data.irq, NULL,
			pac7620_irq_thread_fn,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"pac7620_irq", &pac7620data);

	if (ret < 0) {
		printk(KERN_ERR "%s (%d) : request_irq failed for gpio %d (%d)\n", __func__, __LINE__, pac7620data.irq , ret);
		return 0;
	}

	disable_irq_nosync(pac7620data.irq);
*/
	return ret;
}

static int pac7620_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err = 0;

	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);

	dev_info(&client->dev,"pac7620 probe\n");

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)) {
		err = -EIO;
		return err;
	}	

	pac7620data.client = client;
	pac7620data.mode = INITIAL_MODE;
	pac7620data.last_mode = INITIAL_MODE;
	pac7620data.bank =BANK0;
	
	atomic_set(&pac7620data.proximity_data.enabled, 0);
	atomic_set(&pac7620data.irmotion_data.enabled, 0);
		
	pac7620_change_state(RESUME_STATE);

	pac7620_register_mode(INITIAL_MODE);


	INIT_INPUT_DATA(pac7620data.proximity_input_dev, proximity_attribute_group, "proximity");

	INIT_INPUT_DATA(pac7620data.irmotion_input_dev, irmotion_attribute_group, "irmotion");


	pac7620_init_interrupt();

	INIT_DELAYED_WORK(&pac7620data.work_irmotion, pac7620_irmotion_work_func);
	INIT_DELAYED_WORK(&pac7620data.work_proximity, pac7620_proximity_work_func);

	pac7620_register_debug();

	pac7620_change_state(STANDBY_STATE);

	return 0;
}


static int pac7620_i2c_remove(struct i2c_client *client)
{
	dev_info(&client->dev,"pac7620 remove\n");
		
	pac7620_set_reg(PAC7620_ADDR_SUSPEND_CMD, PAC7620_I2C_SUSPEND);

	if(PollingON == 0){
		free_irq(pac7620data.irq, &pac7620data);
	}

	gpio_free(PAC7620_INT_GPIO);

	sysfs_remove_group(&pac7620data.proximity_input_dev->dev.kobj,
			&proximity_attribute_group);

	input_unregister_device(pac7620data.proximity_input_dev);

	sysfs_remove_group(&pac7620data.irmotion_input_dev->dev.kobj,
			&irmotion_attribute_group);

	input_unregister_device(pac7620data.irmotion_input_dev);

	return 0;
}

static int pac7620_suspend(struct device *dev)
{
	printk(KERN_INFO "pac7620 suspend\n");

	if(pac7620data.mode == PROXIMITY_MODE){
		if(PollingON == 0){
			irq_set_irq_wake(pac7620data.irq,1);
		}
	}else{
		pac7620_change_state(SUSPEND_STATE);
	}	

	return 0;
}

static int pac7620_resume(struct device *dev)
{
	printk(KERN_INFO "pac7620 resume\n");
	
	if(pac7620data.mode == PROXIMITY_MODE){
		if(PollingON == 0){
			irq_set_irq_wake(pac7620data.irq,0);
		}
	}else{
		pac7620_change_state(RESUME_STATE);
	}	

	return 0;
}

static const struct i2c_device_id pac7620_device_id[] = {
	{"irmotion", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, pac7620_device_id);

static const struct dev_pm_ops pac7620_pm_ops = {
	.suspend = pac7620_suspend,
	.resume = pac7620_resume
};

static struct i2c_driver pac7620_i2c_driver = {
	.driver = {
		.name = "irmotion",
		.owner = THIS_MODULE,
		.pm = &pac7620_pm_ops},
	.probe = pac7620_i2c_probe,
	.remove = pac7620_i2c_remove,
	.id_table = pac7620_device_id,
};

static int __init pac7620_init(void)
{
	printk(KERN_INFO "%s (%d) : init module\n", __func__, __LINE__);
	pac7672_pm(true); 
	return i2c_add_driver(&pac7620_i2c_driver);
}

static void __exit pac7620_exit(void)
{
	printk(KERN_INFO "%s (%d) : exit module\n", __func__, __LINE__);	

	i2c_del_driver(&pac7620_i2c_driver);
}

module_init(pac7620_init);
module_exit(pac7620_exit);
MODULE_AUTHOR("pantech");
MODULE_DESCRIPTION("pac7620 motion sensor driver");
MODULE_LICENSE("GPL");
