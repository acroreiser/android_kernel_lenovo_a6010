/*
 * apds993x.c - Linux kernel modules for ambient light + proximity sensor
 *
 * Copyright (C) 2012 Lee Kai Koon <kai-koon.lee@avagotech.com>
 * Copyright (C) 2012 Avago Technologies
 * Copyright (C) 2013 LGE Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/i2c/apds993x.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/sensors.h>
#include <linux/wakelock.h>

#define APDS993X_HAL_USE_SYS_ENABLE

#define APDS993X_DRV_NAME	"apds993x"
#define DRIVER_VERSION		"1.0.0"

#define ALS_POLLING_ENABLED

#define APDS993X_PS_DETECTION_THRESHOLD		800
#define APDS993X_PS_HSYTERESIS_THRESHOLD	700
#define APDS993X_PS_PULSE_NUMBER		10

#define APDS993X_ALS_THRESHOLD_HSYTERESIS	20	/* % */

#define APDS993X_GA	48	/* 0.48 without glass window */
#define APDS993X_COE_B	223	/* 2.23 without glass window */
#define APDS993X_COE_C	70	/* 0.70 without glass window */
#define APDS993X_COE_D	142	/* 1.42 without glass window */
#define APDS993X_DF	52

/* Change History
 *
 * 1.0.0	Fundamental Functions of APDS-993x
 *
 */
#define APDS993X_IOCTL_PS_ENABLE	1
#define APDS993X_IOCTL_PS_GET_ENABLE	2
#define APDS993X_IOCTL_PS_GET_PDATA	3	/* pdata */
#define APDS993X_IOCTL_ALS_ENABLE	4
#define APDS993X_IOCTL_ALS_GET_ENABLE	5
#define APDS993X_IOCTL_ALS_GET_CH0DATA	6	/* ch0data */
#define APDS993X_IOCTL_ALS_GET_CH1DATA	7	/* ch1data */
#define APDS993X_IOCTL_ALS_DELAY	8

/*
 * Defines
 */
#define APDS993X_ENABLE_REG	0x00
#define APDS993X_ATIME_REG	0x01
#define APDS993X_PTIME_REG	0x02
#define APDS993X_WTIME_REG	0x03
#define APDS993X_AILTL_REG	0x04
#define APDS993X_AILTH_REG	0x05
#define APDS993X_AIHTL_REG	0x06
#define APDS993X_AIHTH_REG	0x07
#define APDS993X_PILTL_REG	0x08
#define APDS993X_PILTH_REG	0x09
#define APDS993X_PIHTL_REG	0x0A
#define APDS993X_PIHTH_REG	0x0B
#define APDS993X_PERS_REG	0x0C
#define APDS993X_CONFIG_REG	0x0D
#define APDS993X_PPCOUNT_REG	0x0E
#define APDS993X_CONTROL_REG	0x0F
#define APDS993X_REV_REG	0x11
#define APDS993X_ID_REG		0x12
#define APDS993X_STATUS_REG	0x13
#define APDS993X_CH0DATAL_REG	0x14
#define APDS993X_CH0DATAH_REG	0x15
#define APDS993X_CH1DATAL_REG	0x16
#define APDS993X_CH1DATAH_REG	0x17
#define APDS993X_PDATAL_REG	0x18
#define APDS993X_PDATAH_REG	0x19
#define APDS993X_POFFSET_REG	0x1E

#define CMD_BYTE		0x80
#define CMD_WORD		0xA0
#define CMD_SPECIAL		0xE0

#define CMD_CLR_PS_INT		0xE5
#define CMD_CLR_ALS_INT		0xE6
#define CMD_CLR_PS_ALS_INT	0xE7


/* Register Value define : ATIME */
#define APDS993X_100MS_ADC_TIME	0xDB  /* 100.64ms integration time */
#define APDS993X_50MS_ADC_TIME	0xED  /* 51.68ms integration time */
#define APDS993X_27MS_ADC_TIME	0xF6  /* 27.2ms integration time */

/* Register Value define : PRXCNFG */
#define APDS993X_ALS_REDUCE	0x04  /* ALSREDUCE - ALS Gain reduced by 4x */

/* Register Value define : PERS */
#define APDS993X_PPERS_0	0x00  /* Every proximity ADC cycle */
#define APDS993X_PPERS_1	0x10  /* 1 consecutive proximity value out of range */
#define APDS993X_PPERS_2	0x20  /* 2 consecutive proximity value out of range */
#define APDS993X_PPERS_3	0x30  /* 3 consecutive proximity value out of range */
#define APDS993X_PPERS_4	0x40  /* 4 consecutive proximity value out of range */
#define APDS993X_PPERS_5	0x50  /* 5 consecutive proximity value out of range */
#define APDS993X_PPERS_6	0x60  /* 6 consecutive proximity value out of range */
#define APDS993X_PPERS_7	0x70  /* 7 consecutive proximity value out of range */
#define APDS993X_PPERS_8	0x80  /* 8 consecutive proximity value out of range */
#define APDS993X_PPERS_9	0x90  /* 9 consecutive proximity value out of range */
#define APDS993X_PPERS_10	0xA0  /* 10 consecutive proximity value out of range */
#define APDS993X_PPERS_11	0xB0  /* 11 consecutive proximity value out of range */
#define APDS993X_PPERS_12	0xC0  /* 12 consecutive proximity value out of range */
#define APDS993X_PPERS_13	0xD0  /* 13 consecutive proximity value out of range */
#define APDS993X_PPERS_14	0xE0  /* 14 consecutive proximity value out of range */
#define APDS993X_PPERS_15	0xF0  /* 15 consecutive proximity value out of range */

#define APDS993X_APERS_0	0x00  /* Every ADC cycle */
#define APDS993X_APERS_1	0x01  /* 1 consecutive proximity value out of range */
#define APDS993X_APERS_2	0x02  /* 2 consecutive proximity value out of range */
#define APDS993X_APERS_3	0x03  /* 3 consecutive proximity value out of range */
#define APDS993X_APERS_5	0x04  /* 5 consecutive proximity value out of range */
#define APDS993X_APERS_10	0x05  /* 10 consecutive proximity value out of range */
#define APDS993X_APERS_15	0x06  /* 15 consecutive proximity value out of range */
#define APDS993X_APERS_20	0x07  /* 20 consecutive proximity value out of range */
#define APDS993X_APERS_25	0x08  /* 25 consecutive proximity value out of range */
#define APDS993X_APERS_30	0x09  /* 30 consecutive proximity value out of range */
#define APDS993X_APERS_35	0x0A  /* 35 consecutive proximity value out of range */
#define APDS993X_APERS_40	0x0B  /* 40 consecutive proximity value out of range */
#define APDS993X_APERS_45	0x0C  /* 45 consecutive proximity value out of range */
#define APDS993X_APERS_50	0x0D  /* 50 consecutive proximity value out of range */
#define APDS993X_APERS_55	0x0E  /* 55 consecutive proximity value out of range */
#define APDS993X_APERS_60	0x0F  /* 60 consecutive proximity value out of range */

/* Register Value define : CONTROL */
#define APDS993X_AGAIN_1X	0x00  /* 1X ALS GAIN */
#define APDS993X_AGAIN_8X	0x01  /* 8X ALS GAIN */
#define APDS993X_AGAIN_16X	0x02  /* 16X ALS GAIN */
#define APDS993X_AGAIN_120X	0x03  /* 120X ALS GAIN */

#define APDS993X_PRX_IR_DIOD	0x20  /* Proximity uses CH1 diode */

#define APDS993X_PGAIN_1X	0x00  /* PS GAIN 1X */
#define APDS993X_PGAIN_2X	0x04  /* PS GAIN 2X */
#define APDS993X_PGAIN_4X	0x08  /* PS GAIN 4X */
#define APDS993X_PGAIN_8X	0x0C  /* PS GAIN 8X */

