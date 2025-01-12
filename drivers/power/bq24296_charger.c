#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/wakelock.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>

#include <linux/power/bq24296_charger.h>

#define BQ24296_POWER_SUPPLY

static struct bq24296_device *p_bq24296_dev;

struct bq24296_otg_regulator {
	struct regulator_desc	rdesc;
	struct regulator_dev	*rdev;
};


struct bq24296_device {
	struct device *dev;
	struct bq24296_platform_data init_data;
#ifdef BQ24296_POWER_SUPPLY
	struct power_supply charger;
#endif
	struct power_supply	*usb_psy;	
	struct power_supply	*bms_psy;	
	struct delayed_work work;
	enum bq24296_status reported_mode;/* mode reported by hook function */
	enum bq24296_status status;		/* current configured mode */
	enum bq24296_chip chip;
	enum bq24296_charger_type chr_type;
	struct wake_lock battery_suspend_lock; 
	const char *timer_error;
	char *model;
	char *name;
	int autotimer;	/* 1 - if driver automatically reset timer, 0 - not */
	int automode;	/* 1 - enabled, 0 - disabled; -1 - not supported */
	int id;
	int temp;	
	bool temp_debug_flag;	
	unsigned int en_flags;	
	bool usb_present;
	bool suspend_flag;	
	bool chg_temp_protect;	
    struct bq24296_otg_regulator otg_vreg;
	bool charger_plug_in_flag;	
};

/* each registered chip must have unique id */
static DEFINE_IDR(bq24296_id);

static DEFINE_MUTEX(bq24296_id_mutex);
static DEFINE_MUTEX(bq24296_timer_mutex);
static DEFINE_MUTEX(bq24296_i2c_mutex);

/**** i2c read functions ****/

/* read value from register */
static int bq24296_i2c_read(struct bq24296_device *bq, u8 reg)
{
	struct i2c_client *client = to_i2c_client(bq->dev);
	struct i2c_msg msg[2];
	u8 val;
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = &val;
	msg[1].len = sizeof(val);

	mutex_lock(&bq24296_i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&bq24296_i2c_mutex);

	if (ret < 0)
		return ret;

	return val;
}

/* read value from register, apply mask and right shift it */
static int bq24296_i2c_read_mask(struct bq24296_device *bq, u8 reg,
				 u8 mask, u8 shift)
{
	int ret;

	if (shift > 8)
		return -EINVAL;

	ret = bq24296_i2c_read(bq, reg);
	if (ret < 0)
		return ret;
	
	return (ret & (mask<<shift)) >> shift;
}

/* read value from register and return one specified bit */
#if 0
static int bq24296_i2c_read_bit(struct bq24296_device *bq, u8 reg, u8 bit)
{
	if (bit > 8)
		return -EINVAL;
	return bq24296_i2c_read_mask(bq, reg, BIT(bit), bit);
}
#endif

/**** i2c write functions ****/

/* write value to register */
static int bq24296_i2c_write(struct bq24296_device *bq, u8 reg, u8 val)
{
	struct i2c_client *client = to_i2c_client(bq->dev);
	struct i2c_msg msg[1];
	u8 data[2];
	int ret;

	data[0] = reg;
	data[1] = val;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = data;
	msg[0].len = ARRAY_SIZE(data);

	mutex_lock(&bq24296_i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&bq24296_i2c_mutex);

	/* i2c_transfer returns number of messages transferred */
	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EIO;

	return 0;
}

/* read value from register, change it with mask left shifted and write back */
static int bq24296_i2c_write_mask(struct bq24296_device *bq, u8 reg, u8 val,
				  u8 mask, u8 shift)
{
	int ret;

	if (shift > 8)
		return -EINVAL;

	ret = bq24296_i2c_read(bq, reg);
	if (ret < 0)
		return ret;

	ret &= ~(mask<<shift);
	ret |= val << shift;

	if((reg==bq24296_CON1)&&(shift!=CON1_REG_RST_SHIFT))
		ret &= 0x7f;

	return bq24296_i2c_write(bq, reg, ret);
}

#if 0
/* change only one bit in register */
static int bq24296_i2c_write_bit(struct bq24296_device *bq, u8 reg,
				 bool val, u8 bit)
{
	if (bit > 8)
		return -EINVAL;
	return bq24296_i2c_write_mask(bq, reg, val, BIT(bit), bit);
}
#endif

/**** global functions ****/

/**********************************************************
  *
  *   [Internal Function] 
  *
  *********************************************************/
//============================reg operation============================= 
//CON0----------------------------------------------------

int bq24296_set_en_hiz(struct bq24296_device *bq, unsigned int val)
{
	return bq24296_i2c_write_mask(bq, bq24296_CON0, 
							(unsigned char)val, 
							(unsigned char)(CON0_EN_HIZ_MASK),
							(unsigned char)(CON0_EN_HIZ_SHIFT));
}

unsigned int bq24296_get_en_hiz(struct bq24296_device *bq)
{
	int val = 0;
	
	val = bq24296_i2c_read_mask(bq, bq24296_CON0, 
							(unsigned char)(CON0_EN_HIZ_MASK),
							(unsigned char)(CON0_EN_HIZ_SHIFT));
	return val;
}

int bq24296_set_vindpm(struct bq24296_device *bq, unsigned int val)
{
	return bq24296_i2c_write_mask(bq, bq24296_CON0, 
							(unsigned char)val, 
							(unsigned char)(CON0_VINDPM_MASK),
							(unsigned char)(CON0_VINDPM_SHIFT));	
}

int bq24296_set_iinlim(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON0), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON0_IINLIM_MASK),
                                    (unsigned char)(CON0_IINLIM_SHIFT)
                                    );
}

int bq24296_get_iinlim(struct bq24296_device *bq)
{
    return bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON0), 
                                    (unsigned char)(CON0_IINLIM_MASK),
                                    (unsigned char)(CON0_IINLIM_SHIFT)
                                    );
}

//CON1----------------------------------------------------

int bq24296_set_reg_rst(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_REG_RST_MASK),
                                    (unsigned char)(CON1_REG_RST_SHIFT)
                                    );
}

int bq24296_set_wdt_rst(struct bq24296_device *bq)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON1), 
                                    (unsigned char)(1),
                                    (unsigned char)(CON1_WDT_RST_MASK),
                                    (unsigned char)(CON1_WDT_RST_SHIFT)
                                    );
}

int bq24296_set_otg_config(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_CFG_OTG_MASK),
                                    (unsigned char)(CON1_CFG_OTG_SHIFT)
                                    );
}

unsigned int bq24296_get_otg_config(struct bq24296_device *bq)
{
    unsigned int val=0;    

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON1), 
                                    (unsigned char)(CON1_CFG_OTG_MASK),
                                    (unsigned char)(CON1_CFG_OTG_SHIFT)
                                    );

	return val;
}

int bq24296_set_chg_config(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_CFG_CHG_MASK),
                                    (unsigned char)(CON1_CFG_CHG_SHIFT)
                                    );
}

unsigned int bq24296_get_chg_config(struct bq24296_device *bq)
{
    unsigned int val = 0;    

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON1), 
                                    (unsigned char)(CON1_CFG_CHG_MASK),
                                    (unsigned char)(CON1_CFG_CHG_SHIFT)
                                    );

	return val;
}

int bq24296_set_sys_min(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_SYS_MIN_MASK),
                                    (unsigned char)(CON1_SYS_MIN_SHIFT)
                                    );
}

int bq24296_set_boost_lim(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON1), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON1_BOOST_LIM_MASK),
                                    (unsigned char)(CON1_BOOST_LIM_SHIFT)
                                    );
}

int bq24296_get_boost_lim(struct bq24296_device *bq)
{
    unsigned int val=0;    

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON1), 
                                    (unsigned char)(CON1_BOOST_LIM_MASK),
                                    (unsigned char)(CON1_BOOST_LIM_SHIFT)
                                    );

	return val;
}

//CON2----------------------------------------------------
int bq24296_set_ichg(struct bq24296_device *bq, unsigned int val)
{
   return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON2), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON2_ICHG_MASK),
                                    (unsigned char)(CON2_ICHG_SHIFT)
                                    );
}

unsigned int bq24296_get_ichg(struct bq24296_device *bq)
{
    unsigned char val=0;  

    val=bq24296_i2c_read_mask(bq, (unsigned char)(bq24296_CON2), 
                                    (unsigned char)(CON2_ICHG_MASK),
                                    (unsigned char)(CON2_ICHG_SHIFT)
                                    );

    return val;
}
/*
void bq24296_set_force_20pct(struct bq24296_device *bq, unsigned int val)
{
    unsigned int ret=0;    

    bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON2), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON2_FORCE_20PCT_MASK),
                                    (unsigned char)(CON2_FORCE_20PCT_SHIFT)
                                    );
}
*/
//CON3----------------------------------------------------

int bq24296_set_iprechg(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON3), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON3_IPRECHG_MASK),
                                    (unsigned char)(CON3_IPRECHG_SHIFT)
                                    );
}

int bq24296_set_iterm(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON3), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON3_ITERM_MASK),
                                    (unsigned char)(CON3_ITERM_SHIFT)
                                    );
}

