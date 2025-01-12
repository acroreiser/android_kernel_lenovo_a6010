/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/i2c/mms114.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <linux/of_gpio.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/gpio.h>

//#include <mach/hardware.h>
//#include <mach/gpiomux.h>

#include <linux/io.h>
//#include <mach/gpio.h>
#include <linux/of_gpio.h>
#include "mms200_ts.h"
#include <linux/kthread.h>

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void mms_ts_early_suspend(struct early_suspend *h);
static void mms_ts_late_resume(struct early_suspend *h);
#endif
static int mms_power_on(struct mms_ts_info *info, bool on);
static void mms_ts_enable(struct mms_ts_info *info);

#define DRIVER_NAME "mms200_i2c"
#define MMS_POLLING_MODE 0
//#define TYPE_B_PROTOCOL 
//#define DEBUG_TOUCH_INFO
#define LENOVO_CTP_GESTURE_WAKEUP
#define LENOVO_CTP_TEST_FLUENCY
#define LENOVO_CTP_EAGE_LIMIT

#ifdef LENOVO_CTP_EAGE_LIMIT
static int eage_x = 5;
static int eage_y = 5;	
#endif

#if(MMS_POLLING_MODE)
static struct task_struct *mms_kthread;
#endif
#define GOODIX_VTG_MIN_UV	2600000
#define GOODIX_VTG_MAX_UV	3300000
#define GOODIX_I2C_VTG_MIN_UV	1800000
#define GOODIX_I2C_VTG_MAX_UV	1800000
#define GTP_ADDR_LENGTH 2


/* Minimum delay time is 50us between stop and start signal of i2c */
#define MMS114_I2C_DELAY				20
/* 200ms needs after power on */
#define MMS114_POWERON_DELAY		200
/* Touchscreen absolute values */
#define MMS114_MAX_AREA				0xff
/* mms_ts_enable - wake-up func (VDD on)  */
#ifdef LENOVO_CTP_GESTURE_WAKEUP
static int letter = 0 ;
#endif
static int mms_wakeup_control_flag = 0;  

#ifdef LENOVO_CTP_TEST_FLUENCY
#define LCD_X 720
#define LCD_Y 1280
static struct hrtimer tpd_test_fluency_timer;
#define TIMER_MS_TO_NS(x) (x * 1000 * 1000)
//static void tpd_up(s32 x, s32 y, s32 id);
//static void tpd_down(s32 x, s32 y, s32 size, s32 id);
static int test_x = 0;
static int test_y = 0;
static int test_w = 0;
static int test_id = 0;
static int coordinate_interval = 2;
static int 	delay_time = 10;
//void tpd_delay_handler(void);
//static DECLARE_WORK(tpd_delay_work, tpd_delay_handler);
static struct completion report_point_complete;
#endif
static void mms_ts_reset(struct mms_ts_info *info)
{
	//
	if(info->power_down){
		dev_err(&info->client->dev, "ahe cancel reset while suspend !!!\n");
		return;
	}
	MMS_DEBUG("ahe resume reset device after switch low power mode to normal mode!! ");
	mutex_lock(&info->lock);
	disable_irq_nosync(info->irq);
	info->enabled = false;

	gpio_direction_output(info->pdata->reset_gpio, 0);
	MMS_DEBUG("ahe resume reset GPIO value is %d",gpio_get_value(info->pdata->reset_gpio));
	msleep(30);

	gpio_direction_output(info->pdata->reset_gpio, 1);
	MMS_DEBUG("ahe resume reset GPIO value is %d",gpio_get_value(info->pdata->reset_gpio));
	
	schedule_work(&info->mms_ts_work);
	mutex_unlock(&info->lock);

}

static void mms_ts_enable(struct mms_ts_info *info)
{
	if (info->enabled)
		return;
	
	mutex_lock(&info->lock);
	MMS_DEBUG(" mms_ts_enable  -->enable irq!!");
	//dev_err(&info->client->dev, "ahe mms_ts_enable\n");
/*
	MMS_DEBUG("ahe brfore resume reset GPIO value is %d",gpio_get_value(info->pdata->reset_gpio));
	gpio_direction_output(info->pdata->reset_gpio, 1);
	msleep(50);
	MMS_DEBUG("ahe after resume reset GPIO value is %d",gpio_get_value(info->pdata->reset_gpio));
*/
	
	enable_irq(info->irq);
	info->enabled = true;
	mutex_unlock(&info->lock);
}

/* mms_ts_disable - sleep func (VDD off) */
static void mms_ts_disable(struct mms_ts_info *info)
{
	if (!info->enabled)
		return;

	mutex_lock(&info->lock);
	//disable_irq(info->irq);
	disable_irq_nosync(info->irq);
	//dev_err(&info->client->dev, "ahe mms_ts_disable\n");
/*
//disable
	gpio_direction_output(info->pdata->reset_gpio, 0);
	msleep(50);
	MMS_DEBUG("ahe suspend reset GPIO value is %d",gpio_get_value(info->pdata->reset_gpio));
*/
	info->enabled = false;
	mutex_unlock(&info->lock);
}

/*
 * mms_ts_input_open - Register input device after call this function 
 * this function is wait firmware flash wait
 */ 
static int mms_ts_input_open(struct input_dev *dev)
{
	struct mms_ts_info *info = input_get_drvdata(dev);
	int ret=0;
#if 0
	ret = wait_for_completion_interruptible_timeout(&info->init_done,
			msecs_to_jiffies(90 * MSEC_PER_SEC));

	if (ret > 0) {
		if (info->irq != -1) {
			mms_ts_enable(info);
			ret = 0;
		} else {
			ret = -ENXIO;
		}
	} else {
		dev_err(&dev->dev, "error while waiting for device to init\n");
		ret = -ENXIO;
	}
#else
		if (info->irq != -1) {
			mms_ts_enable(info);
			ret = 0;
		} else {
			ret = -ENXIO;
		}

#endif
	return ret;
}

/*
 * mms_ts_input_close -If device power off state call this function
 */
static void mms_ts_input_close(struct input_dev *dev)
{
	struct mms_ts_info *info = input_get_drvdata(dev);

	mms_ts_disable(info);
}

/* Track state changes of LED */
static void mms_ts_set_enable(struct work_struct *work)
{
	int ret ;
	struct mms_ts_info * info =  container_of(work, struct mms_ts_info, mms_ts_work);
	MMS_DEBUG(" we should wait for  TP IC to boot up !!");
	if(info->power_down)
		return;
	msleep(50);//we should wait for  TP IC to boot up 
	mms_ts_enable(info);
	//reopen gesture after reset
	if(info->gesture_open){
		ret  =i2c_smbus_write_byte_data(info->client, MMS_GESTURE_SUPPORT, 0x01);
		if(ret ==0){ 
			dev_err(&info->client->dev," ahe reopen gesture after reset success!!\n");
			}
		else{
			info->gesture_open =false;
			dev_err(&info->client->dev," ahe reopen gesture after reset fail!!\n");
		}
	}		
}
/*
 * get_fw_version - f/w version read
 */
static int get_fw_version(struct i2c_client *client, u8 *buf)
{
	u8 cmd = MMS_FW_VERSION;
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = &cmd,
			.len = 1,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = buf,
			.len = MAX_SECTION_NUM,
		},
	};
	return (i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg)) != ARRAY_SIZE(msg));
}

