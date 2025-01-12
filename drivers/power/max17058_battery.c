/*
 *  max17058_battery.c
 *  fuel-gauge systems for lithium-ion (Li+) batteries
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
//#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/power/max17058_battery.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

#include <linux/qpnp/qpnp-adc.h>
#include <linux/time.h>

#include <linux/regulator/consumer.h>

#define max17058_VCELL_REG/*max17058_VCELL_MSB*/	0x02

#define max17058_SOC_REG/*max17058_SOC_MSB*/		0x04

#define max17058_MODE_REG/*max17058_MODE_MSB*/		0x06

#define max17058_VER_REG/*max17058_VER_MSB*/		0x08

#define max17058_RCOMP_REG/*max17058_RCOMP_MSB*/	0x0C

#define max17058_VRESET_REG                       0x18

#define max17058_STATUS_REG                      0x1A

#define max17058_CMD_REG/*max17058_CMD_MSB*/		0xFE

#define max17058_MODEL_ACCESS_REG			0x3E
#define max17058_MODEL_ACCESS_UNLOCK			0x4A57
#define max17058_MODEL_ACCESS_LOCK			0x0000

//#define max17058_POR_REG			0xfe

#define max17058_DELAY		10*HZ //1000->10*HZ
#define max17058_BATTERY_FULL	95

#define DEF_R_BAT		180

//below are from .ini file
//--------------------.ini file---------------------------------
static int INI_RCOMP = 127;
#define INI_RCOMP_FACTOR 1000
static int TempCoHot = -0.7*INI_RCOMP_FACTOR;
static int TempCoCold = -5.175*INI_RCOMP_FACTOR;

static int INI_SOCCHECKA = 228;
static int INI_SOCCHECKB = 230;
static int INI_OCVTEST = 57408; //57648
static int INI_BITS = 19;

const static u8 ini_model_data[64] = {
0xAA,
0x00,
0xB6,
0x40,
0xB7,
0xA0,
0xB9,
0xC0,
0xBB,
0xE0,
0xBC,
0xD0,
0xBD,
0x40,
0xBD,
0xA0,
0xC0,
0x30,
0xC0,
0xF0,
0xC4,
0x80,
0xC6,
0x00,
0xC8,
0x80,
0xCB,
0x00,
0xCF,
0xE0,
0xD6,
0x40,
0x01,
0xC0,
0x17,
0x60,
0x1D,
0xA0,
0x17,
0x80,
0x54,
0x80,
0x5F,
0x80,
0x7E,
0xA0,
0x14,
0xA0,
0x4A,
0x60,
0x0E,
0x00,
0x22,
0x40,
0x10,
0x00,
0x10,
0x00,
0x14,
0x00,
0x0C,
0x00,
0x0C,
0x00
};
//--------------------.ini file end-------------------------------

#define VERIFY_AND_FIX 1
#define LOAD_MODEL !(VERIFY_AND_FIX)

static void max17058_get_soc(struct i2c_client *client);
static int max17058_check_por(struct i2c_client *client);
void prepare_to_load_model(struct i2c_client *client);
void load_model(struct i2c_client *client);
bool verify_model_is_correct(struct i2c_client *client);
void cleanup_model_load(struct i2c_client *client);
//extern u8 get_temp(void);//get external temperature
static int max17058_write_reg(struct i2c_client *client, u8 reg, u16 value);
static int max17058_read_reg(struct i2c_client *client, u8 reg);
static u8 original_OCV_1=0, original_OCV_2=0;

struct max17058_chip {
	struct i2c_client		*client;
	struct delayed_work		work;
	struct delayed_work		hand_work;
	struct power_supply		fgbattery;
	struct power_supply		*bms_psy;	
	struct power_supply		*charger_psy;		
	struct max17058_platform_data	*pdata;
	/* State Of Connect */
	int online;
	/* battery voltage */
	int vcell;
	/* battery cal_soc*/
	int cal_soc;		
	/* battery cal_soc pre*/
	int cal_soc_pre;	
	/* battery capacity */
	int soc;
	/*soc for ui display*/
	int ui_soc;	
	/* State Of Charge */	
	int status;
	int current_now;
	struct regulator *vcc_i2c;
#ifdef CONFIG_MULTI_BATTERY
	int bat_id;
#endif	
};

static int max17058_write_reg(struct i2c_client *client, u8 reg, u16 value)
{
	int ret;

	ret = i2c_smbus_write_word_data(client, reg, swab16(value));

	if (ret < 0)
	{
		dump_stack();
		dev_err(&client->dev, "%s: err %d.reg=0x%x, val=0x%x\n", __func__, ret, reg, value);
	}

	return ret;
}

#ifdef CONFIG_MULTI_BATTERY
static int max17058_get_bat_id(struct i2c_client *client)
{
	union power_supply_propval val;
	struct power_supply* psy;
	int ret;
	
	psy = power_supply_get_by_name("bms");

	if(!psy)
	{
		pr_err("%s get bms psy error!\n", __func__);
		return -1;
	}

	psy->get_property(psy, POWER_SUPPLY_PROP_SERIAL_NUMBER, &val);
	if(val.strval==0)
		return -2;

	if(strcmp(val.strval, MAX17058_BAT_STR_ERR)==0)
		return -1;
		
	if(strcmp(val.strval, MAX17058_BAT_STR_2)==0)
		ret =  MAX17058_BAT_2;
	else
		ret = MAX17058_BAT_1;

	pr_err("%s, bat id = %s, ret %d\n", __func__, val.strval, ret);

	return ret;
}

