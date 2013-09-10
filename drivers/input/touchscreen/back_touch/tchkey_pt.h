#ifndef _TCHKEYPT_H_
#define _TCHKEYPT_H_

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

struct tchkeyptdata_t{
	struct input_dev *tchkeypt;
	struct i2c_client	*client;
	struct work_struct work;
	struct mutex		i2clock;
//	struct work_struct work_firmware_start;
#ifdef CHECK_FINISH_FW_TIMER
	struct work_struct work_chg_mode_start;
#endif
	struct completion check_ver_completion;
//	struct completion chg_mode_completion;
	
	unsigned int status_mode;	//interrupt mode & status_change mode
	
	unsigned int in_status_chg_mode;	// write mode & read mode
	unsigned int in_status_check_fw_ver_mode;	// write mode & read mode
	unsigned int in_status_chg_parameter_mode;	// write mode & read mode

	unsigned int setting_mode;	// setting mode 1=ON, 2=OFF
	
	unsigned char status_cmd[2];
	
	unsigned char fw_ver[2];	//
	unsigned int check_ver_flag;
	unsigned int check_i2c_fw;
	unsigned int timer_count;

	u16 change_reg;	//For change parameter value
	unsigned char change_parameter_value[2];	//change parameter value

	unsigned int MSM_STATUS;
	unsigned int PAD_FUNCTION;
	unsigned int PAD_TEMP_FUNCTION;

	//Debug option
	unsigned int dbg_op;
	atomic_t enable;
	spinlock_t lock;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};


void tchkeypt_status_change_mode_front_touch_reset(void);


#endif // _TCHKEYPT_H_
