/* Copyright (c) 2013-2014 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <asm/param.h>    //#include <linux/param.h>  
#include <linux/power/fan54015.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/wakelock.h>


#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>                              
#include <linux/regulator/machine.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/alarmtimer.h>





/******************************************************************************
* Register addresses    
******************************************************************************/
#define FAN54015_REG_CONTROL0                 0
#define FAN54015_REG_CONTROL1                 1
#define FAN54015_REG_OREG                 2
#define FAN54015_REG_IC_INFO                   3
#define FAN54015_REG_IBAT                 4
#define FAN54015_REG_SP_CHARGER                5
#define FAN54015_REG_SAFETY                    6
#define FAN54015_REG_MONITOR                  16

/******************************************************************************
* Register bits 
******************************************************************************/
/* FAN54015_REG_CONTROL0 (0x00) */
#define FAN54015_FAULT                   (0x07)
#define FAN54015_FAULT_SHIFT                  0 
#define FAN54015_BOOST              (0x01 << 3) 
#define FAN54015_BOOST_SHIFT                  3
#define FAN54015_STAT               (0x3 <<  4) 
#define FAN54015_STAT_SHIFT                   4
#define FAN54015_EN_STAT            (0x01 << 6)
#define FAN54015_EN_STAT_SHIFT                6
#define FAN54015_TMR_RST_OTG        (0x01 << 7)  // writing a 1 resets the t32s timer, writing a 0 has no effect
#define FAN54015_TMR_RST_OTG_SHIFT            7

/* FAN54015_REG_CONTROL1 (0x01) */
#define FAN54015_OPA_MODE                (0x01)
#define FAN54015_OPA_MODE_SHIFT               0
#define FAN54015_HZ_MODE            (0x01 << 1)
#define FAN54015_HZ_MODE_SHIFT                1  
#define FAN54015_CE_N               (0x01 << 2)
#define FAN54015_CE_N_SHIFT                   2 
#define FAN54015_TE                 (0x01 << 3)
#define FAN54015_TE_SHIFT                     3
#define FAN54015_VLOWV              (0x03 << 4)
#define FAN54015_VLOWV_SHIFT                  4
#define FAN54015_IINLIM             (0x03 << 6)
#define FAN54015_IINLIM_SHIFT                 6

/* FAN54015_REG_OREG (0x02) */
#define FAN54015_OTG_EN                  (0x01)
#define FAN54015_OTG_EN_SHIFT                 0
#define FAN54015_OTG_PL             (0x01 << 1)
#define FAN54015_OTG_PL_SHIFT                 1
#define FAN54015_OREG               (0x3f << 2)
#define FAN54015_OREG_SHIFT                   2

/* FAN54015_REG_IC_INFO (0x03) */
#define FAN54015_REV                     (0x03)
#define FAN54015_REV_SHIFT                    0
#define FAN54015_PN                 (0x07 << 2)
#define FAN54015_PN_SHIFT                     2
#define FAN54015_VENDOR_CODE        (0x07 << 5)
#define FAN54015_VENDOR_CODE_SHIFT            5

/* FAN54015_REG_IBAT (0x04) */
#define FAN54015_ITERM                   (0x07)
#define FAN54015_ITERM_SHIFT                  0
#define FAN54015_IOCHARGE           (0x07 << 4)
#define FAN54015_IOCHARGE_SHIFT               4
#define FAN54015_RESET              (0x01 << 7)
#define FAN54015_RESET_SHIFT                  7

/* FAN54015_REG_SP_CHARGER (0x05) */
#define FAN54015_VSP                     (0x07)
#define FAN54015_VSP_SHIFT                    0
#define FAN54015_EN_LEVEL           (0x01 << 3)
#define FAN54015_EN_LEVEL_SHIFT               3
#define FAN54015_SP                 (0x01 << 4)
#define FAN54015_SP_SHIFT                     4
#define FAN54015_IO_LEVEL           (0x01 << 5)
#define FAN54015_IO_LEVEL_SHIFT               5
#define FAN54015_DIS_VREG           (0x01 << 6)
#define FAN54015_DIS_VREG_SHIFT               6

/* FAN54015_REG_SAFETY (0x06) */
#define FAN54015_VSAFE                   (0x0f)
#define FAN54015_VSAFE_SHIFT                  0
#define FAN54015_ISAFE              (0x07 << 4)
#define FAN54015_ISAFE_SHIFT                  4

/* FAN54015_REG_MONITOR (0x10) */
#define FAN54015_CV                      (0x01)
#define FAN54015_CV_SHIFT                     0
#define FAN54015_VBUS_VALID         (0x01 << 1)
#define FAN54015_VBUS_VALID_SHIFT             1
#define FAN54015_IBUS               (0x01 << 2)
#define FAN54015_IBUS_SHIFT                   2
#define FAN54015_ICHG               (0x01 << 3)
#define FAN54015_ICHG_SHIFT                   3
#define FAN54015_T_120              (0x01 << 4)
#define FAN54015_T_120_SHIFT                  4
#define FAN54015_LINCHG             (0x01 << 5)
#define FAN54015_LINCHG_SHIFT                 5
#define FAN54015_VBAT_CMP           (0x01 << 6)
#define FAN54015_VBAT_CMP_SHIFT               6
#define FAN54015_ITERM_CMP          (0x01 << 7)
#define FAN54015_ITERM_CMP_SHIFT              7