static int max17058_set_module_data_as_bat_id(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
	
	if(chip->bat_id==MAX17058_BAT_1)
	{
		INI_RCOMP = chip->pdata->ini_rcomp;
		TempCoHot = chip->pdata->ini_temp_co_hot;
		TempCoCold = chip->pdata->ini_temp_co_cold;
		INI_SOCCHECKA = chip->pdata->ini_soccheck_a;
		INI_SOCCHECKB = chip->pdata->ini_soccheck_b;
		INI_OCVTEST = chip->pdata->ini_ocv_test;
		INI_BITS = chip->pdata->ini_bits;
	}else if(chip->bat_id==MAX17058_BAT_2)
	{
		INI_RCOMP = chip->pdata->ini_rcomp_2;
		TempCoHot = chip->pdata->ini_temp_co_hot_2;
		TempCoCold = chip->pdata->ini_temp_co_cold_2;
		INI_SOCCHECKA = chip->pdata->ini_soccheck_a_2;
		INI_SOCCHECKB = chip->pdata->ini_soccheck_b_2;
		INI_OCVTEST = chip->pdata->ini_ocv_test_2;
		INI_BITS = chip->pdata->ini_bits_2;	
	}else
	{
		INI_RCOMP = chip->pdata->ini_rcomp;
		TempCoHot = chip->pdata->ini_temp_co_hot;
		TempCoCold = chip->pdata->ini_temp_co_cold;
		INI_SOCCHECKA = chip->pdata->ini_soccheck_a;
		INI_SOCCHECKB = chip->pdata->ini_soccheck_b;
		INI_OCVTEST = chip->pdata->ini_ocv_test;
		INI_BITS = chip->pdata->ini_bits;		
	}

	pr_err("%s, bat params = %d, %d, %d, %d, %d, %d, %d\n", __func__, INI_RCOMP, TempCoHot, TempCoCold, INI_SOCCHECKA, INI_SOCCHECKB, INI_OCVTEST, INI_BITS);

	return 0;
}

static int max17058_proc_multi_battery(struct i2c_client *client)
{
	const int COUNT_TIME = 6;
	static int bat_id_check_cnt = COUNT_TIME;
	struct max17058_chip *chip = i2c_get_clientdata(client);	

	if(chip->bat_id!=-1)
		return 0;
	
	chip->bat_id = max17058_get_bat_id(client);
	if(chip->bat_id==-1)
	{
		bat_id_check_cnt--;
		if(bat_id_check_cnt>0)
		{
			pr_err("max17058 get bat id fail(%d)\n", bat_id_check_cnt);
			return -1;
		}else
		{
			bat_id_check_cnt = COUNT_TIME;
			chip->bat_id = MAX17058_BAT_DEFAULT;
			pr_err("max17058 get bat id fail timeout, use default id\n");
		}
	}

	max17058_set_module_data_as_bat_id(client);

	return 0;
}
#endif

static int max17058_read_reg(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_word_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}
//check POR
static int max17058_check_por(struct i2c_client *client)
{
	int val = 0;

	val = max17058_read_reg(client, max17058_STATUS_REG);
	//printk("ww_Debug, check por val = %d(0x%x)\n", val, val);
	if(val>=0)
		val = val&0x01;
  
	return val;
}
//get chip version
static u16 max17058_get_version(struct i2c_client *client)
{
    u16 fg_version = 0;
    u16 chip_version = 0;

    fg_version = max17058_read_reg(client, max17058_VER_REG);
    chip_version = ((u16)(fg_version & 0xFF)<<8) + ((u16)(fg_version & 0xFF00)>>8);
    chip_version = swab16(fg_version);
  
    dev_info(&client->dev, "max17058 Fuel-Gauge Ver 0x%04x\n", chip_version);
    return chip_version;
}


void prepare_to_load_model(struct i2c_client *client) {
    u16 msb;
    u16 check_times = 0;
    u8 unlock_test_OCV_1/*MSB*/, unlock_test_OCV_2/*LSB*/;
	  u16 chip_version;
	  
  
    /******************************************************************************
		Step 2.5.1 MAX17058/59 Only
		To ensure good RCOMP_Seg values in MAX17058/59, a POR signal has to be sent to
		MAX17058. The OCV must be saved before the POR signal is sent for best Fuel Gauge
		performance.for chip version 0x0011 only */
	chip_version = max17058_get_version(client);

    if(chip_version == 0x0011){
	    do {
					//max17058_write_reg(client, 0xFE, 0x5400);
					max17058_write_reg(client, max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_UNLOCK);
	        msleep(100);
	        msb = max17058_read_reg(client, 0x0E);
					
	        unlock_test_OCV_1 = (msb)&(0x00FF);//"big endian":low byte save MSB
	        unlock_test_OCV_2 = ((msb)&(0xFF00))>>8;
	
					if(check_times++ >= 3) {//avoid of while(1)
					    check_times = 0;
					    printk("max17058:time out3...");
					    break;
					}
	    }while ((unlock_test_OCV_1==0xFF)&&(unlock_test_OCV_2==0xFF));
  }
	printk("max17058:test ocv1=0x%x, ocv2=0x%x\n", unlock_test_OCV_1, unlock_test_OCV_2);
    //Step3: Write OCV
    //only for max17058/1/3/4, 
    //do nothing for MAX17058

    //Step4: Write RCOMP to its Maximum Value
    // only for max17058/1/3/4
    // max17058_write_reg(client,0x0C, 0xFF00);
    //do nothing for MAX17058
}


