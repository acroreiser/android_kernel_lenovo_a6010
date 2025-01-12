/* drivers/i2c/chips/epl8975.c - light and proxmity sensors driver
 * Copyright (C) 2011 ELAN Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/** VERSION: 1.02**/

#include <linux/hrtimer.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
//#include <asm/mach-types.h>
#include <asm/setup.h>
#include <linux/wakelock.h>
#include <linux/jiffies.h>
#include <linux/i2c/epl8801.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/poll.h>
#include <linux/kobject.h>
#include <linux/sensors.h>


/******************************************************************************
* configuration
*******************************************************************************/
#define POCKET_MODE 1
#define DYN_INTT    1

#if POCKET_MODE
bool pkt_flag = false;

#endif
#define FT_VTG_MIN_UV           2600000
#define FT_VTG_MAX_UV           3300000
#define FT_I2C_VTG_MIN_UV       1800000
#define FT_I2C_VTG_MAX_UV       1800000

#define ALS_POLLING_MODE         	1					// 1 is polling mode, 0 is interrupt mode
#define PS_POLLING_MODE         	0					// 1 is polling mode, 0 is interrupt mode

#define ALS_LOW_THRESHOLD		1000
#define ALS_HIGH_THRESHOLD		2000

#define PS_LOW_THRESHOLD		5000
#define PS_HIGH_THRESHOLD		6000

#define LUX_PER_COUNT			700

#define PS_DYN_K_ENABLE 	1

#if PS_DYN_K_ENABLE
#define PS_MAX_CT	10000
#define PS_DYN_H_OFFSET 500
#define PS_DYN_L_OFFSET 300
#endif

#define COMMON_DEBUG 0
#define ALS_DEBUG   0
#define PS_DEBUG    0

//int HS_INTT_CENTER = EPL_INTT_PS_55;

bool polling_flag = false;

#if DYN_INTT
//Dynamic INTT
int als_report_count = 0;
int dynamic_intt_idx;
int dynamic_intt_init_idx = 1;	//initial dynamic_intt_idx
int c_gain = 48; // 48/1000=0.048
int dynamic_intt_lux = 0;

uint16_t dynamic_intt_high_thr;
uint16_t dynamic_intt_low_thr;
uint32_t dynamic_intt_max_lux = 15000;//8700;
uint32_t dynamic_intt_min_lux = 0;
uint32_t dynamic_intt_min_unit = 1000;

static int als_dynamic_intt_intt[] = {EPL_ALS_INTT_8000, EPL_ALS_INTT_70};
static int als_dynamic_intt_value[] = {8000, 70};
static int als_dynamic_intt_gain[] = {EPL_GAIN_MID, EPL_GAIN_MID};
static int als_dynamic_intt_high_thr[] = {60000, 53000};
static int als_dynamic_intt_low_thr[] = {200, 300};
static int als_dynamic_intt_intt_num =  sizeof(als_dynamic_intt_value)/sizeof(int);
#endif

/******************************************************************************
*******************************************************************************/

#define TXBYTES                             2
#define RXBYTES                             2

#define PACKAGE_SIZE 			8
#define I2C_RETRY_COUNT 		10

typedef enum
{
    CMC_BIT_RAW   			= 0x0,
    CMC_BIT_PRE_COUNT     	= 0x1,
    CMC_BIT_DYN_INT			= 0x2,
    CMC_BIT_DEF_LIGHT		= 0x4,
    CMC_BIT_TABLE			= 0x8,
} CMC_ALS_REPORT_TYPE;

typedef struct _epl_raw_data
{
    u8 raw_bytes[PACKAGE_SIZE];
    u16 renvo;
    u16 hs_data[200];

} epl_raw_data;

struct epl_sensor_priv
{
    struct i2c_client *client;
    struct input_dev *als_input_dev;
    struct input_dev *ps_input_dev;
    struct sensors_classdev als_cdev;
    struct sensors_classdev ps_cdev;
    struct regulator *vdd;
    struct regulator *vcc_i2c;
    struct workqueue_struct *epl_wq;

    int intr_pin;
    int (*power)(int on);

    int ps_opened;
    int als_opened;

    int polling_mode_hs;

    int als_suspend;
    int ps_suspend;

    int lux_per_count;

    int enable_pflag;
    int enable_lflag;

    int read_flag;
    int irq;
    spinlock_t lock;
    int hs_intt;
    unsigned int type;
} ;

static struct sensors_classdev sensors_light_cdev = {
	.name = "epl8801-light",
	.vendor = "epl",
	.version = 1,
	.handle = SENSORS_LIGHT_HANDLE,
	.type = SENSOR_TYPE_LIGHT,
	.max_range = "30000",
	.resolution = "0.0125",
	.sensor_power = "0.20",
	.min_delay = 1000, /* in microseconds */
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.enabled = 0,
	.delay_msec = 100,
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
};

static struct sensors_classdev sensors_proximity_cdev = {
	.name = "epl8801-proximity",
	.vendor = "epl",
	.version = 1,
	.handle = SENSORS_PROXIMITY_HANDLE,
	.type = SENSOR_TYPE_PROXIMITY,
	.max_range = "1",
	.resolution = "1.0",
	.sensor_power = "3",
	.min_delay = 1000, /* in microseconds */
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.enabled = 0,
	.delay_msec = 100,
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
};

static struct wake_lock g_ps_wlock;
struct epl_sensor_priv *epl_sensor_obj;
static epl_optical_sensor epl_sensor;

static epl_raw_data	gRawData;

static struct mutex sensor_mutex;
static struct wake_lock ps_lock;

static const char ElanPsensorName[]="proximity";
static const char ElanALsensorName[]="light";

#define LOG_TAG                      "[EPL8801] "
#define LOG_FUN(f)               	 printk(KERN_INFO LOG_TAG"%s\n", __FUNCTION__)
#define LOG_INFO(fmt, args...)    	 printk(KERN_INFO LOG_TAG fmt, ##args)
#define LOG_ERR(fmt, args...)   	 printk(KERN_ERR  LOG_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)

void epl_sensor_update_mode(struct i2c_client *client);
static int epl_sensor_setup_interrupt(struct epl_sensor_priv *epld);

static void epl_sensor_eint_work(struct work_struct *work);
static DECLARE_WORK(epl_sensor_irq_work, epl_sensor_eint_work);

static void epl_sensor_polling_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(polling_work, epl_sensor_polling_work);

static int ps_sensing_time(int intt, int adc, int cycle);
static int als_sensing_time(int intt, int adc, int cycle);
static int elan_power_on(struct epl_sensor_priv *data, bool on);


/*
//====================I2C write operation===============//
*/
static int epl_sensor_I2C_Write_Cmd(struct i2c_client *client, uint8_t regaddr, uint8_t data, uint8_t txbyte)
{
    uint8_t buffer[2];
    int ret = 0;
    int retry;

    buffer[0] = regaddr ;
    buffer[1] = data;

    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_send(client, buffer, txbyte);

        if (ret == txbyte)
        {
            break;
        }

        LOG_ERR("i2c write error,TXBYTES %d\n",ret);
        mdelay(10);
    }


    if(retry>=I2C_RETRY_COUNT)
    {
        LOG_ERR("i2c write retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    return ret;
}

static int epl_sensor_I2C_Write(struct i2c_client *client, uint8_t regaddr, uint8_t data)
{
    epl_sensor_I2C_Write_Cmd(client, regaddr, data, 0x02);
    return 0;
}
/*----------------------------------------------------------------------------*/
static int epl_sensor_I2C_Read(struct i2c_client *client, uint8_t regaddr, uint8_t bytecount)
{

    uint8_t buffer[bytecount];
    int ret = 0, i =0;
    int retry;

    epl_sensor_I2C_Write_Cmd(client, regaddr, 0x00, 0x01);

    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_recv(client, buffer, bytecount);

        if (ret == bytecount)
            break;

        LOG_ERR("i2c read error,RXBYTES %d\r\n",ret);
        mdelay(10);
    }

    if(retry>=I2C_RETRY_COUNT)
    {
        LOG_ERR("i2c read retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    for(i=0; i<bytecount; i++)
        gRawData.raw_bytes[i] = buffer[i];

    return ret;
}

/*----------------------------------------------------------------------------*/
static void epl_sensor_restart_polling(void)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;
    cancel_delayed_work(&polling_work);
    queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(50));
}

/*----------------------------------------------------------------------------*/
static void epl_sensor_report_lux(int repott_lux)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;
#if ALS_DEBUG
    LOG_INFO("-------------------  ALS raw = %d, lux = %d\n\n",epl_sensor.als.data.channels[1],  repott_lux);
#endif
    input_report_abs(epld->als_input_dev, ABS_MISC, repott_lux);
    input_sync(epld->als_input_dev);
}

#if DYN_INTT
long raw_convert_to_lux(u16 raw_data)
{
    long lux = 0;

    lux = dynamic_intt_min_unit * raw_data / c_gain / als_dynamic_intt_value[dynamic_intt_idx];
#if ALS_DEBUG
    LOG_INFO("[%s]:raw_data=%d, lux=%ld\r\n", __func__, raw_data, lux);
#endif

    if(lux >= (dynamic_intt_max_lux)){
#if ALS_DEBUG
        LOG_INFO("[%s]:raw_convert_to_lux: change max lux\r\n", __func__);
#endif
        lux = dynamic_intt_max_lux;
    }
    return lux;
}
#endif

/*----------------------------------------------------------------------------*/
static int epl_sensor_get_als_value(struct epl_sensor_priv *obj, u16 als)
{

#if DYN_INTT
	long now_lux=0, lux_tmp=0;
    bool change_flag = false;
#endif
	switch(epl_sensor.als.report_type)
	{
		case CMC_BIT_RAW:
			return als;
		break;

		case CMC_BIT_PRE_COUNT:
			return (als * epl_sensor.als.factory.lux_pre_count)/1000;
		break;
#if DYN_INTT
		case CMC_BIT_DYN_INT:
#if ALS_DEBUG
            LOG_INFO("[%s]: dynamic_intt_idx=%d, als_dynamic_intt_value=%d, dynamic_intt_gain=%d, als=%d \r\n",
                                    __func__, dynamic_intt_idx, als_dynamic_intt_value[dynamic_intt_idx], als_dynamic_intt_gain[dynamic_intt_idx], als);
#endif

            if(als > dynamic_intt_high_thr)
        	{
          		if(dynamic_intt_idx == (als_dynamic_intt_intt_num - 1)){
                    als = dynamic_intt_high_thr;
          		    lux_tmp = raw_convert_to_lux(als);
#if ALS_DEBUG
        	      	LOG_INFO(">>>>>>>>>>>>>>>>>>>>>>>> INTT_MAX_LUX\r\n");
#endif
          		}
                else{
                    change_flag = true;
        			als  = dynamic_intt_high_thr;
              		lux_tmp = raw_convert_to_lux(als);
                    dynamic_intt_idx++;
#if ALS_DEBUG
                    LOG_INFO(">>>>>>>>>>>>>>>>>>>>>>>>change INTT high: %d, raw: %d \r\n", dynamic_intt_idx, als);
#endif
                }
            }
            else if(als < dynamic_intt_low_thr)
            {
                if(dynamic_intt_idx == 0){
                    //als = dynamic_intt_low_thr;
                    lux_tmp = raw_convert_to_lux(als);
#if ALS_DEBUG
                    LOG_INFO(">>>>>>>>>>>>>>>>>>>>>>>> INTT_MIN_LUX\r\n");
#endif
                }
                else{
                    change_flag = true;
        			als  = dynamic_intt_low_thr;
                	lux_tmp = raw_convert_to_lux(als);
                    dynamic_intt_idx--;
#if ALS_DEBUG
                    LOG_INFO(">>>>>>>>>>>>>>>>>>>>>>>>change INTT low: %d, raw: %d \r\n", dynamic_intt_idx, als);
#endif
                }
            }
            else
            {
            	lux_tmp = raw_convert_to_lux(als);
            }

            now_lux = lux_tmp;
            dynamic_intt_lux = now_lux;
            if(als_report_count == (als_dynamic_intt_intt_num - 1))
            {
                epl_sensor_report_lux(now_lux);
            }
            else
            {
                als_report_count++;
                if(als_report_count >= (als_dynamic_intt_intt_num - 1))
                {
                    als_report_count = (als_dynamic_intt_intt_num - 1);
                }
                LOG_INFO("[%s]: als_report_count=%d \r\n", __func__, als_report_count);
            }

            if(change_flag == true)
            {
                epl_sensor.als.integration_time = als_dynamic_intt_intt[dynamic_intt_idx];
                epl_sensor.als.gain = als_dynamic_intt_gain[dynamic_intt_idx];
                dynamic_intt_high_thr = als_dynamic_intt_high_thr[dynamic_intt_idx];
                dynamic_intt_low_thr = als_dynamic_intt_low_thr[dynamic_intt_idx];
                epl_sensor_update_mode(obj->client);
                change_flag = false;
            }
            return now_lux;

		break;
#endif
	}

	return 0;
}
/*----------------------------------------------------------------------------*/

