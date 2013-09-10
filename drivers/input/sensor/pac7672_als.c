
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>	
#include <linux/delay.h>	


#define REG_SET_BIT(val)	(1 << val)
#define REG_CLEAR_BIT(val)	(0xff & (0 << val))

// REGISTER DESCRIPTION --------------------------------------------
#define PAC7672_REG_OPMODE				0x03
#define ALS_OP_ENABLE		REG_SET_BIT(1)
#define ALS_OP_DISABLE		REG_CLEAR_BIT(1)
#define PS_ALS_OP_DISABLE	(REG_CLEAR_BIT(1) & REG_CLEAR_BIT(0))
#define ALS_OP_MODE_CHANGE	REG_SET_BIT(3)


#define PAC7672_REG_STATE_CONFIG		0x04
#define PAC7672_REG_INT_CONFIG			0x09
#define PAC7672_REG_WIN_HIGH_THRE		0x0A
#define PAC7672_REG_WIN_LOW_THRE		0x0B
#define PAC7672_REG_READ_ALS_LOW		0x0E
#define PAC7672_REG_READ_ALS_HIGH		0x0F

#define PAC7672_REG_INT_PIN_CONFIG		0x11
#define PS_APPROACH_N	REG_SET_BIT(5)			// RW

#define ALS_HIGH_TH			0x000	// 127
#define ALS_LOW_TH			0x001	// 255
// -----------------------------------------------------------------


enum {
	ALS_ENABLE = 0,
	ALS_DISABLE,
};

// STATE
typedef enum {
	SUSPEND_STATE = 0,
	OPERATION_STATE,	// simpified mode
	NONE_STATE,	
} state_e;

//////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
	struct i2c_client	*client;
	struct input_dev *als_input_dev;
	struct delayed_work work;
} pac7672_data_t;

static  pac7672_data_t als_data;

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

static int pac7672_i2c_write(u8 reg, u8 *data, int len)
{
	u8  buf[20];
	int rc;
	int ret = 0;
	int i;

	buf[0] = reg;
	if (len >= 20) {
		printk("%s (%d) : FAILED: buffer size is limitted(20) %d\n", __func__, __LINE__, len);
		dev_err(&als_data.client->dev, "pac7672_i2c_write FAILED: buffer size is limitted(20)\n");
		return -1;
	}

	for( i=0 ; i<len; i++ ) {
		buf[i+1] = data[i];
	}
 
	rc = i2c_master_send(als_data.client, buf, len+1);

	if (rc != len+1) {
		printk("%s (%d) : FAILED: writing to reg 0x%x\n", __func__, __LINE__, reg);

		ret = -1;
	}

	return ret;
}


static int pac7672_i2c_read(u8 reg)
{
	int value;

	value = i2c_smbus_read_byte_data(als_data.client, reg);

	if (value < 0) {
		return 0;
	}	

	return value;
}


static int pac7672_set_reg(u8 reg, u8 data)
{
	int ret = pac7672_i2c_write(reg, &data, 1);
	
	printk("%s (%d) : set register \n", __func__, __LINE__);


	return  ret;
}


static int pac7672_init_reg(void)
{
	int value=0, ret=0;
	
	value = pac7672_i2c_read(PAC7672_REG_OPMODE);									// default 0x03	
	printk("%s (%d) : OP Mode value : %d\n", __func__, __LINE__, value);
	
	value = (value | ALS_OP_MODE_CHANGE);
	ret = pac7672_set_reg(PAC7672_REG_OPMODE, value & PS_ALS_OP_DISABLE);

	value = pac7672_i2c_read(PAC7672_REG_INT_PIN_CONFIG);							// default 0x0C
	printk("%s (%d) : INT_N pin value : %d\n", __func__, __LINE__, value);	
	
	ret = pac7672_set_reg(PAC7672_REG_INT_PIN_CONFIG,value | PS_APPROACH_N);		// 1 : Not Aproach
	ret = pac7672_set_reg(PAC7672_REG_WIN_HIGH_THRE, ALS_HIGH_TH);					// ALS Window High Threshold
	ret = pac7672_set_reg(PAC7672_REG_WIN_LOW_THRE, ALS_LOW_TH);					// ALS Window Low Threshold
	
	printk("%s (%d) : pac7672 initialize register.\n", __func__, __LINE__);
	
	return ret;
}

static int pac7672_als_change_state(state_e st)
{
	int reg_value = 0;

	switch(st) {
		case OPERATION_STATE:
			reg_value = pac7672_i2c_read(PAC7672_REG_OPMODE);
			/* enable setting */
			pac7672_set_reg(PAC7672_REG_OPMODE, reg_value | ALS_OP_ENABLE);
			break;

		case SUSPEND_STATE:
			reg_value = pac7672_i2c_read(PAC7672_REG_OPMODE);
			/* enable setting */
			pac7672_set_reg(PAC7672_REG_OPMODE, reg_value & ALS_OP_DISABLE);
			break;
			
		default:	// 
			break;
	}
	
	return 0;
}


static void pac7672_als_enable(void)
{
	printk("%s (%d) : als enable\n", __func__, __LINE__);
	pac7672_als_change_state(OPERATION_STATE);	
	schedule_delayed_work(&als_data.work, 10);	
}

static void pac7672_als_disable(void)
{
	printk("%s (%d) : als disable\n", __func__, __LINE__);
	pac7672_als_change_state(SUSPEND_STATE);	
	cancel_delayed_work_sync(&als_data.work);
}