#ifdef LENOVO_CTP_GESTURE_WAKEUP

static int get_array_flag(void)
{
    return letter;
}
static ssize_t mms_gesture_flag_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",get_array_flag());
}
static DEVICE_ATTR(lenovo_flag, 0664,
							mms_gesture_flag_show, 
							 NULL);

static ssize_t mms_gesture_wakeup_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",mms_wakeup_control_flag);
}

static ssize_t mms_gesture_wakeup_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;
	int error;
	struct mms_ts_info *info = dev_get_drvdata(dev);
	error = sscanf(buf, "%d", &val);
	if(error != 1)
		return error;
	if(val == 1)
	{
		if(!mms_wakeup_control_flag)
		{
		    mms_wakeup_control_flag = 1;
		    enable_irq_wake(info->irq);
		    dev_err(&info->client->dev, "%s,gesture flag is  = %d",__func__,val);
		    }else
			    return count;
	}else{
		if(mms_wakeup_control_flag)
		{
		    mms_wakeup_control_flag = 0;
		   disable_irq_wake(info->irq);
		   dev_err(&info->client->dev, "%s,gesture flag is  = %d",__func__,val);
		}else
			return count;
	}
	return count;
}

static DEVICE_ATTR(tpd_suspend_status, 0664,
							mms_gesture_wakeup_show, 
							 mms_gesture_wakeup_store);


#endif

void mms_fw_update_controller(const struct firmware *fw, void * context)
{
	struct mms_ts_info *info = context;
	int retires = 3;
	int ret;

	if (!fw) {
		dev_err(&info->client->dev, "ahe failed to read firmware\n");
		//complete_all(&info->init_done);//ahe change
		return;
	}
	do {
		dev_err(&info->client->dev, " ahe firmware update times :%d\n",retires);
		ret = mms_flash_fw(info,fw->data,fw->size,false);
	} while (ret&& --retires);

	if (!retires) {
		dev_err(&info->client->dev, "ahe failed to flash firmware after 3 retires\n");
	}
	release_firmware(fw);
}
static ssize_t mms_force_update(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
    struct i2c_client *client = to_i2c_client(dev);
	struct file *fp; 
	mm_segment_t old_fs;
	size_t fw_size, nread;
	int error = 0;
	int result = 0;
	unsigned int input;

	error = sscanf(buf, "%u", &input);
	if(error != 1 || input != 1){
		dev_err(&info->client->dev,"Input error,val %d is invalid !!! \n",input);
		return -EINVAL;
	}

	if(info->suspended){
		dev_err(&info->client->dev,"%s: Tp is already suspend,please wake it up and try again! \n", __func__);
		return -EINVAL;
	}
 	disable_irq_nosync(client->irq);
	old_fs = get_fs();
	set_fs(KERNEL_DS);  
 
	fp = filp_open(EXTRA_FW_PATH, O_RDONLY, S_IRUSR);
	MMS_DEBUG("ahe mms_force_update by sdcard fw!\n");
	if (IS_ERR(fp)) {
		dev_err(&info->client->dev,
		"%s: failed to open %s.\n", __func__, EXTRA_FW_PATH);
		error = -ENOENT;
		goto open_err;
	}
 	fw_size = fp->f_path.dentry->d_inode->i_size;
	info->fw_updating = true;
	if (0 < fw_size) {
		unsigned char *fw_data;
		fw_data = kzalloc(fw_size, GFP_KERNEL);
		nread = vfs_read(fp, (char __user *)fw_data,fw_size, &fp->f_pos);
		dev_info(&info->client->dev,
		"%s: start, file path %s, size %d Bytes\n", __func__,EXTRA_FW_PATH, (int)(fw_size));
		if (nread != fw_size) {
			    dev_err(&info->client->dev,
			    "%s: failed to read firmware file, nread %d Bytes\n", __func__, (int)nread);
		    error = -EIO;
		} else{
			result=mms_flash_fw(info,fw_data,fw_size, true); // last argument is full update
		}
		kfree(fw_data);
	}
	info->fw_updating = false;
 	filp_close(fp, current->files);
open_err:
	enable_irq(client->irq);
	set_fs(old_fs);
	return error;
}

//static DEVICE_ATTR(fw_update, 0664, mms_force_update, NULL);
static DEVICE_ATTR(fw_update, 0664,NULL,mms_force_update);
static ssize_t mms_show_fw_version(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	int ret = 0;
	u8 ver[3]={0,};
	ret = get_fw_version(info->client,ver);
	if(ret ==1){
		  MMS_DEBUG("ahe read fw fail!! \n");
		  ver[0] = 0;
		  ver[1] = 0;
		  ver[2] = 0;
		}
			
	return snprintf(buf, PAGE_SIZE, "0x%x - 0x%x - 0x%x\n",ver[0],ver[1],ver[2]);

}
static DEVICE_ATTR(fw_version, 0664, mms_show_fw_version, NULL);


#ifdef LENOVO_CTP_TEST_FLUENCY
enum hrtimer_restart tpd_test_fluency_handler(struct hrtimer *timer)
{
    //int i = 0;
/*
    if(point < LCD_X)
//    for(point = 50; i < LCD_X; i+=coordinate_interval)
    {
        #if defined(LENOVO_AREA_TOUCH)
        tpd_down(point, test_y, test_w, test_id);
        #else
        tpd_down(point, test_y, test_w, test_id);
        #endif
        input_sync(tpd->dev);
        point+=coordinate_interval;
        mdelay(delay_time);
        return HRTIMER_RESTART;
    }
//schedule_work(&tpd_delay_work);
    point = 50;
*/
   // hrtimer_start(&ts->timer, ktime_set(0, (GTP_POLL_TIME+6)*1000000), HRTIMER_MODE_REL);

    complete(&report_point_complete);

    return HRTIMER_NORESTART;
}

static ssize_t mms_test_fluency(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;
	int count ;
	struct mms_ts_info *info = dev_get_drvdata(dev);

	test_x =	10 ;// LCD_Y/2 ;
	test_y = LCD_Y/2;
	test_w = 30;
       test_id = 0;
	  count = (LCD_X-20)/coordinate_interval ;
	disable_irq_nosync(info->irq);
	info->enabled = false;
	dev_err(&info->client->dev, "ahe mms_test_fluency start ! \n");
	 for(i = 0; i < (2*count); i++)
        {
        	if(i<=count)
			test_x +=coordinate_interval;
		else 
			test_x -=coordinate_interval;
			
		input_report_key(info->input_dev,BTN_TOUCH, 1);
              input_report_abs(info->input_dev, ABS_MT_TRACKING_ID, test_id);
              input_report_abs(info->input_dev, ABS_MT_TOUCH_MAJOR, test_w);
              input_report_abs(info->input_dev, ABS_MT_PRESSURE, 30);
              input_report_abs(info->input_dev,ABS_MT_POSITION_X, test_x);
              input_report_abs(info->input_dev,ABS_MT_POSITION_Y, test_y);
		input_sync(info->input_dev);
		
            	init_completion(&report_point_complete);
	     	hrtimer_start(&tpd_test_fluency_timer, ktime_set(delay_time / 1000, (delay_time % 1000) * 1000000), HRTIMER_MODE_REL);
            	wait_for_completion(&report_point_complete);

        }
	input_report_key(info->input_dev,BTN_TOUCH, 0);
	input_sync(info->input_dev);
	enable_irq(info->irq);
	info->enabled = true;
       dev_err(&info->client->dev, "ahe mms_test_fluency end  !!! \n");

	return 1;
}