int epl_sensor_read_als(struct i2c_client *client)
{
	struct epl_sensor_priv *epld = i2c_get_clientdata(client);

	if(client == NULL)
	{
		LOG_ERR("CLIENT CANN'T EQUL NULL\n");
		return -1;
	}

	epl_sensor_I2C_Read(epld->client, 0x13, 8);

	epl_sensor.als.data.channels[0] = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];
	epl_sensor.als.data.channels[1] = (gRawData.raw_bytes[3]<<8) | gRawData.raw_bytes[2];
	epl_sensor.als.data.channels[2] = (gRawData.raw_bytes[5]<<8) | gRawData.raw_bytes[4];
	epl_sensor.als.data.channels[3] = (gRawData.raw_bytes[7]<<8) | gRawData.raw_bytes[6];
#if ALS_DEBUG
	LOG_INFO("read als channe 0 = %d\n", epl_sensor.als.data.channels[0]);
	LOG_INFO("read als channe 1 = %d\n", epl_sensor.als.data.channels[1]);
	LOG_INFO("read als channe 2 = %d\n", epl_sensor.als.data.channels[2]);
	LOG_INFO("read als channe 3 = %d\n", epl_sensor.als.data.channels[3]);
#endif
	return 0;
}

/*----------------------------------------------------------------------------*/

static void epl_sensor_report_ps_status(void)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;


    LOG_INFO("------------------- epl_sensor.ps.data.data=%d, value=%d \n\n", epl_sensor.ps.data.data, epl_sensor.ps.compare_low >> 3);

    input_report_abs(epld->ps_input_dev, ABS_DISTANCE, epl_sensor.ps.compare_low >> 3);
    input_sync(epld->ps_input_dev);
}

int epl_sensor_read_ps(struct i2c_client *client)
{

	if(client == NULL)
	{
		LOG_ERR("CLIENT CANN'T EQUL NULL\n");
		return -1;
	}

	epl_sensor_I2C_Read(client,0x1c, 4);

	epl_sensor.ps.data.ir_data = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];
	epl_sensor.ps.data.data = (gRawData.raw_bytes[3]<<8) | gRawData.raw_bytes[2];

#if PS_DEBUG
	LOG_INFO("[%s] data = %d\n", __FUNCTION__, epl_sensor.ps.data.data);
	LOG_INFO("[%s] ir data = %d\n", __FUNCTION__, epl_sensor.ps.data.ir_data);
	LOG_INFO("[%s] cancelation data = %d\n", __FUNCTION__, epl_sensor.ps.cancelation);
#endif
             if(epl_sensor.wait == EPL_WAIT_SINGLE)
	    epl_sensor_I2C_Write(client, 0x11, epl_sensor.power | epl_sensor.reset);

	return 0;
}

int epl_sensor_read_ps_status(struct i2c_client *client)
{
	u8 buf;

	if(client == NULL)
	{
		LOG_ERR("CLIENT CANN'T EQUL NULL\n");
		return -1;
	}

	epl_sensor_I2C_Read(client, 0x1b, 1);

	buf = gRawData.raw_bytes[0];

	epl_sensor.ps.saturation = (buf & 0x20);
	epl_sensor.ps.compare_high = (buf & 0x10);
	epl_sensor.ps.compare_low = (buf & 0x08);
	epl_sensor.ps.interrupt_flag = (buf & 0x04);
	epl_sensor.ps.compare_reset = (buf & 0x02);
	epl_sensor.ps.lock= (buf & 0x01);
#if PS_DEBUG
	LOG_INFO("ps: ~~~~ PS ~~~~~ \n");
	LOG_INFO("ps: buf = 0x%x\n", buf);
	LOG_INFO("ps: sat = 0x%x\n", epl_sensor.ps.saturation);
	LOG_INFO("ps: cmp h = 0x%x, l = 0x%x\n", epl_sensor.ps.compare_high, epl_sensor.ps.compare_low);
	LOG_INFO("ps: int_flag = 0x%x\n",epl_sensor.ps.interrupt_flag);
	LOG_INFO("ps: cmp_rstn = 0x%x, lock = %x\n", epl_sensor.ps.compare_reset, epl_sensor.ps.lock);
#endif
	return 0;
}
static int set_psensor_intr_threshold(uint16_t low_thd, uint16_t high_thd)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;
    struct i2c_client *client = epld->client;
    uint8_t high_msb ,high_lsb, low_msb, low_lsb;


    high_msb = (uint8_t) (high_thd >> 8);
    high_lsb   = (uint8_t) (high_thd & 0x00ff);
    low_msb  = (uint8_t) (low_thd >> 8);
    low_lsb    = (uint8_t) (low_thd & 0x00ff);

    LOG_INFO("%s: low_thd = %d, high_thd = %d \n",__FUNCTION__, low_thd, high_thd);

    epl_sensor_I2C_Write(client,0x0c,low_lsb);
    epl_sensor_I2C_Write(client,0x0d,low_msb);
    epl_sensor_I2C_Write(client,0x0e,high_lsb);
    epl_sensor_I2C_Write(client,0x0f,high_msb);

    return 0;
}

static int set_lsensor_intr_threshold(uint16_t low_thd, uint16_t high_thd)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;
    struct i2c_client *client = epld->client;
    uint8_t high_msb ,high_lsb, low_msb, low_lsb;

    high_msb = (uint8_t) (high_thd >> 8);
    high_lsb   = (uint8_t) (high_thd & 0x00ff);
    low_msb  = (uint8_t) (low_thd >> 8);
    low_lsb    = (uint8_t) (low_thd & 0x00ff);

    epl_sensor_I2C_Write(client,0x08,low_lsb);
    epl_sensor_I2C_Write(client,0x09,low_msb);
    epl_sensor_I2C_Write(client,0x0a,high_lsb);
    epl_sensor_I2C_Write(client,0x0b,high_msb);
#if ALS_DEBUG
    LOG_INFO("%s: low_thd = %d, high_thd = %d \n",__FUNCTION__, low_thd, high_thd);
#endif
    return 0;
}

int epl_sensor_read_als_status(struct i2c_client *client)
{
	struct epl_sensor_priv *epld = epl_sensor_obj;
	u8 buf;

	if(client == NULL)
	{
		LOG_ERR("CLIENT CANN'T EQUL NULL\n");
		return -1;
	}

	epl_sensor_I2C_Read(epld->client, 0x12, 1);

	buf = gRawData.raw_bytes[0];

	epl_sensor.als.saturation = (buf & 0x20);
	epl_sensor.als.compare_high = (buf & 0x10);
	epl_sensor.als.compare_low = (buf & 0x08);
	epl_sensor.als.interrupt_flag = (buf & 0x04);
	epl_sensor.als.compare_reset = (buf & 0x02);
	epl_sensor.als.lock= (buf & 0x01);
#if ALS_DEBUG
	LOG_INFO("als: ~~~~ ALS ~~~~~ \n");
	LOG_INFO("als: buf = 0x%x\n", buf);
	LOG_INFO("als: sat = 0x%x\n", epl_sensor.als.saturation);
	LOG_INFO("als: cmp h = 0x%x, l = %d\n", epl_sensor.als.compare_high, epl_sensor.als.compare_low);
	LOG_INFO("als: int_flag = 0x%x\n",epl_sensor.als.interrupt_flag);
	LOG_INFO("als: cmp_rstn = 0x%x, lock = 0x%0x\n", epl_sensor.als.compare_reset, epl_sensor.als.lock);
#endif
	return 0;
}

/*
//====================write global variable===============//
*/
static void write_global_variable(struct i2c_client *client)
{
	u8 buf;

	//wake up chip
	buf = epl_sensor.reset | epl_sensor.power;
	epl_sensor_I2C_Write(client,0x11, buf);

       /* read revno*/
	epl_sensor_I2C_Read(client, 0x20, 2);
	epl_sensor.revno = gRawData.raw_bytes[0] | gRawData.raw_bytes[1] << 8;

	/*ps setting*/
	buf = epl_sensor.ps.integration_time | epl_sensor.ps.gain;
	epl_sensor_I2C_Write(client,0x03, buf);

	buf = epl_sensor.ps.adc | epl_sensor.ps.cycle;
	epl_sensor_I2C_Write(client,0x04, buf);

	buf = epl_sensor.ps.ir_on_control | epl_sensor.ps.ir_mode | epl_sensor.ps.ir_boost | epl_sensor.ps.ir_driver;
	epl_sensor_I2C_Write(client,0x05, buf);

	buf = epl_sensor.interrupt_control | epl_sensor.ps.persist |epl_sensor.ps.interrupt_type;
	epl_sensor_I2C_Write(client,0x06, buf);

	buf = epl_sensor.ps.compare_reset | epl_sensor.ps.lock;
	epl_sensor_I2C_Write(client,0x1b, buf);

	epl_sensor_I2C_Write(client,0x22, (u8)(epl_sensor.ps.cancelation& 0xff));
	epl_sensor_I2C_Write(client,0x23, (u8)((epl_sensor.ps.cancelation & 0xff0) >> 8));
	set_psensor_intr_threshold(epl_sensor.ps.low_threshold, epl_sensor.ps.high_threshold);

	/*als setting*/
	buf = epl_sensor.als.integration_time | epl_sensor.als.gain;
	epl_sensor_I2C_Write(client,0x01, buf);

	buf = epl_sensor.als.adc | epl_sensor.als.cycle;
	epl_sensor_I2C_Write(client,0x02, buf);

	buf = epl_sensor.als.ir_on_control | epl_sensor.als.ir_mode | epl_sensor.als.ir_boost | epl_sensor.als.ir_driver;
	epl_sensor_I2C_Write(client,0xFB, buf);

	buf = epl_sensor.als.interrupt_channel_select | epl_sensor.als.persist | epl_sensor.als.interrupt_type;
	epl_sensor_I2C_Write(client,0x07, buf);

	buf = epl_sensor.als.compare_reset | epl_sensor.als.lock;
	epl_sensor_I2C_Write(client,0x12, buf);

	set_lsensor_intr_threshold(epl_sensor.als.low_threshold, epl_sensor.als.high_threshold);


	/*osc setting*/
	epl_sensor_I2C_Write(client,0xfc, epl_sensor.osc_sel);

	//set mode and wait
	buf = epl_sensor.wait | epl_sensor.mode;
	epl_sensor_I2C_Write(client,0x00, buf);

}