void load_model(struct i2c_client *client) {	

#if 1
	struct max17058_chip *chip = i2c_get_clientdata(client);
	u8* model_data = chip->pdata->ini_model_data;
#else
	u8 model_data[64] = {0xAA,0x00,0xB6,0xC0,0xB7,0xE0,0xB9,0x30,
			     0xBA,0xF0,0xBB,0x90,0xBC,0xA0,0xBD,0x40,
			     0xBE,0x20,0xBF,0x40,0xC0,0xD0,0xC3,0xF0,
			     0xC6,0xE0,0xCC,0x90,0xD1,0xA0,0xD7,0x30,
			     0x01,0xC0,0x0E,0xC0,0x2C,0x00,0x1C,0x00,
			     0x00,0xC0,0x46,0x00,0x51,0xE0,0x31,0xE0,
			     0x22,0x80,0x2B,0xE0,0x13,0xC0,0x15,0xA0,
			     0x10,0x20,0x10,0x20,0x0E,0x00,0x0E,0x00};//you need to get model data from maxim integrated
#endif

   /******************************************************************************
	Step 5. Write the Model
	Once the model is unlocked, the host software must write the 64 byte model
	to the device. The model is located between memory 0x40 and 0x7F.
	The model is available in the INI file provided with your performance
	report. See the end of this document for an explanation of the INI file.
	Note that the table registers are write-only and will always read
	0xFF. Step 9 will confirm the values were written correctly.
	*/
	int k=0;
	u16 value = 0;

#ifdef CONFIG_MULTI_BATTERY
	if(chip->bat_id==MAX17058_BAT_2)
		model_data =  chip->pdata->ini_model_data_2;
	else
		model_data = chip->pdata->ini_model_data;
#endif

	//Once the model is unlocked, the host software must write the 64 bytes model to the device
	for(k=0;k<0x40;k+=2)
	{
		value = (model_data[k]<<8)+model_data[k+1];
		//The model is located between memory 0x40 and 0x7F
		max17058_write_reg(client, 0x40+k, value);
	}
	
	//Write RCOMPSeg (for MAX17048/MAX17049 only)
	/*for(k=0;k<0x10;k++){
	    max17058_write_reg(client,0x80, 0x0080);
	}*/
}

bool verify_model_is_correct(struct i2c_client *client) {
    u8 SOC_1, SOC_2;
    u16 msb;
	
    msleep(400);//Delay at least 150ms(max17058/1/3/4 only)

    //Step 7. Write OCV:write(reg[0x0E], INI_OCVTest_High_Byte, INI_OCVTest_Low_Byte)
printk("!!!!max17058: INI_OCVTEST=0x%x\n", INI_OCVTEST);
    
    max17058_write_reg(client,0x0E, INI_OCVTEST);

    //Step 7.1 Disable Hibernate (MAX17048/49 only)
    //max17058_write_reg(client,0x0A,0x0);

    //Step 7.2. Lock Model Access (MAX17048/49/58/59 only)
    max17058_write_reg(client, max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_LOCK);

    //Step 8: Delay between 150ms and 600ms, delaying beyond 600ms could cause the verification to fail
    msleep(200);
 
    //Step 9. Read SOC register and compare to expected result
    msb = max17058_read_reg(client, max17058_SOC_REG);

    SOC_1 = (msb)&(0x00FF);//"big endian":low byte save MSB
    SOC_2 = ((msb)&(0xFF00))>>8;

printk("!!!!max17058: msb=0x%x, SOC_1=0x%x SOC_2=0x%x\n", msb, SOC_1, SOC_2);

    if(SOC_1 >= INI_SOCCHECKA && SOC_1 <= INI_SOCCHECKB) {
				pr_err("####max17058:model was loaded successfully####\n");
        return true;
    }
    else {		
				pr_err("!!!!max17058:model was NOT loaded successfully!!!! SOC_1=0x%x SOC_2=0x%x\n", SOC_1, SOC_2);
        return false; 
    }   
}

void cleanup_model_load(struct i2c_client *client) {	
    u16 original_ocv=0;
	int val;
	
    original_ocv = ((u16)((original_OCV_1)<<8)+(u16)original_OCV_2);
    //step9.1, Unlock Model Access (MAX17048/49/58/59 only): To write OCV, requires model access to be unlocked
    max17058_write_reg(client,max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_UNLOCK);

    //step 10 Restore CONFIG and OCV: write(reg[0x0C], INI_RCOMP, Your_Desired_Alert_Configuration)
    max17058_write_reg(client,0x0C, 0x7f1C);//RCOMP0=127 , battery empty Alert threshold = 4% -> 0x1C
    max17058_write_reg(client,0x0E, original_ocv); 

    //step 10.1 Restore Hibernate (MAX17048/49 only)
    //do nothing for MAX17058

    //step 11 Lock Model Access
    max17058_write_reg(client,max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_LOCK);
    //step 12,//delay at least 150ms before reading SOC register
    mdelay(200); 

	//clear por bit
	val = max17058_read_reg(client, max17058_STATUS_REG);
	val = val&0xfffe;
	max17058_write_reg(client, max17058_STATUS_REG, swab16(val));

	//check
	val = max17058_read_reg(client, max17058_STATUS_REG);
	pr_err("!!!!max17058!!!!val=0x%x\n", val);	
}

void max17058_unlock_model(struct i2c_client *client)
{
	  u16 msb;
    u16 check_times = 0;
    u8 unlock_test_OCV_1/*MSB*/, unlock_test_OCV_2/*LSB*/;

    do {
				
				max17058_write_reg(client, max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_UNLOCK);
        msleep(100);
        msb = max17058_read_reg(client, 0x0E);
				
        unlock_test_OCV_1 = (msb)&(0x00FF);//"big endian":low byte save MSB
        unlock_test_OCV_2 = ((msb)&(0xFF00))>>8;

				if(check_times++ >= 3) {//avoid of while(1)
				    check_times = 0;
				    printk("max17058:failded to unlock the model...");
				    break;
				}
    }while ((unlock_test_OCV_1==0xFF)&&(unlock_test_OCV_2==0xFF));

}