static DEVICE_ATTR(test_fluency, 0664,mms_test_fluency, NULL);

static ssize_t mms_test_fluency_interval_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",coordinate_interval);
}

static ssize_t mms_test_fluency_interval_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;
	int error;
	//struct mms_ts_info *info = dev_get_drvdata(dev);
	error = sscanf(buf, "%d", &val);
	if(error != 1)
		return error;
	coordinate_interval = val;
	return count;
}

static DEVICE_ATTR(test_fluency_intval, 0664,
							mms_test_fluency_interval_show, 
							 mms_test_fluency_interval_store);

static ssize_t mms_test_fluency_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",delay_time);
}

static ssize_t mms_test_fluency_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;
	int error;
	//struct mms_ts_info *info = dev_get_drvdata(dev);
	error = sscanf(buf, "%d", &val);
	if(error != 1)
		return error;
	delay_time= val;
	return count;
}

static DEVICE_ATTR(test_fluency_delay, 0664,
							mms_test_fluency_delay_show, 
							 mms_test_fluency_delay_store);

#endif

static struct attribute *mms_attrs[] = {
	&dev_attr_fw_update.attr,
#ifdef LENOVO_CTP_GESTURE_WAKEUP
	&dev_attr_tpd_suspend_status.attr,
#endif
	&dev_attr_fw_version.attr,
	&dev_attr_lenovo_flag.attr,
#ifdef  LENOVO_CTP_TEST_FLUENCY
	&dev_attr_test_fluency.attr,
	&dev_attr_test_fluency_delay.attr,
	&dev_attr_test_fluency_intval.attr,
#endif
	NULL,
};

static const struct attribute_group mms_attr_group = {
	.attrs = mms_attrs,
};


/* mms_reboot - IC reset */ 
//static void mms_reboot(struct mms_ts_info *info)
void mms_reboot(struct mms_ts_info *info)
{
	//struct i2c_adapter *adapter = to_i2c_adapter(info->client->dev.parent);

	//i2c_smbus_write_byte_data(info->client, MMS_POWER_CONTROL, 1);
	//i2c_lock_adapter(adapter);
	msleep(50);
       MMS_DEBUG("RESET GPIO IS %d",info->pdata->reset_gpio);
	gpio_direction_output(info->pdata->reset_gpio, 0);
	msleep(150);

	gpio_direction_output(info->pdata->reset_gpio, 1);
	msleep(50);
	
	//i2c_unlock_adapter(adapter);

}
/*
 * mms_clear_input_data - all finger point release
 */
void mms_clear_input_data(struct mms_ts_info *info)
{
	int i;

	for (i = 0; i < MAX_FINGER_NUM; i++) {
#ifdef TYPE_B_PROTOCOL
		input_mt_slot(info->input_dev, i);
		input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, false);
#endif
	}
   	 MMS_DEBUG( "ahe mms_clear_input_data\n");
	input_report_key(info->input_dev,BTN_TOUCH, 0);
	input_sync(info->input_dev);

	return;
}
void mms_report_key_event(struct mms_ts_info *info,  u8 *buf)
{
	int key_code;
	int key_state;
	key_code = -1;
	switch (buf[0] & 0xf) {
	case 1:
		key_code = KEY_MENU;
		break;
	case 2:
		key_code = KEY_HOMEPAGE;
		break;
	case 3:
		key_code = KEY_BACK;
		break;
	default:
		dev_err(&info->client->dev, "unknown key type\n");
		break;
	}
	if(key_code>0){
		key_state = (buf[0] & 0x80) ? 1 : 0;
		input_report_key(info->input_dev, key_code, key_state);
		//input_sync(info->input_dev);
	}
}