static void set_als_ps_intr_type(struct i2c_client *client, bool ps_polling, bool als_polling)
{

    //set als / ps interrupt control mode and trigger type
	switch((ps_polling << 1) | als_polling)
	{
		case 0: // ps and als interrupt
			epl_sensor.interrupt_control = 	EPL_INT_CTRL_ALS_OR_PS;
			epl_sensor.als.interrupt_type = EPL_INTTY_ACTIVE;
			epl_sensor.ps.interrupt_type = EPL_INTTY_ACTIVE;
		break;

		case 1: //ps interrupt and als polling
			epl_sensor.interrupt_control = 	EPL_INT_CTRL_PS;
			epl_sensor.als.interrupt_type = EPL_INTTY_DISABLE;
			epl_sensor.ps.interrupt_type = EPL_INTTY_ACTIVE;
		break;

		case 2: // ps polling and als interrupt
			epl_sensor.interrupt_control = 	EPL_INT_CTRL_ALS;
			epl_sensor.als.interrupt_type = EPL_INTTY_ACTIVE;
			epl_sensor.ps.interrupt_type = EPL_INTTY_DISABLE;
		break;

		case 3: //ps and als polling
			epl_sensor.interrupt_control = 	EPL_INT_CTRL_ALS_OR_PS;
			epl_sensor.als.interrupt_type = EPL_INTTY_DISABLE;
			epl_sensor.ps.interrupt_type = EPL_INTTY_DISABLE;
		break;
	}
}

//====================initial global variable===============//
static void initial_global_variable(struct i2c_client *client, struct epl_sensor_priv *obj)
{

	//general setting
	epl_sensor.power = EPL_POWER_ON;
	epl_sensor.reset = EPL_RESETN_RUN;
	epl_sensor.mode = EPL_MODE_IDLE;
	epl_sensor.wait = EPL_WAIT_150_MS;
	epl_sensor.osc_sel = EPL_OSC_SEL_4MHZ;

	//als setting
	epl_sensor.als.polling_mode = ALS_POLLING_MODE;
	epl_sensor.als.integration_time =EPL_ALS_INTT_350; //EPL_ALS_INTT_50;
	epl_sensor.als.gain = EPL_GAIN_MID;//EPL_GAIN_LOW;
	epl_sensor.als.adc = EPL_PSALS_ADC_13; //EPL_OSR_1024; //EPL_OSR_512;
	epl_sensor.als.cycle = EPL_CYCLE_32; //EPL_CYCLE_32;
	epl_sensor.als.interrupt_channel_select = EPL_ALS_INT_CHSEL_1;
	epl_sensor.als.persist = EPL_PERIST_1;
	epl_sensor.als.ir_on_control = EPL_IR_ON_CTRL_OFF;
	epl_sensor.als.ir_mode = EPL_IR_MODE_CURRENT;
	epl_sensor.als.ir_boost = EPL_IR_BOOST_100;
	epl_sensor.als.ir_driver = EPL_IR_DRIVE_100;
	epl_sensor.als.compare_reset = EPL_CMP_RESET;
	epl_sensor.als.lock = EPL_UN_LOCK;
	epl_sensor.als.report_type = CMC_BIT_DYN_INT; //CMC_BIT_RAW; //
	epl_sensor.als.high_threshold = ALS_HIGH_THRESHOLD;
        epl_sensor.als.low_threshold = ALS_LOW_THRESHOLD;
	//als factory
	epl_sensor.als.factory.calibration_enable =  false;
	epl_sensor.als.factory.calibrated = false;
	epl_sensor.als.factory.lux_pre_count = LUX_PER_COUNT;
#if DYN_INTT
    if(epl_sensor.als.report_type == CMC_BIT_DYN_INT)
    {
        als_report_count = 0;
        dynamic_intt_idx = dynamic_intt_init_idx;
        epl_sensor.als.integration_time = als_dynamic_intt_intt[dynamic_intt_idx];
        epl_sensor.als.gain = als_dynamic_intt_gain[dynamic_intt_idx];
        dynamic_intt_high_thr = als_dynamic_intt_high_thr[dynamic_intt_idx];
        dynamic_intt_low_thr = als_dynamic_intt_low_thr[dynamic_intt_idx];
    }
#endif
	//ps setting
	epl_sensor.ps.polling_mode = PS_POLLING_MODE;
	epl_sensor.ps.integration_time = EPL_PS_INTT_200;
	epl_sensor.ps.gain = EPL_GAIN_LOW;
	epl_sensor.ps.adc = EPL_PSALS_ADC_13;
	epl_sensor.ps.cycle = EPL_CYCLE_8;
	epl_sensor.ps.persist = EPL_PERIST_1;
	epl_sensor.ps.ir_on_control = EPL_IR_ON_CTRL_ON;
	epl_sensor.ps.ir_mode = EPL_IR_MODE_CURRENT;
	epl_sensor.ps.ir_boost = EPL_IR_BOOST_200;
	epl_sensor.ps.ir_driver = EPL_IR_DRIVE_100;
	epl_sensor.ps.compare_reset = EPL_CMP_RESET;
	epl_sensor.ps.lock = EPL_UN_LOCK;
	//epl_sensor.ps.high_threshold = PS_HIGH_THRESHOLD;
	//epl_sensor.ps.low_threshold = PS_LOW_THRESHOLD;
	//ps factory
	epl_sensor.ps.factory.calibration_enable = false;
	epl_sensor.ps.factory.calibrated = false;
	epl_sensor.ps.factory.cancelation= 0;

    set_als_ps_intr_type(client, epl_sensor.ps.polling_mode, epl_sensor.als.polling_mode);
	//write setting to sensor
	write_global_variable(client);
}

static int als_sensing_time(int intt, int adc, int cycle)
{
    long sensing_us_time;
    int sensing_ms_time;
    int als_intt, als_adc, als_cycle;

    als_intt = als_intt_value[intt>>1];
    als_adc = adc_value[adc>>3];
    als_cycle = cycle_value[cycle];
#if COMMON_DEBUG
    LOG_INFO("ALS: INTT=%d, ADC=%d, Cycle=%d \r\n", als_intt, als_adc, als_cycle);
#endif

    sensing_us_time = (als_intt + als_adc*2*3) * 2 * als_cycle;
    sensing_ms_time = sensing_us_time / 1000;
    if(epl_sensor.osc_sel == EPL_OSC_SEL_4MHZ)
    {
        sensing_ms_time = sensing_ms_time/4;
    }
#if COMMON_DEBUG
    LOG_INFO("[%s]: sensing=%d ms \r\n", __func__, sensing_ms_time);
#endif
    return (sensing_ms_time + 5);
}

static int ps_sensing_time(int intt, int adc, int cycle)
{
    long sensing_us_time;
    int sensing_ms_time;
    int ps_intt, ps_adc, ps_cycle;

    ps_intt = ps_intt_value[intt>>1];
    ps_adc = adc_value[adc>>3];
    ps_cycle = cycle_value[cycle];
#if COMMON_DEBUG
    LOG_INFO("PS: INTT=%d, ADC=%d, Cycle=%d \r\n", ps_intt, ps_adc, ps_cycle);
#endif

    sensing_us_time = (ps_intt*3 + ps_adc*2*2) * ps_cycle;
    sensing_ms_time = sensing_us_time / 1000;
    if(epl_sensor.osc_sel == EPL_OSC_SEL_4MHZ)
    {
        sensing_ms_time = sensing_ms_time/4;
    }
#if COMMON_DEBUG
    LOG_INFO("[%s]: sensing=%d ms\r\n", __func__, sensing_ms_time);
#endif
    return (sensing_ms_time + 5);
}

static int epl_sensor_get_wait_time(int ps_time, int als_time)
{
    int wait_idx = 0;
    int wait_time = 0;

    wait_time = als_time - ps_time;
    if(wait_time < 0){
        wait_time = 0;
    }
#if COMMON_DEBUG
    LOG_INFO("[%s]: wait_len = %d \r\n", __func__, wait_len);
#endif
    for(wait_idx = 0; wait_idx < wait_len; wait_idx++)
	{
	    if(wait_time < wait_value[wait_idx])
	    {
	        break;
	    }
	}
	if(wait_idx >= wait_len){
        wait_idx = wait_len - 1;
	}

#if COMMON_DEBUG
	LOG_INFO("[%s]: wait_idx = %d, wait = %d \r\n", __func__, wait_idx, wait_value[wait_idx]);
#endif
	return (wait_idx << 4);
}