void max17058_get_init_ocv(struct i2c_client *client)
{
  	u16 check_times = 0;
    //u16 msb;  
	u16 OCV;  
	//u16 VCell1, VCell2, OCV;//, Desired_OCV;
  
	  msleep(200);
#if 0	
	  msb = max17058_read_reg(client, max17058_VCELL_REG);
    VCell1 = swab16(msb);
    msleep(500);
    msb = max17058_read_reg(client, max17058_VCELL_REG);
    VCell2 = swab16(msb);
#endif    
	  /*
	  The OCV Register will be modified during the process of loading the custom
    model.  Read and store this value so that it can be written back to the 
    device after the model has been loaded. do it for only the first time after power up or chip reset
	  */
	  do {
    //Step1:unlock model access, enable access to OCV and table registers
    		max17058_write_reg(client, max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_UNLOCK);
    //Step2:Read OCV, verify Model Access Unlocked
   
    	  msleep(100);  
        OCV = max17058_read_reg(client, 0x0E);//read OCV     
                 
        original_OCV_1 = (OCV)&(0x00FF);//"big endian":low byte save to MSB
        original_OCV_2 = ((OCV)&(0xFF00))>>8;

				if(check_times++ >= 3) {//avoid of while(1)
				    check_times = 0;
				    printk("max17058:failed to ulock the model...");
				    break;
				}
    }while ((original_OCV_1==0xFF)&&(original_OCV_2==0xFF));//verify Model Access Unlocked

	printk("max17058:init ocv1=0x%x, ocv2=0x%x\n", original_OCV_1, original_OCV_2);
	
 #if 0
 		if ((VCell1 > VCell2) && (VCell1 > OCV)) {
        Desired_OCV = VCell1;
    } else if ((VCell2 > VCell1) && (VCell2 > OCV)) {
        Desired_OCV = VCell2;
    } else {
        Desired_OCV = OCV;
    }
    max17058_write_reg(client,0x0E, Desired_OCV);
    max17058_write_reg(client,0x3E, 0x0);
    original_OCV_1 = (Desired_OCV)&(0x00FF);//"big endian":low byte save to MSB
    original_OCV_2 = ((Desired_OCV)&(0xFF00))>>8;
    printk("%s: Desired_OCV = %d, OCV = %d, VCell1 = %d, VCell2 = %d\n", __func__, Desired_OCV, OCV, VCell1, VCell2);
    msleep(125);
 #endif   
    
    
}


void handle_model(struct i2c_client *client, int load) {
	bool model_load_ok = false;
	int status;
	int check_times;
	static int load_or_verify = LOAD_MODEL;
	struct max17058_chip *chip = i2c_get_clientdata(client);
	
    //check POR firstly
    status = max17058_check_por(client);
    if(status==1)
    	pr_err("max17058 POR happens(0x%x),need to load model data\n", status);
    else
    {
    	//pr_err("max17058 POR does not happen,don't need to load model data\n");
    	return;
    }

#ifdef CONFIG_MULTI_BATTERY
	{
		const int COUNT_TIME = 6;
		static int bat_id_check_cnt = COUNT_TIME;
		struct max17058_chip *chip = i2c_get_clientdata(client);	
		
		chip->bat_id = max17058_get_bat_id(client);
		if(chip->bat_id==-1)
		{
			bat_id_check_cnt--;
			if(bat_id_check_cnt>0)
			{
				pr_err("max17058 get bat id fail(%d)\n", bat_id_check_cnt);
				return;
			}else
			{
				bat_id_check_cnt = COUNT_TIME;
				chip->bat_id = MAX17058_BAT_DEFAULT;
				pr_err("max17058 get bat id fail timeout, use default id\n");
			}
		}

		max17058_set_module_data_as_bat_id(client);
	}
#endif

    //read ocv and remeber it,later it will be written back   	
    max17058_get_init_ocv(client);

    	pr_err("max17058 load_or_verify=%d\n", load_or_verify);
    
	check_times = 0;
    do {
       
      if(load_or_verify == LOAD_MODEL) {		
      	// Steps 1-4		
	    	prepare_to_load_model(client);
		    // Step 5
		    load_model(client);
        }
        // Steps 6-9
        model_load_ok = verify_model_is_correct(client);
        if(!model_load_ok) {
            load_or_verify = LOAD_MODEL;
            max17058_unlock_model(client);
        }else
        	load_or_verify = !LOAD_MODEL;
		
	if(check_times++ >= 3) {
	    check_times = 0;
	    printk("max17058 handle model :time out1...");
	    break;
	}
    } while (!model_load_ok);

    // Steps 10-12
    cleanup_model_load(client);

	if(model_load_ok==true)
	{
		max17058_get_soc(client);
	    	printk("max17058 module re-loaded, sync ui_soc(%d) to soc(%d)\n", chip->ui_soc, chip->soc);		
		chip->ui_soc = chip->soc;
	}
}

static int max17058_get_temp(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
	union power_supply_propval val;
	
	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("battery");

	if(!chip->bms_psy)
	{
		pr_err("%s get battery error!\n", __func__);
		return 25;//defult 25C
	}

	chip->bms_psy->get_property(chip->bms_psy, POWER_SUPPLY_PROP_TEMP, &val);

	val.intval = val.intval/10;
	pr_err("%s, temp = %d\n", __func__, val.intval);
	
	return val.intval;
}

static int max17058_get_charger_state(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
	union power_supply_propval val;
	struct power_supply *phy = 0;

#ifdef CONFIG_BQ24296_CHARGER
	if (!chip->charger_psy)
		chip->charger_psy = power_supply_get_by_name("ex-charger");

	phy = chip->charger_psy;
#else
	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("battery");

	phy = chip->bms_psy;
#endif

	if(!phy)
	{
		pr_err("%s get battery power_supply error!\n", __func__);
		return POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;//defult 25C
	}
	
	phy->get_property(phy, POWER_SUPPLY_PROP_STATUS, &val);

	pr_err("%s, state = %d\n", __func__, val.intval);
	
	return val.intval;			
}

