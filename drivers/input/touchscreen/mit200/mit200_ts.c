/*
 * MELFAS MIT200 Touchscreen Driver
 *
 * Copyright (C) 2014 MELFAS Inc.
 *
 */

#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/wakelock.h>
#include "mit200_ts.h"
#ifdef CONFIG_HAS_EARLYSUSPEND
static void mit_ts_early_suspend(struct early_suspend *h);
static void mit_ts_late_resume(struct early_suspend *h);
#endif
//extern int gpio_tlmm_config(unsigned config, unsigned disable);
#define GOODIX_VTG_MIN_UV	2600000
#define GOODIX_VTG_MAX_UV	3300000
#define GOODIX_I2C_VTG_MIN_UV	1800000
#define GOODIX_I2C_VTG_MAX_UV	1800000
#define MIT_ADDR_LENGTH 2
#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data);
static int mit_suspend_flag = 0;
#endif
#if MIT_SYS_FILE
static atomic_t gt_device_count;
unsigned int MIT_REG_ADDRL = 0x00;
unsigned int MIT_REG_ADDRH = 0x00;
#endif
#if MIT_SLIDE_WAKEUP
enum doze {
	DOZE_DISABLED = 0,
	DOZE_ENABLED = 1,
	DOZE_WAKEUP = 2,
};
static enum doze doze_status = DOZE_DISABLED;
static void mit_sensor_slidewake(struct mit_ts_info *rmi4_data);
static void mit_sensor_doze(struct mit_ts_info *info);
int mit_wakeup_flag = 0;
static struct wake_lock gesture_wakelock;
#endif
static int mit_debug_switch = 1;
#if MIT_GLOVE_MODE
int mit_glove_flag = 0;
void mit_glove_close(int val);
static struct mit_ts_info *glove_info;
static int glove_close = 0;
#endif
#if MIT_CANCEL_X
static int X_DEL = 30;
static int cancel_id_flag;
static int cancel_flag = 0;
static int CANCELED_ID = -1;
#endif
#if MIT_DISCARD_X
static int X_DISCARD = 14;
#endif
#if MIT_DISCARD_Y
static int Y_DISCARD = 14;
#endif
#if MIT_CANCEL_Y
static int Y_DEL = 30;
static int cancel_flag_y = 0;
#endif
static void mit_sensor_lpwg(struct mit_ts_info *info);
static int mit_ts_suspend(struct device *dev);
static int mit_ts_resume(struct device *dev);
static void mit_ts_enable(struct mit_ts_info *info)
{
	if (info->enabled)
		return;
	
	mutex_lock(&info->lock);

	info->enabled = true;
	enable_irq(info->irq);

	mutex_unlock(&info->lock);

}

static void mit_ts_disable(struct mit_ts_info *info)
{
	if (!info->enabled)
		return;

	mutex_lock(&info->lock);

	disable_irq_nosync(info->irq);

	info->enabled = false;

	mutex_unlock(&info->lock);
}
s32 mit_i2c_read(struct i2c_client *client, u8 *buf, s32 len)
{
    struct i2c_msg msgs[2];
    s32 ret=-1;
    s32 retries = 0;
    msgs[0].flags = !I2C_M_RD;
    msgs[0].addr  = client->addr;
    msgs[0].len   = MIT_ADDR_LENGTH;
    msgs[0].buf   = &buf[0];
    //msgs[0].scl_rate = 300 * 1000;    // for Rockchip, etc.
    
    msgs[1].flags = I2C_M_RD;
    msgs[1].addr  = client->addr;
    msgs[1].len   = len - MIT_ADDR_LENGTH;
    msgs[1].buf   = &buf[MIT_ADDR_LENGTH];
    //msgs[1].scl_rate = 300 * 1000;

    while(retries < 5)
    {
        ret = i2c_transfer(client->adapter, msgs, 2);
        if(ret == 2)
			break;
        retries++;
    }
    if((retries >= 5))
    {
        MIT_DEBUG("I2C Read: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(buf[0] << 8)) | buf[1]), len-2, ret);
    }
     //  MIT_DEBUG("I2C read: 0x%04X,value: %x.", (((u16)(buf[0] << 8)) | buf[1]), buf[2]);
    return ret;
}

s32 mit_i2c_write(struct i2c_client *client,u8 *buf,s32 len)
{
    struct i2c_msg msg;
    s32 ret = -1;
    s32 retries = 0;
    msg.flags = !I2C_M_RD;
    msg.addr  = client->addr;
    msg.len   = len;
    msg.buf   = buf;
    //msg.scl_rate = 300 * 1000;    // for Rockchip, etc

    while(retries < 5)
    {
        ret = i2c_transfer(client->adapter, &msg, 1);
        if (ret == 1)break;
        retries++;
    }
    if((retries >= 5))
    {
        MIT_DEBUG("I2C write: 0x%04X, %d bytes failed, errcode: %d! Process reset.", (((u16)(buf[0] << 8)) | buf[1]), len-2, ret);
    }
    return ret;
}

void mit_reboot(struct mit_ts_info *info)
{
	//struct i2c_adapter *adapter = to_i2c_adapter(info->client->dev.parent);

//	i2c_lock_adapter(adapter);
//	msleep(50);

	char glove_buf[3] = {0x10,0x8f,0x01};
	gpio_direction_output(info->pdata->ctp_gpio, 0);
	msleep(20);
	gpio_direction_output(info->pdata->ctp_gpio, 1);
	msleep(5);
	//reboot will make tp exit glove mode;so reenable glove mode when reboot if glove flag is 1
	if(mit_glove_flag)
	{
		mit_i2c_write(info->client,glove_buf,3);
		MIT_DEBUG("%s,reenable glove mode",__func__);
	}
//	i2c_unlock_adapter(adapter);
}
void mit_clear_input_data(struct mit_ts_info *info)
{
	int i;

	for (i = 0; i < MAX_FINGER_NUM; i++) {
		input_mt_slot(info->input_dev, i);
		input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, false);
	}
	input_sync(info->input_dev);
#if MIT_CANCEL_X
	cancel_flag = 0;
	cancel_id_flag = 0;
	CANCELED_ID = -1;
#endif

	return;
}