static int irq_wake_flag = 0;
static bool ps_dyn_index = false;
void epl_sensor_update_mode(struct i2c_client *client)
{
	struct epl_sensor_priv *epld = epl_sensor_obj;
	int als_time = 0, ps_time = 0;
	u8 last_mode = epl_sensor.mode;
	bool enable_ps = epld->enable_pflag==1 && epld->ps_suspend==0;
    bool enable_als = epld->enable_lflag==1 && epld->als_suspend==0;

#if PS_DYN_K_ENABLE
	uint16_t low_thre;
	uint16_t high_thre;
#endif

	als_time = als_sensing_time(epl_sensor.als.integration_time, epl_sensor.als.adc, epl_sensor.als.cycle);
    ps_time = ps_sensing_time(epl_sensor.ps.integration_time, epl_sensor.ps.adc, epl_sensor.ps.cycle);
#if 0 //20141013 by ices
    if(epl_sensor.mode ==( (enable_als << 1) | enable_ps))
    return;
#endif
    polling_flag = false;
    //msleep(als_time+ps_time+wait_value[epl_sensor.wait>>4]);

	LOG_INFO("mode selection =0x%x\n", enable_ps | (enable_als << 1));
    if((last_mode == EPL_MODE_IDLE)&&((enable_als)||(enable_ps)))
        {
           disable_irq(client->irq);
           if(epl_sensor.power_on_index== EPL_POWER_OFF)
           {
               elan_power_on(epld, true);
	       epl_sensor.power_on_index = EPL_POWER_ON;
           }
	   msleep(10);
           initial_global_variable(client, epld);
           enable_irq(client->irq);
        LOG_INFO("[%s]: power on \r\n", __func__);
        }
    //**** mode selection ****
	switch( (enable_als << 1) | enable_ps)
	{
		case 0: //disable all
			epl_sensor.mode = EPL_MODE_IDLE;
		break;

		case 1: //als = 0, ps = 1
			epl_sensor.mode = EPL_MODE_PS;
		break;

		case 2: //als = 1, ps = 0
			epl_sensor.mode = EPL_MODE_ALS;
		break;

		case 3: //als = 1, ps = 1
			epl_sensor.mode = EPL_MODE_ALS_PS;
		break;
	}
#if 0   //20141013 by ices
    if(last_mode == epl_sensor.mode)
    {
       LOG_INFO("[%s]: mode is the same %d\r\n", __func__,last_mode);
       epl_sensor_restart_polling();
        return;
    }
#endif
#if POCKET_MODE
    if(!enable_ps)
    {
        LOG_INFO("[%s]:1:irq_wake_flag=%d, enable_ps=%d\r\n", __func__, irq_wake_flag, enable_ps);
        if(irq_wake_flag == 0)
        {
            irq_set_irq_wake(epld->irq, 0);
            irq_wake_flag = 1;
            disable_irq(epld->irq);
        }
    }
#endif
	//**** write setting ****
	// step 1. mask platform interrupt
	// step 2. set sensor at idle mode
	// step 3. uplock als / ps status
	// step 4. set interrupt enable / disable
	// step 5. set sensor at operation mode
    epl_sensor_I2C_Write(client, 0x11, EPL_RESETN_RESET | EPL_POWER_OFF);   //ices add

	epl_sensor_I2C_Write(epld->client,0x00,epl_sensor.wait | EPL_MODE_IDLE);

#ifndef  PS_DYN_K_ENABLE
       set_psensor_intr_threshold(epl_sensor.ps.low_threshold, epl_sensor.ps.high_threshold);
#endif
	//LOG_INFO("chenlj2 piht=%d pilt=%d\n", epl_sensor.ps.high_threshold,epl_sensor.ps.low_threshold);

    epl_sensor_I2C_Write(client,0x06, epl_sensor.interrupt_control | epl_sensor.ps.persist |epl_sensor.ps.interrupt_type);
#if 0
    //PS unlock and run
    epl_sensor.ps.compare_reset = EPL_CMP_RUN;
	epl_sensor.ps.lock = EPL_UN_LOCK;
	epl_sensor_I2C_Write(client,0x1b, epl_sensor.ps.compare_reset |epl_sensor.ps.lock);
#else
    epl_sensor.ps.compare_reset = EPL_CMP_RESET;
	epl_sensor.ps.lock = EPL_UN_LOCK;
	epl_sensor_I2C_Write(client,0x1b, epl_sensor.ps.compare_reset |epl_sensor.ps.lock);
#endif
    //ALS reset and run, clear ALS compare
    epl_sensor.als.compare_reset = EPL_CMP_RESET;
	epl_sensor.als.lock = EPL_UN_LOCK;
	epl_sensor_I2C_Write(epld->client,0x12, epl_sensor.als.compare_reset | epl_sensor.als.lock);

    //ALS unlock and run
   	epl_sensor.als.compare_reset = EPL_CMP_RUN;
	epl_sensor.als.lock = EPL_UN_LOCK;
	epl_sensor_I2C_Write(client,0x12, epl_sensor.als.compare_reset | epl_sensor.als.lock);

	epl_sensor_I2C_Write(client,0x07, epl_sensor.als.interrupt_channel_select | epl_sensor.als.persist | epl_sensor.als.interrupt_type);

#if DYN_INTT
    if(epl_sensor.als.report_type == CMC_BIT_DYN_INT){
	    epl_sensor_I2C_Write(client, 0x01, epl_sensor.als.integration_time | epl_sensor.als.gain);
    }
#endif

    if(epl_sensor.mode == EPL_MODE_ALS_PS && epl_sensor.als.polling_mode == 0 && epl_sensor.ps.polling_mode == 0){
        int wait = 0;
        wait = epl_sensor_get_wait_time(ps_time, als_time);
        epl_sensor_I2C_Write(client, 0x00, wait | epl_sensor.mode);
    }
    else{
        epl_sensor_I2C_Write(client, 0x00, epl_sensor.wait | epl_sensor.mode);
    }


    epl_sensor_I2C_Write(client, 0x11, epl_sensor.reset | epl_sensor.power);

#if COMMON_DEBUG
	//**** check setting ****
    LOG_INFO("[%s] PS:low_thd = %d, high_thd = %d \n",__func__, epl_sensor.ps.low_threshold, epl_sensor.ps.high_threshold);

    LOG_INFO("[%s] ALS:low_thd = %d, high_thd = %d \n",__func__, epl_sensor.als.low_threshold, epl_sensor.als.high_threshold);

	LOG_INFO("[%s] reg0x00= 0x%x, (0x%x)\n", __FUNCTION__, epl_sensor.wait | epl_sensor.mode);

	LOG_INFO("[%s] reg0x07= 0x%x, (0x%x)\n", __FUNCTION__ , epl_sensor.als.interrupt_channel_select | epl_sensor.als.persist | epl_sensor.als.interrupt_type);

	LOG_INFO("[%s] reg0x06= 0x%x, (0x%x)\n", __FUNCTION__ , epl_sensor.interrupt_control | epl_sensor.ps.persist |epl_sensor.ps.interrupt_type);

	LOG_INFO("[%s] reg0x11= 0x%x, (0x%d) \n", __FUNCTION__ , epl_sensor.power | epl_sensor.reset);

	LOG_INFO("[%s] reg0x12= 0x%x, (0x%x)\n", __FUNCTION__ , epl_sensor.als.compare_reset | epl_sensor.als.lock);

	LOG_INFO("[%s] reg0x1b= 0x%x, (0x%x)\n", __FUNCTION__ , epl_sensor.ps.compare_reset | epl_sensor.ps.lock);
#endif

    if(enable_ps == 1 && enable_als == 0)
    {
        msleep(ps_time);
 #if COMMON_DEBUG
        LOG_INFO("[%s] PS only(%dms)\r\n", __func__, ps_time);
 #endif
    }
    else if (enable_ps == 0 && enable_als == 1)
    {
        msleep(als_time);
 #if COMMON_DEBUG
        LOG_INFO("[%s] ALS only(%dms)\r\n", __func__, als_time);
 #endif
    }
    else if(enable_ps == 1 && enable_als == 1 && epl_sensor.als.polling_mode == 0 && epl_sensor.ps.polling_mode == 0)
    {
        int wait = 0;
        wait = epl_sensor_get_wait_time(ps_time, als_time);
        msleep(ps_time+als_time+wait_value[wait>>4]);
#if COMMON_DEBUG
        LOG_INFO("[%s] PS+ALS intr(%dms)\r\n", __func__, ps_time+als_time+wait_value[wait>>4]);
#endif
    }
    else if(enable_ps == 1 && enable_als == 1)
    {
        msleep(ps_time+als_time);
#if COMMON_DEBUG
        LOG_INFO("[%s] PS+ALS(%dms)\r\n", __func__, ps_time+als_time);
#endif
    }

#if POCKET_MODE

    if(enable_ps)
    {

#if PS_DYN_K_ENABLE
       if(!ps_dyn_index)
       	{
   	       // mutex_lock(&sensor_mutex);
   	        epl_sensor_read_ps_status(epld->client);
	        epl_sensor_read_ps(epld->client);
	      //  mutex_unlock(&sensor_mutex);
    		if(epl_sensor.ps.data.data < (epl_sensor.ps.high_threshold+4000) && (epl_sensor.ps.saturation == 0))
    		{
    		    LOG_INFO("[%s]: epl_sensor.ps.data.data=%d \r\n", __func__, epl_sensor.ps.data.data);
    			high_thre = epl_sensor.ps.data.data + PS_DYN_H_OFFSET;
    			low_thre = epl_sensor.ps.data.data + PS_DYN_L_OFFSET;
    			set_psensor_intr_threshold(low_thre, high_thre);
	       }
		else
	        {
	          if((epl_sensor.ps.high_threshold ==0)||(epl_sensor.ps.low_threshold==0))
	          {
	                LOG_INFO("[%s]:threshold is err; epl_sensor.ps.data.data=%d, ps_time=%d, als_time=%d\r\n", __func__, epl_sensor.ps.data.data, ps_time, als_time);
		        epl_sensor.ps.high_threshold = PS_HIGH_THRESHOLD;
                        epl_sensor.ps.low_threshold = PS_LOW_THRESHOLD;
	          }
	          set_psensor_intr_threshold(epl_sensor.ps.low_threshold, epl_sensor.ps.high_threshold);
	        }
		//restart chip
		epl_sensor_I2C_Write(client, 0x11, EPL_RESETN_RESET | EPL_POWER_OFF);  
		epl_sensor_I2C_Write(client, 0x11, epl_sensor.reset | epl_sensor.power);
	        //msleep(ps_time);
	       ps_dyn_index =true;
       	}
#endif

        if(irq_wake_flag == 1)
        {
            enable_irq(epld->irq);
            irq_set_irq_wake(epld->irq, 1);
            irq_wake_flag = 0;
        }
    }

   // enable_irq(epld->irq);
	//if(enable_ps && (epl_sensor.ps.polling_mode == false))
	if(pkt_flag == true)
	{
#if 0
		epl_sensor.ps.compare_reset = EPL_CMP_RESET;
		epl_sensor.ps.lock = EPL_UN_LOCK;
		epl_sensor_I2C_Write(client,0x1b, epl_sensor.ps.compare_reset |epl_sensor.ps.lock);
#endif
		epl_sensor.ps.compare_reset = EPL_CMP_RUN;
		epl_sensor.ps.lock = EPL_UN_LOCK;
		epl_sensor_I2C_Write(client,0x1b, epl_sensor.ps.compare_reset |epl_sensor.ps.lock);
 #if COMMON_DEBUG
        LOG_INFO("[%s] reset interrupt\r\n", __func__);
 #endif
	}
#endif
    //epl_sensor_I2C_Write(client, 0x11, epl_sensor.reset | epl_sensor.power);    //ices add

	//**** start ps\als polling schedule *****
	if((enable_als && epl_sensor.als.polling_mode) || (enable_ps && epl_sensor.ps.polling_mode)){
        epl_sensor_restart_polling();
	}
    polling_flag = true;

     if(epl_sensor.mode == EPL_MODE_IDLE)
     {
            if(epl_sensor.power_on_index == EPL_POWER_ON)
            {
                  elan_power_on(epld, false);
	          epl_sensor.power_on_index = EPL_POWER_OFF;
            }
           LOG_INFO("[%s]: power off \r\n", __func__);
     }

}

/*----------------------------------------------------------------------------*/
static void epl_sensor_polling_work(struct work_struct *work)
{

    struct epl_sensor_priv *epld = epl_sensor_obj;
    struct i2c_client *client = epld->client;

    bool enable_ps = epld->enable_pflag==1 && epld->ps_suspend==0;
    bool enable_als = epld->enable_lflag==1 && epld->als_suspend==0;

    int als_time, ps_time;
  als_time = als_sensing_time(epl_sensor.als.integration_time, epl_sensor.als.adc, epl_sensor.als.cycle);
    ps_time = ps_sensing_time(epl_sensor.ps.integration_time, epl_sensor.ps.adc, epl_sensor.ps.cycle);

    //LOG_INFO("enable_pflag = %d, enable_lflag = %d \n", enable_ps, enable_als);
    if(polling_flag == false)
    {
        LOG_INFO("[%s]: epl_sensor_update_mode is running!\r\n", __func__);
        return;
    }

    cancel_delayed_work(&polling_work);

    if((enable_als &&  epl_sensor.als.polling_mode == 1) || (enable_ps &&  epl_sensor.ps.polling_mode == 1))
    {
        int polling_rate = als_time+ps_time+wait_value[epl_sensor.wait>>4];
        if(polling_rate < 200){
            polling_rate = 200;
        }
        //queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(50));
        queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(polling_rate));
	 #if COMMON_DEBUG
        LOG_INFO("[%s]: polling rate: %dms \r\n", __func__, polling_rate);
	 #endif
    }


    if(enable_als &&  epl_sensor.als.polling_mode == 1)
    {
	    int report_lux = 0;
	    mutex_lock(&sensor_mutex);
	    epl_sensor_read_als(client);
	    report_lux = epl_sensor_get_als_value(epld, epl_sensor.als.data.channels[1]);
	    mutex_unlock(&sensor_mutex);
#if DYN_INTT
        if(epl_sensor.als.report_type != CMC_BIT_DYN_INT){
            epl_sensor_report_lux(report_lux);
        }
#else
        epl_sensor_report_lux(report_lux);
#endif
    }
    if(enable_ps && epl_sensor.ps.polling_mode == 1)
    {
        epl_sensor_read_ps_status(client);
        epl_sensor_read_ps(client);
        epl_sensor_report_ps_status();


    }

    if(enable_als==false && enable_ps==false)
    {
        cancel_delayed_work(&polling_work);
	 #if COMMON_DEBUG
        LOG_INFO("disable sensor\n");
	 #endif
    }

}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static irqreturn_t epl_sensor_eint_func(int irqNo, void *handle)
{
    struct epl_sensor_priv *epld = (struct epl_sensor_priv*)handle;

    wake_lock_timeout(&ps_lock,HZ);
    disable_irq_nosync(epld->irq);
    queue_work(epld->epl_wq, &epl_sensor_irq_work);

    return IRQ_HANDLED;
}
/*----------------------------------------------------------------------------*/
static void epl_sensor_intr_als_report_lux(void)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;


    int report_lux = 0;
	epl_sensor_read_als(epld->client);

	report_lux = epl_sensor_get_als_value(epld, epl_sensor.als.data.channels[1]);
#if DYN_INTT
    if(epl_sensor.als.report_type != CMC_BIT_DYN_INT){
        epl_sensor_report_lux(report_lux);
    }
#else
    epl_sensor_report_lux(report_lux);