#define APDS993X_PDRVIE_100MA	0x00  /* PS 100mA LED drive */
#define APDS993X_PDRVIE_50MA	0x40  /* PS 50mA LED drive */
#define APDS993X_PDRVIE_25MA	0x80  /* PS 25mA LED drive */
#define APDS993X_PDRVIE_12_5MA	0xC0  /* PS 12.5mA LED drive */

/*calibration*/
#define DEFAULT_CROSS_TALK	400
#define ADD_TO_CROSS_TALK	300
#define SUB_FROM_PS_THRESHOLD	100
#define APDS993X_DEFAULT_POFFSET 135

/*PS tuning value*/
static int apds993x_ps_detection_threshold = 0;
static int apds993x_ps_hsyteresis_threshold = 0;
static int apds993x_ps_pulse_number = 0;
static int apds993x_ps_pgain = 0;
static struct wake_lock ps_lock;

typedef enum
{
	APDS993X_ALS_RES_10240 = 0,    /* 27.2ms integration time */
	APDS993X_ALS_RES_19456 = 1,    /* 51.68ms integration time */
	APDS993X_ALS_RES_37888 = 2     /* 100.64ms integration time */
} apds993x_als_res_e;

typedef enum
{
	APDS993X_ALS_GAIN_1X    = 0,    /* 1x AGAIN */
	APDS993X_ALS_GAIN_8X    = 1,    /* 8x AGAIN */
	APDS993X_ALS_GAIN_16X   = 2,    /* 16x AGAIN */
	APDS993X_ALS_GAIN_120X  = 3     /* 120x AGAIN */
} apds993x_als_gain_e;

/*
 * Structs
 */
struct apds993x_data {
	struct i2c_client *client;
	struct mutex update_lock;
	struct delayed_work	dwork;		/* for PS interrupt */
	struct delayed_work	als_dwork;	/* for ALS polling */
	struct input_dev *input_dev_als;
	struct input_dev *input_dev_ps;
	struct regulator *vdd;
	struct regulator *vio;
	struct sensors_classdev als_cdev;
	struct sensors_classdev ps_cdev;

	struct apds993x_platform_data *platform_data;
	int irq;

	unsigned int enable;
	unsigned int atime;
	unsigned int ptime;
	unsigned int wtime;
	unsigned int ailt;
	unsigned int aiht;
	unsigned int pilt;
	unsigned int piht;
	unsigned int pers;
	unsigned int config;
	unsigned int ppcount;
	unsigned int control;
	unsigned int poffset;

	/* control flag from HAL */
	atomic_t  enable_ps_sensor;
	atomic_t  enable_als_sensor;

	/* save sensor enabling state for resume */
	unsigned int als_suspend_state;
	unsigned int ps_suspend_state;
	/* PS parameters */
	unsigned int ps_threshold;
	unsigned int ps_hysteresis_threshold; 	/* always lower than ps_threshold */
	unsigned int ps_detection;		/* 5 = near-to-far; 0 = far-to-near */
	unsigned int ps_data;			/* to store PS data */
	unsigned int auto_piht;
	unsigned int auto_pilt;


	/* ALS parameters */
	unsigned int als_threshold_l;	/* low threshold */
	unsigned int als_threshold_h;	/* high threshold */
	unsigned int als_data;		/* to store ALS data */
	int als_prev_lux;		/* to store previous lux value */

	unsigned int als_gain;		/* needed for Lux calculation */
	unsigned int als_poll_delay;	/* needed for light sensor polling : micro-second (us) */
	unsigned int als_atime_index;	/* storage for als integratiion time */
	unsigned int als_again_index;	/* storage for als GAIN */
	unsigned int als_reduce;	/* flag indicate ALS 6x reduction */
	unsigned int type;
};

static struct sensors_classdev sensors_light_cdev = {
	.name = "apds9930-light",
	.vendor = "avago",
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
	.name = "apds9930-proximity",
	.vendor = "avago",
	.version = 1,
	.handle = SENSORS_PROXIMITY_HANDLE,
	.type = SENSOR_TYPE_PROXIMITY,
	.max_range = "5",
	.resolution = "5.0",
	.sensor_power = "3",
	.min_delay = 1000, /* in microseconds */
	.fifo_reserved_event_count = 0,
	.fifo_max_event_count = 0,
	.enabled = 0,
	.delay_msec = 100,
	.sensors_enable = NULL,
	.sensors_poll_delay = NULL,
};

/*
 * Global data
 */
static struct apds993x_data *pdev_data = NULL;

/* global i2c_client to support ioctl */
static struct i2c_client *apds993x_i2c_client = NULL;
static struct workqueue_struct *apds993x_workqueue = NULL;

static unsigned char apds993x_als_atime_tb[] = { 0xF6, 0xED, 0xDB };
static unsigned short apds993x_als_integration_tb[] = {2720, 5168, 10064};
static unsigned short apds993x_als_res_tb[] = { 10240, 19456, 37888 };
static unsigned char apds993x_als_again_tb[] = { 1, 8, 16, 120 };
static unsigned char apds993x_als_again_bit_tb[] = { 0x00, 0x01, 0x02, 0x03 };


/* ALS tuning */
static int apds993x_ga = 0;
static int apds993x_coe_b = 0;
static int apds993x_coe_c = 0;
static int apds993x_coe_d = 0;

#ifdef ALS_POLLING_ENABLED
//static int apds993x_set_als_poll_delay(struct i2c_client *client, unsigned int val);
#endif
static int apds993x_init_device(struct i2c_client *client);

/*
 * Management functions
 */
static int apds993x_set_command(struct i2c_client *client, int command)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;
	int clearInt;

	if (command == 0)
		clearInt = CMD_CLR_PS_INT;
	else if (command == 1)
		clearInt = CMD_CLR_ALS_INT;
	else
		clearInt = CMD_CLR_PS_ALS_INT;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte(client, clearInt);
	mutex_unlock(&data->update_lock);

	return ret;
}

static int apds993x_set_enable(struct i2c_client *client, int enable)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_ENABLE_REG, enable);
	mutex_unlock(&data->update_lock);

	data->enable = enable;
	pr_info("%s: enable=%d\n", __func__,data->enable);
	return ret;
}

static int apds993x_set_atime(struct i2c_client *client, int atime)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_ATIME_REG, atime);
	mutex_unlock(&data->update_lock);

	data->atime = atime;

	return ret;
}

static int apds993x_set_ptime(struct i2c_client *client, int ptime)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_PTIME_REG, ptime);
	mutex_unlock(&data->update_lock);

	data->ptime = ptime;

	return ret;
}

static int apds993x_set_wtime(struct i2c_client *client, int wtime)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_WTIME_REG, wtime);
	mutex_unlock(&data->update_lock);

	data->wtime = wtime;

	return ret;
}



static int apds993x_set_pilt(struct i2c_client *client, int threshold)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_PILTL_REG, threshold);
	mutex_unlock(&data->update_lock);

	pr_info("%s: pilt=%d\n", __func__,data->pilt);

	return ret;
}

static int apds993x_set_piht(struct i2c_client *client, int threshold)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_PIHTL_REG, threshold);
	mutex_unlock(&data->update_lock);


	pr_info("%s: piht=%d\n", __func__,data->piht);

	return ret;
}

static int apds993x_set_pers(struct i2c_client *client, int pers)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_PERS_REG, pers);
	mutex_unlock(&data->update_lock);

	data->pers = pers;

	return ret;
}

static int apds993x_set_config(struct i2c_client *client, int config)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_CONFIG_REG, config);
	mutex_unlock(&data->update_lock);

	data->config = config;

	return ret;
}

static int apds993x_set_ppcount(struct i2c_client *client, int ppcount)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_PPCOUNT_REG, ppcount);
	mutex_unlock(&data->update_lock);

	data->ppcount = ppcount;

	return ret;
}