void mit_report_input_data(struct mit_ts_info *info, u8 sz, u8 *buf)
{
	int i;
	struct i2c_client *client = info->client;
	int id;
	int x;
	int y;
	int touch_major;
	int pressure;
	u8 *tmp;
#if MIT_SLIDE_WAKEUP
	int type;
#endif
    int touch_type;
	if (buf[0] == MIT_ET_ERROR) {
		dev_info(&client->dev, "Error Event : Row[%d] Col[%d]\n", buf[2], buf[3]);

		//reboot
		mit_ts_disable(info);
		mit_clear_input_data(info);
		mit_reboot(info);	
		mit_ts_enable(info);
		goto out;
	}
#if MIT_SLIDE_WAKEUP
	else if (buf[0] == MIT_ET_NAP) {
		//Nap Event	
		type = ((buf[1]>>4)&0x01);
		
		wake_lock_timeout(&gesture_wakelock,1*HZ);
		if(type == 0){
			//Key
			dev_info(&client->dev, "Nap Event : Key\n");
            input_report_key(info->input_dev,KEY_GESTURE_DT_HOME,1);
			input_sync(info->input_dev);
            input_report_key(info->input_dev,KEY_GESTURE_DT_HOME,0);
			input_sync(info->input_dev);
		}
		else if(type == 1){
			//Screen
			for (i = 2; i < sz; i += NAP_EVENT_SZ) {
				tmp = buf + i;
				x = tmp[1] | ((tmp[0] & 0xf) << 8);
				y = tmp[2] | (((tmp[0] >> 4 ) & 0xf) << 8);
				dev_info(&client->dev, "Nap Event : Screen (%d, %d)\n", x, y);	
			}
                input_report_key(info->input_dev,KEY_GESTURE_DT,1);
				input_sync(info->input_dev);
                input_report_key(info->input_dev,KEY_GESTURE_DT,0);
				input_sync(info->input_dev);
		}
	}	
#endif
	else{
		//Touch Event
		for (i = 0; i < sz; i += FINGER_EVENT_SZ) {
			tmp = buf + i;
            touch_type = (tmp[0]&0x20);
			id = (tmp[0] & 0xf) -1;

			if(touch_type)//screen
			{
			x = tmp[2] | ((tmp[1] & 0xf) << 8);
			y = tmp[3] | (((tmp[1] >> 4 ) & 0xf) << 8);
			touch_major = tmp[4];
			pressure = tmp[5];
#if MIT_DISCARD_X
			if(x <X_DISCARD || x > (info->pdata->max_x - X_DISCARD))
			{
				MIT_DEBUG("x:%d,y:%d discard",x,y);
				input_mt_slot(info->input_dev, id);
				input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, false);
				input_sync(info->input_dev);
#if MIT_CANCEL_X
				if(id == CANCELED_ID && CANCELED_ID != -1)
				{
				cancel_flag = 0;
				cancel_id_flag = 0;
				CANCELED_ID = -1;
				}
#endif
				continue;
			}
#endif
#if MIT_DISCARD_Y
			if(y > (info->pdata->max_y - Y_DISCARD))
			{
				MIT_DEBUG("x:%d,y:%d discard",x,y);
				input_mt_slot(info->input_dev, id);
				input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, false);
				input_sync(info->input_dev);
				continue;
			}
#endif
#if MIT_CANCEL_X
			if(sz > 8 && x > X_DEL && x< (info->pdata->max_x-X_DEL))
				cancel_flag = 1;
#endif
			if (!(tmp[0] & 0x80)) {
				input_mt_slot(info->input_dev, id);
				input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, false);
				input_sync(info->input_dev);
#if MIT_CANCEL_X
				cancel_flag = 0;
				if(id == CANCELED_ID && CANCELED_ID != -1)
				{
				cancel_id_flag = 0;
				CANCELED_ID = -1;
				}
#endif
				continue;
			}
#if MIT_CANCEL_X  
		if(cancel_flag || cancel_id_flag)
			{
				if(x < X_DEL || x> (info->pdata->max_x - X_DEL))
				{
				input_mt_slot(info->input_dev,id);
				input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, false);
				input_sync(info->input_dev);
				cancel_id_flag = 1;
				CANCELED_ID = id;
				MIT_DEBUG("X:%d,Y:%d is canceled",x,y);
				continue;
				}
			}
#endif
#if MIT_CANCEL_Y  
		if(cancel_flag_y)
			{
				if(y >(info->pdata->max_y- Y_DEL))
				{
				input_mt_slot(info->input_dev,id);
				input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, false);
				input_sync(info->input_dev);
				MIT_DEBUG("X:%d,Y:%d is canceled",x,y);
				continue;
				}
			}
#endif
			input_mt_slot(info->input_dev, id);
			input_mt_report_slot_state(info->input_dev, MT_TOOL_FINGER, true);
			input_report_abs(info->input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(info->input_dev, ABS_MT_POSITION_Y, y);
			input_report_abs(info->input_dev, ABS_MT_TOUCH_MAJOR, touch_major);
			input_report_abs(info->input_dev, ABS_MT_PRESSURE, pressure);
			input_sync(info->input_dev);
			if(mit_debug_switch)
			MIT_DEBUG("F:%d,X:%d,Y:%d",id,x,y);

			}
			else //key
			{
				if(id == 2)
				{
					if(mit_debug_switch)
					MIT_DEBUG("KEY BACK :%d",(tmp[0] & 0x80)?1:0);
                input_report_key(info->input_dev,KEY_BACK,(tmp[0] & 0x80));
				input_sync(info->input_dev);
				}
				if(id == 0)
				{
					if(mit_debug_switch)
					MIT_DEBUG("KEY MENU :%d",(tmp[0] & 0x80)?1:0);
                input_report_key(info->input_dev,KEY_MENU,(tmp[0] & 0x80));
				input_sync(info->input_dev);
				}
				if(id == 1)
				{
					if(mit_debug_switch)
					MIT_DEBUG("KEY HOME :%d",(tmp[0] & 0x80)?1:0);
                input_report_key(info->input_dev,KEY_HOME,(tmp[0] & 0x80));
				input_sync(info->input_dev);
				}
#if MIT_CANCEL_Y
				if(tmp[0] & 0x80)
					cancel_flag_y = 1;
				else
					cancel_flag_y = 0;
#endif
				
			}
		}
	}	
	