int mms_report_touch_event(struct mms_ts_info *info, u8 sz, u8 *buf)
{
	int i;
	int id;
	int x;
	int y;
	int touch_major;
	int pressure;
	u8 *tmp;
	 int touch_count=0;
	for (i = 0; i < sz; i += FINGER_EVENT_SZ) {
		tmp = buf + i;
		if(tmp[0] & MMS_TOUCH_KEY_EVENT){
			mms_report_key_event(info, tmp);
			continue;
		}
		else{
			id = (tmp[0] & 0xf) -1;
			x = tmp[2] | ((tmp[1] & 0xf) << 8);
			y = tmp[3] | (((tmp[1] >> 4 ) & 0xf) << 8);
			touch_major = tmp[4];
			pressure = tmp[5];
			pressure = pressure * 2;
			if(pressure>255)
				pressure = 255;	
		//touch_count ++;
#ifdef DEBUG_TOUCH_INFO
			dev_err(&info->client->dev,
			"%s: Finger %d:\n"
			"status = 0x%02x\n"
			"x = %d\n"
			"y = %d\n"
			"touch_major = %d\n"
			"pressure = %d\n",
			__func__, id,
			(tmp[0] & 0x80),
			x, y, touch_major, pressure);
#endif

#ifdef LENOVO_CTP_EAGE_LIMIT
		if(x < eage_x || x > (MMS_MAX_X- eage_x) ||  y <  eage_y  ||y > (MMS_MAX_Y - eage_y))
           	 {	
                		dev_err(&info->client->dev,"ahe filter : Finger=%d x=%d y=%d dropped!\n",id,x,y);	
                		continue;	
           	 }	
#endif
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(info->input_dev, id);
			input_mt_report_slot_state(info->input_dev,MT_TOOL_FINGER, (tmp[0] & 0x80) != 0);
#endif
			if ((tmp[0] & 0x80)) {//have touch
                  	//dev_err(&info->client->dev, "ahe ----> down :ID= %d ,i=%d,l_x:%d\n",id,i,x);
                	  input_report_key(info->input_dev,BTN_TOUCH, 1);
                  	//input_report_key(info->input_dev,BTN_TOOL_FINGER, 1);
                  	input_report_abs(info->input_dev, ABS_MT_TRACKING_ID, id);
                 	 input_report_abs(info->input_dev, ABS_MT_TOUCH_MAJOR, touch_major);
                  	input_report_abs(info->input_dev, ABS_MT_PRESSURE, pressure);
                  	input_report_abs(info->input_dev,ABS_MT_POSITION_X, x);
                  	input_report_abs(info->input_dev,ABS_MT_POSITION_Y, y);
#ifndef TYPE_B_PROTOCOL
                  	input_mt_sync(info->input_dev);
#endif
		//printk("ahe loop_num:%d , ID:%d ---->(%d,%d )\n",i,id,x,y);
		//dev_err(&info->client->dev,"ahe  ID:%d ---->(%d,%d )\n",id,x,y);
			//dev_err(&info->client->dev,"ahe  x =%d      y = %d ....n",x,y);
			touch_count ++;
			}	
		}
	}
	if (touch_count == 0) {
		input_report_key(info->input_dev,BTN_TOUCH, 0);
		input_report_key(info->input_dev,BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
		input_mt_sync(info->input_dev);
#endif
	}
	input_sync(info->input_dev);
	return touch_count;
}
#ifdef LENOVO_CTP_GESTURE_WAKEUP
int mms_report_gesture_event(struct mms_ts_info *info,  u8 *buf)
{
	u8 gesture = 0;

       gesture = buf[0]&0x1f;
	letter = 0x00;
	switch(gesture){
		case 1 :
		{//double_click snap shot 
			letter = 0x24;
			MMS_DEBUG("ahe ---->gesture double_click  wake up\n");
			break;
		}
		case 3 :
		{//slide wake up
			letter = 0x20;
			MMS_DEBUG("ahe ---->gesture slide wake up\n");
			break;
		}
		case 5:
		{//  home double touch
			letter = 0x50;
			MMS_DEBUG("ahe ---->double_click  snap shot \n");
			break;
		}
		case 6:
		{//Buttom to top
			//letter = 0x20;
			MMS_DEBUG("ahe ---->Buttom to top \n");
			break;
		}
		case 7:
		{//"V"
			letter = 0x34;
			MMS_DEBUG("ahe ---->Draw   V \n");
			break;
		}
		case 8:
		{//"O" 
			letter = 0x33; //circle
			MMS_DEBUG("ahe ---->Draw  O \n");
			break;
		}
		default:
			MMS_DEBUG("ahe no such gesture  \n");
			break;
	}
	if(letter!=0x00){
		input_report_key(info->input_dev, KEY_SLIDE, 1);
    		input_sync(info->input_dev);
    		input_report_key(info->input_dev, KEY_SLIDE, 0);
     		input_sync(info->input_dev);
	}
	dev_err(&info->client->dev,"ahe ---->letter :%02x \n",letter);
	return 0;
}

#endif
/* mms_report_input_data - The position of a touch send to platfrom  */
void mms_report_input_data(struct mms_ts_info *info, u8 sz, u8 *buf)
{
	struct i2c_client *client = info->client;
	//dev_err(&client->dev," ahe mms_report_input_data \n");
	if (buf[0] == MMS_NOTIFY_EVENT) {
		dev_info(&client->dev, "TSP mode changed (%d)\n", buf[1]);
		goto out;
	} else if (buf[0] == MMS_ERROR_EVENT) {
		dev_info(&client->dev, "Error detected, restarting TSP\n");
		mms_clear_input_data(info);
		mms_reboot(info);
/*
		esd_cnt++;
		if (esd_cnt>= ESD_DETECT_COUNT)
		{
			i2c_smbus_write_byte_data(info->client, MMS_MODE_CONTROL, 0x04);
			esd_cnt =0;
		}
*/
		goto out;
	}
	mms_report_touch_event( info, sz, buf);

out:
	return ;
}


static int __mms_read_reg(struct mms_ts_info *data, unsigned int reg,
			     unsigned int len, u8 *val)
{
	struct i2c_client *client = data->client;
	struct i2c_msg xfer[2];
	u8 buf = reg & 0xff;
	int error;
	int retry;
	int ret;
	mutex_lock(&(data->mms_i2c_mutex));
	/* Write register: use repeated start */
	xfer[0].addr = client->addr;
	xfer[0].flags = I2C_M_TEN | I2C_M_NOSTART;
	xfer[0].len = 1;
	xfer[0].buf = &buf;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = len;
	xfer[1].buf = val;
	for(retry=0;retry<MMS_I2C_RETRY_TIMES;retry++){
		error = i2c_transfer(client->adapter, xfer, 2);
		if (error == 2) {
			ret =len;
			break;
		}
		if(data->power_down){
			ret =error;
			dev_err(&data->client->dev, "ahe cancel i2c transfer  while suspend !!!\n");
			break;
		}
		dev_err(&client->dev,
			"%s: ahe i2c transfer failed,retry: (%d)\n", __func__, retry+1);
		mdelay(MMS114_I2C_DELAY);
	}
	if(retry==MMS_I2C_RETRY_TIMES)
		ret =error;
	mutex_unlock(&(data->mms_i2c_mutex));
	return ret ;
}

 int mms_read_reg(struct mms_ts_info *data, unsigned int reg)
{
	u8 val;
	int error;

//	if (reg == MMS114_MODE_CONTROL)
//		return data->cache_mode_control;

	error = __mms_read_reg(data, reg, 1, &val);
	return error < 0 ? error : val;
}
 
static irqreturn_t mms_ts_interrupt(int irq, void *dev_id)
{
	struct mms_ts_info *data = dev_id;
	struct input_dev *input_dev = data->input_dev;
	int packet_size = 0;
	int touch_size;
	int error;
	u8 buf[MAX_FINGER_NUM * FINGER_EVENT_SZ] = { 0, };
	u8 gesture = 0;
	struct i2c_client *client = data->client;
	//mutex_lock(&input_dev->mutex);
	if (!input_dev->users) {
		dev_err(&client->dev,"input_dev->users\n");
		//mutex_unlock(&input_dev->mutex);
		goto out;
	}
	//mutex_unlock(&input_dev->mutex);
#ifdef LENOVO_CTP_GESTURE_WAKEUP
	if(data->suspended ==true){
		MMS_DEBUG("ahe ----> mms_ts_interrupt \n");
		gesture = mms_read_reg(data, MMS_INPUT_EVENT);
		if (gesture <= 0 ){
			dev_err(&client->dev,"ahe mms_ts_interrupt  gesture %d\n",gesture);
			goto reset;
			}
	 	if((gesture&MMS_TOUCH_GESTURE_EVENT)==MMS_TOUCH_GESTURE_EVENT){
			MMS_DEBUG("ahe ----> gesture buf[0] : %02x\n",gesture);
		 	mms_report_gesture_event(data, &gesture);
			return IRQ_HANDLED;
	 		}
	}
#endif	
	packet_size = mms_read_reg(data, MMS_EVENT_PKT_SZ);
	//printk("ahe packet_size %d \n",i2c_smbus_read_byte_data(client, MMS_EVENT_PKT_SZ));
	if (packet_size < 0 ){
		dev_err(&client->dev,"ahe mms_ts_interrupt -packet_size %d\n",packet_size);
		goto reset;
		}
	else if(packet_size ==0){
		dev_err(&client->dev,"ahe mms_ts_interrupt -packet_size %d\n",packet_size);
		goto out;
		}
	
	if(packet_size > (MAX_FINGER_NUM * FINGER_EVENT_SZ) ){
		dev_err(&client->dev,
			"ahe packet_size error packet_size: %d bytes \n",
			packet_size);
		packet_size = MAX_FINGER_NUM * FINGER_EVENT_SZ;
		goto out;
		}
		
	touch_size = packet_size / FINGER_EVENT_SZ;
	//dev_err(&client->dev,	"ahe packet_size to read %d bytes \n",packet_size);
	error = __mms_read_reg(data, MMS_INPUT_EVENT, packet_size,buf);
	if (error < 0) {
		dev_err(&client->dev,
			"failed to read %d bytes of touch data (%d)\n",
			packet_size, error);
		goto reset;
	} else {
		mms_report_input_data(data, packet_size, buf);
	}
	return IRQ_HANDLED;
reset:
	mms_ts_reset(data);
out:
	return IRQ_HANDLED;
}

#if 0
/*
 * mms_ts_config - f/w check download & irq thread register
 */
static int mms_ts_config(struct mms_ts_info
 *info)
{
	struct i2c_client *client = info->client;
	int ret;
	ret = request_threaded_irq(client->irq, NULL, mms_ts_interrupt,
				IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				"mms_ts", info);
      //s7 liuyh has modify as IRQF_TRIGGER_FALLING 
	if (ret) {
		dev_err(&client->dev, "failed to register irq\n");
		goto out;
	}
	disable_irq(client->irq);
	info->irq = client->irq;
	barrier();

  	info->tx_num = i2c_smbus_read_byte_data(client, MMS_TX_NUM);
	info->rx_num = i2c_smbus_read_byte_data(client, MMS_RX_NUM);
	info->key_num = i2c_smbus_read_byte_data(client, MMS_KEY_NUM);

	dev_info(&client->dev, "Melfas touch controller initialized\n");
	//mms_reboot(info);
	//complete_all(&info->init_done);

out:
	return ret;
}
#endif
//melfas test mode
//ahe change start 
#if(MMS_POLLING_MODE)
static int mms_ts_read(void *dev_id)
{
	struct mms_ts_info *info = dev_id;
	struct i2c_client *client = info->client;
	u8 buf[MAX_FINGER_NUM * FINGER_EVENT_SZ] = { 0, };
	u8 len= 0;
	int ret;
	int sz;
	u8 reg = MMS_INPUT_EVENT;
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = &reg,
			.len = 1,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = buf,
		},
	};
	
	if(!get_package_len(client, &len))
		{
			sz = len & 0x0f;
	 		printk("ahe the length :  %d\n" ,sz );
			msg[1].len = sz;
		}else {
		//mms_report_input_data(info, sz, buf);
		printk("ahe read  length : fail \n"  );
		}
		
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev,
			"failed to read %d bytes of touch data (%d)\n",
			sz, ret);
	} else {
		//mms_report_input_data(info, sz, buf);
		printk("[ahe] report data %s %d \n",__func__,__LINE__);
		printk("[ahe] report data  %d \n",buf[0]);
		printk("[ahe] report data  %d \n",buf[1]);
		printk("[ahe] report data  %d \n",buf[2]);
	}

	return ret;
}

