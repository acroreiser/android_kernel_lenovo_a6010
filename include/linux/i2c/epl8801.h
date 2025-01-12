#ifndef __ELAN_EPL8866__
#define __ELAN_EPL8866__


#define ELAN_IOCTL_MAGIC 'c'
#define ELAN_EPL8800_IOCTL_GET_PFLAG _IOR(ELAN_IOCTL_MAGIC, 1, int *)
#define ELAN_EPL8800_IOCTL_GET_LFLAG _IOR(ELAN_IOCTL_MAGIC, 2, int *)
#define ELAN_EPL8800_IOCTL_ENABLE_PFLAG _IOW(ELAN_IOCTL_MAGIC, 3, int *)
#define ELAN_EPL8800_IOCTL_ENABLE_LFLAG _IOW(ELAN_IOCTL_MAGIC, 4, int *)
#define ELAN_EPL8800_IOCTL_GETDATA _IOR(ELAN_IOCTL_MAGIC, 5, int *)

#define MODE_IDLE			(0)
#define MODE_ALS			(1)
#define MODE_PS				(2)
#define MODE_PS_ALS		(5)

#define EPL_MODE_IDLE		(0x00)
#define EPL_MODE_ALS		(0x01)
#define EPL_MODE_PS			(0x02)
#define EPL_MODE_COLOR		(0x03)
#define EPL_MODE_ALS_PS	(0x05)
#define EPL_MODE_PS_COLOR	(0x06)

#define POWER_DOWN		(1)
#define POWER_WAKE			(0)

#define RESET				(0<<1)
#define RUN					(1<<1)

#define EPL_INDEX_1			(0<<5)
#define EPL_INDEX_2			(1<<5)
#define EPL_INDEX_4			(2<<5)
#define EPL_INDEX_8			(3<<5)


#define OSC_1M				(0<<3)
#define OSC_4M				(1<<3)


#define EPL_ALS_INTT_2			(0<<1)
#define EPL_ALS_INTT_3			(1<<1)
#define EPL_ALS_INTT_4			(2<<1)
#define EPL_ALS_INTT_6			(3<<1)
#define EPL_ALS_INTT_8			(4<<1)
#define EPL_ALS_INTT_10			(5<<1)
#define EPL_ALS_INTT_15			(6<<1)
#define EPL_ALS_INTT_20			(7<<1)
#define EPL_ALS_INTT_35			(8<<1)
#define EPL_ALS_INTT_50			(9<<1)
#define EPL_ALS_INTT_70			(10<<1)
#define EPL_ALS_INTT_100		(11<<1)
#define EPL_ALS_INTT_150		(12<<1)
#define EPL_ALS_INTT_250		(13<<1)
#define EPL_ALS_INTT_350		(14<<1)
#define EPL_ALS_INTT_500		(15<<1)
#define EPL_ALS_INTT_750		(16<<1)
#define EPL_ALS_INTT_1000		(17<<1)
#define EPL_ALS_INTT_2000		(18<<1)
#define EPL_ALS_INTT_3000		(19<<1)
#define EPL_ALS_INTT_4000		(20<<1)
#define EPL_ALS_INTT_6000		(21<<1)
#define EPL_ALS_INTT_8000		(22<<1)
#define EPL_ALS_INTT_12000		(23<<1)
#define EPL_ALS_INTT_16000		(24<<1)
static int als_intt_value[] = {2, 3, 4, 6, 8, 10, 15, 20, 35, 50, 70, 100, 150, 250, 350, 500, 750, 1000, 2000, 3000, 4000, 6000, 8000, 12000, 16000};