out:
	return;

}



static irqreturn_t mit_ts_interrupt(int irq, void *dev_id)
{
	struct mit_ts_info *info = dev_id;
	struct i2c_client *client = info->client;
	u8 buf[MAX_FINGER_NUM * FINGER_EVENT_SZ] = { 0, };
	int ret;
	u8 sz=0;
	u8 reg[2] = {MIT_REGH_CMD, MIT_REGL_INPUT_EVENT};
	u8 cmd[2] = {MIT_REGH_CMD, MIT_REGL_EVENT_PKT_SZ};
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = cmd,
			.len = 2,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = &sz,
		},
	};
	MIT_DEBUG("%s",__func__);
	
	msg[1].len = 1;
	i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
//20140701	
	if(sz==0){
		dev_err(&client->dev, "%s : event size is zero\n", __func__);		//debug
		return IRQ_HANDLED;
	}
	else if(sz > MAX_FINGER_NUM * FINGER_EVENT_SZ){
		sz = MAX_FINGER_NUM * FINGER_EVENT_SZ;
		dev_err(&client->dev, "%s : event size overflow\n", __func__);	//debug
	}
	
	msg[0].buf = reg;
	msg[1].buf = buf;
	msg[1].len = sz;
	
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));

	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev,
			"failed to read %d bytes of touch data (%d)\n",
			sz, ret);
	} else {
		mit_report_input_data(info, sz, buf);
	}

	return IRQ_HANDLED;
}

#if 0
static int mit_ts_input_open(struct input_dev *dev)
{
	struct mit_ts_info *info = input_get_drvdata(dev);
	int ret;

	ret = wait_for_completion_interruptible_timeout(&info->init_done,
			msecs_to_jiffies(90 * MSEC_PER_SEC));
	if (ret > 0) {
		if (info->irq != -1) {
			mit_ts_enable(info);
			ret = 0;
		} else {
			ret = -ENXIO;
		}
	} else {
		dev_err(&dev->dev, "error while waiting for device to init\n");
		ret = -ENXIO;
	}

	return ret;
}

static void mit_ts_input_close(struct input_dev *dev)
{
	struct mit_ts_info *info = input_get_drvdata(dev);

	mit_ts_disable(info);
}
#endif
static int mit_ts_config(struct mit_ts_info *info)
{
	struct i2c_client *client = info->client;
	int ret;
	u8 cmd[2] = {MIT_REGH_CMD, MIT_REGL_ROW_NUM};
	u8 num[2];
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = cmd,
			.len = 2,
		},{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = num,
			.len = 2,
		},
	};
	//20140701
	dev_info(&client->dev, "%s : START\n", __func__);	//debug

	ret = request_threaded_irq(client->irq, NULL, mit_ts_interrupt,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"mit_ts", info);

	if (ret) {
		dev_err(&client->dev, "failed to register irq\n");
		goto out;
	}
	disable_irq_nosync(client->irq);
	//mit_ts_disable(info);
	info->irq = client->irq;
//	barrier();


	ret=i2c_transfer(client->adapter,msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
//20140701
		dev_err(&client->dev, "%s : ERROR - i2c_transfer\n", __func__);	//debug
		dev_err(&client->dev,
			"failed to read bytes of touch data (%d)\n",
			 ret);
	}
	info->row_num = num[0];
	info->col_num = num[1];		
	dev_info(&client->dev, "MIT touch controller initialized\n");

//	complete_all(&info->init_done);

out:
	return ret;
}
static int mit_parse_dt(struct device *dev,
			struct mit_ts_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	/* reset, irq gpio info */
	pdata->reset_gpio = of_get_named_gpio_flags(np, "mit,reset-gpio",
				0, &pdata->reset_gpio_flags);

	if (pdata->reset_gpio < 0)
		return pdata->reset_gpio;

	pdata->ctp_gpio = of_get_named_gpio_flags(np, "mit,ctp-gpio",
				0, &pdata->ctp_gpio_flags);

	if (pdata->ctp_gpio < 0)
		return pdata->ctp_gpio;
	pdata->irq_gpio = of_get_named_gpio_flags(np, "mit,irq-gpio",
				0, &pdata->irq_gpio_flags);

	if (pdata->irq_gpio < 0)
		return pdata->irq_gpio;
	return 0;
}
static int mit_power_on(struct mit_ts_info *info, bool on)
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
static int mit_power_init(struct mit_ts_info *info, bool on)
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
static s8 mit_request_io_port(struct mit_ts_info *info)
{

	struct i2c_client *client = info->client;
	struct mit_ts_platform_data *pdata = info->pdata;
	int ret;
	if (gpio_is_valid(pdata->ctp_gpio)) {
		ret = gpio_request(pdata->ctp_gpio, "mit_ts__ctp_gpio");
		if (ret) {
			dev_err(&client->dev, "reset gpio request failed\n");
			goto free_ctp_gpio;
		}

		ret = gpio_direction_output(pdata->ctp_gpio, 1);
		if (ret) {
			dev_err(&client->dev,
					"set_direction for reset gpio failed\n");
			goto pwr_off;
		}
       
		
	} else {
		dev_err(&client->dev, "reset gpio is invalid!\n");
		ret = -EINVAL;
		goto free_ctp_gpio;
	}
	usleep(100);
	if (gpio_is_valid(pdata->irq_gpio)) {
		ret = gpio_request(pdata->irq_gpio, "mit_ts_irq_gpio");
		if (ret) {
			dev_err(&client->dev, "irq gpio request failed\n");
			goto free_ctp_gpio;
		}
		
		ret = gpio_direction_output(pdata->irq_gpio, 1);
		if (ret) {
			dev_err(&client->dev,
					"set_direction for reset gpio failed\n");
			goto free_irq_gpio;
		}
		
		ret = gpio_direction_input(pdata->irq_gpio);
		if (ret) {
			dev_err(&client->dev,
					"set_direction for irq gpio failed\n");
			goto free_irq_gpio;
		}
       
	} else {
		dev_err(&client->dev, "irq gpio is invalid!\n");
		ret = -EINVAL;
		goto free_ctp_gpio;
	}

	if (gpio_is_valid(pdata->reset_gpio)) {
		ret = gpio_request(pdata->reset_gpio, "mit_ts__reset_gpio");
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
       
		
	} else {
		dev_err(&client->dev, "reset gpio is invalid!\n");
		ret = -EINVAL;
		goto free_irq_gpio;
	}

	return ret;

free_reset_gpio:
	if (gpio_is_valid(pdata->reset_gpio))
		gpio_free(pdata->reset_gpio);
free_irq_gpio:
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->irq_gpio);
free_ctp_gpio:
	if (gpio_is_valid(pdata->ctp_gpio))
		gpio_free(pdata->ctp_gpio);