static int apds993x_set_control(struct i2c_client *client, int control)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&data->update_lock);
	ret = i2c_smbus_write_byte_data(client,
			CMD_BYTE|APDS993X_CONTROL_REG, control);
	mutex_unlock(&data->update_lock);

	data->control = control;

	return ret;
}
/*calibration*/
void apds993x_swap(int *x, int *y)
{
	int temp = *x;
	*x = *y;
	*y = temp;
}
static int LuxCalculation(struct i2c_client *client, int ch0data, int ch1data)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int luxValue=0;
	int IAC1=0;
	int IAC2=0;
	int IAC=0;

	if (ch0data >= apds993x_als_res_tb[data->als_atime_index] ||
			ch1data >= apds993x_als_res_tb[data->als_atime_index]) {
		luxValue = data->als_prev_lux;
		return luxValue;
	}

	/* re-adjust COE_B to avoid 2 decimal point */
	IAC1 = (ch0data - (apds993x_coe_b * ch1data) / 100);
	/* re-adjust COE_C and COE_D to void 2 decimal point */
	IAC2 = ((apds993x_coe_c * ch0data) / 100 -
			(apds993x_coe_d * ch1data) / 100);

	if (IAC1 > IAC2)
		IAC = IAC1;
	else if (IAC1 <= IAC2)
		IAC = IAC2;
	else
		IAC = 0;

	if (IAC1 < 0 && IAC2 < 0) {
		IAC = 0;	/* cdata and irdata saturated */
		return -1; 	/* don't report first, change gain may help */
	}

	if (data->als_reduce) {
		luxValue = ((IAC * apds993x_ga * APDS993X_DF) / 100) * 65 / 10 /
			((apds993x_als_integration_tb[data->als_atime_index] /
			  100) * apds993x_als_again_tb[data->als_again_index]);
	} else {
		luxValue = ((IAC * apds993x_ga * APDS993X_DF) /100) /
			((apds993x_als_integration_tb[data->als_atime_index] /
			  100) * apds993x_als_again_tb[data->als_again_index]);
	}

	return luxValue;
}

static void apds993x_change_ps_threshold(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);

       mutex_lock(&data->update_lock);
	data->ps_data =	i2c_smbus_read_word_data(
			client, CMD_WORD|APDS993X_PDATAL_REG);
	mutex_unlock(&data->update_lock);

	if ((data->ps_data > data->auto_pilt) && (data->ps_data >= data->auto_piht)) {
		/* far-to-near detected */
		data->ps_detection = 1;

		/* FAR-to-NEAR detection */
		input_report_abs(data->input_dev_ps, ABS_DISTANCE, 0);
		input_sync(data->input_dev_ps);
              mutex_lock(&data->update_lock);
		i2c_smbus_write_word_data(client,
				CMD_WORD|APDS993X_PILTL_REG,
				data->auto_pilt);
		i2c_smbus_write_word_data(client,
				CMD_WORD|APDS993X_PIHTL_REG, 1023);
		mutex_unlock(&data->update_lock);

		pr_info("%s: far-to-near pilt=%d piht=%d\n", __func__,data->auto_pilt,data->auto_piht);
	} else if ((data->ps_data <= data->auto_pilt) &&
			(data->ps_data < data->auto_piht)) {
		/* near-to-far detected */
		data->ps_detection = 0;

		/* NEAR-to-FAR detection */
		input_report_abs(data->input_dev_ps, ABS_DISTANCE, 5);
		input_sync(data->input_dev_ps);

              mutex_lock(&data->update_lock);
		i2c_smbus_write_word_data(client,
				CMD_WORD|APDS993X_PILTL_REG, 0);
		i2c_smbus_write_word_data(client,
				CMD_WORD|APDS993X_PIHTL_REG,
				data->auto_piht);
		mutex_unlock(&data->update_lock);

		pr_info("%s: near-to-far pilt=%d piht=%d\n", __func__,data->auto_pilt,data->auto_piht);
	}
}

static void apds993x_reschedule_work(struct apds993x_data *data, unsigned long delay)
{
	/*
	 * If work is already scheduled then subsequent schedules will not
	 * change the scheduled time that's why we have to cancel it first.
	 */
	cancel_delayed_work(&data->dwork);
	queue_delayed_work(apds993x_workqueue, &data->dwork, delay);
}


#ifdef ALS_POLLING_ENABLED
/* ALS polling routine */
static void apds993x_als_polling_work_handler(struct work_struct *work)
{
	struct apds993x_data *data = container_of(work,
			struct apds993x_data, als_dwork.work);
	struct i2c_client *client=data->client;
	int ch0data, ch1data;
	int luxValue=0;
	int err;
	int status;
	unsigned char change_again=0;
	unsigned char control_data=0;
	unsigned char lux_is_valid=1;
	
	status = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_STATUS_REG);
	if(status & 0x01)
	{
		mutex_lock(&data->update_lock);
		ch0data = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_CH0DATAL_REG);
		ch1data = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_CH1DATAL_REG);
		mutex_unlock(&data->update_lock);

		luxValue = LuxCalculation(client, ch0data, ch1data);
		if (luxValue >= 0) 
		{
			luxValue = luxValue<30000 ? luxValue : 30000;
			data->als_prev_lux = luxValue;
		} 
		else 
		{
			lux_is_valid = 0;
			luxValue = data->als_prev_lux;
			if (data->als_reduce)
				lux_is_valid = 1;
		}
		if (lux_is_valid) 
		{
			input_report_abs(data->input_dev_als, ABS_MISC, luxValue);
			input_sync(data->input_dev_als);
		}

		data->als_data = ch0data;

		if (data->als_data >= (apds993x_als_res_tb[data->als_atime_index]* 90) / 100) 
		{
			/* lower AGAIN if possible */
			if (data->als_again_index != APDS993X_ALS_GAIN_1X)
			{
				data->als_again_index--;
				change_again = 1;
			} 
			else 
			{
				err = i2c_smbus_write_byte_data(client,CMD_BYTE|APDS993X_CONFIG_REG, APDS993X_ALS_REDUCE);
				if (err >= 0)
					data->als_reduce = 1;
			}
		} 
		else if (data->als_data <= (apds993x_als_res_tb[data->als_atime_index] * 10) / 100) 
		{
			/* increase AGAIN if possible */
			if (data->als_reduce) {
				err = i2c_smbus_write_byte_data(client, CMD_BYTE|APDS993X_CONFIG_REG, 0);
				if (err >= 0)
					data->als_reduce = 0;
			}
			else if (data->als_again_index != APDS993X_ALS_GAIN_120X) 
			{
				data->als_again_index++;
				change_again = 1;
			}
		}

		if (change_again) 
		{
			control_data = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_CONTROL_REG);
			control_data = control_data & 0xFC;
			control_data = control_data |apds993x_als_again_bit_tb[data->als_again_index];
			i2c_smbus_write_byte_data(client,CMD_BYTE|APDS993X_CONTROL_REG, control_data);
		}
	}
	/* restart timer */
	queue_delayed_work(apds993x_workqueue, &data->als_dwork, msecs_to_jiffies(data->als_poll_delay));
}
#endif /* ALS_POLLING_ENABLED */

/* PS interrupt routine */
static void apds993x_work_handler(struct work_struct *work)
{
	struct apds993x_data *data =
		container_of(work, struct apds993x_data, dwork.work);
	struct i2c_client *client=data->client;
	int status;
	int ch0data;
	int enable;

       mutex_lock(&data->update_lock);
	status = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_STATUS_REG);
	enable = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_ENABLE_REG);

	/* disable 993x's ADC first */
	i2c_smbus_write_byte_data(client, CMD_BYTE|APDS993X_ENABLE_REG, 1);
	mutex_unlock(&data->update_lock);

	pr_debug("%s: status = %x\n", __func__, status);

	if ((status & enable & 0x30) == 0x30) {
		/* both PS and ALS are interrupted */

		ch0data = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_CH0DATAL_REG);
		pr_info("%s:ch0:%d ps_detection:%d\n",  __func__, ch0data, data->ps_detection);
		apds993x_change_ps_threshold(client);

		/* 2 = CMD_CLR_PS_ALS_INT */
		apds993x_set_command(client, 2);
	} else if ((status & enable & 0x20) == 0x20) {
		/* only PS is interrupted */

		/* check if this is triggered by background ambient noise */
		ch0data = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_CH0DATAL_REG);
		pr_info("%s:ch0:%d ps_detection:%d\n",  __func__, ch0data, data->ps_detection);
		apds993x_change_ps_threshold(client);

		/* 0 = CMD_CLR_PS_INT */
		apds993x_set_command(client, 0);
	} else if ((status & enable & 0x10) == 0x10) {
		/* only ALS is interrupted */

		/* 1 = CMD_CLR_ALS_INT */
		apds993x_set_command(client, 1);
	}
       mutex_lock(&data->update_lock);
	i2c_smbus_write_byte_data(client, CMD_BYTE|APDS993X_ENABLE_REG, enable);
	mutex_unlock(&data->update_lock);
	
}