static ssize_t als_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int value = 0;
	
	if (sysfs_streq(buf, "1"))
		value = true;
	else if (sysfs_streq(buf, "0"))
		value = false;
	else {
		printk("%s (%d) : invalid value %d\n", __func__, __LINE__,*buf);		
		return 0;
	}

	if(value == 1){
		pac7672_als_enable();	
	}else if(value == 0){
		pac7672_als_disable();
	}

	return size;
}

static ssize_t als_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("%s (%d) : als enable show\n", __func__, __LINE__);
	return 0;
}

static DEVICE_ATTR(als_enable, S_IRUGO | S_IWUSR | S_IWGRP | S_IROTH | S_IWOTH, als_enable_show, als_enable_store);


static struct attribute *als_sysfs_attrs[] = {
	&dev_attr_als_enable.attr,
	NULL
};

static struct attribute_group als_attribute_group = {
	.attrs = als_sysfs_attrs,
};


static int pac7672_init_als_data(void)
{
	int ret = 0;

	printk("%s (%d) : initialize data\n", __func__, __LINE__);
	
	als_data.als_input_dev = input_allocate_device();
	
	if (!als_data.als_input_dev) {
		printk("%s (%d) : could not allocate als input device\n", __func__, __LINE__);
		return -1;
	}

	input_set_drvdata(als_data.als_input_dev, &als_data);
	als_data.als_input_dev->name = "pac7672_als";
	
	input_set_capability(als_data.als_input_dev, EV_ABS, ABS_MISC);	
	input_set_abs_params(als_data.als_input_dev, ABS_X, 0, 24000, 0, 0);
	input_set_abs_params(als_data.als_input_dev, ABS_Y, 0, 256, 0, 0);
	input_set_abs_params(als_data.als_input_dev, ABS_Z, 0, 256, 0, 0);
	ret = input_register_device(als_data.als_input_dev);

	if (ret < 0) {
		input_free_device(als_data.als_input_dev);
		printk("%s (%d) : could not register input device\n", __func__, __LINE__);	
		return ret;
	}

	ret = sysfs_create_group(&als_data.als_input_dev->dev.kobj, &als_attribute_group);	
	if (ret) {
		printk("%s (%d) : could not create sysfs group\n", __func__, __LINE__);
		return -1;
	}
	
	return 0;	
}

static void pac7672_als_work_func(struct work_struct *work)
{	
	unsigned char value_h=0, value_l=0;
	int als_lux = 0;

	value_h = pac7672_i2c_read(PAC7672_REG_READ_ALS_HIGH);		// ALS Readout
	value_l = pac7672_i2c_read(PAC7672_REG_READ_ALS_LOW);			// ALS Readout
	als_lux = (value_h << 8) | value_l;

	//printk("%s (%d) : als value : %d(0x%x,%x)\n", __func__, __LINE__, als_lux,value_h,value_l);

	// report vlaue
	input_report_abs(als_data.als_input_dev, ABS_X, als_lux);
	input_report_abs(als_data.als_input_dev, ABS_Y, (int)value_h);
	input_report_abs(als_data.als_input_dev, ABS_Z, (int)value_l);
	
	input_sync(als_data.als_input_dev);

	schedule_delayed_work(&als_data.work, 10);	
}


static int pac7672_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err = 0;
	
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);

	printk("%s (%d) : probe module\n", __func__, __LINE__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)) {
		err = -EIO;
		return err;
	}	

	als_data.client = client;
	
	err = pac7672_init_reg();
	
	if (err < 0) {
		printk("%s (%d) : pac7672 is not connected.(%d)\n", __func__, __LINE__, err);
		return err;
	}

	pac7672_init_als_data();

	INIT_DELAYED_WORK(&als_data.work, pac7672_als_work_func);

	return 0;	
}


static int pac7672_i2c_remove(struct i2c_client *client)
{
// todo : check diable als

	sysfs_remove_group(&als_data.als_input_dev->dev.kobj,
			   &als_attribute_group);
	
	input_unregister_device(als_data.als_input_dev);

	return 0;
}

static int pac7672_suspend(struct device *dev)
{

	return 0;
}

static int pac7672_resume(struct device *dev)
{

	return 0;
}

static const struct i2c_device_id pac7672_device_id[] = {
	{"pac7672", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, pac7672_device_id);

static const struct dev_pm_ops pac7672_pm_ops = {
	.suspend = pac7672_suspend,
	.resume = pac7672_resume
};

static struct i2c_driver pac7672_i2c_driver = {
	.driver = {
		   .name = "pac7672",
		   .owner = THIS_MODULE,
		   .pm = &pac7672_pm_ops},
	.probe = pac7672_i2c_probe,
	.remove = pac7672_i2c_remove,
	.id_table = pac7672_device_id,
};

static int __init pac7672_init(void)
{
	int ret = 0;

	pac7672_pm(true); 

	ret = i2c_add_driver(&pac7672_i2c_driver);
	printk("%s : init module (%d)\n", __func__, ret);

	return ret;
}

static void __exit pac7672_exit(void)
{
	printk("%s (%d) : exit module\n", __func__, __LINE__);	

	i2c_del_driver(&pac7672_i2c_driver);
}

module_init(pac7672_init);
module_exit(pac7672_exit);
MODULE_AUTHOR("pantech");
MODULE_DESCRIPTION("pac7672 als driver");
MODULE_LICENSE("GPL");