/******************************************************************************
* bit definitions
******************************************************************************/
/********** FAN54015_REG_CONTROL0 (0x00) **********/
// EN_STAT [6]
#define ENSTAT 1
#define DISSTAT 0
// TMR_RST [7]
#define RESET32S 1

/********** FAN54015_REG_CONTROL1 (0x01) **********/
// OPA_MODE [0]
#define CHARGEMODE 0
#define BOOSTMODE 1
//HZ_MODE [1]
#define NOTHIGHIMP 0
#define HIGHIMP 1
// CE/ [2]
#define ENCHARGER 0
#define DISCHARGER 1
// TE [3]
#define DISTE 0
#define ENTE 1
// VLOWV [5:4]
#define VLOWV3P4 0
#define VLOWV3P5 1
#define VLOWV3P6 2
#define VLOWV3P7 3
// IINLIM [7:6]
#define IINLIM100 0
#define IINLIM500 1
#define IINLIM800 2
#define NOLIMIT 3

/********** FAN54015_REG_OREG (0x02) **********/
// OTG_EN [0]
#define DISOTG 0
#define ENOTG 1
// OTG_PL [1]
#define OTGACTIVELOW 0
#define OTGACTIVEHIGH 1
// OREG [7:2]
#define VOREG4P1 30
#define VOREG4P2 35  // refer to table 3
#define VOREG4P34 42
#define VOREG4P36 43
#define VOREG4P4 45
#define VOREG4P42 46



/********** FAN54015_REG_IC_INFO (0x03) **********/

/********** FAN54015_REG_IBAT (0x04) **********/
// ITERM [2:0] - 68mOhm
#define ITERM49 0
#define ITERM97 1
#define ITERM146 2
#define ITERM194 3
#define ITERM243 4
#define ITERM291 5
#define ITERM340 6
#define ITERM388 7
// IOCHARGE [6:4] - 68mOhm
#define IOCHARGE550 0
#define IOCHARGE650 1
#define IOCHARGE750 2
#define IOCHARGE850 3
#define IOCHARGE1050 4
#define IOCHARGE1150 5
#define IOCHARGE1350 6
#define IOCHARGE1450 7

/********** FAN54015_REG_SP_CHARGER (0x05) **********/
// VSP [2:0] 
#define VSP4P213 0
#define VSP4P293 1
#define VSP4P373 2
#define VSP4P453 3
#define VSP4P533 4
#define VSP4P613 5
#define VSP4P693 6
#define VSP4P773 7
// IO_LEVEL [5]
#define ENIOLEVEL 0
#define DISIOLEVEL 1
// DIS_VREG [6]
#define VREGON 0
#define VREGOFF 1

/********** FAN54015_REG_SAFETY (0x06) **********/
// VSAFE [3:0]
#define VSAFE4P20 0
#define VSAFE4P22 1
#define VSAFE4P24 2
#define VSAFE4P26 3
#define VSAFE4P28 4
#define VSAFE4P30 5
#define VSAFE4P32 6
#define VSAFE4P34 7
#define VSAFE4P36 8
#define VSAFE4P38 9
#define VSAFE4P40 10
#define VSAFE4P42 11    
#define VSAFE4P44 12
// ISAFE [6:4] - 68mOhm
#define ISAFE550 0
#define ISAFE650 1
#define ISAFE750 2
#define ISAFE850 3
#define ISAFE1050 4
#define ISAFE1150 5
#define ISAFE1350 6
#define ISAFE1450 7

/* reset the T32s timer every 10 seconds   */
#define T32S_RESET_INTERVAL      (10LL * NSEC_PER_SEC)   
//#define T32S_RESET_INTERVAL      (20*HZ)  


#define FAN54015_DEBUG_FS


static const BYTE fan54015_def_reg[17] = {
    0x40,    // #0x00(CONTROL0)
    0x30,    // #0x01(CONTROL1)
    0x0a,    // #0x02(OREG)
    0x84,    // #0x03(IC_INFO)
    0x09,    // #0x04(IBAT) default is 0x89 but writing 1 to IBAT[7] resets charge parameters, except the safety reg, so 
    0x24,    // #0x05(SP_CHARGER)
    0x40,    // #0x06(SAFETY)
    0x00,    // #0x07 - unused
    0x00,    // #0x08 - unused
    0x00,    // #0x09 - unused
    0x00,    // #0x0a - unused
    0x00,    // #0x0b - unused
    0x00,    // #0x0c - unused
    0x00,    // #0x0d - unused
    0x00,    // #0x0e - unused
    0x00,    // #0x0f - unused    
    0x00,    // #0x10(MONITOR)
};


struct fan54015_otg_regulator {
	struct regulator_desc	rdesc;
	struct regulator_dev	*rdev;
}fan54015_otg_regulator;

struct work_struct chg_update_work;
struct delayed_work chg_plugin_work;
struct work_struct chg_fast_work;