static int  mms_read_event(void *arg)
{

	while (1) {
		mms_ts_read(arg);
		
		msleep(50);		
		printk("ahe polling read data !!\n");
		}
	return 1;
}
#endif
static int mms_parse_dt(struct device *dev,
			struct mms_ts_platform_data *pdata)
{
	
	struct device_node *np = dev->of_node;
	/* reset, irq gpio info */
	pdata->reset_gpio = of_get_named_gpio_flags(np, "mms,reset-gpio",
				0, &pdata->reset_gpio_flags);
	MMS_DEBUG("ahe RESET GPIO IS %d\n",pdata->reset_gpio);
	if (pdata->reset_gpio < 0)
		return pdata->reset_gpio;

	pdata->irq_gpio = of_get_named_gpio_flags(np, "mms,irq-gpio",
				0, &pdata->irq_gpio_flags);
	MMS_DEBUG("ahe irq GPIO IS %d\n",pdata->irq_gpio);
	if (pdata->irq_gpio < 0)
		return pdata->irq_gpio;
	return 0;
}
static int mms_power_on(struct mms_ts_info *info, bool on)
{
	int ret;

	if (!on)
		goto power_off;

	if (!IS_ERR(info->vdd)) {
		ret = regulator_enable(info->vdd);
		if (ret) {
			dev_err(&info->client->dev,
				"Regulator vdd enable failed ret=%d\n", ret);
			goto err_enable_vdd;
		}
	}
	if (!IS_ERR(info->vcc_i2c)) {
		ret = regulator_enable(info->vcc_i2c);
		if (ret) {
			dev_err(&info->client->dev,
				"Regulator vcc_i2c enable failed ret=%d\n",
				ret);
			regulator_disable(info->vdd);
			goto err_enable_vcc_i2c;
			}
	}

	return 0;

err_enable_vcc_i2c:
	if (!IS_ERR(info->vdd))
		ret = regulator_disable(info->vdd);

err_enable_vdd:

	return ret;

power_off:
	if (!IS_ERR(info->vcc_i2c)) {
		ret = regulator_disable(info->vcc_i2c);
		if (ret)
			dev_err(&info->client->dev,
				"Regulator vcc_i2c disable failed ret=%d\n",
				ret);
	}

	if (!IS_ERR(info->vdd)) {
		ret = regulator_disable(info->vdd);
		if (ret)
			dev_err(&info->client->dev,
				"Regulator vdd disable failed ret=%d\n", ret);
	}

	return 0;
}
static int mms_power_init(struct mms_ts_info *info, bool on)
{
	int ret;

	if (!on)
		goto pwr_deinit;
	info->vdd = regulator_get(&info->client->dev, "vdd");
	if (IS_ERR(info->vdd)) {
		dev_info(&info->client->dev,
			"Regulator get failed vdd\n");
		return -1;
	} else if (regulator_count_voltages(info->vdd) > 0) {
		ret = regulator_set_voltage(info->vdd, GOODIX_VTG_MIN_UV,
					   GOODIX_VTG_MAX_UV);
		if (ret) {
			dev_err(&info->client->dev,
				"Regulator set_vtg failed vdd ret=%d\n", ret);
			goto err_vdd_put;
		}
	}

	info->vcc_i2c = regulator_get(&info->client->dev, "vcc_i2c");
	if (IS_ERR(info->vcc_i2c)) {
		dev_info(&info->client->dev,
			"Regulator get failed vcc_i2c\n");
		return -1;
	} else if (regulator_count_voltages(info->vcc_i2c) > 0) {
		ret = regulator_set_voltage(info->vcc_i2c, GOODIX_I2C_VTG_MIN_UV,
					   GOODIX_I2C_VTG_MAX_UV);
		if (ret) {
			dev_err(&info->client->dev,
			"Regulator set_vtg failed vcc_i2c ret=%d\n", ret);
			goto err_vcc_i2c_put;
		}
	}

	return 0;

err_vcc_i2c_put:
	regulator_put(info->vcc_i2c);

	if ((!IS_ERR(info->vdd)) && (regulator_count_voltages(info->vdd) > 0))
		regulator_set_voltage(info->vdd, 0, GOODIX_VTG_MAX_UV);
err_vdd_put:
	regulator_put(info->vdd);
	return ret;

pwr_deinit:

	if ((!IS_ERR(info->vdd)) &&
		(regulator_count_voltages(info->vdd) > 0))
		regulator_set_voltage(info->vdd, 0, GOODIX_VTG_MAX_UV);

	regulator_put(info->vdd);

	if ((!IS_ERR(info->vcc_i2c)) &&
		(regulator_count_voltages(info->vcc_i2c) > 0))
		regulator_set_voltage(info->vcc_i2c, 0, GOODIX_I2C_VTG_MAX_UV);

	regulator_put(info->vcc_i2c);
	return 0;
}
static int mms_pinctrl_init(struct mms_ts_info *info)
{
	int retval;

	/* Get pinctrl if target uses pinctrl */
	info->ts_pinctrl = devm_pinctrl_get(&(info->client->dev));
	if (IS_ERR_OR_NULL(info->ts_pinctrl)) {
		dev_dbg(&info->client->dev,
			"Target does not use pinctrl\n");
		retval = PTR_ERR(info->ts_pinctrl);
		info->ts_pinctrl = NULL;
		return retval;
	}

	info->gpio_state_active
		= pinctrl_lookup_state(info->ts_pinctrl, "pmx_ts_active");
	if (IS_ERR_OR_NULL(info->gpio_state_active)) {
		dev_dbg(&info->client->dev,
			"Can not get ts default pinstate\n");
		retval = PTR_ERR(info->gpio_state_active);
		info->ts_pinctrl = NULL;
		return retval;
	}

	info->gpio_state_suspend
		= pinctrl_lookup_state(info->ts_pinctrl, "pmx_ts_suspend");
	if (IS_ERR_OR_NULL(info->gpio_state_suspend)) {
		dev_dbg(&info->client->dev,
			"Can not get ts sleep pinstate\n");
		retval = PTR_ERR(info->gpio_state_suspend);
		info->ts_pinctrl = NULL;
		return retval;
	}

	return 0;
}