/* assume this is ISR */
static irqreturn_t apds993x_interrupt(int vec, void *info)
{
	struct i2c_client *client=(struct i2c_client *)info;
	struct apds993x_data *data = i2c_get_clientdata(client);

       wake_lock_timeout(&ps_lock,HZ);

	apds993x_reschedule_work(data, 0);

	return IRQ_HANDLED;
}

/*
 * IOCTL support
 */
static int apds993x_enable_als_sensor(struct i2c_client *client, int val)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	struct apds993x_platform_data *pdata = data->platform_data;
	int rc;

	pr_info("%s: val=%d\n", __func__, val);

	if ((val != 0) && (val != 1)) {
		pr_err("%s: invalid value (val = %d)\n", __func__, val);
		return -EINVAL;
	}

	if (val == 1) {
		/* turn on light  sensor */
		if ((atomic_read(&data->enable_als_sensor) == 0) &&
				(atomic_read(&data->enable_ps_sensor) == 0)) {
			/* Power on and initalize the device */
			if (pdata->power_on)
				pdata->power_on(true);

			rc = apds993x_init_device(client);
			if (rc) {
				dev_err(&client->dev, "Failed to init apds993x\n");
				return rc;
			}
		}

		if (atomic_read(&data->enable_als_sensor) == 0) {
			atomic_set(&data->enable_als_sensor,1);
			/* Power Off */
			apds993x_set_enable(client,0);

#ifdef ALS_POLLING_ENABLED
			if (atomic_read(&data->enable_ps_sensor)==1) {
				/* Enable PS with interrupt */
				apds993x_set_enable(client, 0x27);
			} else {
				/* no interrupt*/
				apds993x_set_enable(client, 0x03);
			}
#else
			/*
			 *  force first ALS interrupt in order to get environment reading
			 */
			apds993x_set_ailt( client, 0xFFFF);
			apds993x_set_aiht( client, 0);

			if (atomic_read(&data->enable_ps_sensor) ==1){
				/* Enable both ALS and PS with interrupt */
				apds993x_set_enable(client, 0x37);
			} else {
				/* only enable light sensor with interrupt*/
				apds993x_set_enable(client, 0x13);
				if (data->irq)
					enable_irq(data->irq);
			}
#endif

#ifdef ALS_POLLING_ENABLED
			/*
			 * If work is already scheduled then subsequent schedules will not change the scheduled time that's why we have to cancel it first.
			 */
			cancel_delayed_work(&data->als_dwork);
			flush_delayed_work(&data->als_dwork);
			queue_delayed_work(apds993x_workqueue, &data->als_dwork, msecs_to_jiffies(data->als_poll_delay));
#endif
		}
	} else {
		/*
		 * turn off light sensor
		 * what if the p sensor is active?
		 */
		atomic_set(&data->enable_als_sensor,0);

		if (atomic_read(&data->enable_ps_sensor)==1) {
			/* Power Off */

		} else {
			apds993x_set_enable(client, 0);
		}

#ifdef ALS_POLLING_ENABLED
		/*
		 * If work is already scheduled then subsequent schedules will not change the scheduled time that's why we have to cancel it first.
		 */
		cancel_delayed_work(&data->als_dwork);
		flush_delayed_work(&data->als_dwork);
#endif
		input_report_abs(data->input_dev_als, ABS_MISC, -1);
		input_sync(data->input_dev_als);
	}

	/* Vote off  regulators if both light and prox sensor are off */
	if( (atomic_read(&data->enable_als_sensor) == 0) &&
			(atomic_read(&data->enable_ps_sensor) == 0) &&
			(pdata->power_on))
		pdata->power_on(false);

	return 0;
}

static int apds993x_auto_cali_process(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int i,sum,data_cali;    
	int data1[5];
	int status;
	pr_info("%s enter: pilt=%d piht=%d\n", __func__, data->pilt,data->piht);

	sum = 0;
	for(i=0;i<5;)
	{
		status = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_STATUS_REG);
		if(status & 0x02)
		{
			data1[i] = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_PDATAL_REG);
			sum += data1[i];
			++i;					
		}
	}
	data_cali =sum/5;
	if(data_cali>700)
	{
		pr_info("%s: pilt=%d piht=%d\n", __func__, data->pilt,data->piht);
		data->auto_piht = data->piht;
		data->auto_pilt = data->pilt;
	}
	else if(data_cali>200)
	{
		data->auto_piht = data_cali+100;
		data->auto_pilt = data_cali+60;
	}
	else
	{
		data->auto_piht = data_cali+90;
		data->auto_pilt = data_cali+50;
	}
	mutex_lock(&data->update_lock);
	i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_PILTL_REG, data->auto_pilt);
	i2c_smbus_write_word_data(client,
			CMD_WORD|APDS993X_PIHTL_REG, data->auto_piht);
	mutex_unlock(&data->update_lock);
	pr_info("%s exit auto_pilt=%d auto_piht=%d data_cali=%d\n", __func__, data->auto_pilt,data->auto_piht,data_cali);
	return 0;   
}
static int apds993x_enable_ps_sensor(struct i2c_client *client, int val)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	struct apds993x_platform_data *pdata = data->platform_data;
	int rc;

	pr_info("%s: val=%d pilt=%d,piht=%d\n", __func__, val,data->pilt,data->piht);

	if ((val != 0) && (val != 1)) {
		pr_err("%s: invalid value=%d\n", __func__, val);
		return -EINVAL;
	}

	if (val == 1) {
		/* turn on p sensor */
		if ((atomic_read(&data->enable_als_sensor) == 0) &&
				(atomic_read(&data->enable_ps_sensor) == 0)) {
			/* Power on and initalize the device */
			if (pdata->power_on)
				pdata->power_on(true);

			rc = apds993x_init_device(client);
			if (rc) {
				dev_err(&client->dev, "Failed to init apds993x\n");
				return rc;
			}
		}

		if (atomic_read(&data->enable_ps_sensor)==0) {
			atomic_set(&data->enable_ps_sensor, 1);

			/* Power Off */
			apds993x_set_enable(client,0);
			if (atomic_read(&data->enable_als_sensor)==0) {
				/* only enable PS interrupt */
				apds993x_set_enable(client, 0x27);
				apds993x_auto_cali_process(client);
				if (data->irq) {
					enable_irq(data->irq);
					irq_set_irq_wake(client->irq, 1);
				}
			} else {
#ifdef ALS_POLLING_ENABLED
				/* enable PS interrupt */
				apds993x_set_enable(client, 0x27);
				apds993x_auto_cali_process(client);
				if (data->irq) {
					enable_irq(data->irq);
					irq_set_irq_wake(client->irq, 1);
				}
#else
				/* enable ALS and PS interrupt */
				apds993x_set_enable(client, 0x37);
				irq_set_irq_wake(client->irq, 1);
#endif
			}
		}
	} 
	else 
	{	
		/* turn off p sensor - kk 25 Apr 2011 we can't turn off the entire sensor, the light sensor may be needed by HAL*/
		 if (atomic_read(&data->enable_ps_sensor) ==0){
				return 0;//if Psensor is off do nothing!
		 }
		 
		atomic_set(&data->enable_ps_sensor ,0);
		if (atomic_read(&data->enable_als_sensor) ==1)
		{
#ifdef ALS_POLLING_ENABLED
			/* no ALS interrupt */
			if (data->irq) {
				irq_set_irq_wake(client->irq, 0);
				disable_irq(data->irq);
			}

			apds993x_set_enable(client, 0x03);

			/*
			 * If work is already scheduled then subsequent schedules will not change the scheduled time that's why we have to cancel it first.
			 */
			cancel_delayed_work(&data->als_dwork);
			flush_delayed_work(&data->als_dwork);
			/* 100ms */
			queue_delayed_work(apds993x_workqueue, &data->als_dwork, msecs_to_jiffies(data->als_poll_delay));

#else
			/* reconfigute light sensor setting */
			if (data->irq)
				irq_set_irq_wake(client->irq, 0);

			/* Power Off */
			apds993x_set_enable(client,0);
			/* Force ALS interrupt */
			apds993x_set_ailt( client, 0xFFFF);
			apds993x_set_aiht( client, 0);

			/* enable ALS interrupt */
			apds993x_set_enable(client, 0x13);
#endif
		} else {
			if (data->irq) {
				irq_set_irq_wake(client->irq, 0);
				disable_irq(data->irq);
			}
			apds993x_set_enable(client, 0);
#ifdef ALS_POLLING_ENABLED
			/*
			 * If work is already scheduled then subsequent schedules will not change the scheduled time that's why we have to cancel it first.
			 */
			cancel_delayed_work(&data->als_dwork);
			flush_delayed_work(&data->als_dwork);
#endif
		}
		data->auto_piht = 0;
		data->auto_pilt = 0;
		input_report_abs(data->input_dev_ps, ABS_DISTANCE, -1);
		input_sync(data->input_dev_ps);
	}
	if ((atomic_read(&data->enable_als_sensor)== 0) &&
			(atomic_read(&data->enable_ps_sensor) == 0) && (pdata->power_on))
		pdata->power_on(false);
	return 0;
}
/*
 * SysFS support
 */