//update 
void update_rcomp(struct i2c_client *client) 
{
	int NewRCOMP;
	u16 cfg=0;
	u8 temp=0;

	temp = max17058_get_temp(client);//25;//get_temp();//get temperature input//ww_Debug for test, need to get temp from pmic!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	if(temp > 20) 
	{
		NewRCOMP = INI_RCOMP + ((temp - 20) * TempCoHot)/INI_RCOMP_FACTOR;
	}else if(temp <20) 
	{
		NewRCOMP = INI_RCOMP +  ((temp - 20) * TempCoCold)/INI_RCOMP_FACTOR;
	}else 
	{
		NewRCOMP = INI_RCOMP;
	}
	
	if(NewRCOMP > 0xFF)
	{
		NewRCOMP = 0xFF;
	}else if(NewRCOMP <0) 
		NewRCOMP = 0;
	
	cfg=(NewRCOMP<<8)|0x1c;//soc alert:4%   
	max17058_write_reg(client, 0x0c, cfg);
	msleep(150);
}

static int max17058_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max17058_chip *chip = container_of(psy,
				struct max17058_chip, fgbattery);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = chip->status;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = chip->vcell;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = chip->ui_soc;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = chip->current_now;
		break;		
	default:
		return -EINVAL;
	}
	return 0;
}

static void max17058_reset(struct i2c_client *client)
{
	max17058_write_reg(client, max17058_CMD_REG, 0x5400);//
}

static void max17058_get_vcell(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
	u16 fg_vcell = 0;
	u32 vcell_mV = 0;

	fg_vcell = max17058_read_reg(client, max17058_VCELL_REG);
	vcell_mV = (u32)(((fg_vcell & 0xFF)<<8) + ((fg_vcell & 0xFF00)>>8))*5/64;//78125uV/(1000*1000) = 5/64 mV/cell
	chip->vcell = vcell_mV;

	pr_err("max17058:chip->vcell = \t%d\t mV\n", chip->vcell);
}

static int  max17058_get_ocv(struct i2c_client *client)
{
	u16 ocv = 0;

	max17058_write_reg(client, max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_UNLOCK);

       ocv = max17058_read_reg(client, 0x0E);//read OCV     
	max17058_write_reg(client, max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_LOCK);       
	
  	ocv = (u32)(((ocv & 0xFF)<<8) + ((ocv & 0xFF00)>>8))*5/64;//78125uV/(1000*1000) = 5/64 mV/cell

	return ocv;
}

static void max17058_get_soc(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
	u16 fg_soc = 0;
	u8 soc_count = 0;
	static int first_flag = 0;
	
	fg_soc = max17058_read_reg(client, max17058_SOC_REG);

	chip->soc = ((u16)(fg_soc & 0xFF)<<8) + ((u16)(fg_soc & 0xFF00)>>8);

	if(INI_BITS == 19) {
	    chip->soc = chip->soc/512;
	}else if(INI_BITS == 18){
	    chip->soc = chip->soc/256;
	}

	if(chip->soc>100)	chip->soc = 100;
	if((soc_count++)%5 == 0){
	    soc_count = 0;
	    pr_err("max17058:Get SOC = %d\n", chip->soc);
	}

	if(first_flag==0)
	{
		chip->ui_soc = chip->soc;
		first_flag++;
	}
}


static void max17058_get_online(struct i2c_client *client)
{
    struct max17058_chip *chip = i2c_get_clientdata(client);

    if (chip->pdata->battery_online)
			chip->online = chip->pdata->battery_online();
    else
			chip->online = 1;
}

static void max17058_get_status(struct i2c_client *client)
{
    struct max17058_chip *chip = i2c_get_clientdata(client);

    if (!chip->pdata->charger_online || !chip->pdata->charger_enable) {
	    chip->status = POWER_SUPPLY_STATUS_UNKNOWN;
	    return;
    }

    if (chip->pdata->charger_online()) {
	if (chip->pdata->charger_enable())
	    chip->status = POWER_SUPPLY_STATUS_CHARGING;
	else
	    chip->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
    } else {
	chip->status = POWER_SUPPLY_STATUS_DISCHARGING;
    }

    if (chip->soc > max17058_BATTERY_FULL)
	    chip->status = POWER_SUPPLY_STATUS_FULL;
}

static int max17058_cal_cur(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
	static struct timespec last_cal_time;
	const int cal_timer = 1;
	static int cal_cnt = cal_timer ;
	static int cal_soc_bak = -1;
	struct timespec now_time;
	int delta_time = 0;
	int de_soc = 0;
	int tmp;
	int state;
	
	if(cal_cnt>0)
	{
		cal_cnt--;
		return -1;
	}

	cal_cnt = cal_timer;

	getrawmonotonic(&now_time);

	delta_time = now_time.tv_sec - last_cal_time.tv_sec;
	if(delta_time==0)
	{
		pr_err("%s delta_time is 0 continue \n", __func__);
		return -2;
	}
	
	tmp = max17058_read_reg(client, max17058_SOC_REG);

	chip->cal_soc = ((u16)(tmp & 0xFF)<<8) + ((u16)(tmp & 0xFF00)>>8);

	if(chip->cal_soc_pre==-1)
		chip->cal_soc_pre = chip->cal_soc;

	if(cal_soc_bak==-1)
		cal_soc_bak = chip->cal_soc_pre;
	
	de_soc = chip->cal_soc- chip->cal_soc_pre;

	if(de_soc==0)
	{
		if((cal_soc_bak!=-1)&&(chip->cal_soc>cal_soc_bak))
		{
			state = max17058_get_charger_state(chip->client);	
			
			if(state==POWER_SUPPLY_STATUS_CHARGING)
			{
				de_soc = chip->cal_soc - cal_soc_bak;
			}
		}

		if(chip->cal_soc<cal_soc_bak)
			cal_soc_bak = chip->cal_soc;
	}else
	{
		cal_soc_bak = chip->cal_soc_pre;
		chip->cal_soc_pre = chip->cal_soc;
	}

	if(INI_BITS == 19) {
	    chip->current_now = (100*3600*de_soc/512/100/delta_time)*30;// 2 is  a compensite value
	}else if(INI_BITS == 18){
	    chip->current_now = (100*3600*de_soc/256/100/delta_time)*30;
	}	

	pr_debug("%s %d, %d, %d, %d, %d, %d\n", __func__, delta_time,  de_soc, chip->cal_soc,  chip->cal_soc_pre, cal_soc_bak, chip->current_now);
	
	last_cal_time = now_time;

	return 0;	
}

