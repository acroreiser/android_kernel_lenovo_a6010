#ifndef BQ24296_CHARGER_H
#define BQ24296_CHARGER_H

#define BQ24296_I2C_ADDR          0x6b

/* timeout for resetting chip timer */
#define BQ24296_TIMER_TIMEOUT		10

#define bq24296_CON0      0x00
#define bq24296_CON1      0x01
#define bq24296_CON2      0x02
#define bq24296_CON3      0x03
#define bq24296_CON4      0x04
#define bq24296_CON5      0x05
#define bq24296_CON6      0x06
#define bq24296_CON7      0x07
#define bq24296_CON8      0x08
#define bq24296_CON9      0x09
#define bq24296_CON10      0x0A

//CON0
#define CON0_EN_HIZ_MASK   0x01
#define CON0_EN_HIZ_SHIFT  7

#define CON0_VINDPM_MASK       0x0F
#define CON0_VINDPM_SHIFT      3

#define CON0_IINLIM_MASK   0x07
#define CON0_IINLIM_SHIFT  0

//CON1
#define CON1_REG_RST_MASK     0x01
#define CON1_REG_RST_SHIFT    7

#define CON1_WDT_RST_MASK     0x01
#define CON1_WDT_RST_SHIFT    6

#define CON1_CFG_OTG_MASK        0x01
#define CON1_CFG_OTG_SHIFT       5

#define CON1_CFG_CHG_MASK        0x01
#define CON1_CFG_CHG_SHIFT       4

#define CON1_SYS_MIN_MASK        0x07
#define CON1_SYS_MIN_SHIFT       1

#define CON1_BOOST_LIM_MASK   0x01
#define CON1_BOOST_LIM_SHIFT  0

//CON2
#define CON2_ICHG_MASK    0x3F
#define CON2_ICHG_SHIFT   2

#define CON2_BCOLD_MASK    0x01
#define CON2_BCOLD_SHIFT   1

#define CON2_FORCE_20PCT_MASK    0x1
#define CON2_FORCE_20PCT_SHIFT   0

//CON3
#define CON3_IPRECHG_MASK   0x0F
#define CON3_IPRECHG_SHIFT  4

#define CON3_ITERM_MASK           0x0F
#define CON3_ITERM_SHIFT          0

//CON4
#define CON4_VREG_MASK     0x3F
#define CON4_VREG_SHIFT    2

#define CON4_BATLOWV_MASK     0x01
#define CON4_BATLOWV_SHIFT    1

#define CON4_VRECHG_MASK    0x01
#define CON4_VRECHG_SHIFT   0

//CON5
#define CON5_EN_TERM_MASK      0x01
#define CON5_EN_TERM_SHIFT     7

#define CON5_WDT_MASK      0x03
#define CON5_WDT_SHIFT     4

#define CON5_EN_SAFE_TIMER_MASK      0x01
#define CON5_EN_SAFE_TIMER_SHIFT     3

#define CON5_SAFE_TIMER_MASK      0x03
#define CON5_SAFE_TIMER_SHIFT     1

//CON6
#define CON6_BOOSTV_MASK     0x0f
#define CON6_BOOSTV_SHIFT    4

#define CON6_BHOT_MASK     0x03
#define CON6_BHOT_SHIFT    2

#define CON6_TREG_MASK     0x03
#define CON6_TREG_SHIFT    0

//CON7
#define CON7_DPDM_EN_MASK      0x01
#define CON7_DPDM_EN_SHIFT     7

#define CON7_TMR2X_EN_MASK      0x01
#define CON7_TMR2X_EN_SHIFT     6

#define CON7_BATFET_DISABLE_MASK      0x01
#define CON7_BATFET_DISABLE_SHIFT     5

#define CON7_INT_MASK_MASK     0x03
#define CON7_INT_MASK_SHIFT    0

//CON8
#define CON8_VBUS_STAT_MASK      0x03
#define CON8_VBUS_STAT_SHIFT     6

#define CON8_CHRG_STAT_MASK           0x03
#define CON8_CHRG_STAT_SHIFT          4

#define CON8_DPM_STAT_MASK           0x01
#define CON8_DPM_STAT_SHIFT          3

#define CON8_PG_STAT_MASK           0x01
#define CON8_PG_STAT_SHIFT          2

#define CON8_THERM_STAT_MASK           0x01
#define CON8_THERM_STAT_SHIFT          1

#define CON8_VSYS_STAT_MASK           0x01
#define CON8_VSYS_STAT_SHIFT          0

//CON9
#define CON9_WDT_FAULT_MASK      0x01
#define CON9_WDT_FAULT_SHIFT     7

#define CON9_OTG_FAULT_MASK           0x01
#define CON9_OTG_FAULT_SHIFT          6

#define CON9_CHRG_FAULT_MASK           0x03
#define CON9_CHRG_FAULT_SHIFT          4

#define CON9_BAT_FAULT_MASK           0x01
#define CON9_BAT_FAULT_SHIFT          3

#define CON9_NTC_FAULT_MASK           0x07
#define CON9_NTC_FAULT_SHIFT          0

//CON10
#define CON10_PN_MASK      0x07
#define CON10_PN_SHIFT     5

#define CON10_REV_MASK           0x07
#define CON10_REV_SHIFT          0

#define BQ24296_DEF_CHG_CURRENT		1500
#define BQ24296_DEF_CURRENT_LIMIT		1500
#define BQ24296_DEF_CV_VOL				4350	
#define BQ24296_DEF_TERM_CURRENT		256
#define BQ24296_DEF_CURRENT_ABNORMAL		600

enum bq24296_command {
	BQ24296_SET_CHR_TYPE,//35
	BQ24296_SET_CHR_ENABLE,
	BQ24296_SET_CHR_DISABLE,
};

/* Supported modes with maximal current limit */
enum bq24296_status {
	BQ24296_READY,
	BQ24296_CHARGING,
	BQ24296_DONE,
	BQ24296_ERROR,
};

struct bq24296_platform_data {
	int current_limit;		/* mA */
	int weak_battery_voltage;	/* mV */
	int battery_regulation_voltage;	/* mV */
	int charge_current;		/* mA */
	int termination_current;	/* mA */
	unsigned int en_gpio;	
	unsigned int en_gpio_dvt1;
	unsigned int en_gpio_dvt2;
	unsigned int irq_gpio;
	unsigned int psel_gpio;
	unsigned int otg_gpio;
	int chg_temp_protect_0m;	/* mA */
	int chg_temp_protect_10m;	/* mA */
	int chg_temp_protect_45m;	/* mA */
	int chg_temp_protect_50m;	/* mA */	
	int chg_temp_protect_50p;	/* mA */	
	int chg_temp_protect_vol_0m;	/* mV */
	int chg_temp_protect_vol_10m;	/* mV */
	int chg_temp_protect_vol_45m;	/* mA */
	int chg_temp_protect_vol_50m;	/* mV */	
	int chg_temp_protect_vol_50p;	/* mV */		
};

enum bq24296_chip {
	BQ24296,
	BQ24297,
};

enum bq24296_charger_type {
	CHARGER_UNKNOWN,
	CHARGER_AC,
	CHARGER_USB,
	CHARGER_UNSTANDARD,	
};

extern int bq24296_exec_command(enum bq24296_command command, unsigned char param);

#endif