static BYTE fan54015_curr_reg[17];
static struct i2c_client *this_client;
static int reset_flag = 0;      // use when assert reset
bool IsUsbPlugIn=false, IsTAPlugIn=false,IsChargingOn=false,TrunOnChg=false,ResetFan54015=false,ChgrCFGchanged=false,OTGturnOn=false,VbusValid=false,InSOCrecharge=false,BattFull=false,RemoveBTC=false;     //IsColdToNormal,IsHotToNormal   
bool LowChgCurrent=false;
bool FakeBatteryReport=false;
struct wake_lock Fan54015WatchDogKicker,Fan54015OTGLocker;
uint   BattSOC=0,BattVol=0;
int BattTemp=0;
struct alarm  FanWDTkicker;
struct power_supply  * pFan_batt_psy;
extern int IsBattPresent(void);
extern  void SetLBCchgrCTRLreg(void);
extern  void GetLBCchgrCTRLreg(void);

static int fan54015_write_reg(int reg, int val)      
{
    int ret;
    ret = i2c_smbus_write_byte_data(this_client, reg, val);
    if (ret < 0)
        printk(KERN_WARNING "%s: error = %d \n", __func__, ret);

    if((reset_flag == 1) 
        || ((reg == FAN54015_REG_IBAT) && (val & FAN54015_RESET)))  
    {
        memcpy(fan54015_curr_reg,fan54015_def_reg,6);  // resets charge paremeters, except the safety register(#6)
        reset_flag = 0;
    }
    else
    {
        fan54015_curr_reg[reg] = val;
    }
    return ret;
}

static int fan54015_read_reg(int reg)
{
    int ret;
    ret = i2c_smbus_read_byte_data(this_client, reg);
    if (ret < 0)
        printk(KERN_WARNING  "%s: error = %d \n", __func__, ret);         
    return ret;
}

static void fan54015_set_value(BYTE reg, BYTE reg_bit,BYTE reg_shift, BYTE val)
{
    BYTE tmp;
    tmp = fan54015_curr_reg[reg] & (~reg_bit);
    tmp |= (val << reg_shift);
    if(reg_bit == FAN54015_RESET&&reg ==FAN54015_REG_IBAT)
    {
        reset_flag = 1;
    }
    fan54015_write_reg(reg,tmp);
}

static BYTE fan54015_get_value(BYTE reg, BYTE reg_bit, BYTE reg_shift)
{
    BYTE tmp,ret;
    tmp = (BYTE)fan54015_read_reg(reg);
    ret = (tmp & reg_bit) >> reg_shift;
    return ret;    
}

/******************************************************************************
* Function:     
* Parameters: None
* Return: None
*
* Description:  if VBUS present(charging & boost),write 1 every 10 seconds
*
******************************************************************************/
void fan54015_USB_startcharging(void);
void fan54015_TA_startcharging(void);
void fan54015_stopcharging(void);
fan54015_monitor_status fan54015_monitor(void);


int fan_54015_batt_current=0,fan_54015_batt_ocv=0;
int Fan54015Voreg=0,Fan54015Iochg=0,Fan54015OTGPin=0;
struct pinctrl  *fan_pinctrl;
struct pinctrl_state *fan_otgPin_high,*fan_otgPin_low;


static void fan54015_Reset_Chip(void)
{

   return;
   #ifdef FAN54015_DEBUG_FS
          printk(KERN_WARNING  "~FAN54015 Reset Chip   \n");//FAN54015_RESET         FAN54015_REG_IBAT  
  #endif
    fan54015_set_value(FAN54015_REG_IBAT, FAN54015_REG_IBAT,FAN54015_RESET_SHIFT, 1);  

   
}


#if 1
static enum alarmtimer_restart fan54015_alarm_work_func(struct alarm *alarm, ktime_t now) 
{
//pm_stay_awake(&this_client->dev);
schedule_work(&chg_update_work);
return ALARMTIMER_NORESTART;

}
#endif