int max17058_sync_soc_as_state(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
	int state;
	const int sync_timer = 1;
	static int sync_cnt = sync_timer;
	static int pre_soc = -1;
	
	state = max17058_get_charger_state(chip->client);	
	pr_err("%s state %d, cnt %d\n", __func__, state, sync_cnt);
	switch(state)
	{
		case POWER_SUPPLY_STATUS_FULL:
			if(chip->ui_soc<100)
				chip->ui_soc++;	

			if(chip->ui_soc>100)
				chip->ui_soc = 100;
			
			break;
		case POWER_SUPPLY_STATUS_CHARGING:
			if(chip->ui_soc==100)//re-charging
			{
				pr_err("%s re-charging state, keep ui_soc as 100. soc=%d\n", __func__, chip->soc);
				chip->ui_soc = 100;
			}
			
			if(chip->ui_soc < chip->soc)
			{
				if(sync_cnt>0)
				{
					sync_cnt--;
					break;
				}

				if(sync_cnt==0)
				{
					chip->ui_soc++;
					sync_cnt = sync_timer;
				}
			}else
				sync_cnt = sync_timer;
			
			break;
		case POWER_SUPPLY_STATUS_DISCHARGING:
		case POWER_SUPPLY_STATUS_NOT_CHARGING:
			if(chip->ui_soc > chip->soc)
			{
				if(sync_cnt>0)
				{
					sync_cnt--;
					break;
				}

				if(sync_cnt==0)
				{
					chip->ui_soc--;
					sync_cnt = sync_timer;
				}
			}else
				sync_cnt = sync_timer;
			
			break;
		default:
			pr_err("%s invalid state %d\n", __func__, state);
			return -1;
	}

	if(pre_soc != chip->ui_soc)
	{
		pre_soc = chip->ui_soc;
		power_supply_changed(&chip->fgbattery);
	}
	
	return 0;
}

//===============dummy function for not used static function=========================
void max17058_dummy(struct i2c_client *client)
{
	max17058_reset(client);
	max17058_get_ocv(client);
}
//=======================================end=========================================

static void max17058_work(struct work_struct *work)
{
    struct max17058_chip *chip;

    chip = container_of(work, struct max17058_chip, work.work);

#ifdef CONFIG_MULTI_BATTERY
	max17058_proc_multi_battery(chip->client);
#endif

    max17058_get_vcell(chip->client);
    max17058_get_soc(chip->client);	
	max17058_cal_cur(chip->client);
    update_rcomp(chip->client);//update rcomp periodically
    if(0)
    {
	    max17058_get_online(chip->client);
	    max17058_get_status(chip->client);
    }

	max17058_sync_soc_as_state(chip->client);

	pr_err("%s v=%d, soc=%d, ui_soc=%d, cur=%d\n", __func__, chip->vcell, chip->soc, chip->ui_soc, chip->current_now);

	schedule_delayed_work(&chip->work, max17058_DELAY);
}

static void max17058_handle_work(struct work_struct *work)
{
	struct max17058_chip *chip;
		
	chip = container_of(work, struct max17058_chip, hand_work.work);

	handle_model(chip->client, LOAD_MODEL);

	schedule_delayed_work(&chip->hand_work,HZ*10);
}