pwr_off:
	return ret;

}
/*
static void mit_fw_update_controller(const struct firmware *fw, void * context){
	struct mit_ts_info *info = context;
	int retires = 3;
	int ret;
	MIT_DEBUG("%s",__func__);
	if (!fw) {
		dev_err(&info->client->dev, "failed to read firmware\n");
		complete_all(&info->init_done);
		return;
	}

	do {
		ret = mit_flash_fw(info,fw->data,fw->size);
	} while (ret && --retires);

	if (!retires) {
		dev_err(&info->client->dev, "failed to flash firmware after retires\n");
	}
	release_firmware(fw);
}
*/
#if MIT_SYS_FILE
#if MIT_UPDATE_FW
static ssize_t mit_fw_update(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mit_ts_info *info = dev_get_drvdata(dev);
        struct i2c_client *client = to_i2c_client(dev);
	struct file *fp; 
	mm_segment_t old_fs;
	size_t fw_size, nread;
	int error = 0;
	int result = 0;
 	disable_irq(client->irq);
	old_fs = get_fs();
	set_fs(KERNEL_DS);  
 
	fp = filp_open(EXTRA_FW_PATH, O_RDONLY, S_IRUSR);
	if (IS_ERR(fp)) {
		dev_err(&info->client->dev,
		"%s: failed to open %s.\n", __func__, EXTRA_FW_PATH);
		error = -ENOENT;
		goto open_err;
	}
 	fw_size = fp->f_path.dentry->d_inode->i_size;
	if (0 < fw_size) {
		unsigned char *fw_data;
		fw_data = kzalloc(fw_size, GFP_KERNEL);
		nread = vfs_read(fp, (char __user *)fw_data,fw_size, &fp->f_pos);
		dev_info(&info->client->dev,
		"%s: start, file path %s, size %z Bytes\n", __func__,EXTRA_FW_PATH, fw_size);
		if (nread != fw_size) {
			    dev_err(&info->client->dev,
			    "%s: failed to read firmware file, nread %u Bytes\n", __func__, nread);
		    error = -EIO;
		} else{
			result=mit_flash_fw(info,fw_data,fw_size);
		}
		kfree(fw_data);
	}
 	filp_close(fp, current->files);
open_err:
	enable_irq(client->irq);
	set_fs(old_fs);
	return result;
}
static DEVICE_ATTR(fw_update, 0644, mit_fw_update, NULL);
#endif
static ssize_t mit_read_version(struct device *dev, struct device_attribute *attr, char *buf)
{
   //struct i2c_client *client = to_i2c_client(dev);
	struct mit_ts_info *info = dev_get_drvdata(dev);
	char data[255];
	int ret;
	u8 ver[2];

	get_fw_version(info->client,ver);

	sprintf(data,"f/w version 0x%x, 0x%x\n",ver[0],ver[1]);
	ret = snprintf(buf,PAGE_SIZE,"%s\n",data);
	return ret;
}
static DEVICE_ATTR(version, 0644,mit_read_version, NULL);

static ssize_t mit_read_info(struct device *dev, struct device_attribute *attr, char *buf)
{
   //struct i2c_client *client = to_i2c_client(dev);
	struct mit_ts_info *info = dev_get_drvdata(dev);
	char data[255];
	int ret;
	u8 ver[2];

	get_fw_version(info->client,ver);

	sprintf(data,"mit200_biel_%x.%x\n",ver[0],ver[1]);
	ret = snprintf(buf,PAGE_SIZE,"%s",data);
	return ret;
}
static DEVICE_ATTR(ic_info, 0644,mit_read_info, NULL);
static ssize_t mit_reset(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	unsigned int input;
	struct mit_ts_info *info = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;
	//gpio_direction_output(info->pdata->reset_gpio, input);
	mit_reboot(info);
	return count;
}
static DEVICE_ATTR(reset, 0644,NULL,mit_reset);
#if MIT_UPDATE_FW
static ssize_t mit_update_fw(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	unsigned int input;
	struct mit_ts_info *info = dev_get_drvdata(dev);
	int ret;
	const struct firmware *fw_entry = NULL;

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;
				ret = request_firmware(&fw_entry, "mit_fw.bin",
						&info->client->dev);
				if (ret != 0) {
					MIT_DEBUG("Firmware image mit_ts.fw not available\n");
				}
				if(fw_entry)
				{
					MIT_DEBUG("UPDATE FIRMWAR");
		ret = mit_flash_fw_force(info,fw_entry->data,fw_entry->size);
				}
	//gpio_direction_output(info->pdata->reset_gpio, input);
	return count;
}
static DEVICE_ATTR(reflash, 0644,NULL,mit_update_fw);
#endif
static ssize_t mit_read_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mit_ts_info *info = dev_get_drvdata(dev);
	char data[255];
	int ret;
	u8 sz = 0;
	u8 cmd[3] = {MIT_REGH_CMD, MIT_REGL_EVENT_PKT_SZ,0};
	mit_i2c_read(info->client, cmd,3);
	sz = cmd[2];
	sprintf(data,"sz is %d\n",sz);
	ret = snprintf(buf,PAGE_SIZE,"%s\n",data);
	return ret;
}
static DEVICE_ATTR(data, 0644,mit_read_data, NULL);
/*
exchange the register address
example:change the register addr to 0015
echo "0015">register_address
cat register_address
*/
static ssize_t mit_regl_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	
	return snprintf(buf, PAGE_SIZE, "0X%x\n",MIT_REG_ADDRL);
}