int bq24296_get_iterm(struct bq24296_device *bq)
{
    unsigned int val=0;    

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON3), 
                                    (unsigned char)(CON3_ITERM_MASK),
                                    (unsigned char)(CON3_ITERM_SHIFT)
                                    );
	return val;
}
//CON4----------------------------------------------------

int bq24296_set_vreg(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_VREG_MASK),
                                    (unsigned char)(CON4_VREG_SHIFT)
                                    );
}

unsigned char bq24296_get_vreg(struct bq24296_device *bq)
{
    return bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON4), 
                                    (unsigned char)(CON4_VREG_MASK),
                                    (unsigned char)(CON4_VREG_SHIFT)
                                    );
}

int bq24296_set_batlowv(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_BATLOWV_MASK),
                                    (unsigned char)(CON4_BATLOWV_SHIFT)
                                    );
}

int bq24296_set_vrechg(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON4), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON4_VRECHG_MASK),
                                    (unsigned char)(CON4_VRECHG_SHIFT)
                                    );
}

//CON5----------------------------------------------------
int bq24296_set_en_term(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_EN_TERM_MASK),
                                    (unsigned char)(CON5_EN_TERM_SHIFT)
                                    );
}

unsigned int bq24296_get_en_term(struct bq24296_device *bq)
{
    unsigned int val=0;    

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON5), 
                                    (unsigned char)(CON5_EN_TERM_MASK),
                                    (unsigned char)(CON5_EN_TERM_SHIFT)
                                    );
	return val;
}

int bq24296_set_watchdog(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq, (unsigned char)(bq24296_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_WDT_MASK),
                                    (unsigned char)(CON5_WDT_SHIFT)
                                    );
}

int bq24296_set_en_timer(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq, (unsigned char)(bq24296_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_EN_SAFE_TIMER_MASK),
                                    (unsigned char)(CON5_EN_SAFE_TIMER_SHIFT)
                                    );
}

int bq24296_set_chg_timer(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq, (unsigned char)(bq24296_CON5), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON5_SAFE_TIMER_MASK),
                                    (unsigned char)(CON5_SAFE_TIMER_SHIFT)
                                    );
}

//CON6----------------------------------------------------

int bq24296_set_boostv(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq, (unsigned char)(bq24296_CON6), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON6_BOOSTV_MASK),
                                    (unsigned char)(CON6_BOOSTV_SHIFT)
                                    );
}

int bq24296_set_bhot(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq, (unsigned char)(bq24296_CON6), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON6_BHOT_MASK),
                                    (unsigned char)(CON6_BHOT_SHIFT)
                                    );
}

int bq24296_set_treg(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq,  (unsigned char)(bq24296_CON6), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON6_TREG_MASK),
                                    (unsigned char)(CON6_TREG_SHIFT)
                                    );
}

//CON7----------------------------------------------------
int bq24296_set_dpdm_en(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq, (unsigned char)(bq24296_CON7), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON7_DPDM_EN_MASK),
                                    (unsigned char)(CON7_DPDM_EN_SHIFT)
                                    );
}

int bq24296_set_tmr2x_en(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq, (unsigned char)(bq24296_CON7), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON7_TMR2X_EN_MASK),
                                    (unsigned char)(CON7_TMR2X_EN_SHIFT)
                                    );
}

int bq24296_set_batfet_disable(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq, (unsigned char)(bq24296_CON7), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON7_BATFET_DISABLE_MASK),
                                    (unsigned char)(CON7_BATFET_DISABLE_SHIFT)
                                    );
}

int bq24296_set_int_mask(struct bq24296_device *bq, unsigned int val)
{
    return bq24296_i2c_write_mask(bq, (unsigned char)(bq24296_CON7), 
                                    (unsigned char)(val),
                                    (unsigned char)(CON7_INT_MASK_MASK),
                                    (unsigned char)(CON7_INT_MASK_SHIFT)
                                    );
}

//CON8----------------------------------------------------
unsigned int bq24296_get_system_status(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq, (unsigned char)(bq24296_CON8), 
                                    (unsigned char)(0xFF),
                                    (unsigned char)(0x0)
                                    );
    return val;
}