#endif
    epl_sensor.als.compare_reset = EPL_CMP_RESET;
	epl_sensor.als.lock = EPL_UN_LOCK;
	epl_sensor_I2C_Write(epld->client,0x12, epl_sensor.als.compare_reset | epl_sensor.als.lock);

	//set dynamic threshold
	if(epl_sensor.als.compare_high >> 4)
	{
		epl_sensor.als.high_threshold = epl_sensor.als.high_threshold + 250;
		epl_sensor.als.low_threshold = epl_sensor.als.low_threshold + 250;

		if (epl_sensor.als.high_threshold > 60000)
		{
			epl_sensor.als.high_threshold = epl_sensor.als.high_threshold -250;
			epl_sensor.als.low_threshold = epl_sensor.als.low_threshold - 250;
		}
	}
	if(epl_sensor.als.compare_low>> 3)
	{
		epl_sensor.als.high_threshold = epl_sensor.als.high_threshold -250;
		epl_sensor.als.low_threshold = epl_sensor.als.low_threshold - 250;

		if (epl_sensor.als.high_threshold < 250)
		{
			epl_sensor.als.high_threshold = epl_sensor.als.high_threshold + 250;
			epl_sensor.als.low_threshold = epl_sensor.als.low_threshold + 250;
		}
	}

	if(epl_sensor.als.high_threshold < epl_sensor.als.low_threshold)
	{
	    //LOG_INFO("[%s]:recover default setting \r\n", __FUNCTION__);
	    epl_sensor.als.high_threshold = ALS_HIGH_THRESHOLD;
	    epl_sensor.als.low_threshold = ALS_LOW_THRESHOLD;
	}

	//write new threshold
	set_lsensor_intr_threshold(epl_sensor.als.low_threshold, epl_sensor.als.high_threshold);
}
/*----------------------------------------------------------------------------*/
static void epl_sensor_eint_work(struct work_struct *work)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;

	bool enable_ps = epld->enable_pflag==1 && epld->ps_suspend==0;
    bool enable_als = epld->enable_lflag==1 && epld->als_suspend==0;

	//LOG_INFO("xxxxxxxxxxx\n\n");
    mutex_lock(&sensor_mutex);
	if(enable_ps && epl_sensor.ps.polling_mode == 0)
	{
	    epl_sensor_read_ps_status(epld->client);
		if(epl_sensor.ps.interrupt_flag == EPL_INT_TRIGGER)
		{
			epl_sensor_read_ps(epld->client);

			epl_sensor_report_ps_status();

			//PS unlock and run
			epl_sensor.ps.compare_reset = EPL_CMP_RUN;
			epl_sensor.ps.lock = EPL_UN_LOCK;
			epl_sensor_I2C_Write(epld->client,0x1b, epl_sensor.ps.compare_reset |epl_sensor.ps.lock);
		}
	}
	if(enable_als && epl_sensor.als.polling_mode == 0)
	{
	    epl_sensor_read_als_status(epld->client);
    	if(epl_sensor.als.interrupt_flag == EPL_INT_TRIGGER)
    	{
            epl_sensor_intr_als_report_lux();
            //ALS unlock and run
    		epl_sensor.als.compare_reset = EPL_CMP_RUN;
    		epl_sensor.als.lock = EPL_UN_LOCK;
    		epl_sensor_I2C_Write(epld->client,0x12, epl_sensor.als.compare_reset | epl_sensor.als.lock);
        }
	}

	enable_irq(epld->irq);
	mutex_unlock(&sensor_mutex);
}

/*----------------------------------------------------------------------------*/
static int epl_sensor_setup_interrupt(struct epl_sensor_priv *epld)
{
    struct i2c_client *client = epld->client;

    int err = 0;
    //msleep(5);

    unsigned int irq_gpio;
    unsigned int irq_gpio_flags;
    struct device_node *np = client->dev.of_node;

    irq_gpio = of_get_named_gpio_flags(np, "epl,irq-gpio",
                             0, &irq_gpio_flags);
    epld->intr_pin = irq_gpio;
    if (irq_gpio < 0) {
        gpio_free(epld->intr_pin);
        goto initial_fail;
    }
    if (gpio_is_valid(irq_gpio)) {
        err = gpio_request(irq_gpio, "epl_irq_gpio");
        if (err) {
            LOG_ERR( "irq gpio request failed");
        }

        err = gpio_direction_input(irq_gpio);
        if (err) {
            LOG_ERR("set_direction for irq gpio failed\n");
        }
    }
    err = request_irq(epld->irq,epl_sensor_eint_func, IRQF_TRIGGER_FALLING,
                      client->dev.driver->name, epld);
    if(err <0)
    {
        LOG_ERR("request irq pin %d fail for gpio\n",err);
        goto fail_free_intr_pin;
    }

    return err;

initial_fail:
fail_free_intr_pin:
    gpio_free(epld->intr_pin);
//    free_irq(epld->irq, epld);
    return err;
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/

static ssize_t epl_sensor_show_reg(struct device *dev, struct device_attribute *attr, char *buf)
{

    ssize_t len = 0;
    struct i2c_client *client = epl_sensor_obj->client;

    if(!epl_sensor_obj)
    {
        LOG_ERR("epl_obj is null!!\n");
        return 0;
    }

    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x00 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x00));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x01 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x01));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x02 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x02));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x03 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x03));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x04 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x04));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x05 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x05));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x06 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x06));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x07 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x07));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x08 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x08));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x09 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x09));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x0A value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x0A));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x0B value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x0B));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x0C value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x0C));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x0D value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x0D));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x0E value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x0E));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x0F value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x0F));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x11 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x11));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x12 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x12));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x1B value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x1B));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x22 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x22));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x23 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x23));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x24 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x24));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x25 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x25));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0xFB value = 0x%x\n", i2c_smbus_read_byte_data(client, 0xFB));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0xFC value = 0x%x\n", i2c_smbus_read_byte_data(client, 0xFC));

    return len;

}

/*----------------------------------------------------------------------------*/
static ssize_t epl_sensor_show_status(struct device *dev, struct device_attribute *attr, char *buf)
{
    ssize_t len = 0;
    struct epl_sensor_priv *epld = epl_sensor_obj;
    bool enable_ps = epld->enable_pflag==1 && epld->ps_suspend==0;
    bool enable_als = epld->enable_lflag==1 && epld->als_suspend==0;

    if(!epl_sensor_obj)
    {
        LOG_ERR("epl_sensor_obj is null!!\n");
        return 0;
    }

     len += snprintf(buf+len, PAGE_SIZE-len, "als/ps polling is %d-%d\n", epl_sensor.als.polling_mode, epl_sensor.ps.polling_mode);
     len += snprintf(buf+len, PAGE_SIZE-len, "wait = %d, mode = %d\n",epl_sensor.wait >> 4, epl_sensor.mode);
     len += snprintf(buf+len, PAGE_SIZE-len, "interrupt control = %d\n", epl_sensor.interrupt_control >> 4);

     if(enable_ps)
     {
        len += snprintf(buf+len, PAGE_SIZE-len, ">>>>>>>>>>>>>> ps <<<<<<<<<<<<<<< \n");
        len += snprintf(buf+len, PAGE_SIZE-len, "INTEG = %d, gain = %d\n", epl_sensor.ps.integration_time >> 1, epl_sensor.ps.gain);
        len += snprintf(buf+len, PAGE_SIZE-len, "adc = %d, cycle = %d, ir drive = %d\n", epl_sensor.ps.adc >> 3, epl_sensor.ps.cycle, epl_sensor.ps.ir_driver);
        len += snprintf(buf+len, PAGE_SIZE-len, "saturation = %d, int flag = %d\n", epl_sensor.ps.saturation >> 5, epl_sensor.ps.interrupt_flag >> 2);
        len += snprintf(buf+len, PAGE_SIZE-len, "pals data = %d, data = %d\n", epl_sensor.ps.data.ir_data, epl_sensor.ps.data.data);
     }
     if(enable_als)
     {
        len += snprintf(buf+len, PAGE_SIZE-len, ">>>>>>>>>>>>>> als <<<<<<<<<<<<<<< \n");
        len += snprintf(buf+len, PAGE_SIZE-len, "INTEG = %d, gain = %d\n", epl_sensor.als.integration_time >> 1, epl_sensor.als.gain);
        len += snprintf(buf+len, PAGE_SIZE-len, "adc = %d, cycle = %d\n", epl_sensor.als.adc >> 3, epl_sensor.als.cycle);
        len += snprintf(buf+len, PAGE_SIZE-len, "ch0 = %d, ch1 = %d\n", epl_sensor.als.data.channels[0], epl_sensor.als.data.channels[1]);
     }

    return len;
}
static ssize_t epl_sensor_show_als_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
     struct epl_sensor_priv *epld = epl_sensor_obj;

     return sprintf(buf, "%d\n", epld->enable_lflag);
}
static int epl_enable_ps_sensor(struct i2c_client *client, int val)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;

     if(epld->enable_pflag != val)
    {
	epld->enable_pflag = val;
	
	#if POCKET_MODE
        if(epld->enable_pflag == true)
        {
            pkt_flag = true;
        }
	#endif
	epl_sensor_update_mode(epld->client);
    }
    if(!val)
    {
         input_report_abs(epld->ps_input_dev, ABS_DISTANCE, -1);
	  input_sync(epld->ps_input_dev);
    }
    return 0;
}
static int epl_enable_als_sensor(struct i2c_client *client, int val)
{
     struct epl_sensor_priv *epld = epl_sensor_obj;

     if(epld->enable_lflag != val)
    {
#if DYN_INTT
        if(epl_sensor.als.report_type == CMC_BIT_DYN_INT)
        {
            als_report_count = 0;
            dynamic_intt_idx = dynamic_intt_init_idx;
            epl_sensor.als.integration_time = als_dynamic_intt_intt[dynamic_intt_idx];
            epl_sensor.als.gain = als_dynamic_intt_gain[dynamic_intt_idx];
            dynamic_intt_high_thr = als_dynamic_intt_high_thr[dynamic_intt_idx];
            dynamic_intt_low_thr = als_dynamic_intt_low_thr[dynamic_intt_idx];
        }
#endif
	    epld->enable_lflag = val;

	epl_sensor_update_mode(epld->client);
    }
    if(!val)
    {
         input_report_abs(epld->als_input_dev, ABS_MISC, -1);
    	  input_sync(epld->als_input_dev);
    }
    return 0;
}
static int epl_als_set_enable(struct sensors_classdev *sensors_cdev,
		unsigned int enable)
{
	struct epl_sensor_priv *data = container_of(sensors_cdev,
			struct epl_sensor_priv, als_cdev);

	if ((enable != 0) && (enable != 1)) {
		pr_err("%s: invalid value(%d)\n", __func__, enable);
		return -EINVAL;
	}

	return epl_enable_als_sensor(data->client, enable);
}

static int epl_ps_set_enable(struct sensors_classdev *sensors_cdev,
		unsigned int enable)
{
	struct epl_sensor_priv *data = container_of(sensors_cdev,
			struct epl_sensor_priv, ps_cdev);

	if ((enable != 0) && (enable != 1)) {
		pr_err("%s: invalid value(%d)\n", __func__, enable);
		return -EINVAL;
	}

	return epl_enable_ps_sensor(data->client, enable);
}
/*----------------------------------------------------------------------------*/
static ssize_t epl_sensor_store_als_enable(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint16_t mode=0;
    struct epl_sensor_priv *epld = epl_sensor_obj;
    LOG_FUN();


    sscanf(buf, "%hu",&mode);
    if(epld->enable_lflag != mode)
    {
#if DYN_INTT
        if(epl_sensor.als.report_type == CMC_BIT_DYN_INT)
        {
            als_report_count = 0;
            dynamic_intt_idx = dynamic_intt_init_idx;
            epl_sensor.als.integration_time = als_dynamic_intt_intt[dynamic_intt_idx];
            epl_sensor.als.gain = als_dynamic_intt_gain[dynamic_intt_idx];
            dynamic_intt_high_thr = als_dynamic_intt_high_thr[dynamic_intt_idx];
            dynamic_intt_low_thr = als_dynamic_intt_low_thr[dynamic_intt_idx];
        }
#endif
        epld->enable_lflag = mode;
        mutex_lock(&sensor_mutex);
        epl_sensor_update_mode(epld->client);
		mutex_unlock(&sensor_mutex);
    }
     if(!mode)
     {
         input_report_abs(epld->als_input_dev, ABS_MISC, -1);
    	  input_sync(epld->als_input_dev);
     }

    return count;
}