static ssize_t mit_regl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int regl = 0x00;
	int input_number = 0;
	
	input_number = count - 1;
	printk("input number is %d\n",input_number);
	if (sscanf(buf, "%x", &regl) != 1)
		return -EINVAL;
	printk("regaddress is 0x%x\n",regl);
	if (input_number == 2)
	{
	MIT_REG_ADDRL = regl;
	}
	else
	printk("input number is error,please input 4 charter\n");
	return count;
}
static DEVICE_ATTR(rl, 0644,mit_regl_show,mit_regl_store);
static ssize_t mit_regh_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	
	return snprintf(buf, PAGE_SIZE, "0X%x\n",MIT_REG_ADDRH);
}

static ssize_t mit_regh_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int regh = 0x00;
	int input_number = 0;
	
	input_number = count - 1;
	printk("input number is %d\n",input_number);
	if (sscanf(buf, "%x", &regh) != 1)
		return -EINVAL;
	printk("regaddress is 0x%x\n",regh);
	if (input_number == 2)
	{
	MIT_REG_ADDRH = regh;
	}
	else
	printk("input number is error,please input 4 charter\n");
	return count;
}
static DEVICE_ATTR(rh, 0644,mit_regh_show,mit_regh_store);
static ssize_t mit_regvalue_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mit_ts_info *info = dev_get_drvdata(dev);
	u8 cmd[3] = {MIT_REG_ADDRH,MIT_REG_ADDRL,0};
	mit_i2c_read(info->client, cmd,3);
	return snprintf(buf,PAGE_SIZE,"Value:0x%x\n",cmd[2]);
}

static ssize_t mit_regvalue_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int input_number = 0;
	unsigned char regvalue;
	struct mit_ts_info *info = dev_get_drvdata(dev);
	u8 cmd[3] = {MIT_REG_ADDRH,MIT_REG_ADDRL,0};
	input_number = count - 1;
	MIT_DEBUG("RH:%x,RL:%x",cmd[0],cmd[1]);
	printk("input number is %d\n",input_number);
	if (sscanf(buf, "%x",(int*)&regvalue) != 1)
		return -EINVAL;
	printk("regvalue is 0x%x\n",regvalue);
	if (input_number != 2)
	printk("input number is error,please input 2 charter\n");
	MIT_DEBUG("RH:%x,RL:%x",cmd[0],cmd[1]);
	cmd[0] = MIT_REG_ADDRH;
	cmd[1] = MIT_REG_ADDRL;
	cmd[2] = regvalue;
	MIT_DEBUG("RH:%x,RL:%x",cmd[0],cmd[1]);
	mit_i2c_write(info->client,cmd,3);
	return count;
}

static DEVICE_ATTR(rv, 0644,mit_regvalue_show,mit_regvalue_store);

static ssize_t mit_suspend_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned char regvalue;
	struct mit_ts_info *info = dev_get_drvdata(dev);
	if (sscanf(buf, "%d",(int*)&regvalue) != 1)
		return -EINVAL;
	printk("regvalue is 0x%x\n",regvalue);
	if(regvalue)
	mit_sensor_lpwg(info);
	else
	mit_reboot(info);
	return count;
}

static DEVICE_ATTR(suspend, 0644,NULL,mit_suspend_store);
static ssize_t mit_debug_switch_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",mit_debug_switch);
}

static ssize_t mit_debug_switch_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	int error;
	error = sscanf(buf, "%d", &val);
	if(error != 1)
		return error;
	if(val == 1)
	{
		mit_debug_switch = 1;
	}else
	{
		mit_debug_switch = 0;
	}
	return count;
}
static DEVICE_ATTR(debug_switch, 0644,mit_debug_switch_show,mit_debug_switch_store);
#if MIT_SLIDE_WAKEUP
static ssize_t mit_gesture_wakeup_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",mit_wakeup_flag);
}

static ssize_t mit_gesture_wakeup_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	int error;
	struct mit_ts_info *info = dev_get_drvdata(dev);
	error = sscanf(buf, "%d", &val);
	if(error != 1)
		return error;
	if(val == 1)
	{
		if(!mit_wakeup_flag)
		{
		mit_wakeup_flag = 1;
		enable_irq_wake(info->irq);
		MIT_DEBUG("%s,gesture flag is  = %d",__func__,val);
		}else
			return count;
	}else
	{
		if(mit_wakeup_flag)
		{
		mit_wakeup_flag = 0;
		disable_irq_wake(info->irq);
		MIT_DEBUG("%s,gesture flag is  = %d",__func__,val);
		}else
			return count;
	}
	return count;
}
static DEVICE_ATTR(gesture_enable, 0644,mit_gesture_wakeup_show,mit_gesture_wakeup_store);
static ssize_t mit_napmode_start_store(struct device *dev, struct device_attribute *attr, const char *buf,size_t count)
{
	struct mit_ts_info *info = dev_get_drvdata(dev);
	char write_buf[255];
	int val;
	int error;
	error = sscanf(buf, "%d", &val);
	if(error != 1)
		return error;
	if(val == 1)
	{
	write_buf[0] = MIT_REGH_CMD;
	write_buf[1] = MIT_REGL_NAP_START;
	write_buf[2] = 0x01;

	if(i2c_master_send(info->client,write_buf,3)!=3)
	{
		dev_err(&info->client->dev, "%s failed\n", __func__);
	}else
	{
		MIT_DEBUG("%s,enter napmode",__func__);
	}
	}else
	{
		mit_reboot(info);
	}
	return count;

}
static DEVICE_ATTR(napmode_start, 0644,NULL,mit_napmode_start_store);
#endif
#if MIT_GLOVE_MODE
static ssize_t mit_glove_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",mit_glove_flag);
}