static void fan54015_update_work_func(struct work_struct *work)  
{   
#ifdef FAN54015_DEBUG_FS
    const int regaddrs[] = {0x00, 0x01, 0x02, 0x03, 0x4, 0x05, 0x06, 0x10 };
    BYTE fan54015_regs[8];
    uint i;
 #endif   
   ktime_t kt;

  wake_lock(&Fan54015WatchDogKicker); 
 //pm_stay_awake(&this_client->dev);

  ResetFan54015 = false; 
   // msleep(500);  //delay   500ms        

   SetLBCchgrCTRLreg();
   GetLBCchgrCTRLreg();
 
   if(ResetFan54015)
   	{  ResetFan54015 = false;
   	   fan54015_Reset_Chip();       

   	}    
    
   //1. kick watchDog every 10s .
   #ifdef FAN54015_DEBUG_FS
    printk(KERN_WARNING  "~fan54015KickWDT, IsUsbPlugIn=%d, IsTAPlugIn=%d,IsChargingOn=%d  batt_current=%d batt_ocv=%d TrunOnChg=%d BattSOC=%d,BattTemp=%d,BattVol=%d,Fan54015Voreg=%d,Fan54015Iochg=%d OTGturnOn=%d InSOCrecharge=%d BattFull=%d FakeBatteryReport=%d \n",
                                             IsUsbPlugIn,IsTAPlugIn,IsChargingOn,fan_54015_batt_current,fan_54015_batt_ocv,TrunOnChg,BattSOC,BattTemp,BattVol,Fan54015Voreg,Fan54015Iochg,OTGturnOn,InSOCrecharge,BattFull,FakeBatteryReport);        
    #endif   

     if(OTGturnOn)
    	{
    		fan54015_set_value(FAN54015_REG_CONTROL0, FAN54015_TMR_RST_OTG,FAN54015_TMR_RST_OTG_SHIFT, RESET32S);  
            printk(KERN_WARNING  "~Start Alarm1\n");     
            kt = ns_to_ktime(T32S_RESET_INTERVAL); 
            alarm_start_relative(&FanWDTkicker, kt);              
            wake_unlock(&Fan54015WatchDogKicker);	
	    	return ;		
    	}
	
    fan54015_set_value(FAN54015_REG_CONTROL0, FAN54015_TMR_RST_OTG,FAN54015_TMR_RST_OTG_SHIFT, RESET32S);  

	
#if 0	   // do not turnoff system when Batt not exist

    BattPresent = IsBattPresent();
    
    #ifdef FAN54015_DEBUG_FS
       printk(KERN_WARNING  "~FAN54015 BattPresent=%d   \n",BattPresent);
   #endif

   if(!BattPresent)    
   	pm_power_off();
#endif   
      
    // 2. TurnOn/Off charger according to charger type
    if(IsUsbPlugIn==true&&IsChargingOn==false&&TrunOnChg==true)     
    	{ IsChargingOn=true;
	            
	   fan54015_USB_startcharging();
	   #ifdef FAN54015_DEBUG_FS
          printk(KERN_WARNING  "~FAN54015 USB charger ON   \n");//add by maxwill	
          #endif
    	}
    else if(IsTAPlugIn==true&&IsChargingOn==false&&TrunOnChg==true)	
		{   IsChargingOn=true;
	             
		     fan54015_TA_startcharging();
		  #ifdef FAN54015_DEBUG_FS	 
                     printk(KERN_WARNING  "~FAN54015 TA charger ON   \n");//add by maxwill
                 #endif    
    	        }
             else if((IsUsbPlugIn==false)&&(IsTAPlugIn==false)&&(IsChargingOn==true))
			{  IsChargingOn=false;
			    
			    fan54015_stopcharging();
			 
			}
		     else if(TrunOnChg==false&&(IsChargingOn==true))            
		     	          {  IsChargingOn=false;
			             
			             fan54015_stopcharging();       
			           
			        }
			    
    if(ChgrCFGchanged&&IsChargingOn==true)
    	{    ChgrCFGchanged = false;
              if(IsUsbPlugIn==true)
                  fan54015_USB_startcharging();
	     else if(IsTAPlugIn==true)
		 	 fan54015_TA_startcharging();

    	}
	if(((BattTemp>500)||(BattTemp<0))&&(!RemoveBTC))
	  {	
	      #ifdef FAN54015_DEBUG_FS	
                   printk(KERN_WARNING  "~BattTemp Not Ok,TurnOff CHGR   \n");
             #endif   
	     fan54015_stopcharging(); 
	  }

   #ifdef FAN54015_DEBUG_FS       
    for ( i = 0; i<8; i++) {
        fan54015_regs[i] = fan54015_read_reg(regaddrs[i]);
    }
     printk(KERN_WARNING  " [0]=0x%x  [1]=0x%x  [2]=0x%x  [3]=0x%x  [4]=0x%x  [5]=0x%x  [6]=0x%x  [16]=0x%x\n ",fan54015_regs[0],fan54015_regs[1],fan54015_regs[2],fan54015_regs[3],fan54015_regs[4],fan54015_regs[5],
	 	                                            fan54015_regs[6],fan54015_regs[7]); //regaddrs[i] ,   fan54015_regs[i]

 #endif

   

   if((IsChargingOn==true)||(true ==InSOCrecharge ))
       {
          printk(KERN_WARNING  "~Start Alarm   \n");     
         kt = ns_to_ktime(T32S_RESET_INTERVAL); 
         alarm_start_relative(&FanWDTkicker, kt);                  
      }
 
    #ifdef FAN54015_DEBUG_FS	
            printk(KERN_WARNING  "~Release WakeLock Fan54015WatchDogKicker  \n");//add by maxwill	 
    #endif    
  wake_unlock(&Fan54015WatchDogKicker);		
 //  pm_relax(&this_client->dev);   
  
}