static int max17058_parse_dt(struct device *dev, struct max17058_platform_data *pdata)
{
	int r = 0;
	int len = 0;
	unsigned char* model_data;
	struct device_node *np = dev->of_node;

	pdata->irq_gpio = of_get_named_gpio(np, "max,irq-gpio", 0);
	if ((!gpio_is_valid(pdata->irq_gpio)))
	{
		dev_err(dev, "'get irq gpio %d failed\n", pdata->irq_gpio);
		//return -EINVAL;
	}
	
	r = of_property_read_u32(np, "max,r-bat", &pdata->r_bat);
	if (r < 0)
	{
		pr_err("%s get r bat of result : %d %d\n", __func__, pdata->r_bat, r);
		pdata->r_bat = DEF_R_BAT;
	}
	
	r = of_property_read_u32(np, "max,ini-rcomp", &pdata->ini_rcomp);
	if (r < 0)
	{
		pr_err("%s get rcomp of result : %d %d\n", __func__, pdata->ini_rcomp, r);
		pdata->ini_rcomp = INI_RCOMP;
	}else
		INI_RCOMP = pdata->ini_rcomp;

	r = of_property_read_u32(np, "max,ini-temp-co-hot", &pdata->ini_temp_co_hot);
	if (r < 0)
	{
		pr_err("%s get temp-co-hot of result : %d %d\n", __func__, pdata->ini_temp_co_hot, r);
		pdata->ini_temp_co_hot = TempCoHot;
	}else
	{
		pdata->ini_temp_co_hot = pdata->ini_temp_co_hot*(-1);
		TempCoHot = pdata->ini_temp_co_hot;
	}

	r = of_property_read_u32(np, "max,ini-temp-co-cold", &pdata->ini_temp_co_cold);
	if (r < 0)
	{
		pr_err("%s get temp-co-cold of result : %d %d\n", __func__, pdata->ini_temp_co_cold, r);
		pdata->ini_temp_co_cold = TempCoCold;
	}else
	{
		pdata->ini_temp_co_cold = pdata->ini_temp_co_cold*(-1);
		TempCoCold = pdata->ini_temp_co_cold;
	}

	r = of_property_read_u32(np, "max,ini-soc-checka", &pdata->ini_soccheck_a);
	if (r < 0)
	{
		pr_err("%s get soc-checka of result : %d %d\n", __func__, pdata->ini_soccheck_a, r);
		pdata->ini_soccheck_a = INI_SOCCHECKA;
	}else
		INI_SOCCHECKA = pdata->ini_soccheck_a;

	r = of_property_read_u32(np, "max,ini-soc-checkb", &pdata->ini_soccheck_b);
	if (r < 0)
	{
		pr_err("%s get soc-checkb of result : %d %d\n", __func__, pdata->ini_soccheck_b, r);
		pdata->ini_soccheck_b = INI_SOCCHECKB;
	}else
		INI_SOCCHECKB = pdata->ini_soccheck_b;

	r = of_property_read_u32(np, "max,ini-ocv-test", &pdata->ini_ocv_test);
	if (r < 0)
	{
		pr_err("%s get ocv-test of result : %d %d\n", __func__, pdata->ini_ocv_test, r);
		pdata->ini_ocv_test = INI_OCVTEST;
	}else
		INI_OCVTEST = pdata->ini_ocv_test;

	r = of_property_read_u32(np, "max,ini-bits", &pdata->ini_bits);
	if (r < 0)
	{
		pr_err("%s get bits of result : %d %d\n", __func__, pdata->ini_bits, r);
		pdata->ini_bits = INI_BITS;
	}else
		INI_BITS = pdata->ini_bits;

	len = 64;
	model_data = (unsigned char*) of_get_property(np, "max,model-data", &len);
	if (model_data ==NULL)
	{
		pr_err("%s get model of result : %d\n", __func__, r);
		memcpy(&pdata->ini_model_data, ini_model_data, 64);
	}else
		memcpy(&pdata->ini_model_data, model_data, 64);

		
	pr_err("%s get of result : %d %d %d %d %d  %d %d %d %d\n", __func__, pdata->irq_gpio,
		pdata->ini_rcomp, pdata->ini_temp_co_hot, pdata->ini_temp_co_cold, pdata->ini_soccheck_a, pdata->ini_soccheck_b, pdata->ini_ocv_test, pdata->ini_bits, pdata->r_bat);
	pr_err("model data : ");
	for(r=0;r<64;r++)
		pr_err("0x%x ", pdata->ini_model_data[r]);
	pr_err(" end \n");
	
#ifdef CONFIG_MULTI_BATTERY
	r = of_property_read_u32(np, "max,ini-rcomp-2", &pdata->ini_rcomp_2);
	if (r < 0)
	{
		pr_err("%s get rcomp 2 of result : %d %d\n", __func__, pdata->ini_rcomp_2, r);
		pdata->ini_rcomp_2 = INI_RCOMP;
	}

	r = of_property_read_u32(np, "max,ini-temp-co-hot-2", &pdata->ini_temp_co_hot_2);
	if (r < 0)
	{
		pr_err("%s get temp-co-hot 2 of result : %d %d\n", __func__, pdata->ini_temp_co_hot_2, r);
		pdata->ini_temp_co_hot_2 = TempCoHot;
	}else
	{
		pdata->ini_temp_co_hot_2 = pdata->ini_temp_co_hot_2*(-1);
	}

	r = of_property_read_u32(np, "max,ini-temp-co-cold-2", &pdata->ini_temp_co_cold_2);
	if (r < 0)
	{
		pr_err("%s get temp-co-cold 2 of result : %d %d\n", __func__, pdata->ini_temp_co_cold_2, r);
		pdata->ini_temp_co_cold_2 = TempCoCold;
	}else
	{
		pdata->ini_temp_co_cold_2 = pdata->ini_temp_co_cold_2*(-1);
	}

	r = of_property_read_u32(np, "max,ini-soc-checka-2", &pdata->ini_soccheck_a_2);
	if (r < 0)
	{
		pr_err("%s get soc-checka 2 of result : %d %d\n", __func__, pdata->ini_soccheck_a_2, r);
		pdata->ini_soccheck_a_2 = INI_SOCCHECKA;
	}

	r = of_property_read_u32(np, "max,ini-soc-checkb-2", &pdata->ini_soccheck_b_2);
	if (r < 0)
	{
		pr_err("%s get soc-checkb 2 of result : %d %d\n", __func__, pdata->ini_soccheck_b_2, r);
		pdata->ini_soccheck_b_2 = INI_SOCCHECKB;
	}

	r = of_property_read_u32(np, "max,ini-ocv-test-2", &pdata->ini_ocv_test_2);
	if (r < 0)
	{
		pr_err("%s get ocv-test 2 of result : %d %d\n", __func__, pdata->ini_ocv_test_2, r);
		pdata->ini_ocv_test_2 = INI_OCVTEST;
	}

	r = of_property_read_u32(np, "max,ini-bits-2", &pdata->ini_bits_2);
	if (r < 0)
	{
		pr_err("%s get bits 2 of result : %d %d\n", __func__, pdata->ini_bits_2, r);
		pdata->ini_bits_2 = INI_BITS;
	}

	len = 64;
	model_data = (unsigned char*) of_get_property(np, "max,model-data-2", &len);
	if (model_data ==NULL)
	{
		pr_err("%s get model 2 of result : %d\n", __func__, r);
		memcpy(&pdata->ini_model_data_2, ini_model_data, 64);
	}else
		memcpy(&pdata->ini_model_data_2, model_data, 64);

		
	pr_err("%s get 2 of result : %d %d %d %d  %d %d %d\n", __func__,
		pdata->ini_rcomp_2, pdata->ini_temp_co_hot_2, pdata->ini_temp_co_cold_2, pdata->ini_soccheck_a_2, pdata->ini_soccheck_b_2, pdata->ini_ocv_test_2, pdata->ini_bits_2);
	pr_err("model 2 data : ");
	for(r=0;r<64;r++)
		pr_err("0x%x ", pdata->ini_model_data_2[r]);
	pr_err(" end \n");
#endif

	return 0;
}