#define EPL_PS_INTT_4			(0<<1)
#define EPL_PS_INTT_6			(1<<1)
#define EPL_PS_INTT_8			(2<<1)
#define EPL_PS_INTT_10			(3<<1)
#define EPL_PS_INTT_16			(4<<1)
#define EPL_PS_INTT_20			(5<<1)
#define EPL_PS_INTT_26			(6<<1)
#define EPL_PS_INTT_30			(7<<1)
#define EPL_PS_INTT_40			(8<<1)
#define EPL_PS_INTT_56			(9<<1)
#define EPL_PS_INTT_70			(10<<1)
#define EPL_PS_INTT_90			(11<<1)
#define EPL_PS_INTT_110			(12<<1)
#define EPL_PS_INTT_150			(13<<1)
#define EPL_PS_INTT_200			(14<<1)
#define EPL_PS_INTT_250			(15<<1)
#define EPL_PS_INTT_350			(16<<1)
static int ps_intt_value[] = {4, 6, 8, 10, 16, 20, 26, 30, 40, 56, 70, 90, 110, 150, 200, 250, 350};

#define EPL_WAIT_0_MS			(0<<4)
#define EPL_WAIT_2_MS			(1<<4)
#define EPL_WAIT_4_MS			(2<<4)
#define EPL_WAIT_8_MS			(3<<4)
#define EPL_WAIT_12_MS			(4<<4)
#define EPL_WAIT_20_MS			(5<<4)
#define EPL_WAIT_30_MS			(6<<4)
#define EPL_WAIT_40_MS			(7<<4)
#define EPL_WAIT_50_MS			(8<<4)
#define EPL_WAIT_75_MS			(9<<4)
#define EPL_WAIT_100_MS		    (10<<4)
#define EPL_WAIT_150_MS		    (11<<4)
#define EPL_WAIT_200_MS		    (12<<4)
#define EPL_WAIT_300_MS		    (13<<4)
#define EPL_WAIT_400_MS		    (14<<4)
#define EPL_WAIT_SINGLE		    (15<<4)

static int wait_value[] = {0, 2, 4, 8, 12, 20, 30, 40, 50, 75, 100, 150, 200, 300, 400};
int wait_len = sizeof(wait_value)/sizeof(int);



#define EPL_GAIN_MID		(0x00)
#define EPL_GAIN_LOW		(0x01)

#define EPL_PSALS_ADC_11	(0x00 << 3)
#define EPL_PSALS_ADC_12	(0x01 << 3)
#define EPL_PSALS_ADC_13	(0x02 << 3)
#define EPL_PSALS_ADC_14	(0x03 << 3)
static int adc_value[] = {128, 256, 512, 1024};



#define EPL_CYCLE_1			(0x00)
#define EPL_CYCLE_2			(0x01)
#define EPL_CYCLE_4			(0x02)
#define EPL_CYCLE_8			(0x03)
#define EPL_CYCLE_16		(0x04)
#define EPL_CYCLE_32		(0x05)
#define EPL_CYCLE_64		(0x06)
static int cycle_value[] = {1, 2, 4, 8, 16, 32, 64};

#define EPL_IR_ON_CTRL_OFF	(0x00 << 5)
#define EPL_IR_ON_CTRL_ON	(0x01 << 5)

#define EPL_IR_MODE_CURRENT	(0x00 << 4)
#define EPL_IR_MODE_VOLTAGE	(0x01 << 4)

#define EPL_IR_BOOST_100	(0x00 << 2)
#define EPL_IR_BOOST_150	(0x01 << 2)
#define EPL_IR_BOOST_200	(0x02 << 2)
#define EPL_IR_BOOST_300	(0x03 << 2)

#define EPL_IR_DRIVE_100	(0x00)
#define EPL_IR_DRIVE_50		(0x01)
#define EPL_IR_DRIVE_20		(0x02)
#define EPL_IR_DRIVE_10		(0x03)


#define EPL_INT_CTRL_ALS_OR_PS		(0x00 << 4)
#define EPL_INT_CTRL_ALS			(0x01 << 4)
#define EPL_INT_CTRL_PS				(0x02 << 4)

#define EPL_PERIST_1		(0x00 << 2)
#define EPL_PERIST_4		(0x01 << 2)
#define EPL_PERIST_8		(0x02 << 2)
#define EPL_PERIST_16		(0x03 << 2)