static ssize_t apds993x_show_ch0data(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ch0data;

	mutex_lock(&data->update_lock);
	ch0data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH0DATAL_REG);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", ch0data);
}

static DEVICE_ATTR(ch0data, S_IRUGO, apds993x_show_ch0data, NULL);

static ssize_t apds993x_show_ch1data(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int ch1data;

	mutex_lock(&data->update_lock);
	ch1data = i2c_smbus_read_word_data(client,
			CMD_WORD|APDS993X_CH1DATAL_REG);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", ch1data);
}

static DEVICE_ATTR(ch1data, S_IRUGO, apds993x_show_ch1data, NULL);

static ssize_t apds993x_show_pdata(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int pdata;

	mutex_lock(&data->update_lock);
	pdata = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_PDATAL_REG);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d", pdata);
}

static DEVICE_ATTR(pdata, S_IWUSR | S_IWGRP | S_IRUGO, apds993x_show_pdata, NULL);

static int apds993x_cali_process(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int i,sum,data_cali,max,min;    
	int data1[20];
	sum = 0;
	max = 0;
	min =  1023;

	for(i=0;i<20;i++)
	{
		mdelay(5);
		mutex_lock(&data->update_lock);
		data1[i] = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_PDATAL_REG);
		mutex_unlock(&data->update_lock);
		sum += data1[i];
		mdelay(55);
		if(data1[i] > max)
		{
			max = data1[i];
		}
		if(data1[i] < min)
		{
			min = data1[i];
		}
	}
	data_cali =sum/20;
	if(data_cali>200)
	{
		return 1;
	}
	else if((max-min)>100)
	{
		return 1;
	}
	else
	{
		data->piht = data_cali+90;
		data->pilt = data_cali+50;
	}
	return 0;   
}

static ssize_t apds993x_run_ps_cali(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	//struct apds993x_data *data = i2c_get_clientdata(client);
	int ret;

	ret = apds993x_cali_process(client);
	return sprintf(buf, "%d\n",ret);
}

static DEVICE_ATTR(pscali, S_IWUSR | S_IWGRP | S_IRUGO, apds993x_run_ps_cali, NULL);

static ssize_t apds993x_show_als_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int tmp;
	int tmp1;
	int luxvalue = 0;
	tmp = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_CH0DATAL_REG);
	tmp1 = i2c_smbus_read_word_data(client, CMD_WORD|APDS993X_CH1DATAL_REG);

	luxvalue = LuxCalculation(client, tmp, tmp1);

	return sprintf(buf, "%d", luxvalue);
}

static DEVICE_ATTR(als_data, S_IWUSR | S_IWGRP | S_IRUGO, apds993x_show_als_data, NULL);

/*calibration sysfs*/
static ssize_t apds993x_show_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int status;
	int rdata;

	mutex_lock(&data->update_lock);
	status = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_STATUS_REG);
	rdata = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_ENABLE_REG);
	mutex_unlock(&data->update_lock);

	pr_info("%s: APDS993x_ENABLE_REG=%2d APDS993x_STATUS_REG=%2d\n", __func__, rdata, status);

	return sprintf(buf, "%d\n", status);
}

static DEVICE_ATTR(status, S_IRUSR | S_IRGRP, apds993x_show_status, NULL);


#ifdef APDS993X_HAL_USE_SYS_ENABLE
static ssize_t apds993x_show_enable_ps_sensor(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&data->enable_ps_sensor));
}

static ssize_t apds993x_store_enable_ps_sensor(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	pr_debug("%s: val=%ld\n", __func__, val);

	if (val != 0 && val != 1) {
		pr_err("%s: invalid value(%ld)\n", __func__, val);
		return -EINVAL;
	}

	apds993x_enable_ps_sensor(client, val);

	return count;
}

static DEVICE_ATTR(enable_ps_sensor, S_IWUSR | S_IWGRP | S_IRUGO,
		apds993x_show_enable_ps_sensor,
		apds993x_store_enable_ps_sensor);

static ssize_t apds993x_show_enable_als_sensor(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&data->enable_als_sensor));
}

static ssize_t apds993x_store_enable_als_sensor(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	pr_debug("%s: val=%ld\n", __func__, val);

	if (val != 0 && val != 1) {
		pr_err("%s: invalid value(%ld)\n", __func__, val);
		return -EINVAL;
	}

	apds993x_enable_als_sensor(client, val);

	return count;
}

static int apds993x_als_set_enable(struct sensors_classdev *sensors_cdev,
		unsigned int enable)
{
	struct apds993x_data *data = container_of(sensors_cdev,
			struct apds993x_data, als_cdev);

	if ((enable != 0) && (enable != 1)) {
		pr_err("%s: invalid value(%d)\n", __func__, enable);
		return -EINVAL;
	}

	return apds993x_enable_als_sensor(data->client, enable);
}

static int apds993x_ps_set_enable(struct sensors_classdev *sensors_cdev,
		unsigned int enable)
{
	struct apds993x_data *data = container_of(sensors_cdev,
			struct apds993x_data, ps_cdev);

	if ((enable != 0) && (enable != 1)) {
		pr_err("%s: invalid value(%d)\n", __func__, enable);
		return -EINVAL;
	}

	return apds993x_enable_ps_sensor(data->client, enable);
}

static DEVICE_ATTR(enable_als_sensor, S_IWUSR | S_IWGRP | S_IRUGO,
		apds993x_show_enable_als_sensor,
		apds993x_store_enable_als_sensor);

static ssize_t apds993x_show_als_poll_delay(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	/* return in micro-second */
	return sprintf(buf, "%d\n", data->als_poll_delay * 1000);
}

static ssize_t apds993x_store_als_poll_delay(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	return count;
}

static DEVICE_ATTR(als_poll_delay, S_IWUSR | S_IRUGO,
		apds993x_show_als_poll_delay, apds993x_store_als_poll_delay);

#endif
static ssize_t apds993x_show_type(struct device *dev,
		struct device_attribute *attr, char *buf){
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->type);
}