static int fan_pinctrl_init(void)
{
	struct i2c_client *client = this_client;

	fan_pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR_OR_NULL(fan_pinctrl)) {
		printk(KERN_WARNING  "Failed to get pinctrl\n");
		return PTR_ERR(fan_pinctrl);
	}

	fan_otgPin_high = pinctrl_lookup_state(fan_pinctrl, "otg_pin_high");
	if (IS_ERR_OR_NULL(fan_otgPin_high)) {
		printk(KERN_WARNING  "Failed to look up otg_pin_high state\n");
		return PTR_ERR(fan_otgPin_high);
	}

	fan_otgPin_low = pinctrl_lookup_state(fan_pinctrl, "otg_pin_low");
	if (IS_ERR_OR_NULL(fan_otgPin_low)) {
		printk(KERN_WARNING  "Failed to look up otg_pin_low state\n");
		return PTR_ERR(fan_otgPin_low);
	}

	return 0;
}


/******************************************************************************
* Function: FAN54015_Initialization     
* Parameters: None
* Return: None
*
* Description:  
*
******************************************************************************/
static void fan54015_init(void)
{

   #ifdef FAN54015_DEBUG_FS
      printk(KERN_WARNING  "~FAN54015 init now   \n");//add by maxwill	
   #endif   
        memcpy(fan54015_curr_reg,fan54015_def_reg,sizeof(fan54015_curr_reg));
      INIT_WORK(&chg_update_work, fan54015_update_work_func);
      INIT_DELAYED_WORK(&chg_plugin_work, fan54015_update_work_func);
	INIT_WORK(&chg_fast_work, fan54015_update_work_func);
		
#ifdef FAN54015_DEBUG_FS
    printk(KERN_WARNING  "~Read  IC_INFO:%d  \n",fan54015_read_reg(0x03));//add by maxwill      
#endif	
//reg 6
    fan54015_set_value(FAN54015_REG_SAFETY, FAN54015_VSAFE,FAN54015_VSAFE_SHIFT, VSAFE4P36);  // VSAFE = 4.36V     
    fan54015_set_value(FAN54015_REG_SAFETY, FAN54015_ISAFE, FAN54015_ISAFE_SHIFT, ISAFE1450);  // ISAFE = 1450mA (68mOhm)
//reg 1
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_VLOWV, FAN54015_VLOWV_SHIFT,VLOWV3P4);  // VLOWV = 3.4V
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_IINLIM, FAN54015_IINLIM_SHIFT,IINLIM500);  // INLIM = 500mA
//reg 2 
    fan54015_set_value(FAN54015_REG_OREG, FAN54015_OREG,FAN54015_OREG_SHIFT, VOREG4P34);  //OREG = 4.34V
//reg 5
    fan54015_set_value(FAN54015_REG_SP_CHARGER, FAN54015_IO_LEVEL,FAN54015_IO_LEVEL_SHIFT, ENIOLEVEL);  //IO_LEVEL is 0. Output current is controlled by IOCHARGE bits.

   fan54015_monitor();

  // schedule_delayed_work(&chg_update_work, T32S_RESET_INTERVAL);

}

/******************************************************************************
* Function: fan54015_TA_startcharging   
* Parameters: None
* Return: None
*
* Description:  
*
******************************************************************************/
void fan54015_TA_startcharging(void)
{  
     // wake_lock(&Fan54015WatchDogKicker); 
	 
    //  1. set charging Voreg
    if(Fan54015Voreg==4350)
          {
              #ifdef FAN54015_DEBUG_FS	
                printk(KERN_WARNING  "~set Voreg to 4350mv  \n");	
              #endif    

	      fan54015_set_value(FAN54015_REG_OREG, FAN54015_OREG,FAN54015_OREG_SHIFT, VOREG4P36);  //OREG = 4.34V

    	  }
    else  if(Fan54015Voreg==4330)     
    	       {
                   #ifdef FAN54015_DEBUG_FS	
                     printk(KERN_WARNING  "~set Voreg to 4330mv  \n");	
                   #endif    
                 fan54015_set_value(FAN54015_REG_OREG, FAN54015_OREG,FAN54015_OREG_SHIFT, VOREG4P34);  //OREG = 4.34V

    	       }
	     else 
	     	{
                   #ifdef FAN54015_DEBUG_FS	
                     printk(KERN_WARNING  "~set Voreg(%d  mV) Value Error!  \n",Fan54015Voreg);	
                   #endif    
                 fan54015_set_value(FAN54015_REG_OREG, FAN54015_OREG,FAN54015_OREG_SHIFT, VOREG4P2);  //OREG = 4.2V
	     	}
		 
     //  2. set charging current
      if(Fan54015Iochg==1150)
          {
              #ifdef FAN54015_DEBUG_FS	
                printk(KERN_WARNING  "~set Iochg to 1150mA\n");	     
              #endif    
               if(LowChgCurrent)
               	{
                   #ifdef FAN54015_DEBUG_FS	
                              printk(KERN_WARNING  "~FallBack to 850mA\n");	    
                   #endif 
		   fan54015_set_value(FAN54015_REG_IBAT, FAN54015_IOCHARGE,FAN54015_IOCHARGE_SHIFT, IOCHARGE850);  //850mA		   
               	}
	      else{		   
	                 fan54015_set_value(FAN54015_REG_IBAT, FAN54015_IOCHARGE,FAN54015_IOCHARGE_SHIFT, IOCHARGE1150);  //1150mA
	      	      }
    	  }
    else  if(Fan54015Iochg==850)
    	       {
                   #ifdef FAN54015_DEBUG_FS	
                     printk(KERN_WARNING  "~set Iochg to 850mA  \n");	
                   #endif    
                  fan54015_set_value(FAN54015_REG_IBAT, FAN54015_IOCHARGE,FAN54015_IOCHARGE_SHIFT, IOCHARGE850);  //550mA

    	       }	  
    else  if(Fan54015Iochg==460)
    	       {
                   #ifdef FAN54015_DEBUG_FS	
                     printk(KERN_WARNING  "~set Iochg to 460mA  \n");	
                   #endif    
                  fan54015_set_value(FAN54015_REG_IBAT, FAN54015_IOCHARGE,FAN54015_IOCHARGE_SHIFT, IOCHARGE650);  //650mA

    	       }
	     else 
	     	{
                   #ifdef FAN54015_DEBUG_FS	
                     printk(KERN_WARNING  "~set Iochg(%d  mA) Value Error!  \n",Fan54015Iochg);	
                   #endif    
                  fan54015_set_value(FAN54015_REG_IBAT, FAN54015_IOCHARGE,FAN54015_IOCHARGE_SHIFT, IOCHARGE550);  //550mA
	     	}
	
    fan54015_set_value(FAN54015_REG_IBAT, FAN54015_ITERM,FAN54015_ITERM_SHIFT, ITERM49);  //194mA
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_IINLIM, FAN54015_IINLIM_SHIFT,NOLIMIT);  // no limit
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_TE,FAN54015_TE_SHIFT, ENTE);
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_CE_N,FAN54015_CE_N_SHIFT, ENCHARGER);

    //3. Set IOCHG controlled by IOCHARGE bits.
    fan54015_set_value(FAN54015_REG_SP_CHARGER, FAN54015_IO_LEVEL,FAN54015_IO_LEVEL_SHIFT, ENIOLEVEL);  //IO_LEVEL is 0. Output current is controlled by IOCHARGE bits.
}
EXPORT_SYMBOL_GPL(fan54015_TA_startcharging);