unsigned int bq24296_get_vbus_stat(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq, (unsigned char)(bq24296_CON8), 
                                    (unsigned char)(CON8_VBUS_STAT_MASK),
                                    (unsigned char)(CON8_VBUS_STAT_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_chrg_stat(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq, (unsigned char)(bq24296_CON8), 
                                    (unsigned char)(CON8_CHRG_STAT_MASK),
                                    (unsigned char)(CON8_CHRG_STAT_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_dpm_stat(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq, (unsigned char)(bq24296_CON8), 
                                    (unsigned char)(CON8_DPM_STAT_MASK),
                                    (unsigned char)(CON8_DPM_STAT_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_pg_stat(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq, (unsigned char)(bq24296_CON8), 
                                    (unsigned char)(CON8_PG_STAT_MASK),
                                    (unsigned char)(CON8_PG_STAT_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_therm_stat(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq, (unsigned char)(bq24296_CON8), 
                                    (unsigned char)(CON8_THERM_STAT_MASK),
                                    (unsigned char)(CON8_THERM_STAT_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_vsys_stat(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq, (unsigned char)(bq24296_CON8), 
                                    (unsigned char)(CON8_VSYS_STAT_MASK),
                                    (unsigned char)(CON8_VSYS_STAT_SHIFT)
                                    );
    return val;
}

//CON9----------------------------------------------------
unsigned int bq24296_get_fault(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON9), 
                                    (unsigned char)(0xff),
                                    (unsigned char)(0)
                                    );
    return val;
}

unsigned int bq24296_get_wdt_fault(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON9), 
                                    (unsigned char)(CON9_WDT_FAULT_MASK),
                                    (unsigned char)(CON9_WDT_FAULT_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_otg_fault(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON9), 
                                    (unsigned char)(CON9_OTG_FAULT_MASK),
                                    (unsigned char)(CON9_OTG_FAULT_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_chrg_fault(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON9), 
                                    (unsigned char)(CON9_CHRG_FAULT_MASK),
                                    (unsigned char)(CON9_CHRG_FAULT_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_bat_fault(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON9), 
                                    (unsigned char)(CON9_BAT_FAULT_MASK),
                                    (unsigned char)(CON9_BAT_FAULT_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_ntc_fault(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON9), 
                                    (unsigned char)(CON9_NTC_FAULT_MASK),
                                    (unsigned char)(CON9_NTC_FAULT_SHIFT)
                                    );
    return val;
}

//CON10----------------------------------------------------
unsigned int bq24296_get_pn(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON10), 
                                    (unsigned char)(CON10_PN_MASK),
                                    (unsigned char)(CON10_PN_SHIFT)
                                    );
    return val;
}

unsigned int bq24296_get_rev(struct bq24296_device *bq)
{
    unsigned char val=0;

    val = bq24296_i2c_read_mask(bq,  (unsigned char)(bq24296_CON10), 
                                    (unsigned char)(CON10_REV_MASK),
                                    (unsigned char)(CON10_REV_SHIFT)
                                    );
    return val;
}

static int bq24296_dump_reg(struct bq24296_device *bq)
{
	int i;
	u8 val = 0;

	pr_err("bq24296 dump : ");
	for(i=0;i<10;i++)
	{
		val = bq24296_i2c_read(bq, i);
		pr_err(" %d: 0x%x;", i, val);
	}
	pr_err("\n");

	return 0;
}
//=============================================================================


/* set current limit in mA */
static int bq24296_set_current_limit(struct bq24296_device *bq, int mA)
{
	int val;

	if (mA < 150)
		val = 0;
	else if(mA >=3008)
		val = 0x7;
	else if((mA>=150)&&(mA<500))
		val = 0x1;
	else if((mA>=500)&&(mA<900))
		val = 0x2;	
	else if((mA>=900)&&(mA<1000))
		val = 0x3;
	else if((mA>=1000)&&(mA<1500))
		val = 0x4;
	else if((mA>=1500)&&(mA<2000))
		val = 0x5;
	else if((mA>=2000)&&(mA<3000))
		val = 0x6;

	return bq24296_set_iinlim(bq, val);
}

/* get current limit in mA */
static int bq24296_get_current_limit(struct bq24296_device *bq)
{
	int ret;

	ret =bq24296_get_iinlim(bq);
	if((ret>=0)&&(ret<=0x7)) 
	{	
		switch(ret)
		{
			case 0:
				ret = 100;
				break;
			case 1:
				ret = 150;
				break;
			case 2:
				ret = 500;
				break;
			case 3:
				ret = 900;
				break;
			case 4:
				ret = 1000;
				break;
			case 5:
				ret = 1500;
				break;
			case 6:
				ret = 2000;
				break;				
			case 7:
				ret = 3000;
				break;				
			}
		return ret;
	}
	
	return -EINVAL;
}

/* set weak battery voltage in mV */
static int bq24296_set_weak_battery_voltage(struct bq24296_device *bq, int mV)
{
	int val;

	/* round to 100mV */
	if (mV <= 3000)
		val = 0;
	else if (mV >= 3700)
		val = 7;
	else
	{
		val = (mV - 3000)/100;
	}

	return bq24296_i2c_write_mask(bq, bq24296_CON1, val,
			CON1_SYS_MIN_MASK, CON1_SYS_MIN_SHIFT);
}

/* get weak battery voltage in mV */
static int bq24296_get_weak_battery_voltage(struct bq24296_device *bq)
{
	int ret;

	ret = bq24296_i2c_read_mask(bq, bq24296_CON1,
			CON1_SYS_MIN_MASK, CON1_SYS_MIN_SHIFT);
	if (ret < 0)
		return ret;

	ret = 300 + ret*100;
	
	return ret;
}

/* set battery regulation voltage in mV */
static int bq24296_set_battery_regulation_voltage(struct bq24296_device *bq,
						  int mV)
{
	int val;

	if (mV < 0)
		mV = 0;
	else if (mV > 4400) /* FIXME: Max is 94 or 122 ? Set max value ? */
		mV = 4400;
	
	val = mV - 3504;
	if(val%16)
		val = val/16 + 1;
	else
		val = val/16;

	//return bq24296_i2c_write_mask(bq, bq24296_CON4, val, 	CON4_VREG_MASK, CON4_VREG_SHIFT);
	return bq24296_set_vreg(bq, val);
}

/* get battery regulation voltage in mV */
static int bq24296_get_battery_regulation_voltage(struct bq24296_device *bq)
{
	int ret;

	ret = bq24296_get_vreg(bq);
	
	if (ret < 0)
		return ret;
	
	return ret*16 + 3504;
}

/* set charge current in mA (platform data must provide resistor sense) */
static int bq24296_set_charge_current(struct bq24296_device *bq, int mA)
{
	int val;

	if (mA <= 512)
		val = 0;
	else if(mA >=3008)
		val = 0x27;
	else
	{
		val = (mA - 512)/64;
	}

	return bq24296_set_ichg(bq, val);	
}

/* get charge current in mA (platform data must provide resistor sense) */
static int bq24296_get_charge_current(struct bq24296_device *bq)
{
	int ret;

	ret =bq24296_get_ichg(bq);
	if((ret>=0)&&(ret<=0x27)) 
	{	
		ret = ret*64 + 512;
		return ret;
	}
	
	return -EINVAL;
}

/* set termination current in mA (platform data must provide resistor sense) */
static int bq24296_set_termination_current(struct bq24296_device *bq, int mA)
{
	int val;

	if(mA<=128)
		mA = 128;
	else if(mA>=2048)
		mA = 2048;

	val = (mA - 128)/128;

	return bq24296_set_iterm(bq, val);
}

/* get termination current in mA (platform data must provide resistor sense) */
static int bq24296_get_termination_current(struct bq24296_device *bq)
{
	int ret;

	ret = bq24296_get_iterm(bq);
	if((ret>=0)&&(ret<=0xf)) 
	{	
		ret = ret*128 + 128;
		return ret;
	}
	
	return -EINVAL;
}

static int bq24296_get_temp(struct bq24296_device *bq)
{
	union power_supply_propval val;

	if(bq->temp_debug_flag)
	{
		pr_err("%s enter temp debug mode temp=%d!\n", __func__, bq->temp);
		return bq->temp;
	}
	
	if (!bq->bms_psy)
		bq->bms_psy = power_supply_get_by_name("battery");

	if(!bq->bms_psy)
	{
		pr_err("%s get battery power_supply error!\n", __func__);
		return 25;//defult 25C
	}
	
	bq->bms_psy->get_property(bq->bms_psy, POWER_SUPPLY_PROP_TEMP, &val);

	val.intval = val.intval/10;
	pr_err("%s, temp = %d\n", __func__, val.intval);

	bq->temp = val.intval;
	
	return val.intval;	
}

static int bq24296_set_chg_as_temp(struct bq24296_device *bq)
{
	int temp;
	//const int step = 1;
	const int charging_protect_threshold_0 = 0,
		charging_protect_threshold_1 = 10,
		charging_protect_threshold_2 = 45,
		charging_protect_threshold_3 = 50;
	
	temp = bq24296_get_temp(bq);
	if(temp<charging_protect_threshold_0)
	{
		pr_err("%s, temp(%d) < %d\n", __func__, temp, charging_protect_threshold_0);
		bq->chg_temp_protect = true;
		bq->init_data.charge_current = bq->init_data.chg_temp_protect_0m;
		bq->init_data.battery_regulation_voltage = bq->init_data.chg_temp_protect_vol_0m;
	}else if((temp>=charging_protect_threshold_0)&&(temp<charging_protect_threshold_1))
	{
		pr_err("%s, temp(%d) < %d\n", __func__, temp, charging_protect_threshold_1);
		bq->chg_temp_protect = false;
		bq->init_data.charge_current= bq->init_data.chg_temp_protect_10m;
		bq->init_data.battery_regulation_voltage = bq->init_data.chg_temp_protect_vol_10m;
	}else if((temp>=charging_protect_threshold_1)&&(temp<charging_protect_threshold_2))
	{
		pr_err("%s, temp(%d) < %d\n", __func__, temp, charging_protect_threshold_2);
		bq->chg_temp_protect = false;
		bq->init_data.charge_current = bq->init_data.chg_temp_protect_45m;
		bq->init_data.battery_regulation_voltage = bq->init_data.chg_temp_protect_vol_45m;
	}else if((temp>=charging_protect_threshold_2)&&(temp<charging_protect_threshold_3))
	{
		pr_err("%s, temp(%d) < %d\n", __func__, temp, charging_protect_threshold_3);
		bq->chg_temp_protect = false;
		bq->init_data.charge_current = bq->init_data.chg_temp_protect_50m;
		bq->init_data.battery_regulation_voltage = bq->init_data.chg_temp_protect_vol_50m;
	}else if(temp>=charging_protect_threshold_3)
	{
		pr_err("%s, temp(%d) > %d\n", __func__, temp, charging_protect_threshold_3);
		bq->chg_temp_protect = true;
		bq->init_data.charge_current = bq->init_data.chg_temp_protect_50p;
		bq->init_data.battery_regulation_voltage = bq->init_data.chg_temp_protect_vol_50p;
	}else
		pr_err("%s, abnormal temp %d\n", __func__, temp);

	return 0;
}

int bq24296_set_chg_as_chr_type(struct bq24296_device *bq)
{
	int current_limit = 0;
	int chg_current = 0;

	if(bq->chr_type==CHARGER_UNKNOWN)
	{
		pr_err("bq24296 chr_type unknown stop charging\n");
		
		bq24296_set_chg_config(bq, 0);
		//bq24196_charger_en(bq, 0);
		wake_unlock(&bq->battery_suspend_lock);
		
		return -1;	
	}

	//bq24196_charger_en(bq, 1);
	wake_lock(&bq->battery_suspend_lock);
		
	switch(bq->chr_type)
	{
		case CHARGER_AC:
			bq24296_set_chg_as_temp(bq);
			current_limit = bq->init_data.current_limit;
			chg_current = bq->init_data.charge_current;
			break;
		case CHARGER_USB:
			current_limit = 500;//bq->init_data.current_limit;
			chg_current = 500;//bq->init_data.charge_current;
			break;
		case CHARGER_UNSTANDARD:
			current_limit = 500;//bq->init_data.current_limit;
			chg_current = 500;//bq->init_data.charge_current;
			break;
		case CHARGER_UNKNOWN:
			bq24296_set_chg_config(bq, 0);
			wake_unlock(&bq->battery_suspend_lock);
			return -1;
	}

	pr_err("bq24296 current_limit=%d, chg_current=%d, vol=%d\n", current_limit, chg_current, bq->init_data.battery_regulation_voltage);

	bq24296_set_current_limit(bq,  current_limit);
	bq24296_set_battery_regulation_voltage(bq, bq->init_data.battery_regulation_voltage);
	if(chg_current>0)
	{
		bq24296_set_charge_current(bq, chg_current);
		bq24296_set_chg_config(bq, 1);
	}else
	{
		bq24296_set_chg_config(bq, 0);
	}

	return 0;
}

int bq24296_set_chr_type(struct bq24296_device *bq, enum bq24296_charger_type type)
{
	static int pre_chr_type = -1;
	
	if(pre_chr_type==type)
	{
		pr_err("bq24296 same chr_type %d\n", type);
		return -1;
	}

	pre_chr_type = type;
	bq->chr_type = type;	

	//bq24296_set_cur_as_chr_type(bq);
	//schedule_work(&bq->work);
	//cancel_delayed_work(&bq->work);
	schedule_delayed_work(&bq->work, 1);
	
	return 0;
}

int bq24296_set_chr_status(struct bq24296_device *bq, enum bq24296_status state)
{
	bq->status = state;

	return 0;
}

/*
static int bq24196_charger_en(struct bq24296_device *bq, unsigned int en)
{
	dev_err(bq->dev, "en port val [%d], io %d\n", en, bq->init_data.en_gpio);

	if (!gpio_is_valid(bq->init_data.en_gpio)) 
	{
		dev_err(bq->dev, "en port invalid [%d]\n", bq->init_data.en_gpio);
		return -1;
	}

	if(en)
		gpio_set_value(bq->init_data.en_gpio, 0);
	else
		gpio_set_value(bq->init_data.en_gpio, 1);

	dev_err(bq->dev, "val of gpio [%d]\n", gpio_get_value(bq->init_data.en_gpio));

	return 0;
}*/
	
static int bq24296_set_chr_enable(struct bq24296_device *bq, int en)
{
	pr_err("%s en=%d\n", __func__, en);
	
	if(en)
	{
		bq24296_set_termination_current(bq, bq->init_data.termination_current);
		bq24296_set_battery_regulation_voltage(bq, bq->init_data.battery_regulation_voltage);

		bq24296_set_en_term(bq, 1);//->enable
		
		bq24296_set_chg_config(bq, 1);	
	}else
	{
		bq24296_set_chg_config(bq, 0);	
	}

	return 0;

}

/* reset all chip registers to default state */
static void bq24296_reset_chip(struct bq24296_device *bq)
{
	bq24296_set_reg_rst(bq, 1);
	
	bq->timer_error = NULL;
}

/*fs operation*/
#include <linux/fs.h>
#define FS_HWID_FILE_NAME "/sys/devices/soc.0/0.hw_ver/version"
 
static int bq24296_get_hwid(void)
{
	struct file* f_p;
	char buf[16] = {0};
	int ret;

	f_p = filp_open(FS_HWID_FILE_NAME, O_RDONLY|O_NONBLOCK, 0);
	if(IS_ERR(f_p))
	{
		pr_err("%s Open %s failed\n", __func__, FS_HWID_FILE_NAME);
		pr_err("%s errno=0x%lx\n", __func__, PTR_ERR(f_p));
		return -1;
	}	
	
	ret = f_p->f_op->read(f_p, buf, 4, &(f_p->f_pos));
	pr_err("%s get hwid %s, %d\n", __func__, buf, ret);
	if(ret<0)
		return ret;

	if((buf[0]==0x30)&&(buf[1]==0x30))
		return 0;
	else
		return 1;

	return 0;
}

//================not usded. just for not used static function compile=============
int bq24296_dummy(struct bq24296_device *bq)
{
	bq24296_get_current_limit(bq);
	bq24296_get_weak_battery_voltage(bq);
	bq24296_get_battery_regulation_voltage(bq);
	bq24296_get_charge_current(bq);
	bq24296_get_termination_current(bq);
	bq24296_get_hwid();

	return 0;
}
//=============================================================

static int bq24296_config_qpnp_charging(struct bq24296_device *bq, int val)
{
	union power_supply_propval psy_val;

	psy_val.intval = val;
	
	if (!bq->bms_psy)
		bq->bms_psy = power_supply_get_by_name("battery");

	if(!bq->bms_psy)
	{
		pr_err("%s get battery power_supply error!\n", __func__);
		return -1;
	}

	bq->bms_psy->set_property(bq->bms_psy, POWER_SUPPLY_PROP_CHARGING_ENABLED, &psy_val);	

	return 0;
}

static int bq24296_config_en_gpio(struct bq24296_device *bq, int en)
{
	int ret = 0;
	int phy_ret = 0;
	
	//ret = bq24296_get_hwid();
	//if(ret>=0)
	if(1)		
	{
		if(ret==1)
		{
			pr_err("%s usd dvt2 config\n", __func__);
			bq->init_data.en_gpio = bq->init_data.en_gpio_dvt2;
		}else
		{
			pr_err("%s usd dvt1 config\n", __func__);
			bq->init_data.en_gpio = bq->init_data.en_gpio_dvt1;
		}

		//en_gpio
		if (!gpio_is_valid(bq->init_data.en_gpio)) 
		{
			dev_err(bq->dev, "en port invalid [%d]\n", bq->init_data.en_gpio);
			return -1;
		}

		/* configure touchscreen reset out gpio */
		ret = gpio_request(bq->init_data.en_gpio, "bq24296_en_gpio");
		if (ret) {
			dev_err(bq->dev, "unable to request gpio [%d]\n", bq->init_data.en_gpio);
			return -2;
		}

	ret = gpio_direction_output(bq->init_data.en_gpio, 1);		
	if (ret) {
		dev_err(bq->dev, "unable to set direction for gpio [%d]\n", bq->init_data.en_gpio);
		gpio_free(bq->init_data.en_gpio);
		return -3;
	}

		phy_ret = bq24296_config_qpnp_charging(bq, 0);
		if(phy_ret==0)
		{
			en = (!en)?1:0;
			gpio_set_value(bq->init_data.en_gpio, en);
		}

		dev_err(bq->dev, "val of gpio %d[%d] phy_ret=%d\n", bq->init_data.en_gpio, gpio_get_value(bq->init_data.en_gpio), phy_ret);
	}else
		dev_err(bq->dev, "get hw id fail\n");
	
	return 0;
}

static int bq24296_config_gpio(struct bq24296_device *bq, int en)
{
	int ret;

	if(bq->init_data.en_gpio==-1)
	{
		bq24296_config_en_gpio(bq, en);
	}
	
#if 1
	//psel_gpio
	if (!gpio_is_valid(bq->init_data.psel_gpio)) 
	{
		dev_err(bq->dev, "en port invalid [%d]\n", bq->init_data.psel_gpio);
		return -1;
	}
	
	/* configure touchscreen reset out gpio */
	ret = gpio_request(bq->init_data.psel_gpio, "bq24296_psel_gpio");
	if (ret) {
		dev_err(bq->dev, "unable to request gpio [%d]\n", bq->init_data.psel_gpio);
		return -2;
	}

	ret = gpio_direction_output(bq->init_data.psel_gpio, 1);		
	if (ret) {
		dev_err(bq->dev, "unable to set direction for gpio [%d]\n", bq->init_data.psel_gpio);
		gpio_free(bq->init_data.psel_gpio);
		return -3;
	}

	gpio_set_value(bq->init_data.psel_gpio, 1);

	dev_err(bq->dev, "val of gpio %d[%d] \n", bq->init_data.psel_gpio, gpio_get_value(bq->init_data.psel_gpio));


	//otg_gpio
	if (!gpio_is_valid(bq->init_data.otg_gpio)) 
	{
		dev_err(bq->dev, "en port invalid [%d]\n", bq->init_data.otg_gpio);
		return -1;
	}
	
	/* configure touchscreen reset out gpio */
	ret = gpio_request(bq->init_data.otg_gpio, "bq24296_psel_gpio");
	if (ret) {
		dev_err(bq->dev, "unable to request gpio [%d]\n", bq->init_data.otg_gpio);
		return -2;
	}

	ret = gpio_direction_output(bq->init_data.otg_gpio, 1);		
	if (ret) {
		dev_err(bq->dev, "unable to set direction for gpio [%d]\n", bq->init_data.otg_gpio);
		gpio_free(bq->init_data.otg_gpio);
		return -3;
	}

	gpio_set_value(bq->init_data.otg_gpio, 1);

	dev_err(bq->dev, "val of gpio %d[%d] \n", bq->init_data.otg_gpio, gpio_get_value(bq->init_data.otg_gpio));
#endif

	return 0;
}

static int bq24296_set_en_gpio(struct bq24296_device *bq, int en)
{
	int ret;
	
	if(bq->init_data.en_gpio==-1)
	{
		ret = bq24296_config_en_gpio(bq, en);
		if(ret<0)
			return -1;
	}
	
	if (!gpio_is_valid(bq->init_data.en_gpio)) 
	{
		dev_err(bq->dev, "en port invalid [%d]\n", bq->init_data.en_gpio);
		return -2;
	}

	en = (!en)?1:0;
	//dev_err(bq->dev, "val of en %dn", en);
	
	gpio_set_value(bq->init_data.en_gpio, en);

	//dev_err(bq->dev, "val of gpio %d[%d] \n", bq->init_data.en_gpio, gpio_get_value(bq->init_data.en_gpio));

	return 0;
}

//yexh1 add for usb otg function
int otg_func_set(bool votg_on)
{	
	struct bq24296_device *bq = p_bq24296_dev;
	
	pr_err("%s votg_on %d\n", __func__, votg_on);

	if(bq==NULL)
	{
		pr_debug("%s p_bq24296_dev null point\n", __func__);
		return -1;
	}
	
	if(bq->init_data.otg_gpio < 0){
		pr_err("chg otg gpio is unvalid.\n");
               return -ENODEV;
	}
	
	if(votg_on){
		gpio_direction_output(bq->init_data.otg_gpio, 1);
		bq24296_set_boostv(bq, 0xF);
		bq24296_set_otg_config(bq, 1);		
	}else{
		
		gpio_direction_output(bq->init_data.otg_gpio, 0);
		bq24296_set_otg_config(bq, 0);		
	}
	return 0;
}

static int bq24296_otg_regulator_enable(struct regulator_dev *rdev)
{
	int rc = 0;	

	rc = otg_func_set(true);
	if (rc)
		pr_err("Couldn't enable  OTG mode rc=%d\n", rc);

	return rc;
}

static int bq24296_otg_regulator_disable(struct regulator_dev *rdev)
{
	int rc = 0;	

	rc = otg_func_set(false);
	if (rc)
		pr_err("Couldn't enable  OTG mode rc=%d\n", rc);

	return rc;
}

static int bq24296_otg_regulator_is_enable(struct regulator_dev *rdev)
{
	struct bq24296_device *bq = p_bq24296_dev;	

	if(bq==NULL)
	{
		pr_debug("%s p_bq24296_dev null point\n", __func__);
		return -1;
	}
	
	return bq24296_get_otg_config(bq);	
}

struct regulator_ops bq24296_otg_reg_ops = {
	.enable		= bq24296_otg_regulator_enable,
	.disable	= bq24296_otg_regulator_disable,
	.is_enabled	= bq24296_otg_regulator_is_enable,
};


static int bq24296_otg_regulator_init(struct bq24296_device *chip)
{
	int rc = 0;
	struct regulator_init_data *init_data;
	struct regulator_config cfg = {};

	init_data = of_get_regulator_init_data(chip->dev, chip->dev->of_node);
	if (!init_data) {
		dev_err(chip->dev, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	if (init_data->constraints.name) {
		chip->otg_vreg.rdesc.owner = THIS_MODULE;
		chip->otg_vreg.rdesc.type = REGULATOR_VOLTAGE;
		chip->otg_vreg.rdesc.ops = &bq24296_otg_reg_ops;
		chip->otg_vreg.rdesc.name = init_data->constraints.name;

		cfg.dev = chip->dev;
		cfg.init_data = init_data;
		cfg.driver_data = chip;
		cfg.of_node = chip->dev->of_node;

		init_data->constraints.valid_ops_mask
			|= REGULATOR_CHANGE_STATUS;

		chip->otg_vreg.rdev = regulator_register(
					&chip->otg_vreg.rdesc, &cfg);
		if (IS_ERR(chip->otg_vreg.rdev)) {
			rc = PTR_ERR(chip->otg_vreg.rdev);
			chip->otg_vreg.rdev = NULL;
			if (rc != -EPROBE_DEFER)
				dev_err(chip->dev,
					"OTG reg failed, rc=%d\n", rc);
		}
	}

	return rc;
}


//yexh1

/* exec command function */
int bq24296_exec_command(enum bq24296_command command, unsigned char param)
{
	int ret;
	struct bq24296_device *bq = p_bq24296_dev;
	
	pr_err("%s command %d\n", __func__, command);

	if((command==0)&&(param!=CHARGER_UNKNOWN))
		bq->charger_plug_in_flag = true;
	else if((command==0)&&(param==CHARGER_UNKNOWN))
		bq->charger_plug_in_flag = false;

	if(bq==NULL)
	{
		pr_err("%s p_bq24296_dev null point\n", __func__);
		return -1;
	}

	if(bq->suspend_flag==1)
	{
		pr_err("%s suspend_flag =0 return\n", __func__);
		return -2;
	}
	
	switch (command) {
		case BQ24296_SET_CHR_TYPE:
			ret = bq24296_set_chr_type(bq, param);	
			break;
		case BQ24296_SET_CHR_ENABLE:
			ret = bq24296_set_chr_enable(bq, 1);	
			break;
		case BQ24296_SET_CHR_DISABLE:
			ret = bq24296_set_chr_enable(bq, 0);	
			break;			
		default :
			ret = -EINVAL;
			break;
	}
	
	return ret;
}

#if 0
/* detect chip type */
static enum bq24296_chip bq24296_detect_chip(struct bq24296_device *bq)
{
	struct i2c_client *client = to_i2c_client(bq->dev);
	int ret = bq24296_exec_command(bq, BQ24296_PART_NUMBER, 0);

	if (ret < 0)
		return ret;

	switch (client->addr) {
	case 0x6b:
		switch (ret) {
		case 1:
			if (bq->chip == BQ24296)
				return bq->chip;
			else
				return BQ24296;
		case 3:
			if (bq->chip == BQ24297)
				return bq->chip;
			else
				return BQ24297;
		default:
			return BQ24296;
		}
		break;
	}

	return BQ24296;
}

/* detect chip revision */
static int bq24296_detect_revision(struct bq24296_device *bq)
{
	int ret = bq24296_exec_command(bq, BQ24296_REVISION, 0);
	int chip = bq24296_detect_chip(bq);

	if (ret < 0 || chip < 0)
		return -1;

	return ret;
}


/* return chip vender code */
static int bq24296_get_vender_code(struct bq24296_device *bq)
{
	int ret;

	ret = bq24296_exec_command(bq, BQ24296_VENDER_CODE, 0);
	if (ret < 0)
		return 0;

	/* convert to binary */
	return (ret & 0x1) +
	       ((ret >> 1) & 0x1) * 10 +
	       ((ret >> 2) & 0x1) * 100;
}
#endif

/**** properties functions ****/

/* set default value of property */
#define bq24296_set_default_value(bq, prop) \
	do { \
		int ret = 0; \
		if (bq->init_data.prop != -1) \
			ret = bq24296_set_##prop(bq, bq->init_data.prop); \
		if (ret < 0) \
			return ret; \
	} while (0)

/* set default values of all properties */
static int bq24296_set_defaults(struct bq24296_device *bq)
{
	bq24296_set_chg_config(bq, 0);
	bq24296_set_en_term(bq, 0);//->enable
	
	bq24296_set_default_value(bq, current_limit);
	bq24296_set_default_value(bq, weak_battery_voltage);
	bq24296_set_default_value(bq, battery_regulation_voltage);
	bq24296_set_default_value(bq, charge_current);	
	bq24296_set_default_value(bq, termination_current);
	
	bq24296_set_en_term(bq, 1);//->enable
	bq24296_set_chg_config(bq, 1);
	return 0;
}

/**** charger mode functions ****/

/* set charger mode */
#if 0
static int bq24296_set_mode(struct bq24296_device *bq, enum bq24296_mode mode)
{
	int ret = 0;
	int charger = 0;
	int boost = 0;

	if (mode == BQ24296_MODE_BOOST)
		boost = 1;
	else if (mode != BQ24296_MODE_OFF)
		charger = 1;

	if (!charger)
		ret = bq24296_exec_command(bq, BQ24296_CHARGER_DISABLE, 0);

	if (!boost)
		ret = bq24296_exec_command(bq, BQ24296_BOOST_MODE_DISABLE, 0);

	if (ret < 0)
		return ret;

	switch (mode) {
	case BQ24296_MODE_OFF:
		dev_dbg(bq->dev, "changing mode to: Offline\n");
		ret = bq24296_set_current_limit(bq, 100);
		break;
	case BQ24296_MODE_NONE:
		dev_dbg(bq->dev, "changing mode to: N/A\n");
		ret = bq24296_set_current_limit(bq, 100);
		break;
	case BQ24296_MODE_HOST_CHARGER:
		dev_dbg(bq->dev, "changing mode to: Host/HUB charger\n");
		ret = bq24296_set_current_limit(bq, 500);
		break;
	case BQ24296_MODE_DEDICATED_CHARGER:
		dev_dbg(bq->dev, "changing mode to: Dedicated charger\n");
		ret = bq24296_set_current_limit(bq, 1800);
		break;
	case BQ24296_MODE_BOOST: /* Boost mode */
		dev_dbg(bq->dev, "changing mode to: Boost\n");
		ret = bq24296_set_current_limit(bq, 100);
		break;
	}

	if (ret < 0)
		return ret;

	if (charger)
		ret = bq24296_exec_command(bq, BQ24296_CHARGER_ENABLE, 0);
	else if (boost)
		ret = bq24296_exec_command(bq, BQ24296_BOOST_MODE_ENABLE, 0);

	if (ret < 0)
		return ret;

	bq24296_set_default_value(bq, weak_battery_voltage);
	bq24296_set_default_value(bq, battery_regulation_voltage);

	bq->mode = mode;
	sysfs_notify(&bq->charger.dev->kobj, NULL, "mode");

	return 0;

}
#endif

/**** timer functions ****/

/* enable/disable auto resetting chip timer */
static void bq24296_set_autotimer(struct bq24296_device *bq, int state)
{
	mutex_lock(&bq24296_timer_mutex);

	if (bq->autotimer == state) {
		mutex_unlock(&bq24296_timer_mutex);
		return;
	}

	bq->autotimer = state;

	if (state) {
		schedule_delayed_work(&bq->work, BQ24296_TIMER_TIMEOUT * HZ);
		bq24296_set_wdt_rst(bq);
		bq->timer_error = NULL;
	} else {
		cancel_delayed_work_sync(&bq->work);
	}

	mutex_unlock(&bq24296_timer_mutex);
}

/* called by bq24296_timer_work on timer error */
static void bq24296_timer_error(struct bq24296_device *bq, const char *msg)
{
#if 0
	bq->timer_error = msg;
	sysfs_notify(&bq->charger.dev->kobj, NULL, "timer");
	dev_err(bq->dev, "%s\n", msg);
	if (bq->automode > 0)
		bq->automode = 0;
	bq24296_set_mode(bq, BQ24296_MODE_OFF);
	bq24296_set_autotimer(bq, 0);
#endif	
}

#ifdef CONFIG_EXT_LED
#define LENOVO_EXT_LED_CLOSE	0x0
#define LENOVO_EXT_LED_RED		0x78200000 //0x000000ff
#define LENOVO_EXT_LED_ORANGE	0x78200000 //0x000080ff
#define LENOVO_EXT_LED_GREEN	0x78200000 //0x0000ff00

static int bq24296_led_ctrl_flag = 1;

static void bq24296_set_led_type(unsigned int type)
{
#ifdef CONFIG_LED_SN3193
	extern void SN3193_PowerOff_Charging_RGB_LED(unsigned int level);	

	SN3193_PowerOff_Charging_RGB_LED(type);
#else	
	//empty
#endif		
}

static ssize_t bq24296_set_led_enable(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t count)
{
	//struct bq24296_device *bq = (struct bq24296_device*) dev;
	long val;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;

	pr_err("%s val = %d\n", __func__, (int)val);			

	if (val == 1)
		bq24296_led_ctrl_flag = 1;
	else 
		bq24296_led_ctrl_flag = 0;

	return count;	
}

static ssize_t bq24296_get_led_enable(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	//struct bq24296_device *bq = (struct bq24296_device*) dev;
	
	return sprintf(buf, "%d\n", bq24296_led_ctrl_flag);	
}

static int bq24296_set_led_as_soc(struct bq24296_device *bq)
{
	int soc = 0;
	union power_supply_propval val;

	if(bq24296_led_ctrl_flag==0)
	{
		return -1;
	}

	if (!bq->bms_psy)
		bq->bms_psy = power_supply_get_by_name("battery");

	if(!bq->bms_psy)
	{
		pr_err("%s get battery power_supply error!\n", __func__);
		return -2;//defult 25C
	}
	
	bq->bms_psy->get_property(bq->bms_psy, POWER_SUPPLY_PROP_CAPACITY, &val);

	soc = val.intval;
	
	pr_err("%s .UI_SOC=%d\n", __func__, soc);

	if(soc<30)
		bq24296_set_led_type(LENOVO_EXT_LED_RED);
	else if((soc>=30)&&(soc<=75))
		bq24296_set_led_type(LENOVO_EXT_LED_ORANGE);
	else if((soc>=75)&&(soc<=99))
		bq24296_set_led_type(LENOVO_EXT_LED_GREEN);	
	else if((soc>=99))
		bq24296_set_led_type(LENOVO_EXT_LED_CLOSE);	

	return 0;
}
#endif

/* delayed work function for auto resetting chip timer */
static void bq24296_timer_work(struct work_struct *work)
{
	struct bq24296_device *bq = container_of(work, struct bq24296_device,
						 work.work);
	int ret;
	int phy_ret = 0;
	
	if (!bq->autotimer)
		return;

	ret = bq24296_set_wdt_rst(bq);
	if (ret < 0) {
		bq24296_timer_error(bq, "Resetting timer failed");
		return;
	}
	bq24296_set_batlowv(bq, 1);//bat low = 3V
	bq24296_set_sys_min(bq, 0);//mini system voltage = 3.0V
	
	ret = bq24296_set_chg_as_chr_type(bq);
	if(ret==0)
	{
		//bq24296_set_en_hiz(bq, 0);
		phy_ret = bq24296_config_qpnp_charging(bq, 0);
		if(bq->en_flags==false)
		{
			bq24296_set_en_gpio(bq, 0);
			bq24296_set_en_hiz(bq, 1);
		}else
		{
			bq24296_set_en_gpio(bq, 1);
			bq24296_set_en_hiz(bq, 0);
		}
		dev_err(bq->dev, "val of gpio %d[%d] flag=%d\n", bq->init_data.en_gpio, gpio_get_value(bq->init_data.en_gpio), bq->en_flags);

		bq24296_set_termination_current(bq, bq->init_data.termination_current);
		bq24296_set_battery_regulation_voltage(bq, bq->init_data.battery_regulation_voltage);

		bq24296_set_en_term(bq, 1);//->enable
		//bq24296_set_chg_config(bq, 1);

#ifdef CONFIG_EXT_LED
		bq24296_set_led_as_soc(bq);
#endif
	}

	bq->charger_plug_in_flag = false;
	
/*	boost = bq24296_exec_command(bq, BQ24296_BOOST_MODE_STATUS, 0);
	if (boost < 0) {
		bq24296_timer_error(bq, "Unknown error");
		return;
	}*/

/*	error = bq24296_exec_command(bq, BQ24296_FAULT_STATUS, 0);
	if (error < 0) {
		bq24296_timer_error(bq, "Unknown error");
		return;
	}*/
	bq24296_dump_reg(bq);

	if(phy_ret==0)		
		schedule_delayed_work(&bq->work, BQ24296_TIMER_TIMEOUT * HZ);
	else
		schedule_delayed_work(&bq->work, HZ);
}

/**** power supply interface code ****/

//power_supply
#ifdef BQ24296_POWER_SUPPLY
static char *bq24296_chip_name[] = {
	"bq24296",
	"bq24297",
};

static enum power_supply_property bq24296_power_supply_props[] = {
	/* TODO: maybe add more power supply properties */
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TEMP,	
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CHARGE_ENABLED,
};

static int bq24296_power_supply_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	struct bq24296_device *bq = container_of(psy, struct bq24296_device, charger);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	{
		static struct power_supply* fg_psy = 0;
		union power_supply_propval psy_ret = {0,};		
		static int soc = 0;

		if(!fg_psy)
			fg_psy = power_supply_get_by_name("max17058_fgauge");
		
		if(fg_psy)
		{
			fg_psy->get_property(fg_psy, POWER_SUPPLY_PROP_CAPACITY, &psy_ret);
			soc = psy_ret.intval;
		}
		
		ret = bq24296_get_chrg_stat(bq);//bq24296_exec_command(bq, BQ24296_CHARGE_STATUS, 0);
		if (ret < 0)
			return ret;
		else if (ret == 0) /* Ready */
		{
			if(bq->charger_plug_in_flag==true)
			{
				if(soc==100)
					val->intval = POWER_SUPPLY_STATUS_FULL;
				else
					val->intval = POWER_SUPPLY_STATUS_CHARGING;
			}else
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}else if (ret == 1) /* Charge in progress pre*/
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (ret == 2) /* Charge in progress */
		{
			if(soc==100)
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
		}else if (ret == 3) /* Charge done */
			val->intval = POWER_SUPPLY_STATUS_FULL;	
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;

		break;
	}
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = bq->temp;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = bq->model;
		break;
	case POWER_SUPPLY_PROP_CHARGE_ENABLED:
		val->intval = (bq->en_flags)? 1:0;		
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bq24296_power_supply_set_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     const union power_supply_propval *val)
{
	struct bq24296_device *bq = container_of(psy, struct bq24296_device, charger);

	switch (psp) {
		//just for charging temp protect function debug 
		case POWER_SUPPLY_PROP_TEMP:
			pr_err("%s debug charging temp, intval = %d\n", __func__, val->intval);
			if((val->intval<-20)&&(val->intval>60))
				bq->temp_debug_flag = false;
			else
			{
				bq->temp_debug_flag = true;
				bq->temp = val->intval;
			}
			break;
		case POWER_SUPPLY_PROP_CHARGE_ENABLED:
			pr_err("%s debug charging enable, intval = %d\n", __func__, val->intval);			
			bq->en_flags = (val->strval==0)? false:true;			
			break;
		default:
			pr_err("%s not support power_supply property cmd\n", __func__);
			return -EINVAL;
	}
	return 0;
}

static int bq24296_power_supply_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp)
{
	switch (psp) {
		case POWER_SUPPLY_PROP_CHARGE_ENABLED:
			return 1;
		default:
			break;
	}

	return 0;
}

static int bq24296_power_supply_init(struct bq24296_device *bq)
{
	int ret;
	int chip;
	char revstr[8];

	bq->charger.name = bq->name;
	bq->charger.type = POWER_SUPPLY_TYPE_USB;
	bq->charger.properties = bq24296_power_supply_props;
	bq->charger.num_properties = ARRAY_SIZE(bq24296_power_supply_props);
	bq->charger.get_property = bq24296_power_supply_get_property;
	bq->charger.set_property = bq24296_power_supply_set_property;
	bq->charger.property_is_writeable = bq24296_power_supply_property_is_writeable;	

	ret = bq24296_get_pn(bq);
	if (ret == 3)
		chip = BQ24297;
	else 
		chip = BQ24296;

	ret = bq24296_get_rev(bq);
	if (ret < 0)
		strcpy(revstr, "unknown");
	else
		sprintf(revstr, "1.%d", ret);

	bq->model = kasprintf(GFP_KERNEL,
				"chip %s, revision %s, vender code %.3d",
				bq24296_chip_name[chip], revstr, ret);

	pr_err("mode info %s\n", bq->model);
	
	if (!bq->model) {
		dev_err(bq->dev, "failed to allocate model name\n");
		return -ENOMEM;
	}

	ret = power_supply_register(bq->dev, &bq->charger);
	if (ret) {
		kfree(bq->model);
		return ret;
	}

	return 0;
}


static void bq24296_power_supply_exit(struct bq24296_device *bq)
{
	bq->autotimer = 0;
	if (bq->automode > 0)
		bq->automode = 0;
	cancel_delayed_work_sync(&bq->work);
	power_supply_unregister(&bq->charger);
	kfree(bq->model);
}
#endif

/**** additional sysfs entries for power supply interface ****/

/* show *_status entries */
static ssize_t bq24296_sysfs_show_status(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
//	struct power_supply *psy = dev_get_drvdata(dev);
//	struct bq24296_device *bq = container_of(psy, struct bq24296_device, charger);
	struct bq24296_device *bq = (struct bq24296_device*) dev;
	int ret;

	if (strcmp(attr->attr.name, "otg_status") == 0)
		ret = bq24296_get_otg_config(bq);
	else if (strcmp(attr->attr.name, "charge_status") == 0)
		ret = bq24296_get_chg_config(bq);
	else if (strcmp(attr->attr.name, "boost_status") == 0)
		ret = -EINVAL;
	else if (strcmp(attr->attr.name, "fault_status") == 0)
		ret = bq24296_get_fault(bq);
	else
		return -EINVAL;

	return sprintf(buf, "%d\n", ret);
}

/* directly set raw value to chip register, format: 'register value' */
static ssize_t bq24296_sysfs_set_registers(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t count)
{
	struct bq24296_device *bq = (struct bq24296_device*) dev;
	ssize_t ret = 0;
	unsigned int reg;
	unsigned int val;

	if (sscanf(buf, "%x %x", &reg, &val) != 2)
		return -EINVAL;

	if (reg > 4 || val > 255)
		return -EINVAL;

	ret = bq24296_i2c_write(bq, reg, val);
	if (ret < 0)
		return ret;
	return count;
}

/* print value of chip register, format: 'register=value' */
static ssize_t bq24296_sysfs_print_reg(struct bq24296_device *bq,
				       u8 reg,
				       char *buf)
{
	int ret = bq24296_i2c_read(bq, reg);

	if (ret < 0)
		return sprintf(buf, "%#.2x=error %d\n", reg, ret);
	return sprintf(buf, "%#.2x=%#.2x\n", reg, ret);
}

/* show all raw values of chip register, format per line: 'register=value' */
static ssize_t bq24296_sysfs_show_registers(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct bq24296_device *bq = (struct bq24296_device*) dev;
	ssize_t ret = 0;

	ret += bq24296_sysfs_print_reg(bq, bq24296_CON0, buf+ret);
	ret += bq24296_sysfs_print_reg(bq, bq24296_CON1, buf+ret);
	ret += bq24296_sysfs_print_reg(bq, bq24296_CON2, buf+ret);
	ret += bq24296_sysfs_print_reg(bq, bq24296_CON3, buf+ret);
	ret += bq24296_sysfs_print_reg(bq, bq24296_CON5, buf+ret);
	ret += bq24296_sysfs_print_reg(bq, bq24296_CON6, buf+ret);
	ret += bq24296_sysfs_print_reg(bq, bq24296_CON7, buf+ret);
	ret += bq24296_sysfs_print_reg(bq, bq24296_CON8, buf+ret);
	ret += bq24296_sysfs_print_reg(bq, bq24296_CON9, buf+ret);
	ret += bq24296_sysfs_print_reg(bq, bq24296_CON10, buf+ret);
	
	return ret;
}

/* set *_enable entries */
static ssize_t bq24296_sysfs_set_enable(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	struct bq24296_device *bq = (struct bq24296_device*) dev;
	long val;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;

	if (strcmp(attr->attr.name, "charge_termination_enable") == 0)
		bq24296_set_en_term(bq, val);
	else if (strcmp(attr->attr.name, "high_impedance_enable") == 0)
		bq24296_set_en_hiz(bq, val);
	else if (strcmp(attr->attr.name, "otg_pin_enable") == 0)
		bq24296_set_otg_config(bq, val);
	else if (strcmp(attr->attr.name, "stat_pin_enable") == 0)
		bq24296_set_chg_config(bq, val);
	else
		return -EINVAL;

	return count;
}

/* show *_enable entries */
static ssize_t bq24296_sysfs_show_enable(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct bq24296_device *bq = (struct bq24296_device*) dev;
	int ret;

	if (strcmp(attr->attr.name, "charge_termination_enable") == 0)
		ret = bq24296_get_en_term(bq);
	else if (strcmp(attr->attr.name, "high_impedance_enable") == 0)
		ret = bq24296_get_en_hiz(bq);
	else if (strcmp(attr->attr.name, "otg_pin_enable") == 0)
		ret = bq24296_get_otg_config(bq);
	else if (strcmp(attr->attr.name, "stat_pin_enable") == 0)
		ret = bq24296_get_chg_config(bq);
	else
		return -EINVAL;

	return sprintf(buf, "%d\n", ret);
}

static DEVICE_ATTR(charge_termination_enable, S_IWUSR | S_IRUGO,
		bq24296_sysfs_show_enable, bq24296_sysfs_set_enable);
static DEVICE_ATTR(high_impedance_enable, S_IWUSR | S_IRUGO,
		bq24296_sysfs_show_enable, bq24296_sysfs_set_enable);
static DEVICE_ATTR(otg_pin_enable, S_IWUSR | S_IRUGO,
		bq24296_sysfs_show_enable, bq24296_sysfs_set_enable);
static DEVICE_ATTR(stat_pin_enable, S_IWUSR | S_IRUGO,
		bq24296_sysfs_show_enable, bq24296_sysfs_set_enable);

static DEVICE_ATTR(registers, S_IWUSR | S_IRUGO,
		bq24296_sysfs_show_registers, bq24296_sysfs_set_registers);

static DEVICE_ATTR(otg_status, S_IRUGO, bq24296_sysfs_show_status, NULL);
static DEVICE_ATTR(charge_status, S_IRUGO, bq24296_sysfs_show_status, NULL);
static DEVICE_ATTR(boost_status, S_IRUGO, bq24296_sysfs_show_status, NULL);
static DEVICE_ATTR(fault_status, S_IRUGO, bq24296_sysfs_show_status, NULL);
#ifdef CONFIG_EXT_LED
static DEVICE_ATTR(led_status, S_IWUSR | S_IRUGO, bq24296_get_led_enable, bq24296_set_led_enable);
#endif

static struct attribute *bq24296_sysfs_attributes[] = {
	/*
	 * TODO: some (appropriate) of these attrs should be switched to
	 * use power supply class props.
	 */
	&dev_attr_charge_termination_enable.attr,
	&dev_attr_high_impedance_enable.attr,
	&dev_attr_otg_pin_enable.attr,
	&dev_attr_stat_pin_enable.attr,

	&dev_attr_registers.attr,

	&dev_attr_otg_status.attr,
	&dev_attr_charge_status.attr,
	&dev_attr_boost_status.attr,
	&dev_attr_fault_status.attr,
#ifdef CONFIG_EXT_LED
	&dev_attr_led_status.attr,
#endif
	NULL,
};

static const struct attribute_group bq24296_sysfs_attr_group = {
	.attrs = bq24296_sysfs_attributes,
};

static int bq24296_sysfs_init(struct bq24296_device *bq)
{
	return sysfs_create_group(&bq->charger.dev->kobj,
			&bq24296_sysfs_attr_group);
}

static void bq24296_sysfs_exit(struct bq24296_device *bq)
{
	sysfs_remove_group(&bq->charger.dev->kobj, &bq24296_sysfs_attr_group);
}

static void determine_initial_status(struct bq24296_device *bq)
{
	int ret;
	
	ret = bq24296_get_vbus_stat(bq);
	printk("[%s]usb val = 0x%x\n", __func__, ret);
	if((ret==0x10)||(ret==0x01))
	{
		bq->usb_present = 1;
	}else
		bq->usb_present = 0;

	if(bq->usb_present)
		power_supply_set_present(bq->usb_psy, 1);
}

static int bq24296_parse_dt(struct device *dev, struct bq24296_platform_data *pdata)
{
	int r = 0;
	struct device_node *np = dev->of_node;

	pdata->en_gpio = -1;

	pdata->en_gpio_dvt1 = of_get_named_gpio(np, "ti,en-gpio-dvt1", 0);
	if ((!gpio_is_valid(pdata->en_gpio_dvt1)))
	{
		dev_err(dev, "'get en gpio d1 %d failed\n", pdata->en_gpio_dvt1);
		return -EINVAL;
	}

	pdata->en_gpio_dvt2 = of_get_named_gpio(np, "ti,en-gpio-dvt1", 0);
	if ((!gpio_is_valid(pdata->en_gpio_dvt2)))
	{
		dev_err(dev, "'get en gpio d2 %d failed\n", pdata->en_gpio_dvt2);
		return -EINVAL;
	}

	pdata->irq_gpio = of_get_named_gpio(np, "ti,irq-gpio", 0);
	if ((!gpio_is_valid(pdata->irq_gpio)))
	{
		dev_err(dev, "'get irq gpio %d failed\n", pdata->irq_gpio);
		return -EINVAL;
	}

	pdata->psel_gpio = of_get_named_gpio(np, "ti,psel-gpio", 0);
	if ((!gpio_is_valid(pdata->psel_gpio)))
	{
		dev_err(dev, "'get psel gpio %d failed\n", pdata->psel_gpio);
		return -EINVAL;
	}

	pdata->otg_gpio = of_get_named_gpio(np, "ti,otg-gpio", 0);
	if ((!gpio_is_valid(pdata->otg_gpio)))
	{
		dev_err(dev, "'get otg gpio %d failed\n", pdata->otg_gpio);
		return -EINVAL;
	}

	r = of_property_read_u32(np, "ti,term-current", &pdata->termination_current);
	if (r < 0)
	{
		pr_err("%s get term of result : %d %d\n", __func__, pdata->termination_current, r);
		pdata->termination_current = BQ24296_DEF_TERM_CURRENT;
	}
	r = of_property_read_u32(np, "ti,chg-current", &pdata->charge_current);
	if (r < 0)
	{
		pr_err("%s get cur of result : %d %d\n", __func__, pdata->charge_current, r);
		pdata->charge_current =  BQ24296_DEF_CHG_CURRENT;
	}
	r = of_property_read_u32(np, "ti,cv-vol", &pdata->battery_regulation_voltage);
	if (r < 0)
	{
		pr_err("%s get cv of result : %d %d\n", __func__, pdata->battery_regulation_voltage, r);
		pdata->battery_regulation_voltage = BQ24296_DEF_CV_VOL;
	}
	
	r = of_property_read_u32(np, "ti,current-limit", &pdata->current_limit);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->current_limit, r);
		pdata->current_limit = BQ24296_DEF_CURRENT_LIMIT;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-cur-0m", &pdata->chg_temp_protect_0m);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_0m, r);
		pdata->chg_temp_protect_0m = 0;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-cur-10m", &pdata->chg_temp_protect_10m);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_10m, r);
		pdata->chg_temp_protect_10m = BQ24296_DEF_CURRENT_ABNORMAL;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-cur-45m", &pdata->chg_temp_protect_45m);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_45m, r);
		pdata->chg_temp_protect_45m = BQ24296_DEF_CHG_CURRENT;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-cur-50m", &pdata->chg_temp_protect_50m);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_50m, r);
		pdata->chg_temp_protect_50m = BQ24296_DEF_CURRENT_ABNORMAL;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-cur-50p", &pdata->chg_temp_protect_50p);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_50p, r);
		pdata->chg_temp_protect_50p = 0;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-vol-0m", &pdata->chg_temp_protect_vol_0m);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_0m, r);
		pdata->chg_temp_protect_0m = 0;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-vol-10m", &pdata->chg_temp_protect_vol_10m);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_10m, r);
		pdata->chg_temp_protect_10m = BQ24296_DEF_CV_VOL;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-vol-45m", &pdata->chg_temp_protect_vol_45m);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_45m, r);
		pdata->chg_temp_protect_45m = BQ24296_DEF_CV_VOL;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-vol-50m", &pdata->chg_temp_protect_vol_50m);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_50m, r);
		pdata->chg_temp_protect_50m = BQ24296_DEF_CV_VOL;
	}

	r = of_property_read_u32(np, "ti,chg-temp-protect-vol-50p", &pdata->chg_temp_protect_vol_50p);
	if (r < 0)
	{
		pr_err("%s get limit of result : %d %d\n", __func__, pdata->chg_temp_protect_50p, r);
		pdata->chg_temp_protect_50p = BQ24296_DEF_CV_VOL;
	}
	
	pr_debug("%s get of result : %d %d %d %d\n", __func__, 
		pdata->termination_current, pdata->charge_current, pdata->battery_regulation_voltage, pdata->current_limit);
	
	return 0;
}

#ifdef CONFIG_PM

static int bq24296_suspend(struct i2c_client *client,
		pm_message_t state)
{
	struct bq24296_device *bq = i2c_get_clientdata(client);

		pr_err("%s cancel_delayed_work\n", __func__);

	bq->suspend_flag = 1;
	
	cancel_delayed_work(&bq->work);
	return 0;
}

static int bq24296_resume(struct i2c_client *client)
{
	struct bq24296_device *bq = i2c_get_clientdata(client);

		pr_err("%s schedule_delayed_work\n", __func__);

	bq->suspend_flag = 0;

	schedule_delayed_work(&bq->work, 0);
	return 0;
}

#else

#define bq24296_suspend NULL
#define bq24296_resume NULL

#endif /* CONFIG_PM */

/* main bq24296 probe function */
static int bq24296_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret;
	int num;
	char *name;
	struct bq24296_device *bq;

	if (client->dev.of_node) {
		int r;
		
		client->dev.platform_data = devm_kzalloc(&client->dev,
			sizeof(struct bq24296_platform_data), GFP_KERNEL);
		if (!client->dev.platform_data) {
  			dev_err(&client->dev, "bq24296 probe: Failed to allocate memory\n");
			return -ENOMEM;
		}

		r = bq24296_parse_dt(&client->dev, client->dev.platform_data);
		if (r)
			return r;
	} 

	/* Get new ID for the new device */
	mutex_lock(&bq24296_id_mutex);
	num = idr_alloc(&bq24296_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&bq24296_id_mutex);
	if (num < 0)
		return num;

	name = kasprintf(GFP_KERNEL, "%s-%d", id->name, num);
	if (!name) {
		dev_err(&client->dev, "failed to allocate device name\n");
		ret = -ENOMEM;
		goto error_1;
	}

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq) {
		dev_err(&client->dev, "failed to allocate device data\n");
		ret = -ENOMEM;
		goto error_2;
	}

	client->addr = BQ24296_I2C_ADDR;//known dts register error....
	
	i2c_set_clientdata(client, bq);

	bq->id = num;
	bq->dev = &client->dev;
	bq->chip = BQ24296;//id->driver_data;
	bq->name = "ex-charger";
	bq->status = BQ24296_READY;
	bq->reported_mode = BQ24296_READY;
	bq->autotimer = 0;
	bq->automode = 0;
	bq->en_flags = true;
	
#ifdef CONFIG_EXT_LED
	bq24296_led_ctrl_flag = 1;
#endif

	bq->usb_psy = power_supply_get_by_name("usb");
	if (!bq->usb_psy) {
		pr_err("usb supply not found deferring probe\n");
		return -EPROBE_DEFER;
	}

	bq->bms_psy = power_supply_get_by_name("battery");
	if (!bq->bms_psy) {
		pr_err("ma17058 battery power supply not found deferring probe\n");
	}
	
	memcpy(&bq->init_data, client->dev.platform_data,
			sizeof(bq->init_data));

	bq24296_reset_chip(bq);

#ifdef BQ24296_POWER_SUPPLY
	ret = bq24296_power_supply_init(bq);
	if (ret) {
		dev_err(bq->dev, "failed to register power supply: %d\n", ret);
		goto error_3;
	}	
#endif

	p_bq24296_dev = bq;

	ret = bq24296_sysfs_init(bq);
	if (ret) {
		dev_err(bq->dev, "failed to create sysfs entries: %d\n", ret);
		goto error_4;
	}

	ret = bq24296_set_defaults(bq);
	if (ret) {
		dev_err(bq->dev, "failed to set default values: %d\n", ret);
		goto error_2;
	}

	//if (bq->init_data.set_mode_hook) {
	if (0) {		
		//empty
	} else {
		bq->automode = -1;
		dev_info(bq->dev, "automode not supported\n");
	}

	INIT_DELAYED_WORK(&bq->work, bq24296_timer_work);
	bq24296_set_autotimer(bq, 1);

	wake_lock_init(&bq->battery_suspend_lock, WAKE_LOCK_SUSPEND, "battery suspend wakelock");  
	
	//bq24196_config_en(bq, 1);//ww_debug
	bq24296_config_gpio(bq, 1);

	bq24296_set_batlowv(bq, 1);//bat low = 3V
	bq24296_set_sys_min(bq, 0);//mini system voltage = 3.0V
	
	determine_initial_status(bq);

    bq24296_otg_regulator_init(bq);
	
	dev_info(bq->dev, "driver registered\n");
	return 0;

error_4:
	bq24296_sysfs_exit(bq);
#ifdef BQ24296_POWER_SUPPLY		
error_3:
	bq24296_power_supply_exit(bq);
#endif
error_2:
	kfree(name);
error_1:
	mutex_lock(&bq24296_id_mutex);
	idr_remove(&bq24296_id, num);
	mutex_unlock(&bq24296_id_mutex);

	return ret;
}

/* main bq24296 remove function */

static int bq24296_remove(struct i2c_client *client)
{
	struct bq24296_device *bq = i2c_get_clientdata(client);

	//if (bq->init_data.set_mode_hook)
	bq24296_sysfs_exit(bq);
#ifdef BQ24296_POWER_SUPPLY	
	bq24296_power_supply_exit(bq);
#endif
	bq24296_reset_chip(bq);

	mutex_lock(&bq24296_id_mutex);
	idr_remove(&bq24296_id, bq->id);
	mutex_unlock(&bq24296_id_mutex);

	dev_info(bq->dev, "driver unregistered\n");

	kfree(bq->name);

	return 0;
}

static struct of_device_id bq24296_match_table[] = {
	{ .compatible = "ti,bq24296-charger",},
	{ },
};

static const struct i2c_device_id bq24296_i2c_id_table[] = {
	{ "bq24296", BQ24296},
//	{ "bq24297", BQ24297},
	{},
};
MODULE_DEVICE_TABLE(i2c, bq24296_i2c_id_table);

static struct i2c_driver bq24296_driver = {
	.driver = {
		.name = "bq24296",
		.of_match_table	= bq24296_match_table,
	},
	.probe = bq24296_probe,
	.remove = bq24296_remove,
	.suspend	= bq24296_suspend,
	.resume		= bq24296_resume,	
	.id_table = bq24296_i2c_id_table,
};
module_i2c_driver(bq24296_driver);

MODULE_AUTHOR("weiweij <weiweij@lenovo.com>");
MODULE_DESCRIPTION("bq24296 charger driver");
MODULE_LICENSE("GPL");