static DEVICE_ATTR(type, S_IWUSR | S_IRUGO, apds993x_show_type, NULL);//Add for EngineerMode

static ssize_t apds993x_show_control(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->control);
}

static ssize_t apds993x_store_control(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	return count;

}

static DEVICE_ATTR(control, S_IWUSR | S_IRUGO, apds993x_show_control, apds993x_store_control);//Add for EngineerMode

static ssize_t apds993x_show_piht(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d", data->piht);
}

static ssize_t apds993x_store_piht(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	struct apds993x_data *data = i2c_get_clientdata(client);


	data->piht = val;
	return count;
}

static DEVICE_ATTR(piht, S_IWUSR | S_IWGRP | S_IRUGO, apds993x_show_piht, apds993x_store_piht);//Add for EngineerMode

static ssize_t apds993x_show_pilt(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d", data->pilt);
}

static ssize_t apds993x_store_pilt(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	struct apds993x_data *data = i2c_get_clientdata(client);

	data->pilt = val;		
	return count;
}

static DEVICE_ATTR(pilt, S_IWUSR | S_IWGRP | S_IRUGO, apds993x_show_pilt, apds993x_store_pilt);//Add for EngineerMode

static ssize_t apds993x_show_ppcount(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->ppcount);
}

static ssize_t apds993x_store_ppcount(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{	
	return count;
}

static DEVICE_ATTR(ppcount, S_IWUSR | S_IRUGO, apds993x_show_ppcount, apds993x_store_ppcount);//Add for EngineerMode

static ssize_t apds993x_show_poffset(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", data->poffset);
}

static ssize_t apds993x_store_poffset(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{			
	return count;
}

static DEVICE_ATTR(poffset, S_IWUSR | S_IRUGO, apds993x_show_poffset, apds993x_store_poffset);//Add for EngineerMode

static ssize_t apds993x_show_regval(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct apds993x_data *data = i2c_get_clientdata(client);
	int status,enable,control,config,ppcount,poffset;

	mutex_lock(&data->update_lock);
	status = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_STATUS_REG);
	enable = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_ENABLE_REG);
	control = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_CONTROL_REG);
	config =  i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_CONFIG_REG);
	ppcount =  i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_PPCOUNT_REG);
	poffset =  i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_POFFSET_REG);
	mutex_unlock(&data->update_lock);

	return sprintf(buf, " status=%x \n enable=%x \n control=%x\n config=%x\n ppcount=%x\n poffset=%x \n", status,enable,control,config,ppcount,poffset);

}

static DEVICE_ATTR(regval, S_IWUSR | S_IRUGO, apds993x_show_regval, NULL);//Add for EngineerMode

static struct attribute *apds993x_attributes[] = {
	&dev_attr_ch0data.attr,
	&dev_attr_ch1data.attr,
	&dev_attr_pdata.attr,
	&dev_attr_pscali.attr,
	&dev_attr_als_data.attr,
#ifdef APDS993X_HAL_USE_SYS_ENABLE
	&dev_attr_enable_ps_sensor.attr,
	&dev_attr_enable_als_sensor.attr,
	&dev_attr_als_poll_delay.attr,
#endif
	/*calibration*/
	&dev_attr_status.attr,
	&dev_attr_type.attr,
	&dev_attr_control.attr,
	&dev_attr_piht.attr,
	&dev_attr_pilt.attr,
	&dev_attr_ppcount.attr,
	&dev_attr_poffset.attr,
	&dev_attr_regval.attr,
	NULL
};

static const struct attribute_group apds993x_attr_group = {
	.attrs = apds993x_attributes,
};
/*
 * Initialization function
 */
static int apds993x_init_device(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int err;

	err = apds993x_set_enable(client, 0);
	if (err < 0)
		return err;

	/* 100.64ms ALS integration time */
	err = apds993x_set_atime(client,
			apds993x_als_atime_tb[data->als_atime_index]);
	if (err < 0)
		return err;

	/* 2.72ms Prox integration time */
	err = apds993x_set_ptime(client, 0xFF);
	if (err < 0)
		return err;

	/* 2.72ms Wait time */
	err = apds993x_set_wtime(client, 0xFF);
	if (err < 0)
		return err;

	err = apds993x_set_ppcount(client, apds993x_ps_pulse_number);
	if (err < 0)
		return err;

	/* no long wait */
	err = apds993x_set_config(client, 0);
	if (err < 0)
		return err;

	err = apds993x_set_control(client,
			APDS993X_PDRVIE_50MA |
			APDS993X_PRX_IR_DIOD |
			apds993x_ps_pgain |
			apds993x_als_again_bit_tb[data->als_again_index]);
	if (err < 0)
		return err;

	/*calirbation*/
	data->ps_detection = 0; /* initial value = far*/

	/* 2 consecutive Interrupt persistence */
	err = apds993x_set_pers(client, APDS993X_PPERS_2|APDS993X_APERS_2);
	if (err < 0)
		return err;

	/* sensor is in disabled mode but all the configurations are preset */
	return 0;
}

static int apds993x_init_client(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int err;
	int id;

	err = apds993x_set_enable(client, 0);
	if (err < 0)
		return err;

	id = i2c_smbus_read_byte_data(client, CMD_BYTE|APDS993X_ID_REG);
	pr_info("apds993x_init_client chip id :%x !!\n", id);
	if (id == 0x30) {
		pr_info("%s: APDS9931\n", __func__);
	} else if (id == 0x39) {
		pr_info("%s: APDS9930\n", __func__);
	} else if (id == 0x29) {
		pr_info("%s: APDS9900\n", __func__);
	} else {
		pr_info("%s: Neither APDS9931 nor APDS9930\n", __func__);
		return -ENODEV;
	}

	/* 100.64ms ALS integration time */
	err = apds993x_set_atime(client,
			apds993x_als_atime_tb[data->als_atime_index]);
	if (err < 0)
		return err;

	/* 2.72ms Prox integration time */
	err = apds993x_set_ptime(client, 0xFF);
	if (err < 0)
		return err;

	/* 2.72ms Wait time */
	err = apds993x_set_wtime(client, 0xFF);
	if (err < 0)
		return err;

	err = apds993x_set_ppcount(client, apds993x_ps_pulse_number);
	if (err < 0)
		return err;

	/* no long wait */
	err = apds993x_set_config(client, 0);
	if (err < 0)
		return err;

	err = apds993x_set_control(client,
			APDS993X_PDRVIE_50MA |
			APDS993X_PRX_IR_DIOD |
			apds993x_ps_pgain |
			apds993x_als_again_bit_tb[data->als_again_index]);
	if (err < 0)
		return err;

	/* init threshold for proximity */
	err = apds993x_set_pilt(client, 0);
	if (err < 0)
		return err;

	err = apds993x_set_piht(client, apds993x_ps_detection_threshold);
	if (err < 0)
		return err;

	/*calirbation*/
	data->ps_detection = 0; /* initial value = far*/



	/* 2 consecutive Interrupt persistence */
	err = apds993x_set_pers(client, APDS993X_PPERS_2|APDS993X_APERS_2);
	if (err < 0)
		return err;

	/* sensor is in disabled mode but all the configurations are preset */
	return 0;
}

static int apds993x_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct apds993x_data *data =  i2c_get_clientdata(client);
	int rc;

	pr_info("##################%s, als, ps is %d, %d\n", __func__, atomic_read(&data->enable_als_sensor), atomic_read(&data->enable_ps_sensor));
	data->als_suspend_state = atomic_read(&data->enable_als_sensor);
	data->ps_suspend_state = atomic_read(&data->enable_ps_sensor);
	if (data->als_suspend_state) 
	{
		rc = apds993x_enable_als_sensor(data->client, 0);
		if (rc)
			dev_err(&data->client->dev, "Disable light sensor fail! rc=%d\n", rc);
	}
	if(!data->ps_suspend_state)
	{
		cancel_delayed_work(&data->dwork);
		flush_delayed_work(&data->dwork);	
		irq_set_irq_wake(client->irq, 0);
		disable_irq(data->irq); 
	}
	return 0;
}