static enum power_supply_property max17058_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int max17058_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);			
	struct max17058_chip *chip;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (client->dev.of_node) {
		int r;
		
		client->dev.platform_data = devm_kzalloc(&client->dev,
			sizeof(struct max17058_platform_data), GFP_KERNEL);
		if (!client->dev.platform_data) {
  			dev_err(&client->dev, "max17058 probe: Failed to allocate memory\n");
			return -ENOMEM;
		}

		r = max17058_parse_dt(&client->dev, client->dev.platform_data);
		if (r)
			return r;
	} 
	
	chip->client = client;
	chip->pdata = client->dev.platform_data;

	i2c_set_clientdata(client, chip);

	chip->fgbattery.name		= "max17058_fgauge";
	chip->fgbattery.type		= POWER_SUPPLY_TYPE_BMS;
	chip->fgbattery.get_property	= max17058_get_property;
	chip->fgbattery.properties	= max17058_battery_props;
	chip->fgbattery.num_properties	= ARRAY_SIZE(max17058_battery_props);

	chip->cal_soc_pre = -1;

#ifdef CONFIG_MULTI_BATTERY
	chip->bat_id = -1;
#endif

	ret = power_supply_register(&client->dev, &chip->fgbattery);
	if (ret) {
		dev_err(&client->dev, "failed: power supply register\n");
		kfree(chip);
		return ret;
	}

	chip->bms_psy = power_supply_get_by_name("battery");
	if (!chip->bms_psy) {
		pr_err("max17058 battery not found deferring probe\n");
		//return -EPROBE_DEFER;
	}

#ifdef CONFIG_BQ24296_CHARGER
	chip->charger_psy = power_supply_get_by_name("ex-charger");
	if (!chip->charger_psy) {
		pr_err("max17058 charger not found deferring probe\n");
		//return -EPROBE_DEFER;
	}
#endif

	max17058_get_version(client);
  
	max17058_write_reg(client, 0x0c, 0x7f1C);
	handle_model(client,LOAD_MODEL);
    //start_soc(client);

	chip->vcc_i2c = regulator_get(&chip->client->dev, "vcc_i2c");
	if (IS_ERR(chip->vcc_i2c)) 
	{
		ret = PTR_ERR(chip->vcc_i2c);
		dev_info(&chip->client->dev, "Regulator get failed vcc_i2c ret=%d\n", ret);
	} else if (regulator_count_voltages(chip->vcc_i2c) > 0) 
	{
		ret = regulator_set_voltage(chip->vcc_i2c, 1800000, 1800000);
		if (ret) {
			dev_err(&chip->client->dev,
			"Regulator set_vtg failed vcc_i2c ret=%d\n", ret);
			//goto err_vcc_i2c_put;
		}
	}

	INIT_DEFERRABLE_WORK(&chip->work, max17058_work);
	INIT_DEFERRABLE_WORK(&chip->hand_work, max17058_handle_work);
	schedule_delayed_work(&chip->hand_work,0);
	schedule_delayed_work(&chip->work, 0);

	return 0;
}

static int max17058_remove(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);

	power_supply_unregister(&chip->fgbattery);
	cancel_delayed_work(&chip->work);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM

static int max17058_ldo_ops(struct max17058_chip *chip, bool enable)
{
	int ret = 0;

	printk("%s enable %d\n", __func__, enable);
	
	if (IS_ERR(chip->vcc_i2c)) 
	{
		chip->vcc_i2c = regulator_get(&chip->client->dev, "vcc_i2c");
		if (IS_ERR(chip->vcc_i2c)) 
		{
			ret = PTR_ERR(chip->vcc_i2c);
			dev_info(&chip->client->dev, "Regulator get failed vcc_i2c ret=%d\n", ret);
			return -1;
		} else if (regulator_count_voltages(chip->vcc_i2c) > 0) 
		{
			ret = regulator_set_voltage(chip->vcc_i2c, 1800000, 1800000);
			if (ret) {
				dev_err(&chip->client->dev, "Regulator set_vtg failed vcc_i2c ret=%d\n", ret);
				return -2;
			}
		}
	}	
	
	if (enable)
	{
		ret = regulator_enable(chip->vcc_i2c);
		if (ret) 
		{
			dev_err(&chip->client->dev, "Regulator vcc_i2c enable failed ret=%d\n", ret);
		}
	}else{
		ret = regulator_disable(chip->vcc_i2c);
		if (ret)
			dev_err(&chip->client->dev, "Regulator vcc_i2c disable failed ret=%d\n",ret);
	}
	
	return ret;
}

static int max17058_suspend(struct i2c_client *client,
		pm_message_t state)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);

	cancel_delayed_work(&chip->work);
	//flush_workqueue(&chip->work.work);

	max17058_ldo_ops(chip, 0);
	return 0;
}

static int max17058_resume(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);

	max17058_ldo_ops(chip, 1);

	schedule_delayed_work(&chip->work, 0);
	return 0;
}

#else

#define max17058_suspend NULL
#define max17058_resume NULL

#endif /* CONFIG_PM */

static struct of_device_id max17058_match_table[] = {
	{ .compatible = "max,max17058-fg"},
	{ },
};

static const struct i2c_device_id max17058_id[] = {
	{ "max17058", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max17058_id);

static struct i2c_driver max17058_i2c_driver = {
	.driver	= {
		.name	= "max17058",
		.of_match_table	= max17058_match_table,
	},
	.probe		= max17058_probe,
	.remove		= max17058_remove,
	.suspend	= max17058_suspend,
	.resume		= max17058_resume,
	.id_table	= max17058_id,
};
module_i2c_driver(max17058_i2c_driver);

MODULE_AUTHOR("maxim integrated");
MODULE_DESCRIPTION("max17058 Fuel Gauge");
MODULE_LICENSE("GPL");