static ssize_t mit_glove_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	int error;
	struct mit_ts_info *info = dev_get_drvdata(dev);
	char glove_buf[3] = {0x10,0x8f,0x00};
	error = sscanf(buf, "%d", &val);
	if(error != 1)
		return error;
	if(val == 1)
	{
		mit_glove_flag = 1;
		glove_buf[2] = 0x01;
		mit_i2c_write(info->client,glove_buf,3);
		MIT_DEBUG("%s,glove flag is  = %d",__func__,val);
	}else
	{
		mit_glove_flag = 0;
		glove_buf[2] = 0;
		mit_i2c_write(info->client,glove_buf,3);
		MIT_DEBUG("%s,glove flag is  = %d",__func__,val);
	}
	return count;
}
static DEVICE_ATTR(glove_enable, 0644,mit_glove_enable_show,mit_glove_enable_store);
void mit_glove_close(int val)
{
	char glove_buf[3] = {0x10,0x8f,0x00};
	if(val && !glove_close)
	{
		glove_close = 1;
		glove_buf[2] = 0x0;
		mit_i2c_write(glove_info->client,glove_buf,3);
		MIT_DEBUG("glove close");
	}
	if(!val && glove_close)
	{
		glove_close = 0;
		glove_buf[2] = 0x01;
		mit_i2c_write(glove_info->client,glove_buf,3);
		MIT_DEBUG("glove open");
	}

}
#endif
static struct attribute *mit_attrs[] = {
#if MIT_UPDATE_FW
	&dev_attr_fw_update.attr,
#endif
	&dev_attr_version.attr,
	&dev_attr_ic_info.attr,
	&dev_attr_data.attr,
	&dev_attr_reset.attr,
	&dev_attr_rl.attr,
	&dev_attr_rh.attr,
	&dev_attr_rv.attr,
#if MIT_UPDATE_FW
	&dev_attr_reflash.attr,
#endif
	&dev_attr_suspend.attr,
	&dev_attr_debug_switch.attr,
#if MIT_SLIDE_WAKEUP
	&dev_attr_gesture_enable.attr,
	&dev_attr_napmode_start.attr,
#endif
#if MIT_GLOVE_MODE
	&dev_attr_glove_enable.attr,
#endif
	NULL,
};

static const struct attribute_group mit_attr_group = {
	.attrs = mit_attrs,
};
#endif
#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct mit_ts_info *ts =
		container_of(self, struct mit_ts_info, fb_notif);
	if (evdata && evdata->data && event == FB_EVENT_BLANK &&
			ts && ts->client) {
		blank = evdata->data;
		if(mit_suspend_flag>3)
		{
		if (*blank == FB_BLANK_UNBLANK)
			mit_ts_resume(&ts->client->dev);
		else if (*blank == FB_BLANK_POWERDOWN)
			mit_ts_suspend(&ts->client->dev);
		}
	}
	mit_suspend_flag++;
	return 0;
}
#endif
#if 0
static int mit_i2c_test(struct i2c_client *client)
{
	u8 buf[3] = {0x00,MIT_FW_VERSION};
	int retry = 5;
	int ret = -EIO;

	while (retry--) {
		ret = mit_i2c_read(client, buf, 3);
		if (ret > 0)
			return ret;
		dev_err(&client->dev, "GTP i2c test failed time %d.\n", retry);
		msleep(20);
	}
	return ret;
}
#endif
static int mit_ts_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct mit_ts_info *info;
	struct mit_ts_platform_data *pdata;
	struct input_dev *input_dev;
	int ret = 0;
	u8 ver[2];
#if MIT_UPDATE_FW
	const struct firmware *fw_entry = NULL;
#endif
	const char *fw_name = FW_NAME;
		if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct mit_ts_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev,
				"MIT Failed to allocate memory for pdata\n");
			return -ENOMEM;
		}

		ret = mit_parse_dt(&client->dev, pdata);
		if (ret)
			return ret;
		pdata->max_x = MIT_MAX_X;
		pdata->max_y = MIT_MAX_Y;

	} else {
		pdata = client->dev.platform_data;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -EIO;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
    if (info == NULL)
    {
        MIT_DEBUG("Alloc GFP_KERNEL memory failed.\n");
        return -ENOMEM;
    }
    memset(info, 0, sizeof(*info));
	info->client = client;
	info->pdata = pdata;
	info->irq = -1;
		ret = mit_power_init(info,true);
		if (ret) {
			dev_err(&client->dev, "MIT power init failed\n");
			goto exit_free_client_data;
		}
		ret = mit_power_on(info,true);
		if (ret) {
			dev_err(&client->dev, "MIT power on failed\n");
			goto exit_deinit_power;
		}
    ret = mit_request_io_port(info);
    if (ret < 0)
    {
        MIT_DEBUG("MIT request IO port failed.");
        kfree(info);
        goto exit_power_off;
    }
#if 0
	ret = mit_i2c_test(client);
	if (ret != 2) {
		dev_err(&client->dev, "I2C communication ERROR!\n");
		goto exit_power_off;
	}
#endif
	mit_ts_config(info);
	get_fw_version(info->client,ver);
	MIT_DEBUG("FW: 0:%x,1:%x",ver[0],ver[1]);

	input_dev = input_allocate_device();

	if (!info || !input_dev) {
		dev_err(&client->dev, "Failed to allocated memory\n");
		return -ENOMEM;
	}
	input_mt_init_slots(input_dev, MAX_FINGER_NUM,0);

	snprintf(info->phys, sizeof(info->phys),
		"%s/input0", dev_name(&client->dev));

	input_dev->name = "qwerty";
	input_dev->phys = info->phys;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
//	input_dev->open = mit_ts_input_open;
//	input_dev->close = mit_ts_input_close;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, MAX_WIDTH, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, MAX_PRESSURE, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, info->pdata->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, info->pdata->max_y, 0, 0);
 	input_set_capability(input_dev, EV_KEY, KEY_MENU);  
  	input_set_capability(input_dev, EV_KEY, KEY_HOME);  
  	input_set_capability(input_dev, EV_KEY, KEY_BACK);  
#if MIT_SLIDE_WAKEUP
  	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_DT);  
  	input_set_capability(input_dev, EV_KEY, KEY_POWER);  
  	input_set_capability(input_dev, EV_KEY, KEY_GESTURE_DT_HOME);  