static ssize_t epl_sensor_show_ps_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
     struct epl_sensor_priv *epld = epl_sensor_obj;

     return sprintf(buf, "%d\n", epld->enable_pflag);
}

/*----------------------------------------------------------------------------*/
static ssize_t epl_sensor_store_ps_enable(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint16_t mode=0;
    struct epl_sensor_priv *epld = epl_sensor_obj;
    LOG_FUN();

    sscanf(buf, "%hu",&mode);
    if(epld->enable_pflag != mode)
    {
	epld->enable_pflag = mode;
#if POCKET_MODE
        if(epld->enable_pflag == true)
        {
            pkt_flag = true;
        }
	else
	{
	    ps_dyn_index =false;
	}
#endif
		mutex_lock(&sensor_mutex);
	    epl_sensor_update_mode(epld->client);
	    mutex_unlock(&sensor_mutex);
            if(!mode)
            {
                   input_report_abs(epld->ps_input_dev, ABS_DISTANCE, -1);
	            input_sync(epld->ps_input_dev);
             }
    }
    return count;
}

static ssize_t epl_show_piht(struct device *dev,
			struct device_attribute *attr, char *buf)
{			
	return sprintf(buf, "%d", epl_sensor.ps.high_threshold);
}
			
static ssize_t epl_store_piht(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	struct epl_sensor_priv *epld = epl_sensor_obj;
	unsigned long val = simple_strtoul(buf, NULL, 10);

	if(!epld)
	{
	    LOG_ERR("epl_sensor_obj is null!!\n");
	    return 0;
	}
        if(val)
	epl_sensor.ps.high_threshold = val;
	LOG_INFO("chenlj2 piht=%d val=%d\n", epl_sensor.ps.high_threshold,(int)val);

			
	//epl_set_piht(epl_sensor.ps.high_threshold);

	return count;
}


static ssize_t epl_sensor_store_ps_offset(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct epl_sensor_priv *epld = epl_sensor_obj;
    int cancelation;
	LOG_FUN();

	sscanf(buf, "%d",&cancelation);

    epl_sensor.ps.cancelation = cancelation;

	epl_sensor_I2C_Write(epld->client,0x22, (u8)(cancelation & 0xff));
	epl_sensor_I2C_Write(epld->client,0x23, (u8)((cancelation & 0xff00) >> 8));

	epl_sensor_I2C_Read(epld->client, 0x22, 2);
	LOG_INFO("[%s]: 0x22, 0x23 = 0x%x, 0x%x\n", __FUNCTION__, gRawData.raw_bytes[0], gRawData.raw_bytes[1]);
	epl_sensor_update_mode(epld->client);

    return count;
}

static ssize_t epl_show_pilt(struct device *dev,
			struct device_attribute *attr, char *buf)
{			
	return sprintf(buf, "%d", epl_sensor.ps.low_threshold);
}
			
static ssize_t epl_store_pilt(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	struct epl_sensor_priv *epld = epl_sensor_obj;
	unsigned long val = simple_strtoul(buf, NULL, 10);

	if(!epld)
	{
	    LOG_ERR("epl_sensor_obj is null!!\n");
	    return 0;
	}
        if(val)
	epl_sensor.ps.low_threshold = val;
	LOG_INFO("chenlj2 pilt=%d val=%d\n", epl_sensor.ps.low_threshold,(int)val);
			
	//epl_set_pilt(epl_sensor.ps.low_threshold);

	return count;
}


/*----------------------------------------------------------------------------*/
static ssize_t epl_sensor_store_integration(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int value=0;

	struct epl_sensor_priv *epld = epl_sensor_obj;

	LOG_FUN();

	sscanf(buf, "%d",&value);

	switch (epl_sensor.mode)
	{
		case EPL_MODE_PS: //ps
			epl_sensor.ps.integration_time = (value & 0x1f) << 1;
			epl_sensor_I2C_Write(epld->client,0x03, epl_sensor.ps.integration_time | epl_sensor.ps.gain);
			epl_sensor_I2C_Read(epld->client, 0x03, 1);
			LOG_INFO("[%s]: 0x03 = 0x%x (0x%x)\n", __FUNCTION__, epl_sensor.ps.integration_time | epl_sensor.ps.gain, gRawData.raw_bytes[0]);
		break;

		case EPL_MODE_ALS: //als
			epl_sensor.als.integration_time = (value & 0x1f) << 1;
			epl_sensor_I2C_Write(epld->client,0x01, epl_sensor.als.integration_time | epl_sensor.als.gain);
			epl_sensor_I2C_Read(epld->client, 0x01, 1);
			LOG_INFO("[%s]: 0x01 = 0x%x (0x%x)\n", __FUNCTION__, epl_sensor.als.integration_time | epl_sensor.als.gain, gRawData.raw_bytes[0]);
		break;
	}

	epl_sensor_update_mode(epld->client);
	return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl_sensor_store_threshold(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct epl_sensor_priv *epld = epl_sensor_obj;
    int hthr = 0, lthr = 0;
	if(!epld)
	{
	    LOG_ERR("epl_sensor_obj is null!!\n");
	    return 0;
	}

	switch(epl_sensor.mode)
	{
		case EPL_MODE_PS:
			sscanf(buf, "%d,%d", &lthr, &hthr);
			epl_sensor.ps.low_threshold = lthr;
			epl_sensor.ps.high_threshold = hthr;
			set_psensor_intr_threshold(epl_sensor.ps.low_threshold, epl_sensor.ps.high_threshold);
		break;

		case EPL_MODE_ALS:
			sscanf(buf, "%d,%d", &lthr, &hthr);
			epl_sensor.als.low_threshold = lthr;
			epl_sensor.als.high_threshold = hthr;
			set_lsensor_intr_threshold(epl_sensor.als.low_threshold, epl_sensor.als.high_threshold);
		break;

	}
	return count;
}


static ssize_t epl_show_type(struct device *dev,
                struct device_attribute *attr, char *buf){
     struct epl_sensor_priv *epld = epl_sensor_obj;

    return sprintf(buf, "%d\n", epld->type);
}
static ssize_t epl_show_pdata(struct device *dev,
			struct device_attribute *attr, char *buf)
{
      struct epl_sensor_priv *epld = epl_sensor_obj;
      LOG_FUN();


      if(polling_flag == true && epl_sensor.ps.polling_mode == 0)
      {
        msleep(10);
	mutex_lock(&sensor_mutex);
        epl_sensor_read_ps(epld->client);
        mutex_unlock(&sensor_mutex);
      }
      return sprintf(buf, "%d",epl_sensor.ps.data.data);
	  
}

static ssize_t epl_show_als_data(struct device *dev, struct device_attribute *attr, char *buf)
{
    //struct epl_sensor_priv *epld = epl_sensor_obj;
    LOG_FUN();
#if DYN_INTT

    LOG_INFO("[%s]: dynamic_intt_lux = %d \r\n", __func__, dynamic_intt_lux);
    return sprintf(buf, "%d",dynamic_intt_lux);

#else
    if(polling_flag == true && epl_sensor.als.polling_mode == 0)
    {
        mutex_lock(&sensor_mutex);
	    epl_sensor_read_als(epld->client);
	    mutex_unlock(&sensor_mutex);
	}
    return sprintf(buf, "%d",epl_sensor.als.data.channels[1]);
#endif
}

#if DYN_INTT
static ssize_t epl_sensor_store_c_gain(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int c;
    LOG_FUN();

    sscanf(buf, "%d",&c);

    c_gain = c;

    LOG_INFO("c_gain = %d \r\n", c_gain);

	return count;
}
#endif

/*----------------------------------------------------------------------------*/

static DEVICE_ATTR(elan_status,					S_IWUSR | S_IRUGO, epl_sensor_show_status,  	  		NULL										);
static DEVICE_ATTR(elan_reg,    					S_IWUSR | S_IRUGO, epl_sensor_show_reg,   				NULL										);				
static DEVICE_ATTR(set_threshold,     				S_IWUSR | S_IRUGO, NULL,   					 		epl_sensor_store_threshold						);
static DEVICE_ATTR(enable_als_sensor,					S_IWUSR | S_IWGRP | S_IRUGO, epl_sensor_show_als_enable,   							epl_sensor_store_als_enable					);
static DEVICE_ATTR(enable_ps_sensor,				S_IWUSR | S_IWGRP | S_IRUGO, epl_sensor_show_ps_enable,   							epl_sensor_store_ps_enable					);						
static DEVICE_ATTR(piht, S_IWUSR | S_IWGRP | S_IRUGO, epl_show_piht, epl_store_piht);//Add for EngineerMode
static DEVICE_ATTR(pilt, S_IWUSR | S_IWGRP | S_IRUGO, epl_show_pilt, epl_store_pilt);//Add for EngineerMode
static DEVICE_ATTR(pdata, S_IWUSR | S_IWGRP | S_IRUGO, epl_show_pdata, NULL);
static DEVICE_ATTR(als_data, S_IWUSR | S_IWGRP | S_IRUGO, epl_show_als_data, NULL);
static DEVICE_ATTR(type, S_IWUSR | S_IRUGO, epl_show_type, NULL);//Add for EngineerMode
static DEVICE_ATTR(integration,					S_IWUSR  | S_IWGRP, NULL,								epl_sensor_store_integration					);
static DEVICE_ATTR(ps_offset,						S_IWUSR  | S_IWGRP, NULL,								epl_sensor_store_ps_offset						);
#if DYN_INTT
static DEVICE_ATTR(c_gain, 						S_IWUSR | S_IRUGO, NULL, 	  		epl_sensor_store_c_gain);
#endif

/*----------------------------------------------------------------------------*/
static struct attribute *epl_sensor_attr_list[] =
{
    &dev_attr_elan_status.attr,
    &dev_attr_elan_reg.attr,
    &dev_attr_enable_als_sensor.attr,
    &dev_attr_enable_ps_sensor.attr,
    &dev_attr_set_threshold.attr,
    &dev_attr_piht.attr,
    &dev_attr_pilt.attr,
    &dev_attr_pdata.attr,
    &dev_attr_als_data.attr,
    &dev_attr_type.attr,
    &dev_attr_integration.attr,
    &dev_attr_ps_offset.attr,
#if DYN_INTT
    &dev_attr_c_gain.attr,
#endif
      NULL
        };
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static struct attribute_group epl_sensor_attr_group =
{
    .attrs = epl_sensor_attr_list,
};
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static int epl_sensor_als_open(struct inode *inode, struct file *file)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;

    LOG_FUN();

    if (epld->als_opened)
    {
        return -EBUSY;
    }
    epld->als_opened = 1;

    return 0;
}
/*----------------------------------------------------------------------------*/
#if 0
/*----------------------------------------------------------------------------*/
static int epl_sensor_als_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;
    int buf[1];
    if(epld->read_flag ==1)
    {
#if DYN_INTT
        buf[0] = dynamic_intt_lux;
#else
        buf[0] = epl_sensor.als.data.channels[1];
#endif
        if(copy_to_user(buffer, &buf , sizeof(buf)))
            return 0;
        epld->read_flag = 0;
        return 12;
    }
    else
    {
        return 0;
    }
}
#endif
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static int epl_sensor_als_release(struct inode *inode, struct file *file)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;

    LOG_FUN();

    epld->als_opened = 0;

    return 0;
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static long epl_sensor_als_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int flag;
    unsigned long buf[1];
    struct epl_sensor_priv *epld = epl_sensor_obj;

    void __user *argp = (void __user *)arg;

    LOG_INFO("als io ctrl cmd %d\n", _IOC_NR(cmd));

    switch(cmd)
    {
        case ELAN_EPL8800_IOCTL_GET_LFLAG:

            LOG_INFO("elan ambient-light IOCTL Sensor get lflag \n");
            flag = epld->enable_lflag;
            if (copy_to_user(argp, &flag, sizeof(flag)))
                return -EFAULT;

            LOG_INFO("elan ambient-light Sensor get lflag %d\n",flag);
            break;

        case ELAN_EPL8800_IOCTL_ENABLE_LFLAG:

            LOG_INFO("elan ambient-light IOCTL Sensor set lflag \n");
            if (copy_from_user(&flag, argp, sizeof(flag)))
                return -EFAULT;
            if (flag < 0 || flag > 1)
                return -EINVAL;
            if(epld->enable_lflag != flag){
#if DYN_INTT
                if(epl_sensor.als.report_type == CMC_BIT_DYN_INT)
                {
                    als_report_count = 0;
                    dynamic_intt_idx = dynamic_intt_init_idx;
                    epl_sensor.als.integration_time = als_dynamic_intt_intt[dynamic_intt_idx];
                    epl_sensor.als.gain = als_dynamic_intt_gain[dynamic_intt_idx];
                    dynamic_intt_high_thr = als_dynamic_intt_high_thr[dynamic_intt_idx];
                    dynamic_intt_low_thr = als_dynamic_intt_low_thr[dynamic_intt_idx];
                }
#endif
                epld->enable_lflag = flag;
                epl_sensor_update_mode(epld->client);
            }

            LOG_INFO("elan ambient-light Sensor set lflag %d\n",flag);
            break;

        case ELAN_EPL8800_IOCTL_GETDATA:
#if DYN_INTT
            buf[0] = dynamic_intt_lux;
#else
            buf[0] = (unsigned long)epl_sensor.als.data.channels[1];
#endif
            if(copy_to_user(argp, &buf , sizeof(buf)))
                return -EFAULT;

            break;

        default:
            LOG_ERR("invalid cmd %d\n", _IOC_NR(cmd));
            return -EINVAL;
    }

    return 0;


}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static struct file_operations epl_sensor_als_fops =
{
    .owner = THIS_MODULE,
    .open = epl_sensor_als_open,
   // .read = epl_sensor_als_read,
    .release = epl_sensor_als_release,
    .unlocked_ioctl = epl_sensor_als_ioctl
};
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static struct miscdevice epl_sensor_als_device =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "elan_als",
    .fops = &epl_sensor_als_fops
};
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static int epl_sensor_ps_open(struct inode *inode, struct file *file)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;

    LOG_FUN();

    if (epld->ps_opened)
        return -EBUSY;

    epld->ps_opened = 1;

    return 0;
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static int epl_sensor_ps_release(struct inode *inode, struct file *file)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;

    LOG_FUN();

    epld->ps_opened = 0;

    return 0;
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static long epl_sensor_ps_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int value;
    int flag;
    struct epl_sensor_priv *epld = epl_sensor_obj;

    void __user *argp = (void __user *)arg;

    LOG_INFO("ps io ctrl cmd %d\n", _IOC_NR(cmd));

    //ioctl message handle must define by android sensor library (case by case)
    switch(cmd)
    {

        case ELAN_EPL8800_IOCTL_GET_PFLAG:

            LOG_INFO("elan Proximity Sensor IOCTL get pflag \n");
            flag = epld->enable_pflag;
            if (copy_to_user(argp, &flag, sizeof(flag)))
                return -EFAULT;

            LOG_INFO("elan Proximity Sensor get pflag %d\n",flag);
            break;

        case ELAN_EPL8800_IOCTL_ENABLE_PFLAG:
            LOG_INFO("elan Proximity IOCTL Sensor set pflag \n");
            if (copy_from_user(&flag, argp, sizeof(flag)))
                return -EFAULT;
            if (flag < 0 || flag > 1)
                return -EINVAL;

            if(epld->enable_pflag != flag)
            {
                epld->enable_pflag = flag;
#if POCKET_MODE
                if(epld->enable_pflag == true)
                {
                    pkt_flag = true;
                }
#endif
                epl_sensor_update_mode(epld->client);
            }

            LOG_INFO("elan Proximity Sensor set pflag %d\n",flag);
            break;

        case ELAN_EPL8800_IOCTL_GETDATA:

            value = epl_sensor.ps.data.data;
            if(copy_to_user(argp, &value , sizeof(value)))
                return -EFAULT;

            LOG_INFO("elan proximity Sensor get data (%d) \n",value);
            break;

        default:
            LOG_ERR("invalid cmd %d\n", _IOC_NR(cmd));
            return -EINVAL;
    }

    return 0;

}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static struct file_operations epl_sensor_ps_fops =
{
    .owner = THIS_MODULE,
    .open = epl_sensor_ps_open,
    .release = epl_sensor_ps_release,
    .unlocked_ioctl = epl_sensor_ps_ioctl
};
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static struct miscdevice epl_sensor_ps_device =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "elan_ps",
    .fops = &epl_sensor_ps_fops
};