static int mms_pinctrl_select(struct mms_ts_info *info,
						bool on)
{
	struct pinctrl_state *pins_state;
	int ret;

	pins_state = on ? info->gpio_state_active
		: info->gpio_state_suspend;
	if (!IS_ERR_OR_NULL(pins_state)) {
		ret = pinctrl_select_state(info->ts_pinctrl, pins_state);
		if (ret) {
			dev_err(&info->client->dev,
				"ahe can not set %s pins\n",
				on ? "pmx_ts_active" : "pmx_ts_suspend");
			return ret;
		}
	} else
		dev_err(&info->client->dev,
			"ahe not a valid '%s' pinstate\n",
				on ? "pmx_ts_active" : "pmx_ts_suspend");

	return 0;
}


static s8 mms_request_io_port(struct mms_ts_info *info , bool on)
{
	//int gpio, func, pull, dir, drvstr, output_val;
	//unsigned cfg;

	struct i2c_client *client = info->client;
	struct mms_ts_platform_data *pdata = info->pdata;
	int ret;
	//u8 config; 
	if(on){
	if (gpio_is_valid(pdata->irq_gpio)) {
		ret = gpio_request(pdata->irq_gpio, "mms_ts_irq_gpio");
		if (ret) {
			dev_err(&client->dev, "ahe irq gpio request failed\n");
			goto pwr_off;
		}

		ret = gpio_direction_input(pdata->irq_gpio);
		if (ret) {
			dev_err(&client->dev,
					"set_direction for irq gpio failed\n");
			goto free_irq_gpio;
		}
		client->irq = gpio_to_irq(pdata->irq_gpio);
		info->irq = client->irq;
       		//MMS_DEBUG("ahe IRQ GPIO IS %d,value is %d\n",pdata->irq_gpio,gpio_get_value(pdata->irq_gpio));
	} else {
		dev_err(&client->dev, "irq gpio is invalid!\n");
		ret = -EINVAL;
		goto pwr_off;
	}
	if (gpio_is_valid(pdata->reset_gpio)) {
		ret = gpio_request(pdata->reset_gpio, "mms_ts_reset_gpio");
		if (ret) {
			dev_err(&client->dev, "reset gpio request failed\n");
			goto free_irq_gpio;
		}
		ret = gpio_direction_output(pdata->reset_gpio, 1);
		if (ret) {
			dev_err(&client->dev,
					"set_direction for reset gpio failed\n");
			goto free_reset_gpio;
		}
//ahe remove set 1,already set before
		 MMS_DEBUG("ahe 1 reset GPIO: %d value is %d",pdata->reset_gpio,gpio_get_value(pdata->reset_gpio));
		//gpio_set_value(pdata->reset_gpio, 1);
		//msleep(80);
        	//MMS_DEBUG("ahe 2 reset GPIO: %d value is %d\n",pdata->reset_gpio,gpio_get_value(pdata->reset_gpio));
		
	} else {
		dev_err(&client->dev, "reset gpio is invalid!\n");
		ret = -EINVAL;
		goto free_irq_gpio;
	}

	return ret;
		}else{
		if (info->disable_gpios) {
			if (gpio_is_valid(info->pdata->irq_gpio))
				gpio_free(info->pdata->irq_gpio);
			if (gpio_is_valid(info->pdata->reset_gpio)) {
 			/*
 			 * This is intended to save leakage current
 	 		* only. Even if the call(gpio_direction_input)
 	 		* fails, only leakage current will be more but
 	 		* functionality will not be affected.
 	 		*/
			ret = gpio_direction_input(info->pdata->reset_gpio);
			if (ret) {
				dev_err(&client->dev, 
					"ahe unable to set direction for gpio "
					"[%d]\n", info->pdata->reset_gpio);
			}
			gpio_free(info->pdata->reset_gpio);
		}
	}
	return 0;
}

free_reset_gpio:
	if (gpio_is_valid(pdata->reset_gpio))
		gpio_free(pdata->reset_gpio);
free_irq_gpio:
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->irq_gpio);
pwr_off:
	return ret;

}
//ahe change end 
#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
static int mms_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mms_ts_info *info = i2c_get_clientdata(client);
	int ret =0;
	if(info->suspended == true){
		dev_err(&client->dev,"ahe already suspend\n");
		return ret ;
		}
	if(info->fw_updating){
		dev_err(&client->dev, "ahe fw_updating right now!!!\n");
		info->suspended = false;
		return ret ;
	}
	mutex_lock(&info->input_dev->mutex);
	dev_err(&client->dev," ahe mms_ts_suspend $$$$$$$$$$$$$$$$$$\n");
#ifdef LENOVO_CTP_GESTURE_WAKEUP
	if(mms_wakeup_control_flag==1){
		ret  =i2c_smbus_write_byte_data(info->client, MMS_GESTURE_SUPPORT, 0x01);
		if(ret ==0){ 
			dev_err(&client->dev," ahe gesture open OK ret : %d!!\n",ret);
			info->gesture_open = true;
			}else{
			dev_err(&client->dev," ahe gesture open fail!!\n");
			}	
	}
#endif
	if (info->input_dev->users) {
		mms_clear_input_data(info);
		if(mms_wakeup_control_flag==0){
			mms_ts_disable(info);
			ret = mms_power_on(info, false);
			if (ret ) {
				dev_err(&client->dev, "ahe failed to do suspend power off\n");
				mutex_unlock(&info->input_dev->mutex);
				goto err_lpm_regulator;
				}
			if (info->disable_gpios) {
				if (info->ts_pinctrl) {
					ret = mms_pinctrl_select(info,false);
					if (ret < 0)
						dev_err(&client->dev, "ahe Cannot get idle pinctrl state\n");
					}
		   		 ret = mms_request_io_port(info, false);
		    		if (ret < 0) {
			    		dev_err(&client->dev, "ahe failed to put gpios in suspend state\n");
					mutex_unlock(&info->input_dev->mutex);
			    		goto err_gpio_configure;
		    			}
				}
			info->power_down = true;
			}
	}
	mutex_unlock(&info->input_dev->mutex);
	dev_err(&client->dev," ahe mms_ts_suspend ~~~~~\n");
	info->suspended = true;
	return 0 ;