#endif
	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&client->dev, "failed to register input dev\n");
		return -EIO;
	}
	info->input_dev = input_dev;
	i2c_set_clientdata(client, info);
	input_set_drvdata(input_dev, info);
	init_completion(&info->init_done);
	mutex_init(&info->lock);
	
	info->fw_name = kstrdup(fw_name, GFP_KERNEL);
#if MIT_UPDATE_FW
				ret = request_firmware(&fw_entry, fw_name,
						&info->client->dev);
				if (ret != 0) {
					MIT_DEBUG("Firmware image %s not available\n",fw_name);
				}
				if(fw_entry)
				{
					MIT_DEBUG("UPDATE FIRMWAR");
		ret = mit_flash_fw(info,fw_entry->data,fw_entry->size);
				}
#endif				
				/*	
	ret = request_firmware_nowait(THIS_MODULE, true, fw_name, &info->client->dev,
			GFP_KERNEL, info,mit_fw_update_controller);
	if (ret) {
		dev_err(&client->dev, "failed to schedule firmware update\n");
		return -EIO;
	}
	kfree(info->fw_name);
	*/
	
   #if defined(CONFIG_FB)
	 info->fb_notif.notifier_call = fb_notifier_callback;
	 ret = fb_register_client(&info->fb_notif);
	 if (ret)
		dev_err(&info->client->dev,
			"Unable to register fb_notifier: %d\n",
			ret);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	info->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	info->early_suspend.suspend = mit_ts_early_suspend;
	info->early_suspend.resume = mit_ts_late_resume;
	register_early_suspend(&info->early_suspend);
#endif

#ifdef __MIT_TEST_MODE__
	if(mit_sysfs_test_mode(info)){
		dev_err(&client->dev, "failed to create sysfs test mode group\n");
		return -EAGAIN;
	}
	if(mit_ts_log(info)){
		dev_err(&client->dev, "failed to create mit log mode\n");
		return -EAGAIN;
	}

#endif
#if MIT_SLIDE_WAKEUP
	wake_lock_init(&gesture_wakelock,WAKE_LOCK_SUSPEND,"gesture_wakelock");
#endif
#if MIT_SYS_FILE
	//add tp class to show tp info
	
		info->tp_class = class_create(THIS_MODULE, "touch");
		if (IS_ERR(info->tp_class))
		{
			MIT_DEBUG("create tp class err!");
			return -1;
		}
		else
		atomic_set(&gt_device_count, 0);
	info->index = atomic_inc_return(&gt_device_count);
	info->dev = device_create(info->tp_class, NULL,
		MKDEV(0, info->index), NULL, "tp_dev");
	if (IS_ERR(info->dev))
	{
		MIT_DEBUG("create device err!");
		return -1;
	}
		ret = sysfs_create_group(&info->dev->kobj,
				&mit_attr_group);
		if (ret < 0) {
			dev_err(&client->dev,
					"%s: Failed to create sysfs attributes\n",
					__func__);
			return -1;
		}
	dev_set_drvdata(info->dev,info);
	//end tp class to show tp info
#endif
#if 0
	if (sysfs_create_group(&info->input_dev->dev.kobj, &mit_attr_group)) {
		dev_err(&info->input_dev->dev, "failed to create sysfs group\n");
		return -EAGAIN;
	}

	if (sysfs_create_link(NULL, &client->dev.kobj, "mit_ts")) {
		dev_err(&client->dev, "failed to create sysfs symlink\n");
		return -EAGAIN;
	}
#endif
	dev_notice(&client->dev, "mit dev initialized\n");
	mit_ts_enable(info);
#if MIT_GLOVE_MODE
	glove_info = info;
#endif
	return 0;
exit_power_off:
	mit_power_on(info,false);
exit_deinit_power:
	mit_power_init(info,false);
exit_free_client_data:
	return 0;
}

static int mit_ts_remove(struct i2c_client *client)
{
	struct mit_ts_info *info = i2c_get_clientdata(client);

	if (info->irq >= 0)
		free_irq(info->irq, info);

#if MIT_SYS_FILE
	if(!info->dev && !info->tp_class)
	{
		sysfs_remove_group(&info->dev->kobj,&mit_attr_group);
	dev_set_drvdata(info->dev,NULL);
	device_destroy(info->tp_class, MKDEV(0, info->index));
	}
	if(!info->tp_class)
	class_destroy(info->tp_class);
#endif
	input_unregister_device(info->input_dev);
#if defined(CONFIG_FB)
	if (fb_unregister_client(&info->fb_notif))
		dev_err(&client->dev,
			"Error occurred while unregistering fb_notifier.\n");
#elif defined(CONFIG_HAS_EARLYSUSPEND)	
	unregister_early_suspend(&info->early_suspend);	
#endif

#ifdef __MIT_TEST_MODE__
	mit_sysfs_remove(info);
	mit_ts_log_remove(info);
	kfree(info->fw_name);
	kfree(info);
#endif

	mit_power_on(info, false);
	mit_power_init(info, false);
	return 0;
}

