/*
 * MELFAS MIT200 Touchscreen Driver
 *
 * Copyright (C) 2014 MELFAS Inc.
 *
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <mit_ts.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

/* Firmware file name */
#define FW_NAME					"mit_ts.fw"
#define EXTRA_FW_PATH				"/sdcard/mit_ts.fw"
#define MIT_UPDATE_FW 0
#define MIT_SLIDE_WAKEUP 0
#define MIT_GLOVE_MODE 1
#define MIT_SYS_FILE 1
#define MIT_CANCEL_Y 0
#define MIT_CANCEL_X 0
#define MIT_DISCARD_X 0
#define MIT_DISCARD_Y 0
/* MIT TEST mode */
//#define __MIT_TEST_MODE__

/*  Show the touch log  */
#define KERNEL_LOG_MSG				1
#define VERSION_NUM				2
#define MAX_FINGER_NUM				10
#define FINGER_EVENT_SZ				8
#define NAP_EVENT_SZ			3
#define NAP_EVENT_SZ			3
#define MAX_WIDTH				30
#define MAX_PRESSURE				255
#define MAX_LOG_LENGTH				128
#define MIT_MAX_X				1080	
#define MIT_MAX_Y				1920

/* Registers */

#define MIT_REGH_CMD				0x10		

#define MIT_REGL_MODE_CONTROL			0x01
#define MIT_REGL_ROW_NUM			0x0B
#define MIT_REGL_COL_NUM			0x0C
#define MIT_REGL_EVENT_PKT_SZ			0x0F
#define MIT_REGL_INPUT_EVENT			0x10
#define MIT_REGL_NAP_START				0x90
#define MIT_REGL_UCMD				0xA0
#define MIT_REGL_UCMD_RESULT_LENGTH		0xAE
#define MIT_REGL_UCMD_RESULT			0xAF
#define MIT_FW_VERSION				0xC2

#define MIT_LPWG_MODE				0x80
#define MIT_LPWG_REPORTRATE		0x81
#define MIT_LPWG_TOUCH_SLOP		0x82
#define MIT_LPWG_TAP_DISTANCE		0x83
#define MIT_LPWG_TAP_TIME_GAP		0x84
#define MIT_LPWG_TOTAL_TAP_COUNT	0x86
#define MIT_LPWG_ACTIVE_AREA		0x87
#define MIT_LPWG_STORE_INFO		0x8F
#define MIT_LPWG_START				0x90


/* Universal commands */
#define MIT_UNIV_ENTER_TESTMODE			0x40
#define MIT_UNIV_TESTA_START			0x41
#define MIT_UNIV_GET_RAWDATA			0x44
#define MIT_UNIV_TESTB_START			0x48
#define MIT_UNIV_GET_OPENSHORT_TEST		0x50
#define MIT_UNIV_EXIT_TESTMODE			0x6F

/* Event types */
#define MIT_ET_LOG				0xD
#define MIT_ET_NAP				0xE
#define MIT_ET_ERROR				0xF

/* ISC mode */
#define ISC_PAGE_WRITE				{0xFB, 0x4A, 0x00, 0x5F, 0x00, 0x00}
#define ISC_PAGE_PROGRAM			{0xFB, 0x4A, 0x00, 0x54, 0x00, 0x00}
#define ISC_FLASH_READ				{0xFB, 0x4A, 0x00, 0xC2, 0x00, 0x00}
#define ISC_STATUS_READ				{0xFB, 0x4A, 0x00, 0xC8, 0x00, 0x00}
#define ISC_EXIT						{0xFB, 0x4A, 0x00, 0x66, 0x00, 0x00}

#define MIT_DEBUG_ON 1
enum {
	GET_COL_NUM	= 1,
	GET_ROW_NUM,
	GET_EVENT_DATA,
};

enum {
	LOG_TYPE_U08	= 2,
	LOG_TYPE_S08,
	LOG_TYPE_U16,
	LOG_TYPE_S16,
	LOG_TYPE_U32	= 8,
	LOG_TYPE_S32,
};

struct mit_ts_info {
	struct i2c_client 	*client;
	struct input_dev 		*input_dev;
	char 				phys[32];

	u8				row_num;
	u8				col_num;

	int 				irq;

	struct mit_ts_platform_data	*pdata;
    struct regulator *vdd;
	struct regulator *vcc_i2c;
	char 				*fw_name;
	struct completion 		init_done;
#if defined(CONFIG_FB)
	struct notifier_block fb_notif;
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend		early_suspend;
#endif

	struct mutex 			lock;
	bool				enabled;

#ifdef __MIT_TEST_MODE__
	struct cdev			cdev;
	dev_t				mit_dev; 
	struct class			*class;
	
	char				raw_cmd;
	u8				*get_data;
	struct mit_log_data {
		u8			*data;
		int			cmd;
	} log;
#endif
#if MIT_SYS_FILE
	struct class *tp_class; 
	int index;
	struct device *dev;
#endif
};
#define MIT_DEBUG(fmt,arg...)          do{\
                                         if(MIT_DEBUG_ON)\
                                      	printk("mit_ts:"fmt"\n",##arg);\
                                       }while(0)
void mit_clear_input_data(struct mit_ts_info *info);
void mit_report_input_data(struct mit_ts_info *info, u8 sz, u8 *buf);
void mit_reboot(struct mit_ts_info *info);
int get_fw_version(struct i2c_client *client, u8 *buf);
int mit_flash_fw(struct mit_ts_info *info, const u8 *fw_data, size_t fw_size);
int mit_flash_fw_force(struct mit_ts_info *info, const u8 *fw_data, size_t fw_size);
#ifdef __MIT_TEST_MODE__
int mit_sysfs_test_mode(struct mit_ts_info *info);
void mit_sysfs_remove(struct mit_ts_info *info);

int mit_ts_log(struct mit_ts_info *info);
void mit_ts_log_remove(struct mit_ts_info *info);
#endif