/******************************************************************************
* Function: fan54015_USB_startcharging  
* Parameters: None
* Return: None
*
* Description:  
*
******************************************************************************/
void fan54015_USB_startcharging(void)
{  
      //wake_lock(&Fan54015WatchDogKicker); 

  //  1. set charging Voreg    
     if(Fan54015Voreg==4350)
          {
              #ifdef FAN54015_DEBUG_FS	
                printk(KERN_WARNING  "~set Voreg to 4350mv  \n");	
              #endif    

	      fan54015_set_value(FAN54015_REG_OREG, FAN54015_OREG,FAN54015_OREG_SHIFT, VOREG4P36);  //OREG = 4.34V

    	  }
    else  if(Fan54015Voreg==4150)
    	       {
                   #ifdef FAN54015_DEBUG_FS	
                     printk(KERN_WARNING  "~set Voreg to 4100mv  \n");	
                   #endif    
                 fan54015_set_value(FAN54015_REG_OREG, FAN54015_OREG,FAN54015_OREG_SHIFT, VOREG4P1);  //OREG = 4.1V

    	       }
	     else 
	     	{
                   #ifdef FAN54015_DEBUG_FS	
                     printk(KERN_WARNING  "~set Voreg(%d  mV) Value Error!  \n",Fan54015Voreg);	
                   #endif    
                 fan54015_set_value(FAN54015_REG_OREG, FAN54015_OREG,FAN54015_OREG_SHIFT, VOREG4P2);  //OREG = 4.2V
	     	}

	
   //  2. set charging current
    fan54015_set_value(FAN54015_REG_IBAT, FAN54015_IOCHARGE,FAN54015_IOCHARGE_SHIFT, IOCHARGE550);  //550mA
    
    fan54015_set_value(FAN54015_REG_IBAT, FAN54015_ITERM, FAN54015_ITERM_SHIFT,ITERM49);  //194mA
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_IINLIM,FAN54015_IINLIM_SHIFT, IINLIM500);  // limit 500mA (default)         
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_TE,FAN54015_TE_SHIFT, ENTE);         
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_CE_N,FAN54015_CE_N_SHIFT, ENCHARGER);

   //3. Set IOCHG controlled by IOCHARGE bits.
    fan54015_set_value(FAN54015_REG_SP_CHARGER, FAN54015_IO_LEVEL,FAN54015_IO_LEVEL_SHIFT, ENIOLEVEL);  //IO_LEVEL is 0. Output current is controlled by IOCHARGE bits.
}
EXPORT_SYMBOL_GPL(fan54015_USB_startcharging);

/******************************************************************************
* Function: fan54015_stopcharging   
* Parameters: None
* Return: None
*
* Description:  
*
******************************************************************************/
void fan54015_stopcharging(void)
{
//   if(!VbusValid||(((BattTemp>500)||(BattTemp<0))&&(!RemoveBTC))) 
      {    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_CE_N,FAN54015_CE_N_SHIFT, DISCHARGER);
            
          #ifdef FAN54015_DEBUG_FS	
              printk(KERN_WARNING  "~Charger OFF   \n");
         #endif   	       

		// wake_unlock(&Fan54015WatchDogKicker);	
     } 