#define EPL_INTTY_DISABLE	(0x00)
#define EPL_INTTY_BINARY	(0x01)
#define EPL_INTTY_ACTIVE	(0x02)
#define EPL_INTTY_FRAME	(0x03)

#define EPL_RESETN_RESET	(0x00 << 1)
#define EPL_RESETN_RUN		(0x01 << 1)

#define EPL_POWER_OFF		(0x01)
#define EPL_POWER_ON		(0x00)

#define EPL_ALS_INT_CHSEL_0	(0x00 << 4)
#define EPL_ALS_INT_CHSEL_1	(0x01 << 4)
#define EPL_ALS_INT_CHSEL_2	(0x02 << 4)
#define EPL_ALS_INT_CHSEL_3	(0x03 << 4)

#define EPL_SATURATION				(0x01 << 5)
#define EPL_SATURATION_NOT		(0x01 << 5)

#define EPL_CMP_H_TRIGGER		(0x01 << 4)
#define EPL_CMP_H_CLEAR		(0x00 << 4)

#define EPL_CMP_L_TRIGGER		(0x01 << 3)
#define EPL_CMP_L_CLEAR		(0x00 << 3)

#define EPL_INT_TRIGGER		(0x01 << 2)
#define EPL_INT_CLEAR		(0x00 << 2)

#define EPL_CMP_RESET		(0x00 << 1)
#define EPL_CMP_RUN			(0x01 << 1)

#define EPL_LOCK			(0x01)
#define EPL_UN_LOCK		(0x00)

#define EPL_OSC_SEL_1MHZ	(0x07)
#define EPL_OSC_SEL_4MHZ	(0x0f)


struct _ps_data
{
	u16 ir_data;
	u16 data;
};

struct _ps_factory
{
	bool calibration_enable;
	bool calibrated;
	u16 cancelation;
	u16 high_threshold;
	u16 low_threshold;
};

#define ALS_CHANNEL_SIZE	4

struct _als_data
{
	u16 channels[ALS_CHANNEL_SIZE];
	u16 lux;
};

struct _als_factory
{
	bool calibration_enable;
	bool calibrated;
	u16 lux_pre_count;
};



struct _ps_setting
{
    bool polling_mode;
	u8 integration_time;
	u8 gain;
	u8 adc;
	u8 cycle;
	u16 high_threshold;
	u16 low_threshold;
	u8 ir_on_control;
	u8 ir_mode;
	u8 ir_boost;
	u8 ir_driver;
	u8 persist;
	u8 interrupt_type;
	u8 saturation;
	u8 compare_high;
	u8 compare_low;
	u8 interrupt_flag;
	u8 compare_reset;
	u8 lock;
	u16 cancelation;
	struct _ps_data data;
	struct _ps_factory factory;
};

struct _als_setting
{
    bool polling_mode;
	u8 report_type;
	u16 report_count;
	u8 integration_time;
	u8 gain;
    u8 adc;
	u8 cycle;
	u16 high_threshold;
	u16 low_threshold;
	u8 ir_on_control;
	u8 ir_mode;
	u8 ir_boost;
	u8 ir_driver;
	u8 persist;
	u8 interrupt_type;
	u8 saturation;
	u8 compare_high;
	u8 compare_low;
	u8 interrupt_flag;
	u8 compare_reset;
	u8 lock;
	u8 interrupt_channel_select;
	u16 dyn_intt_raw;
	struct _als_data data;
	struct _als_factory factory;
};


typedef struct _sensor
{
	u8 wait;
	u8 mode;
       bool enable_factory_calibration;
	u8 early_suspend_mode;
	u8 osc_sel;
	u8 interrupt_control;
	u8 reset;
	u8 power;
	u8 power_on_index;
	struct _ps_setting ps;
	struct _als_setting als;
	u16 revno;
}epl_optical_sensor;
#endif