static int apds993x_resume(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	int rc;
	pr_info("##################%s, als, ps is %d, %d\n", __func__, atomic_read(&data->enable_als_sensor), atomic_read(&data->enable_ps_sensor));
	if (data->als_suspend_state) 
	{
		rc = apds993x_enable_als_sensor(data->client, 1);
		if (rc)
			dev_err(&data->client->dev, "Enable light sensor fail! rc=%d\n", rc);
	}  
	if(!data->ps_suspend_state)
	{
		enable_irq(data->irq); 
		irq_set_irq_wake(client->irq, 1);
	}
	return 0;
}

static int sensor_regulator_configure(struct apds993x_data *data, bool on)
{
	int rc;

	if (!on) {
		if (regulator_count_voltages(data->vdd) > 0)
			regulator_set_voltage(data->vdd, 0,
					APDS993X_VDD_MAX_UV);

		regulator_put(data->vdd);
		regulator_disable(data->vdd);

		if (regulator_count_voltages(data->vio) > 0)
			regulator_set_voltage(data->vio, 0,
					APDS993X_VIO_MAX_UV);

		regulator_put(data->vio);
		regulator_disable(data->vio);

	} else {
		data->vdd = regulator_get(&data->client->dev, "vdd");
		if (IS_ERR(data->vdd)) {
			rc = PTR_ERR(data->vdd);
			dev_err(&data->client->dev,
					"Regulator get failed vdd rc=%d\n", rc);
			return rc;
		}

		if (regulator_count_voltages(data->vdd) > 0) {
			rc = regulator_set_voltage(data->vdd,
					APDS993X_VDD_MIN_UV, APDS993X_VDD_MAX_UV);
			if (rc) {
				dev_err(&data->client->dev,
						"Regulator set failed vdd rc=%d\n",
						rc);
				goto reg_vdd_put;
			}
		}

		data->vio = regulator_get(&data->client->dev, "vio");
		if (IS_ERR(data->vio)) {
			rc = PTR_ERR(data->vio);
			dev_err(&data->client->dev,
					"Regulator get failed vio rc=%d\n", rc);
			goto reg_vdd_set;
		}

		if (regulator_count_voltages(data->vio) > 0) {
			rc = regulator_set_voltage(data->vio,
					APDS993X_VIO_MIN_UV, APDS993X_VIO_MAX_UV);
			if (rc) {
				dev_err(&data->client->dev,
						"Regulator set failed vio rc=%d\n", rc);
				goto reg_vio_put;
			}
		}
	}

	return 0;
reg_vio_put:
	regulator_put(data->vio);

reg_vdd_set:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, APDS993X_VDD_MAX_UV);
reg_vdd_put:
	regulator_put(data->vdd);
	return rc;
}


static int sensor_regulator_power_on(struct apds993x_data *data, bool on)
{
	int rc = 0;

	if (!on) {
		rc = regulator_disable(data->vdd);
		if (rc) {
			dev_err(&data->client->dev,
					"Regulator vdd disable failed rc=%d\n", rc);
			return rc;
		}

		rc = regulator_disable(data->vio);
		if (rc) {
			dev_err(&data->client->dev,
					"Regulator vio disable failed rc=%d\n", rc);
			rc = regulator_enable(data->vdd);
		}
	} else {
		rc = regulator_enable(data->vdd);
		if (rc) {
			dev_err(&data->client->dev,
					"Regulator vdd enable failed rc=%d\n", rc);
			return rc;
		}

		rc = regulator_enable(data->vio);
		if (rc) {
			dev_err(&data->client->dev,
					"Regulator vio enable failed rc=%d\n", rc);
			rc = regulator_disable(data->vdd);
		}
	}

	msleep(130);

	return rc;
}

static int sensor_platform_hw_power_on(bool on)
{
	if (pdev_data == NULL)
		return -ENODEV;

	sensor_regulator_power_on(pdev_data, on);

	return 0;
}

static int sensor_platform_hw_init(void)
{
	struct i2c_client *client;
	struct apds993x_data *data;
	int error;

	if (pdev_data == NULL)
		return -ENODEV;

	data = pdev_data;
	client = data->client;

	error = sensor_regulator_configure(data, true);
	if (error < 0) {
		dev_err(&client->dev, "unable to configure regulator\n");
		return error;
	}

	if (gpio_is_valid(data->platform_data->irq_gpio)) {
		/* configure apds993x irq gpio */
		error = gpio_request_one(data->platform_data->irq_gpio,
				GPIOF_DIR_IN,
				"apds993x_irq_gpio");
		if (error) {
			dev_err(&client->dev, "unable to request gpio %d\n",
					data->platform_data->irq_gpio);
		}
		data->irq = client->irq =
			gpio_to_irq(data->platform_data->irq_gpio);
	} else {
		dev_err(&client->dev, "irq gpio not provided\n");
	}
	return 0;
}

static void sensor_platform_hw_exit(void)
{
	struct apds993x_data *data = pdev_data;

	if (data == NULL)
		return;

	sensor_regulator_configure(data, false);

	if (gpio_is_valid(data->platform_data->irq_gpio))
		gpio_free(data->platform_data->irq_gpio);
}

static int sensor_parse_dt(struct device *dev,
		struct apds993x_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	unsigned int tmp;
	int rc = 0;

	/* set functions of platform data */
	pdata->init = sensor_platform_hw_init;
	pdata->exit = sensor_platform_hw_exit;
	pdata->power_on = sensor_platform_hw_power_on;

	/* irq gpio */
	rc = of_get_named_gpio_flags(dev->of_node,
			"avago,irq-gpio", 0, NULL);
	if (rc < 0) {
		dev_err(dev, "Unable to read irq gpio\n");
		return rc;
	}
	pdata->irq_gpio = rc;

	/* ps tuning data*/
	rc = of_property_read_u32(np, "avago,ps_threshold", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ps_threshold\n");
		return rc;
	}
	pdata->prox_threshold = tmp;

	rc = of_property_read_u32(np, "avago,ps_hysteresis_threshold", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ps_hysteresis_threshold\n");
		return rc;
	}
	pdata->prox_hsyteresis_threshold = tmp;

	rc = of_property_read_u32(np, "avago,ps_pulse", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ps_pulse\n");
		return rc;
	}
	pdata->prox_pulse = tmp;

	rc = of_property_read_u32(np, "avago,ps_pgain", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ps_pgain\n");
		return rc;
	}
	pdata->prox_gain = tmp;

	/* ALS tuning value */
	rc = of_property_read_u32(np, "avago,als_B", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read apds993x_coe_b\n");
		return rc;
	}
	pdata->als_B = tmp;

	rc = of_property_read_u32(np, "avago,als_C", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read apds993x_coe_c\n");
		return rc;
	}
	pdata->als_C = tmp;

	rc = of_property_read_u32(np, "avago,als_D", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read apds993x_coe_d\n");
		return rc;
	}
	pdata->als_D = tmp;

	rc = of_property_read_u32(np, "avago,ga_value", &tmp);
	if (rc) {
		dev_err(dev, "Unable to read ga_value\n");
		return rc;
	}
	pdata->ga_value = tmp;

	return 0;
}

/*
 * I2C init/probing/exit functions
 */