/*  else {
   	    IsChargingOn = true;
            #ifdef FAN54015_DEBUG_FS	
	    printk(KERN_WARNING  "~Keep CHGR ON when Vbus Valid  \n");
           #endif 
          }   */
}
EXPORT_SYMBOL_GPL(fan54015_stopcharging);

/******************************************************************************
* Function: FAN54015_monitor    
* Parameters: None
* Return: status 
*
* Description:  enable the host procdessor to monitor the status of the IC.
*
******************************************************************************/
fan54015_monitor_status fan54015_monitor(void)
{
    fan54015_monitor_status status;
    status = (fan54015_monitor_status)fan54015_read_reg(FAN54015_REG_MONITOR);
#ifdef FAN54015_DEBUG_FS

    printk(KERN_WARNING  "~MONITOR reg:%d   \n",status);//add by maxwill	
#endif    
   
  return    status;
}
EXPORT_SYMBOL_GPL(fan54015_monitor);

int fan54015_getcharge_stat(void)
{
    int stat;
    stat = fan54015_get_value(FAN54015_REG_CONTROL0, FAN54015_STAT, FAN54015_STAT_SHIFT);
    return stat;
}
EXPORT_SYMBOL_GPL(fan54015_getcharge_stat);

int fan54015_otg_regulator_enable(struct regulator_dev * rdev)
{ int rc=0;

  wake_lock(&Fan54015OTGLocker);

  #ifdef FAN54015_DEBUG_FS
    printk(KERN_WARNING  "~OTG Enable   \n");
  #endif
    
   OTGturnOn = true;
   schedule_work(&chg_fast_work);// heming@wt, for OTG remove issue, start work to kickdog

#if  1
   rc = pinctrl_select_state(fan_pinctrl, fan_otgPin_high);
    if (rc) {
		printk(KERN_WARNING  "Can't select fan_otgPin_high state\n");
			
	      }
  
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_HZ_MODE, FAN54015_HZ_MODE_SHIFT, NOTHIGHIMP);
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_OPA_MODE, FAN54015_OPA_MODE_SHIFT, BOOSTMODE);
#endif
	
    wake_unlock(&Fan54015OTGLocker);
    return 0;
   // schedule_delayed_work(&chg_update_work, T32S_RESET_INTERVAL);
}
EXPORT_SYMBOL_GPL(fan54015_otg_regulator_enable);

int  fan54015_otg_regulator_disable(struct regulator_dev * rdev)                  
{   int rc=0;

    wake_lock(&Fan54015OTGLocker);
#ifdef FAN54015_DEBUG_FS
    printk(KERN_WARNING  "~OTG Disable   \n");
#endif
    
    OTGturnOn = false;
    
   rc = pinctrl_select_state(fan_pinctrl, fan_otgPin_low);
    if (rc) {
		printk(KERN_WARNING  "Can't select fan_otgPin_low state\n");
			
	      }
	 
    fan54015_set_value(FAN54015_REG_CONTROL1, FAN54015_OPA_MODE, FAN54015_OPA_MODE_SHIFT, CHARGEMODE);
    wake_unlock(&Fan54015OTGLocker);	
    return 0;
  //  cancel_delayed_work_sync(&chg_update_work);
}
EXPORT_SYMBOL_GPL(fan54015_otg_regulator_disable);

int fan54015_otg_regulator_is_enable(struct regulator_dev * rdev)
{   int regVal=0;
    regVal=fan54015_read_reg(FAN54015_REG_CONTROL0);


   if(regVal<0)
   	{
            printk(KERN_WARNING  "~Read OTG Status Error!   \n" );
            return  0;

   	}
	
    if(regVal&0x08)
    	{
             #ifdef FAN54015_DEBUG_FS
               printk(KERN_WARNING  "~Fan54015 OTG On   \n" );
	     #endif		 
             return  0x1;
    	}
   else
   	{
           #ifdef FAN54015_DEBUG_FS
               printk(KERN_WARNING  "~Fan54015 OTG Off   \n" );
	     #endif		 
             return  0x0;

   	}
}

EXPORT_SYMBOL_GPL(fan54015_otg_regulator_is_enable);


#if 0

static ssize_t dump_regs_show(struct device *dev, struct device_attribute *attr,
                char *buf)
{
    const int regaddrs[] = {0x00, 0x01, 0x02, 0x03, 0x4, 0x05, 0x06, 0x10 };
    const char str[] = "0123456789abcdef";
    BYTE fan54015_regs[0x60];

    int i = 0, index;
    char val = 0;

    for (i=0; i<0x60; i++) {
        if ((i%3)==2)
            buf[i]=' ';
        else
            buf[i] = 'x';
    }
    buf[0x5d] = '\n';
    buf[0x5e] = 0;
    buf[0x5f] = 0;
    
    for ( i = 0; i<0x07; i++) {
        fan54015_regs[i] = fan54015_read_reg(i);
    }
    fan54015_regs[0x10] = fan54015_read_reg(0x10);

    for (i=0; i<ARRAY_SIZE(regaddrs); i++) {
        index = regaddrs[i];
        val = fan54015_regs[index];
            buf[3*index] = str[(val&0xf0)>>4];
        buf[3*index+1] = str[val&0x0f];
        buf[3*index+1] = str[val&0x0f];
    }
    
    return 0x60;
}