err_gpio_configure:
	if (info->ts_pinctrl) {
		ret = mms_pinctrl_select(info, true);
		if (ret < 0)
			dev_err(&client->dev, "ahe Cannot get default pinctrl state\n");
	}
	mms_power_on(info, true);

err_lpm_regulator:
	if (1) {//info->sensor_sleep
		//synaptics_rmi4_sensor_wake(info);
		//synaptics_rmi4_irq_enable(info, true);
		//info->touch_stopped = false;
		mms_ts_enable(info);
		//enable_irq_wake(info->irq);
	}
	return ret;

}

static int mms_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mms_ts_info *info = i2c_get_clientdata(client);
	int ret = 0 ;
	if(info->suspended == false){
		dev_err(&client->dev,"ahe already resume");
		return ret;
		}
	mutex_lock(&info->input_dev->mutex);
#ifdef LENOVO_CTP_GESTURE_WAKEUP
	if(mms_wakeup_control_flag==1){
		if(info->gesture_open){
			//ret  =i2c_smbus_write_byte_data(info->client, MMS_GESTURE_SUPPORT, 0x00);
			info->gesture_open=false;
			mms_ts_reset( info);//use reset to close gesture
			dev_err(&client->dev," ahe  close gesture  !!!\n");
		}
	}
#endif
	MMS_DEBUG(" ahe resume 11\n");
	if (info->input_dev->users){
		if(info->power_down ==true){
			ret = mms_power_on(info, true);
			if (ret) {
				dev_err(&client->dev, "ahe failed to do resume power on \n");
				mutex_unlock(&info->input_dev->mutex);
				return ret;			
			}
			MMS_DEBUG(" ahe resume 22\n");
			if (info->disable_gpios) {
				if (info->ts_pinctrl) {
					ret = mms_pinctrl_select(info,true);
					if (ret < 0)
					dev_err(&client->dev, "ahe resume Cannot get idle pinctrl state\n");
					}
				MMS_DEBUG(" ahe resume 33\n");
   				 ret = mms_request_io_port(info, true);
    				if (ret < 0) {
	    				dev_err(&client->dev, "ahe failed to put gpios in resume state\n");
					mutex_unlock(&info->input_dev->mutex);
	    				goto err_gpio_configure;
    					}
	    		}
		MMS_DEBUG(" ahe resume 44\n");
		info->power_down =false;
		}
		//msleep(50);
		schedule_work(&info->mms_ts_work);
		//mms_ts_enable(info);
	}
	dev_err(&client->dev," ahe resume done\n");
	info->suspended = false ;
	mutex_unlock(&info->input_dev->mutex);
	return ret;
err_gpio_configure:
	if (info->ts_pinctrl) {
		ret = mms_pinctrl_select(info, false);
		if (ret < 0)
			dev_err(&client->dev,"ahe Cannot get idle pinctrl state\n");
	}
	mms_power_on(info, false);
	return ret;
}
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
static void mms_ts_early_suspend(struct early_suspend *h)
{
	
	struct mms_ts_info *info;
	info = container_of(h, struct mms_ts_info, early_suspend);
	dev_err(&info->client->dev," ahe mms_ts_early_suspend!\n");
	mms_ts_suspend(&info->client->dev);
}

static void mms_ts_late_resume(struct early_suspend *h)
{
	struct mms_ts_info *info;
	info = container_of(h, struct mms_ts_info, early_suspend);
	dev_err(&info->client->dev," ahe mms_ts_late_resume!\n");
	mms_ts_resume(&info->client->dev);
}
#endif

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct mms_ts_info *ts =
		container_of(self, struct mms_ts_info, fb_notif);
	
	
	if (evdata && evdata->data && event == FB_EVENT_BLANK &&
			ts && ts->client) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK){
			dev_err(&ts->client->dev,"ahe fb_notifier_callback blank--->resume\n");
			mms_ts_resume(&ts->client->dev);			
			}

		else if (*blank == FB_BLANK_POWERDOWN){
			dev_err(&ts->client->dev,"ahe fb_notifier_callback blank --->suspend\n");
			mms_ts_suspend(&ts->client->dev);
			}
	}
	return 0;
}
#endif

#if 0
#ifdef CONFIG_FB
static void configure_sleep(struct mms_ts_info *data)
{
	int retval = 0;

	data->fb_notif.notifier_call = fb_notifier_callback;

	retval = fb_register_client(&data->fb_notif);
	if (retval)
		dev_err(&data->client->dev,
			"Unable to register fb_notifier: %d\n", retval);
	return;
}
#elif defined CONFIG_HAS_EARLYSUSPEND
static void configure_sleep(struct mms_ts_info *data)
{
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = mms_ts_early_suspend;
	data->early_suspend.resume =  mms_ts_late_resume;
	register_early_suspend(&data->early_suspend);

	return;
}
#else
static void configure_sleep(struct mms_ts_info *data)
{
	return;
}
#endif
#endif

static int mms_ts_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct mms_ts_info *info;
	struct mms_ts_platform_data *pdata;
	struct input_dev *input_dev;
	int ret = 0;
	int fw_update_fig = 0;
	u8 ver[3]={0,0,0};

	const char *fw_name = FW_NAME;
	if (client->dev.of_node) {
	pdata = devm_kzalloc(&client->dev,
		sizeof(struct mms_ts_platform_data), GFP_KERNEL);
	if (!pdata) {
		dev_err(&client->dev,
			"GTP Failed to allocate memory for pdata\n");
		return -ENOMEM;
	}

	ret = mms_parse_dt(&client->dev, pdata);
	if (ret)
		return ret;
	pdata->max_x = MMS_MAX_X;
	pdata->max_y = MMS_MAX_Y;

	} else {
		pdata = client->dev.platform_data;
	}
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -EIO;
	MMS_DEBUG("%s %d",__func__,__LINE__);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
    if (info == NULL)
    {
        MMS_DEBUG("Alloc GFP_KERNEL memory failed.\n");
        return -ENOMEM;
    }
   	 memset(info, 0, sizeof(*info));
	info->client = client;
	info->pdata = pdata;
	info->irq = -1;
	MMS_DEBUG("%s %d",__func__,__LINE__);
	ret = mms_power_init(info,true);
	if (ret) {
		dev_err(&client->dev, "MMS power init failed\n");
		goto exit_free_client_data;
	}
	ret = mms_power_on(info,true);
	if (ret) {
		dev_err(&client->dev, "MMS power on failed\n");
		goto exit_deinit_power;
	}
		
	ret = mms_pinctrl_init(info);
	if (!ret && info->ts_pinctrl) {
		ret = mms_pinctrl_select(info, true);
		if (ret < 0)
			goto exit_power_off;
	}
    ret = mms_request_io_port(info,true);
    if (ret < 0)
    {
        MMS_DEBUG("MIT request IO port failed\n");
        kfree(info);
        goto exit_power_off;
    }
	info->power_down = false;