/*----------------------------------------------------------------------------*/
static ssize_t light_enable_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct epl_sensor_priv *epld  = epl_sensor_obj;
#if ALS_DEBUG
	LOG_INFO("%s: ALS_status=%d\n", __func__, epld->enable_lflag);
#endif
	return sprintf(buf, "%d\n", epld->enable_lflag);
}

static ssize_t light_enable_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct epl_sensor_priv *epld = epl_sensor_obj;
    uint16_t als_enable = 0;
#if ALS_DEBUG
    LOG_INFO("light_enable_store: enable=%s \n", buf);
#endif

	sscanf(buf, "%hu",&als_enable);

    if(epld->enable_lflag != als_enable)
    {
#if DYN_INTT
        if(epl_sensor.als.report_type == CMC_BIT_DYN_INT)
        {
            als_report_count = 0;
            dynamic_intt_idx = dynamic_intt_init_idx;
            epl_sensor.als.integration_time = als_dynamic_intt_intt[dynamic_intt_idx];
            epl_sensor.als.gain = als_dynamic_intt_gain[dynamic_intt_idx];
            dynamic_intt_high_thr = als_dynamic_intt_high_thr[dynamic_intt_idx];
            dynamic_intt_low_thr = als_dynamic_intt_low_thr[dynamic_intt_idx];
        }
#endif
        epld->enable_lflag = als_enable;
        epl_sensor_update_mode(epld->client);
    }

	return size;
}
/*----------------------------------------------------------------------------*/
static struct device_attribute dev_attr_light_enable =
__ATTR(enable, S_IRWXUGO,
	   light_enable_show, light_enable_store);

static struct attribute *light_sysfs_attrs[] = {
	&dev_attr_light_enable.attr,
	NULL
};

static struct attribute_group light_attribute_group = {
	.attrs = light_sysfs_attrs,
};
/*----------------------------------------------------------------------------*/
static int epl_sensor_setup_lsensor(struct epl_sensor_priv *epld)
{
    int err = 0;
    LOG_INFO("epl_sensor_setup_lsensor enter.\n");

    epld->als_input_dev = input_allocate_device();
    if (!epld->als_input_dev)
    {
        LOG_ERR( "could not allocate ls input device\n");
        return -ENOMEM;
    }
    epld->als_input_dev->name = ElanALsensorName;
    set_bit(EV_ABS, epld->als_input_dev->evbit);
    input_set_abs_params(epld->als_input_dev, ABS_MISC, 0, 9, 0, 0);

    err = input_register_device(epld->als_input_dev);
    if (err < 0)
    {
        LOG_ERR("can not register ls input device\n");
        goto err_free_ls_input_device;
    }

    err = misc_register(&epl_sensor_als_device);
    if (err < 0)
    {
        LOG_ERR("can not register ls misc device\n");
        goto err_unregister_ls_input_device;
    }

    err = sysfs_create_group(&epld->als_input_dev->dev.kobj, &light_attribute_group);

	if (err) {
		pr_err("%s: could not create sysfs group\n", __func__);
		goto err_free_ls_input_device;
	}
    return err;


err_unregister_ls_input_device:
    input_unregister_device(epld->als_input_dev);
err_free_ls_input_device:
    input_free_device(epld->als_input_dev);
    return err;
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static ssize_t proximity_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct epl_sensor_priv *epld  = epl_sensor_obj;
#if PS_DEBUG
	LOG_INFO("%s: PS status=%d\n", __func__, epld->enable_pflag);
#endif
	return sprintf(buf, "%d\n", epld->enable_pflag);
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static ssize_t proximity_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;
    uint16_t ps_enable = 0;
#if PS_DEBUG
    LOG_INFO("proximity_enable_store: enable=%s \n", buf);
#endif

	sscanf(buf, "%hu",&ps_enable);

    if(epld->enable_pflag != ps_enable)
    {
        epld->enable_pflag = ps_enable;
#if POCKET_MODE
        if(epld->enable_pflag == true)
        {
            pkt_flag = true;
        }
#endif
        epl_sensor_update_mode(epld->client);
    }

	return size;
}
/*----------------------------------------------------------------------------*/
static struct device_attribute dev_attr_psensor_enable =
__ATTR(enable, S_IRWXUGO,
	   proximity_enable_show, proximity_enable_store);

static struct attribute *proximity_sysfs_attrs[] = {
	&dev_attr_psensor_enable.attr,
	NULL
};

static struct attribute_group proximity_attribute_group = {
	.attrs = proximity_sysfs_attrs,
};
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static int epl_sensor_setup_psensor(struct epl_sensor_priv *epld)
{
    int err = 0;
    LOG_INFO("epl_sensor_setup_psensor enter.\n");


    epld->ps_input_dev = input_allocate_device();
    if (!epld->ps_input_dev)
    {
        LOG_ERR("could not allocate ps input device\n");
        return -ENOMEM;
    }
    epld->ps_input_dev->name = ElanPsensorName;

    set_bit(EV_ABS, epld->ps_input_dev->evbit);
    input_set_abs_params(epld->ps_input_dev, ABS_DISTANCE, 0, 1, 0, 0);

    err = input_register_device(epld->ps_input_dev);
    if (err < 0)
    {
        LOG_ERR("could not register ps input device\n");
        goto err_free_ps_input_device;
    }

    err = misc_register(&epl_sensor_ps_device);
    if (err < 0)
    {
        LOG_ERR("could not register ps misc device\n");
        goto err_unregister_ps_input_device;
    }

    err = sysfs_create_group(&epld->ps_input_dev->dev.kobj, &proximity_attribute_group);

	if (err) {
		pr_err("%s: PS could not create sysfs group\n", __func__);
		goto err_free_ps_input_device;
	}

    return err;


err_unregister_ps_input_device:
    input_unregister_device(epld->ps_input_dev);
err_free_ps_input_device:
    input_free_device(epld->ps_input_dev);
    return err;
}
static  int epl_vdd_on = 0;
/*----------------------------------------------------------------------------*/
static int elan_power_on(struct epl_sensor_priv *data, bool on)
{
	int rc;

	if (!on)
		goto power_off;
       if(!epl_vdd_on )
       	{
		rc = regulator_enable(data->vdd);
		if (rc) {
			dev_err(&data->client->dev,
				"Regulator vdd enable failed rc=%d\n", rc);
			return rc;
		}
		epl_vdd_on = 1;
       	}

	rc = regulator_enable(data->vcc_i2c);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vcc_i2c enable failed rc=%d\n", rc);
		regulator_disable(data->vdd);
	}

	return rc;