#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
#if MIT_SLIDE_WAKEUP
static void mit_sensor_doze(struct mit_ts_info *info)
{
	char write_buf[255];
	write_buf[0] = MIT_REGH_CMD;
	write_buf[1] = MIT_REGL_NAP_START;
	write_buf[2] = 0x01;
	MIT_DEBUG("%s",__func__);
	if(i2c_master_send(info->client,write_buf,3)!=3)
	{
		dev_err(&info->client->dev, "%s failed\n", __func__);
		return;
	}
		doze_status = DOZE_ENABLED;
			return;
}
static void mit_sensor_slidewake(struct mit_ts_info *info)
{
	MIT_DEBUG("%s",__func__);
	doze_status = DOZE_DISABLED;
	mit_reboot(info);
	return;
}
#endif

static void mit_sensor_lpwg(struct mit_ts_info *info)
{

	char write_buf[255];
	write_buf[0] = MIT_REGH_CMD;
	write_buf[1] = MIT_REGL_MODE_CONTROL;
	write_buf[2] = 0x00;
	MIT_DEBUG("%s",__func__);
	mit_i2c_write(info->client,write_buf,3);
	/*
	if(i2c_master_send(info->client,write_buf,3)!=3)
	{
		dev_err(&info->client->dev, "%s failed\n", __func__);
		return;
	}
	*/
	return;
}
static int mit_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mit_ts_info *info = i2c_get_clientdata(client);
	static int ret;
	MIT_DEBUG("%s",__func__);
#if MIT_SLIDE_WAKEUP
	if(mit_wakeup_flag)
	{
		gpio_set_value(978,1);
		gpio_set_value(999,1);
		gpio_set_value(927,1);
		msleep(10);
		mit_clear_input_data(info);
		mit_sensor_doze(info);
	}
	else
	{
	mutex_lock(&info->input_dev->mutex);

	if (info->input_dev->users) {
		mit_ts_disable(info);
		mit_clear_input_data(info);
	}
	mit_sensor_lpwg(info);
	mutex_unlock(&info->input_dev->mutex);
		ret = mit_power_on(info,false);
		if (ret) {
			dev_err(&client->dev, "MIT power on failed\n");
		}
	}
#else
	mutex_lock(&info->input_dev->mutex);

	if (info->input_dev->users) {
		mit_ts_disable(info);
		mit_clear_input_data(info);
	}
	
	gpio_direction_output(info->pdata->ctp_gpio, 0);
	mutex_unlock(&info->input_dev->mutex);
		ret = mit_power_on(info,false);
		if (ret) {
			dev_err(&client->dev, "MIT power on failed\n");
		}
#endif
	return 0;

}

static int mit_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mit_ts_info *info = i2c_get_clientdata(client);
	static int ret;
	MIT_DEBUG("%s",__func__);
#if MIT_SLIDE_WAKEUP
	if(mit_wakeup_flag)
	{
		mit_sensor_slidewake(info);
	}
	else
	{
		ret = mit_power_on(info,true);
		if (ret) {
			dev_err(&client->dev, "MIT power on failed\n");
		}
	mutex_lock(&info->input_dev->mutex);
	if (info->input_dev->users)
	mit_reboot(info);
		mit_ts_enable(info);
	mutex_unlock(&info->input_dev->mutex);
	}
#else
		ret = mit_power_on(info,true);
		if (ret) {
			dev_err(&client->dev, "MIT power on failed\n");
		}
	mutex_lock(&info->input_dev->mutex);

	if (info->input_dev->users)
		mit_ts_enable(info);
	mit_reboot(info);
	mutex_unlock(&info->input_dev->mutex);
#endif
	return 0;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mit_ts_early_suspend(struct early_suspend *h)
{
	struct mit_ts_info *info;
	info = container_of(h, struct mit_ts_info, early_suspend);
	mit_ts_suspend(&info->client->dev);
}

static void mit_ts_late_resume(struct early_suspend *h)
{
	struct mit_ts_info *info;
	info = container_of(h, struct mit_ts_info, early_suspend);
	mit_ts_resume(&info->client->dev);
}
#endif

#if (!defined(CONFIG_FB) && !defined(CONFIG_HAS_EARLYSUSPEND))
static const struct dev_pm_ops mit_ts_pm_ops = {
	.suspend	= mit_ts_suspend,
	.resume		= mit_ts_resume,
};
#else
static const struct dev_pm_ops mit_ts_pm_ops = {
};
#endif

static const struct i2c_device_id mit_ts_id[] = {
	{"mit_ts", 0},
	{ }
};
//MODULE_DEVICE_TABLE(i2c, mit_ts_id);

static struct of_device_id mit_match_table[] = {
	{ .compatible = "mit200", },
	{ },
};

static struct i2c_driver mit_ts_driver = {
	.probe		= mit_ts_probe,
	.remove		= mit_ts_remove,
	.id_table	= mit_ts_id,
	.driver		= {
				.name	= "mit_ts",
#if (defined(CONFIG_PM) && !defined(CONFIG_HAS_EARLYSUSPEND))
				.pm	= &mit_ts_pm_ops,
#endif
        .of_match_table = mit_match_table,
	},
};

static int __init mit_ts_init(void)
{
	return i2c_add_driver(&mit_ts_driver);
}

static void __exit mit_ts_exit(void)
{
	return i2c_del_driver(&mit_ts_driver);
}

module_init(mit_ts_init);
module_exit(mit_ts_exit);

MODULE_VERSION("2014.05.23");
MODULE_DESCRIPTION("MELFAS MIT200 Touchscreen Driver");
MODULE_LICENSE("GPL");