//	init_completion(&info->init_done);
	MMS_DEBUG("%s %d",__func__,__LINE__);
	//mms_ts_config(info);
	msleep(80);//we need a boot delay here
	ret = get_fw_version(info->client,ver);
	if(ret==0)
		MMS_DEBUG("ahe current FW: 0x%x,0x%x,0x%x ",ver[0],ver[1],ver[2]);
	input_dev = input_allocate_device();

	if (!info || !input_dev) {
		dev_err(&client->dev, "Failed to allocated memory\n");
		return -ENOMEM;
	}
/*
	info->client = client;
	info->input_dev = input_dev;
	info->pdata = client->dev.platform_data;
	init_completion(&info->init_done);
	info->irq = -1;
*/

	mutex_init(&info->lock);
	mutex_init(&info->mms_i2c_mutex);
	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
			mms_ts_interrupt, IRQF_TRIGGER_LOW | IRQF_ONESHOT,
			dev_name(&client->dev), info);
	if (ret) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		return ret;
	}
	disable_irq_nosync(client->irq);
	info->enabled = false;

	snprintf(info->phys, sizeof(info->phys),
		"%s/input0", dev_name(&client->dev));

	input_dev->name = DRIVER_NAME;
	input_dev->phys = info->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->open = mms_ts_input_open;
	input_dev->close = mms_ts_input_close;

	input_set_drvdata(input_dev, info);

	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_ABS, input_dev->evbit);
	set_bit(BTN_TOUCH,input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, input_dev->keybit);

#ifdef INPUT_PROP_DIRECT
	set_bit(INPUT_PROP_DIRECT,input_dev->propbit);
#endif

	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, MAX_WIDTH, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 255, 0, 0); //ahe 
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 1, 0, 0); //ahe MAX_PRESSURE
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, info->pdata->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, info->pdata->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 10, 0, 0);

  	input_set_capability(input_dev, EV_KEY, KEY_MENU);  
  	input_set_capability(input_dev, EV_KEY, KEY_HOMEPAGE);  
  	input_set_capability(input_dev, EV_KEY, KEY_BACK);  
	input_set_capability(input_dev, EV_KEY, KEY_POWER);  //ahe 

/*Begin lenovo-sw, lixh10, 20140730, add for slide touch */
	set_bit(KEY_POWER,	input_dev->keybit);
	set_bit(KEY_SLIDE,input_dev->keybit);
	input_set_capability(input_dev, EV_KEY, KEY_POWER);
   	 input_set_capability(input_dev, EV_KEY, KEY_SLIDE);
/*End  lenovo-sw, lixh10, 20140730, add for slide touch */	
  	//input_set_drvdata(input_dev, info);
 	 i2c_set_clientdata(client, info);

#ifdef TYPE_B_PROTOCOL
	input_mt_init_slots(input_dev, MAX_FINGER_NUM,0);
#endif

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&client->dev, "failed to register input dev\n");
		return -EIO;
	}
	info->input_dev = input_dev;

	//configure_sleep(info);
	//init_completion(&info->init_done);
	//mutex_init(&info->lock);
	INIT_WORK(&info->mms_ts_work, mms_ts_set_enable);
//update
	info->fw_name = kstrdup(fw_name, GFP_KERNEL);
#ifdef MMS_FW_UPDATE
	MMS_DEBUG("ahe fw will be updated is : %s ",fw_name);
	if(ver[2]==0xff)
		fw_update_fig =1;
	else{
		if(ver[2]<FW_VERSION_TO_UPDATE)
			fw_update_fig =1;
		else
			fw_update_fig =0;
		}
	if(fw_update_fig){
		MMS_DEBUG("start fw  updating .... ");
		ret = request_firmware_nowait(THIS_MODULE, true, fw_name, &info->client->dev,
				GFP_KERNEL, info,mms_fw_update_controller);
		if (ret) {
			dev_err(&client->dev, "ahe failed to schedule firmware update\n");
			return -EIO;
		}
		kfree(info->fw_name);
		fw_update_fig = 0;
	}
#endif
#ifdef LENOVO_CTP_TEST_FLUENCY
        hrtimer_init(&tpd_test_fluency_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        tpd_test_fluency_timer.function = tpd_test_fluency_handler;  
#endif
#if(MMS_POLLING_MODE)	
	mms_kthread = kthread_create(mms_read_event, info, DRIVER_NAME);
	if (IS_ERR(mms_kthread))
		return -EIO;
	wake_up_process(mms_kthread);
#endif

#if defined(CONFIG_FB)
	 info->fb_notif.notifier_call = fb_notifier_callback;
	 ret = fb_register_client(&info->fb_notif);
	 if (ret)
		dev_err(&info->client->dev,
			"Unable to register fb_notifier: %d\n",
			ret);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	info->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	info->early_suspend.suspend = mms_ts_early_suspend;
	info->early_suspend.resume = mms_ts_late_resume;
	register_early_suspend(&info->early_suspend);
#endif


#ifdef __MMS_LOG_MODE__
	if(mms_ts_log(info)){
		dev_err(&client->dev, "failed to create Log mode.\n");
		return -EAGAIN;
	}

	info->class = class_create(THIS_MODULE, "mms_ts");
	device_create(info->class, NULL, info->mms_dev, NULL, "mms_ts");
#endif

#ifdef __MMS_TEST_MODE__
	if (mms_sysfs_test_mode(info)){
		dev_err(&client->dev, "failed to create sysfs test mode\n");
		return -EAGAIN;
	}

#endif

	if (sysfs_create_group(&client->dev.kobj, &mms_attr_group)) {
		dev_err(&client->dev, "failed to create sysfs group\n");
		return -EAGAIN;
	}

	if (sysfs_create_link(NULL, &client->dev.kobj, "board_properties")) {//mms_ts
		dev_err(&client->dev, "failed to create sysfs symlink\n");
		return -EAGAIN;
	}

	dev_notice(&client->dev, "mms dev initialized\n");
	mms_ts_enable(info);
	info->suspended = false;
	//info->i2c_pull_up = true;
	info->disable_gpios = true;
	info->fw_updating = false;
	info->gesture_open = false;
	info->test_mode = false;

	MMS_DEBUG("Driver  probe over~~~~~");

	return 0;
exit_power_off:
	mms_power_on(info,false);
exit_deinit_power:
	mms_power_init(info,false);
exit_free_client_data:
	return 0;
}


//static SIMPLE_DEV_PM_OPS(mms114_pm_ops, mms114_suspend, mms114_resume);
#if 0 //defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
static const struct dev_pm_ops mms_ts_pm_ops = {
	.suspend	= mms_ts_suspend,
	.resume	= mms_ts_resume,
};
#endif

static const struct i2c_device_id mms114_id[] = {
	{ "mms114", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mms114_id);

#ifdef CONFIG_OF
static struct of_device_id mms114_dt_match[] = {
	{ .compatible = "melfas,mms114" },
	{ }
};
#endif

static struct i2c_driver mms114_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
#if 0 //defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND)
		.pm = &mms_ts_pm_ops,
#endif
		.of_match_table = of_match_ptr(mms114_dt_match),
	},
	.probe		= mms_ts_probe,
	.id_table	= mms114_id,
};

module_i2c_driver(mms114_driver);

/* Module information */
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_DESCRIPTION("MELFAS mms114 Touchscreen driver");
MODULE_LICENSE("GPL");