power_off:
	rc = regulator_disable(data->vcc_i2c);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vcc_i2c disable failed rc=%d\n", rc);
		return rc;
	}
        #if 0
	rc = regulator_disable(data->vdd);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vdd disable failed rc=%d\n", rc);
	}
	#endif

	return rc;
}

static int elan_power_init(struct epl_sensor_priv *data, bool on)
{
	int rc;

	if (!on)
		goto pwr_deinit;

	data->vdd = regulator_get(&data->client->dev, "vdd");
	if (IS_ERR(data->vdd)) {
		rc = PTR_ERR(data->vdd);
		dev_err(&data->client->dev,
			"Regulator get failed vdd rc=%d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(data->vdd) > 0) {
		rc = regulator_set_voltage(data->vdd, FT_VTG_MIN_UV,
					   FT_VTG_MAX_UV);
		if (rc) {
			dev_err(&data->client->dev,
				"Regulator set_vtg failed vdd rc=%d\n", rc);
			goto reg_vdd_put;
		}
	}

	data->vcc_i2c = regulator_get(&data->client->dev, "vcc_i2c");
	if (IS_ERR(data->vcc_i2c)) {
		rc = PTR_ERR(data->vcc_i2c);
		dev_err(&data->client->dev,
			"Regulator get failed vcc_i2c rc=%d\n", rc);
		goto reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(data->vcc_i2c) > 0) {
		rc = regulator_set_voltage(data->vcc_i2c, FT_I2C_VTG_MIN_UV,
					   FT_I2C_VTG_MAX_UV);
		if (rc) {
			dev_err(&data->client->dev,
			"Regulator set_vtg failed vcc_i2c rc=%d\n", rc);
			goto reg_vcc_i2c_put;
		}
	}

	return 0;

reg_vcc_i2c_put:
	regulator_put(data->vcc_i2c);
reg_vdd_set_vtg:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, FT_VTG_MAX_UV);
reg_vdd_put:
	regulator_put(data->vdd);
	return rc;

pwr_deinit:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, FT_VTG_MAX_UV);

	regulator_put(data->vdd);

	if (regulator_count_voltages(data->vcc_i2c) > 0)
		regulator_set_voltage(data->vcc_i2c, 0, FT_I2C_VTG_MAX_UV);

	regulator_put(data->vcc_i2c);
	return 0;
}


/*----------------------------------------------------------------------------*/
#ifdef CONFIG_SUSPEND
static int epl_sensor_suspend(struct i2c_client *client, pm_message_t mesg)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;
    LOG_FUN();

    if(epld ==NULL)
    {
          printk("suspend epl8801 object is NULL\n");
          return -1;
    }

    if(epld->enable_lflag){
		epld->als_suspend = 1;
		epld->enable_lflag = 0;
		//epl_sensor_update_mode(client);
		
	}
	if(epld->enable_pflag)
	{
		//epl_sensor_psensor_enable(epld);
		//epl_sensor_update_mode(client);
	}
	else
	{
		epld->ps_suspend = 1;
                //irq_set_irq_wake(client->irq, 0);
		//disable_irq(client->irq);
		//epl_sensor_update_mode(client);
		//elan_power_on(epld, false);
		//irq_set_irq_wake(client->irq, 0);
		//disable_irq(client->irq);
	}

    //epl_sensor_I2C_Write(client,REG_7, W_SINGLE_BYTE, 0x02, EPL_C_P_DOWN);

    return 0;
}


static int epl_sensor_resume(struct i2c_client *client)
{
    struct epl_sensor_priv *epld = epl_sensor_obj;
    //int err;
    LOG_FUN();

    if(epld ==NULL)
    {
          printk("resume epl8801 object is NULL\n");
          return -1;
    }
    if(epld->als_suspend)
	{
		epld->als_suspend = 0;
		epld->enable_lflag = 1;
		//epl_sensor_update_mode(client);
	}
	if(epld->enable_pflag){
		//epl_sensor_restart_work();
		//epl_sensor_update_mode(epld->client);
	}else{
		epld->ps_suspend = 0;
		//err=elan_power_on(epld, true);
		//if (err) {
			//pr_info("power on failed\n");
		//}
		//enable_irq(client->irq);
		//irq_set_irq_wake(client->irq, 1);
		//epl_sensor_update_mode(epld->client);
	}

    return 0;
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
#endif
/*----------------------------------------------------------------------------*/

static void epl_sensor_dumpReg(struct i2c_client *client)
{

    LOG_INFO("chip id REG 0x00 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x00));
    LOG_INFO("chip id REG 0x01 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x01));
    LOG_INFO("chip id REG 0x02 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x02));
    LOG_INFO("chip id REG 0x03 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x03));
    LOG_INFO("chip id REG 0x04 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x04));
    LOG_INFO("chip id REG 0x05 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x05));
    LOG_INFO("chip id REG 0x06 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x06));
    LOG_INFO("chip id REG 0x07 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x07));
    LOG_INFO("chip id REG 0x11 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x11));
    LOG_INFO("chip id REG 0x12 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x12));
    LOG_INFO("chip id REG 0x1B value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x1B));
    LOG_INFO("chip id REG 0x20 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x20));
    LOG_INFO("chip id REG 0x21 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x21));
    LOG_INFO("chip id REG 0x24 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x24));
    LOG_INFO("chip id REG 0x25 value = 0x%x\n", i2c_smbus_read_byte_data(client, 0x25));

}

/*----------------------------------------------------------------------------*/
static int epl_sensor_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int err = 0;
    struct epl_sensor_priv *epld ;

    LOG_INFO("elan sensor probe enter.\n");

    epld = kzalloc(sizeof(struct epl_sensor_priv), GFP_KERNEL);
    if (!epld)
        return -ENOMEM;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
    {
        dev_err(&client->dev,"No supported i2c func what we need?!!\n");
        err = -ENOTSUPP;
        goto exit;
    }
    epld->client = client;
    i2c_set_clientdata(client, epld);
    err = elan_power_init(epld, true);
    if (err) {
        dev_err(&client->dev, "power init failed");
    }

    err = elan_power_on(epld, true);
    if (err) {
        dev_err(&client->dev, "power on failed");
    }

    msleep(10);
    epl_sensor_dumpReg(client);

	if((i2c_smbus_read_byte_data(client, 0x21)) != 0x61){
        LOG_INFO("elan ALS/PS sensor is failed. \n");
	err = -ENODEV;
        goto i2c_fail;
     }

    epld->type = 2;

    epld->irq = client->irq;

    epl_sensor_obj = epld;
    epld->ps_suspend = 0;
    epld->als_suspend = 0;
    wake_lock_init(&ps_lock, WAKE_LOCK_SUSPEND, "ps wakelock") ;

    epld->epl_wq = create_singlethread_workqueue("elan_sensor_wq");
    if (!epld->epl_wq)
    {
        LOG_ERR("can't create workqueue\n");
        err = -ENOMEM;
        goto err_create_singlethread_workqueue;
    }

    mutex_init(&sensor_mutex);

    //initial global variable and write to senosr
    epl_sensor.power_on_index = EPL_POWER_OFF;
    initial_global_variable(client, epld);
   epl_sensor.ps.high_threshold = PS_HIGH_THRESHOLD;
   epl_sensor.ps.low_threshold = PS_LOW_THRESHOLD;
    err = epl_sensor_setup_lsensor(epld);
    if (err < 0)
    {
        LOG_ERR("epl_sensor_setup_lsensor error!!\n");
        goto err_lightsensor_setup;
    }


    err = epl_sensor_setup_psensor(epld);
    if (err < 0)
    {
        LOG_ERR("epl_sensor_setup_psensor error!!\n");
        goto err_psensor_setup;
    }


    if (epl_sensor.als.polling_mode==0 || epl_sensor.ps.polling_mode==0 || epld->polling_mode_hs ==0)
    {
        err = epl_sensor_setup_interrupt(epld);
        if (err < 0)
        {
            LOG_ERR("setup error!\n");
            goto err_sensor_setup;
        }
    }
    //disable_irq(epld->irq); //ices add

    wake_lock_init(&g_ps_wlock, WAKE_LOCK_SUSPEND, "ps_wakelock");
    err = sysfs_create_group(&client->dev.kobj, &epl_sensor_attr_group);
    if (err !=0)
    {
        dev_err(&client->dev,"%s:create sysfs group error", __func__);
        goto err_fail;
    }
	epld->als_cdev = sensors_light_cdev;
	epld->als_cdev.sensors_enable = epl_als_set_enable;
	epld->als_cdev.sensors_poll_delay = NULL;

	epld->ps_cdev = sensors_proximity_cdev;
	epld->ps_cdev.sensors_enable = epl_ps_set_enable;
	epld->ps_cdev.sensors_poll_delay = NULL;

	err = sensors_classdev_register(&client->dev, &epld->als_cdev);
	if (err) {
		pr_err("%s: Unable to register to sensors class: %d\n",
				__func__, err);
		goto exit_unregister_als_class;
	}

	err = sensors_classdev_register(&client->dev, &epld->ps_cdev);
	if (err) {
		pr_err("%s: Unable to register to sensors class: %d\n",
			       __func__, err);
		goto exit_unregister_ps_class;
	}

     err = elan_power_on(epld, false);
    if (err) {
        dev_err(&client->dev, "power on failed");
    }

    LOG_INFO("sensor probe success.\n");

    return err;

exit_unregister_ps_class:
	sensors_classdev_unregister(&epld->ps_cdev);
exit_unregister_als_class:
	sensors_classdev_unregister(&epld->als_cdev);

err_fail:
    input_unregister_device(epld->als_input_dev);
    input_unregister_device(epld->ps_input_dev);
    input_free_device(epld->als_input_dev);
    input_free_device(epld->ps_input_dev);
err_lightsensor_setup:
err_psensor_setup:
err_sensor_setup:
    destroy_workqueue(epld->epl_wq);
    misc_deregister(&epl_sensor_ps_device);
    misc_deregister(&epl_sensor_als_device);
err_create_singlethread_workqueue:
i2c_fail:
//err_platform_data_null:
    elan_power_on(epld, false);
    kfree(epld);
exit:
    return err;
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static int epl_sensor_remove(struct i2c_client *client)
{
    struct epl_sensor_priv *epld = i2c_get_clientdata(client);

    dev_dbg(&client->dev, "%s: enter.\n", __func__);

    sysfs_remove_group(&client->dev.kobj, &epl_sensor_attr_group);
    input_unregister_device(epld->als_input_dev);
    input_unregister_device(epld->ps_input_dev);
    input_free_device(epld->als_input_dev);
    input_free_device(epld->ps_input_dev);
    misc_deregister(&epl_sensor_ps_device);
    misc_deregister(&epl_sensor_als_device);
    free_irq(epld->irq,epld);
    destroy_workqueue(epld->epl_wq);
    kfree(epld);
    return 0;
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static const struct i2c_device_id epl_sensor_id[] =
{
    { "EPL8801", 0 },
    { }
};
static struct of_device_id epl_match_table[] = {
                { .compatible = "epl,epl8801",},
                { },
        };
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static struct i2c_driver epl_sensor_driver =
{
    .probe	= epl_sensor_probe,
    .remove	= epl_sensor_remove,
    .id_table	= epl_sensor_id,
    .driver	= {
        .name = "EPL8801",
        .owner = THIS_MODULE,
        .of_match_table =epl_match_table,
    },
#ifdef CONFIG_SUSPEND
    .suspend = epl_sensor_suspend,
    .resume = epl_sensor_resume,
#endif
};

static int __init epl_sensor_init(void)
{
    return i2c_add_driver(&epl_sensor_driver);
}

static void __exit  epl_sensor_exit(void)
{
    i2c_del_driver(&epl_sensor_driver);
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
module_init(epl_sensor_init);
module_exit(epl_sensor_exit);
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
MODULE_AUTHOR("Renato Pan <renato.pan@eminent-tek.com>");
MODULE_DESCRIPTION("ELAN EPL8801_3636 driver");
MODULE_LICENSE("GPL");