static DEVICE_ATTR(dump_regs, 0x777, dump_regs_show, NULL);
#endif


struct regulator_ops fan54015_otg_reg_ops = {
	.enable		= fan54015_otg_regulator_enable,
	.disable	= fan54015_otg_regulator_disable,
	.is_enabled	= fan54015_otg_regulator_is_enable,
};


static int fan54015_regulator_init(struct i2c_client *client)
{
	int rc = 0;
	struct regulator_init_data *init_data;
	struct regulator_config cfg = {};

	init_data = of_get_regulator_init_data(&client->dev, client->dev.of_node);
	if (!init_data) {
		dev_err(&client->dev, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	if (init_data->constraints.name) {
		fan54015_otg_regulator.rdesc.owner = THIS_MODULE;
		fan54015_otg_regulator.rdesc.type = REGULATOR_VOLTAGE;
		fan54015_otg_regulator.rdesc.ops = &fan54015_otg_reg_ops;
		fan54015_otg_regulator.rdesc.name = init_data->constraints.name;

		cfg.dev = &client->dev;
		cfg.init_data = init_data;
		cfg.driver_data = client;
		cfg.of_node = client->dev.of_node;   

		init_data->constraints.valid_ops_mask
			|= REGULATOR_CHANGE_STATUS;

		fan54015_otg_regulator.rdev = regulator_register(
					&fan54015_otg_regulator.rdesc, &cfg);
		if (IS_ERR(fan54015_otg_regulator.rdev)) {
			rc = PTR_ERR(fan54015_otg_regulator.rdev);
			fan54015_otg_regulator.rdev = NULL;
			if (rc != -EPROBE_DEFER)
				dev_err(&client->dev,
					"OTG reg failed, rc=%d\n", rc);
		}
	}

	return rc;
}



static int fan54015_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{  ktime_t    kt;
    int rc = 0;
    this_client = client;
	
#ifdef FAN54015_DEBUG_FS
    printk(KERN_WARNING  "~FAN54015 probe now,I2C_addr:0x%x, I2C_flag:0x%x  \n",client->addr,client->flags);//add by maxwill	
#endif
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        pr_err("%s: i2c check functionality error\n", __func__);
        rc = -ENODEV;
        goto check_funcionality_failed;
    }

    fan54015_init();
    alarm_init(&FanWDTkicker, ALARM_REALTIME, fan54015_alarm_work_func);
#if 0   
    device_create_file(&client->dev, &dev_attr_dump_regs);
#endif

    fan_pinctrl_init();     

    rc = pinctrl_select_state(fan_pinctrl, fan_otgPin_low);      
    if (rc) {
		printk(KERN_WARNING  "Can't select fan_otgPin_low state\n");
			
	      }
     
    fan54015_regulator_init(client);

    wake_lock_init(&Fan54015WatchDogKicker, WAKE_LOCK_SUSPEND,"FAN54015_KICKER");
    wake_lock_init(&Fan54015OTGLocker, WAKE_LOCK_SUSPEND,"FAN54015_OTGLocker");


    kt = ns_to_ktime(T32S_RESET_INTERVAL);   
    alarm_start_relative(&FanWDTkicker, kt);
	
    return rc;

check_funcionality_failed:
    regulator_unregister(fan54015_otg_regulator.rdev);	
    return rc;  
}

static int fan54015_remove(struct i2c_client *client)
{
    cancel_work_sync(&chg_update_work);    
    cancel_work_sync(&chg_fast_work);     
#ifdef FAN54015_DEBUG_FS	
     printk(KERN_WARNING  "~Destroy WakeLock  Fan54015WatchDogKicker  \n");//add by maxwill	
#endif     
    wake_lock_destroy(&Fan54015WatchDogKicker);
    wake_lock_destroy(&Fan54015OTGLocker);
    alarm_cancel(&FanWDTkicker);	
    regulator_unregister(fan54015_otg_regulator.rdev);	
    return 0;
}

static int  fan54015_suspend(struct i2c_client *client, pm_message_t message)
{ 
          return 0;
}

static int  fan54015_resume(struct i2c_client *client)
{
    return 0;
}

static struct of_device_id fan54015_match_table[] = {
	{ .compatible = "freescale,fan54015-chg",},
	{ },
};

static const struct i2c_device_id fan54015_id[] = {
	{"fan54015-chg", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, fan54015_id);

static struct i2c_driver fan54015_driver = {
	.driver		= {
		.name		= "fan54015-chg",
		.owner		= THIS_MODULE,
		.of_match_table	= fan54015_match_table,
		
	},
	.probe = fan54015_probe,
	.remove = fan54015_remove,
	.suspend = fan54015_suspend,
	.resume	= fan54015_resume,
	.id_table	= fan54015_id,
};

module_i2c_driver(fan54015_driver);

MODULE_DESCRIPTION("FAN54015 Charger");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:fan54015-chg");
MODULE_AUTHOR("mahao@wingtech.com");
