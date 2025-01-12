
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
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
//#include <linux/platform_data/mms_ts.h>
#include "mms_ts.h"
#include <linux/completion.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

/* Melfas TSP test mode */
//#define __MMS_TEST_MODE__

/* Melfas TSP log mode */
//#define __MMS_LOG_MODE__


/* Flag to enable touch key */
#define MMS_HAS_TOUCH_KEY		1
	
#define ESD_DETECT_COUNT		10

/*
 * ISC_XFER_LEN	- ISC unit transfer length.
 * Give number of 2 power n, where  n is between 2 and 10 
 * i.e. 4, 8, 16 ,,, 1024 
 */
#define ISC_XFER_LEN			128 //1024
#define MMS_I2C_RETRY_TIMES		5
#define __MMS_TEST_MODE__
#define MMS_FLASH_PAGE_SZ		1024
#define ISC_BLOCK_NUM			(MMS_FLASH_PAGE_SZ / ISC_XFER_LEN)

#define FLASH_VERBOSE_DEBUG		1
#define MAX_SECTION_NUM			3

#define MAX_FINGER_NUM			5
#define FINGER_EVENT_SZ			8
#define MAX_WIDTH			30
#define MAX_PRESSURE			255
#define MAX_LOG_LENGTH			128
#define MMS_MAX_X				720//1080	
#define MMS_MAX_Y				1280//1920

/* Registers */
#define MMS_MODE_CONTROL		0x01
#define MMS_TX_NUM			0x0B
#define MMS_RX_NUM			0x0C
#define MMS_KEY_NUM			0x0D
#define MMS_EVENT_PKT_SZ		0x0F
#define MMS_INPUT_EVENT		0x10
#define MMS_UNIVERSAL_CMD		0xA0
#define MMS_UNIVERSAL_RESULT		0xAF
#define MMS_GESTURE_SUPPORT		0xA5

#define MMS_CMD_ENTER_ISC		0x5F
#define MMS_FW_VERSION			0xE1
#define MMS_POWER_CONTROL		0xB0

	
/* Universal commands */	
#define MMS_CMD_SET_LOG_MODE		0x20
#define MMS_EVENT_PKT_SZ		0x0F
#define MMS_INPUT_EVENT			0x10
#define MMS_UNIVERSAL_CMD		0xA0
#define MMS_UNIVERSAL_RESULT		0xAF
#define MMS_UNIVERSAL_RESULT_LENGTH	0xAE
#define MMS_UNIV_ENTER_TEST		0x40
#define MMS_UNIV_TEST_CM		0x41
#define MMS_UNIV_GET_DELTA		0x42
#define MMS_UNIV_GET_KEY_DELTA		0x4A
#define MMS_UNIV_GET_ABS		0x44
#define MMS_UNIV_GET_KEY_ABS		0x4B
#define MMS_UNIV_TEST_JITTER		0x45
#define MMS_UNIV_GET_JITTER		0x46
#define MMS_UNIV_GET_KEY_JITTER		0x4C
#define MMS_UNIV_EXIT_TEST		0x4F
#define MMS_UNIV_INTENSITY		0x70
#define MMS_UNIV_KEY_INTENSITY		0x71

/* Event types */
#define MMS_LOG_EVENT			0xD
#define MMS_NOTIFY_EVENT		0xE
#define MMS_ERROR_EVENT			0xF
#define MMS_TOUCH_KEY_EVENT		0x40
#define MMS_TOUCH_GESTURE_EVENT	0x60
	
/* Firmware file name */
#define MMS_FW_UPDATE
#define FW_NAME				"mms_ts_R29"//	"mms_ts"//"mms_ts.fw"
#define FW_VERSION_TO_UPDATE	0x29
#define EXTRA_FW_PATH			"/sdcard/mms_ts.mfsb"

#define MMS_DEBUG_ON 0
enum{
	SYS_TXNUM = 3,
	SYS_RXNUM,
	SYS_CLEAR,
	SYS_ENABLE,
	SYS_DISABLE,
	SYS_INTERRUPT,
	SYS_RESET,
};

enum {
	GET_RX_NUM	= 1,
	GET_TX_NUM,
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

enum {
	ISC_ADDR		= 0xD5,

	ISC_CMD_READ_STATUS	= 0xD9,	
	ISC_CMD_READ		= 0x4000,
	ISC_CMD_EXIT		= 0x8200,
	ISC_CMD_PAGE_ERASE	= 0xC000,
	ISC_CMD_ERASE		= 0xC100,
	ISC_PAGE_ERASE_DONE	= 0x10000,
	ISC_PAGE_ERASE_ENTER	= 0x20000,
};

struct mms_ts_info {
	struct i2c_client 		*client;
	struct input_dev 		*input_dev;
	char 				phys[32];

	u8				tx_num;
	u8				rx_num;
	u8				key_num;
	int 				irq;
	
	int				data_cmd;

	struct mms_ts_platform_data 	*pdata;
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
	struct mutex 			mms_i2c_mutex;
	bool				enabled;
#ifdef __MMS_LOG_MODE__
	struct cdev			cdev;
	dev_t				mms_dev;
	struct class			*class;

	struct mms_log_data {
		u8			*data;
		int			cmd;
	} log;
#endif
	u8				*get_data;
	bool suspended;
	bool disable_gpios;
	bool fw_updating;
	bool gesture_open;
	bool test_mode;
	bool power_down;
	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
	struct work_struct mms_ts_work;
};

struct mms_bin_hdr {
	char	tag[8];
	u16	core_version;
	u16	section_num;
	u16	contains_full_binary;
	u16	reserved0;

	u32	binary_offset;
	u32	binary_length;

	u32	extention_offset;	
	u32	reserved1;
	
} __attribute__ ((packed));

struct mms_fw_img {
	u16	type;
	u16	version;

	u16	start_page;
	u16	end_page;

	u32	offset;
	u32	length;

} __attribute__ ((packed));

struct isc_packet {
	u8	cmd;
	u32	addr;
	u8	data[0];
} __attribute__ ((packed));

#define MMS_DEBUG(fmt,arg...)          do{\
                                         if(MMS_DEBUG_ON)\
                                         printk("mms:"fmt"\n",##arg);\
                                       }while(0)

void mms_reboot(struct mms_ts_info *info);
void mms_fw_update_controller(const struct firmware *fw, void * context);
int mms_flash_fw(struct mms_ts_info *info, const u8 *data,int size, bool force);
void mms_report_input_data(struct mms_ts_info *info, u8 sz, u8 *buf);
void mms_clear_input_data(struct mms_ts_info *info);

#ifdef __MMS_LOG_MODE__
int mms_ts_log(struct mms_ts_info *info);
#endif

#ifdef __MMS_TEST_MODE__

int mms_sysfs_test_mode(struct mms_ts_info *info);
void mms_sysfs_remove(struct mms_ts_info *info);

int get_cm_test_init(struct mms_ts_info *info);
int get_cm_test_exit(struct mms_ts_info *info);
int get_cm_delta(struct mms_ts_info *info);
int get_cm_abs(struct mms_ts_info *info);
int get_cm_jitter(struct mms_ts_info *info);
int get_intensity(struct mms_ts_info *info);
 int mms_read_reg(struct mms_ts_info *data, unsigned int reg);
#endif