static struct i2c_driver apds993x_driver;
static int apds993x_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct apds993x_data *data;
	struct apds993x_platform_data *pdata;
	int err = 0;

	pr_debug("%s\n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)) {
		err = -EIO;
		goto exit;
	}

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
				sizeof(struct apds993x_platform_data),
				GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		client->dev.platform_data = pdata;
		err = sensor_parse_dt(&client->dev, pdata);
		if (err) {
			pr_err("%s: sensor_parse_dt() err\n", __func__);
			return err;
		}
	} else {
		pdata = client->dev.platform_data;
		if (!pdata) {
			dev_err(&client->dev, "No platform data\n");
			return -ENODEV;
		}
	}

	/* Set the default parameters */
	apds993x_ps_detection_threshold = pdata->prox_threshold;
	apds993x_ps_hsyteresis_threshold = pdata->prox_hsyteresis_threshold;
	apds993x_ps_pulse_number = pdata->prox_pulse;
	apds993x_ps_pgain = pdata->prox_gain;

	apds993x_coe_b = pdata->als_B;
	apds993x_coe_c = pdata->als_C;
	apds993x_coe_d = pdata->als_D;
	apds993x_ga = pdata->ga_value;

	data = kzalloc(sizeof(struct apds993x_data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		err = -ENOMEM;
		goto exit;
	}
	pdev_data = data;

	data->platform_data = pdata;
	data->client = client;
	apds993x_i2c_client = client;

	/* h/w initialization */
	if (pdata->init)
		err = pdata->init();

	if (pdata->power_on)
		err = pdata->power_on(true);

	i2c_set_clientdata(client, data);

	data->enable = 0;	/* default mode is standard */
	data->ps_threshold = apds993x_ps_detection_threshold;
	data->ps_hysteresis_threshold = apds993x_ps_hsyteresis_threshold;
	data->pilt = data->ps_hysteresis_threshold;
	data->piht = data->ps_threshold;
	data->auto_pilt = 0;
	data->auto_piht = 0;

	data->ps_detection = 0;	/* default to no detection */
	atomic_set(&data->enable_als_sensor ,0);	// default to 0
	atomic_set(&data->enable_ps_sensor , 0);	// default to 0
	data->als_poll_delay = 100;	// default to 100ms
	data->als_atime_index = APDS993X_ALS_RES_37888;	// 100ms ATIME
	data->als_again_index = APDS993X_ALS_GAIN_8X;	// 8x AGAIN
	data->als_reduce = 0;	// no ALS 6x reduction
	data->als_prev_lux = 0;
	data->type  = 0;
	data->poffset = APDS993X_DEFAULT_POFFSET;
	wake_lock_init(&ps_lock, WAKE_LOCK_SUSPEND, "ps wakelock") ;

	mutex_init(&data->update_lock);

	/* Initialize the APDS993X chip */
	err = apds993x_init_client(client);
	if (err) {
		pr_err("%s: Failed to init apds993x\n", __func__);
		goto exit_kfree;
	}

	err = request_irq(data->irq, apds993x_interrupt, IRQF_TRIGGER_FALLING,
			APDS993X_DRV_NAME, (void *)client);
	if (err < 0) {
		pr_err("%s: Could not allocate APDS993X_INT !\n", __func__);
		goto exit_kfree;
	}

	irq_set_irq_wake(client->irq, 1);

	INIT_DELAYED_WORK(&data->dwork, apds993x_work_handler);

#ifdef ALS_POLLING_ENABLED
	INIT_DELAYED_WORK(&data->als_dwork, apds993x_als_polling_work_handler);
#endif

	/* Register to Input Device */
	data->input_dev_als = input_allocate_device();
	if (!data->input_dev_als) {
		err = -ENOMEM;
		pr_err("%s: Failed to allocate input device als\n", __func__);
		goto exit_free_dev_ps;
	}

	data->input_dev_ps = input_allocate_device();
	if (!data->input_dev_ps) {
		err = -ENOMEM;
		pr_err("%s: Failed to allocate input device ps\n", __func__);
		goto exit_free_dev_als;
	}

	set_bit(EV_ABS, data->input_dev_als->evbit);
	set_bit(EV_ABS, data->input_dev_ps->evbit);

	input_set_abs_params(data->input_dev_als, ABS_MISC, 0, 30000, 0, 0);
	input_set_abs_params(data->input_dev_ps, ABS_DISTANCE, 0, 5, 0, 0);

	data->input_dev_als->name = "light";
	data->input_dev_ps->name = "proximity";

	err = input_register_device(data->input_dev_als);
	if (err) {
		err = -ENOMEM;
		pr_err("%s: Unable to register input device als: %s\n",
				__func__, data->input_dev_als->name);
		goto exit_unregister_dev_als;
	}

	err = input_register_device(data->input_dev_ps);
	if (err) {
		err = -ENOMEM;
		pr_err("%s: Unable to register input device ps: %s\n",
				__func__, data->input_dev_ps->name);
		goto exit_unregister_dev_ps;
	}

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &apds993x_attr_group);
	if (err)
		goto exit_remove_sysfs_group;
	/* Register to sensors class */
	data->als_cdev = sensors_light_cdev;
	data->als_cdev.sensors_enable = apds993x_als_set_enable;
	data->als_cdev.sensors_poll_delay = NULL;

	data->ps_cdev = sensors_proximity_cdev;
	data->ps_cdev.sensors_enable = apds993x_ps_set_enable;
	data->ps_cdev.sensors_poll_delay = NULL;

	err = sensors_classdev_register(&client->dev, &data->als_cdev);
	if (err) {
		pr_err("%s: Unable to register to sensors class: %d\n",
				__func__, err);
		goto exit_unregister_als_class;
	}

	err = sensors_classdev_register(&client->dev, &data->ps_cdev);
	if (err) {
		pr_err("%s: Unable to register to sensors class: %d\n",
				__func__, err);
		goto exit_unregister_ps_class;
	}

	if (pdata->power_on)
		err = pdata->power_on(false);

	pr_info("%s: Support ver. %s enabled\n", __func__, DRIVER_VERSION);

	return 0;
exit_unregister_ps_class:
	sensors_classdev_unregister(&data->ps_cdev);
exit_unregister_als_class:
	sensors_classdev_unregister(&data->als_cdev);
exit_remove_sysfs_group:
	sysfs_remove_group(&client->dev.kobj, &apds993x_attr_group);
exit_unregister_dev_ps:
	input_unregister_device(data->input_dev_ps);
exit_unregister_dev_als:
	input_unregister_device(data->input_dev_als);
exit_free_dev_ps:
exit_free_dev_als:
	free_irq(data->irq, client);
exit_kfree:
	if (pdata->power_on)
		pdata->power_on(false);
	if (pdata->exit)
		pdata->exit();

	kfree(data);
	pdev_data = NULL;
exit:
	return err;
}

static int apds993x_remove(struct i2c_client *client)
{
	struct apds993x_data *data = i2c_get_clientdata(client);
	struct apds993x_platform_data *pdata = data->platform_data;

	/* Power down the device */
	apds993x_set_enable(client, 0);
	sysfs_remove_group(&client->dev.kobj, &apds993x_attr_group);

	input_unregister_device(data->input_dev_ps);
	input_unregister_device(data->input_dev_als);

	free_irq(client->irq, data);

	if (pdata->power_on)
		pdata->power_on(false);

	if (pdata->exit)
		pdata->exit();

	kfree(data);
	pdev_data = NULL;

	return 0;
}

static const struct i2c_device_id apds993x_id[] = {
	{ "apds993x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, apds993x_id);

static struct of_device_id apds993X_match_table[] = {
	{ .compatible = "avago,apds9930",},
	{ },
};

static struct i2c_driver apds993x_driver = {
	.driver = {
		.name   = APDS993X_DRV_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = apds993X_match_table,
	},
	.probe  = apds993x_probe,
	.remove = apds993x_remove,
	.id_table = apds993x_id,
	.suspend	= apds993x_suspend,
	.resume 	= apds993x_resume,
};

static int __init apds993x_init(void)
{
	apds993x_workqueue = create_workqueue("proximity_als");
	if (!apds993x_workqueue) {
		pr_err("%s: out of memory\n", __func__);
		return -ENOMEM;
	}

	return i2c_add_driver(&apds993x_driver);
}

static void __exit apds993x_exit(void)
{
	if (apds993x_workqueue)
		destroy_workqueue(apds993x_workqueue);
	i2c_del_driver(&apds993x_driver);
}

MODULE_AUTHOR("Lee Kai Koon <kai-koon.lee@avagotech.com>");
MODULE_DESCRIPTION("APDS993X ambient light + proximity sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(apds993x_init);
module_exit(apds993x_exit);
