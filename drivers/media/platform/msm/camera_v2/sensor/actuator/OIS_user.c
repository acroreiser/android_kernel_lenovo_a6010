/////////////////////////////////////////////////////////////////////////////
// File Name	: OIS_user.c
// Function		: User defined function.
// 				  These functions depend on user's circumstance.
//
// Rule         : Use TAB 4
//
// Copyright(c)	Rohm Co.,Ltd. All rights reserved
//
/***** ROHM Confidential ***************************************************/
#ifndef OIS_USER_C
#define OIS_USER_C
#endif
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include "msm_cci.h"

#include "msm_actuator.h"
//#include <stdio.h>
#include "OIS_head.h"
//#include "usb_func.h"
//#include "winbase.h"
extern struct msm_actuator_ctrl_t * actuator_ctrl;

#define SLAVE_ADDR 0x1c

// Following Variables that depend on user's environment			RHM_HT 2013.03.13	add
OIS_WORD	CROP_X 		= 1750;							// x start position for cropping
OIS_WORD	CROP_Y 		= 1150;							// y start position for cropping
OIS_WORD	CROP_WIDTH 	= 750;							// cropping width
OIS_WORD 	CROP_HEIGHT	= 750;							// cropping height
OIS_UBYTE	SLICE_LEVE	= 67;							// slice level of bitmap binalization

double		DISTANCE_BETWEEN_CIRCLE = 50.0;				// distance between center of each circle (vertical and horizontal) [mm]
double		DISTANCE_TO_CIRCLE		= 550.0;			// distance to the circle [mm]
double		D_CF					= 1.00;				// Correction Factor for distance to the circle

OIS_UWORD			FOCUS_VAL	= 0x0122;				// Focus Value

// ==> RHM_HT 2013/07/10	Added new user definition variables for DC gain check
OIS_UWORD 	ACT_DRV = 1650;								// [mV]: Full Scale of OUTPUT DAC.
OIS_UWORD 	FOCAL_LENGTH = 3830;						// [um]: Focal Length 3.83mm
double 		MAX_OIS_SENSE = 1.85;						// [um/mA]: per actuator difinition (change to absolute value)	RHM_HT 2014/03/06	Modified
double		MIN_OIS_SENSE = 0.95;						// [um/mA]: per actuator difinition (change to absolute value)	RHM_HT 2014/03/06	Modified
OIS_UWORD 	MAX_COIL_R = 12.2;							// [ohm]: Max value of coil resistance							RHM_HT 2014/03/06	Modified
OIS_UWORD 	MIN_COIL_R = 8.2;							// [ohm]: Min value of coil resistance							RHM_HT 2014/03/06	Modified
// <== RHM_HT 2013/07/10	Added new user definition variables

// *********************************************************
// Disable/Enable Power Save Mode (CLK_PS port = L->H/H->L)
// and VCO setting
// ---------------------------------------------------------
// <Function>
//		Disable or Enable Power Save Mode of OIS controller chip.
//		This function relate to your own circuit.
//
// <Input>
//		None
//
// <Output>
//		None
//
// <Description>
//
// *********************************************************
void	POWER_UP_AND_PS_DISABLE( void )
{
	/* Please write your source code according to following instructions: */
	// Instractions:
	// 		CLK_PS = L
	// 		1ms wait
	// 		CLK_PS = External clock
	{
		// Sample Code
#if 0

		PortCtrl( _picsig_PS____, _PIO_O_L_ );					// Disable external clock
		Wait(1);												// 1ms wait
		PortCtrl( _picsig_PS____, _PIO_O_H_ );					// Enable external clock
#endif
	}
}
void	POWER_DOWN_AND_PS_ENABLE( void )
{
	/* Please write your source code according to following instructions: */
	// Instractions:
	// 		CLK_PS = L
	// 		1ms wait

	{
		// Sample Code
#if 0
		PortCtrl( _picsig_PS____, _PIO_O_L_ );					// CLK_PS = L
		Wait(1);												// 1ms wait
#endif
	}
}	// 		Depend on your system

void VCOSET0( void )
{

    OIS_UWORD       CLK_PS = 23880;                                                     // Input Frequency [kHz] of CLK/PS terminal (Depend on your system)
    OIS_UWORD       FVCO_1 = 27000;                                                    // Target Frequency [kHz]
    OIS_UWORD       FREF   = 25;                                                             // Reference Clock Frequency [kHz]

    OIS_UWORD       DIV_N  = CLK_PS / FREF - 1;                            // calc DIV_N
    OIS_UWORD       DIV_M  = FVCO_1 / FREF - 1;                                 // calc DIV_M
	pr_err("%s\n",__func__ );

    I2C_OIS_per_write( 0x62, DIV_N  );                                                   // Divider for internal reference clock
    I2C_OIS_per_write( 0x63, DIV_M  );                                                  // Divider for internal PLL clock
    I2C_OIS_per_write( 0x64, 0x4060 );                                                  // Loop Filter

    I2C_OIS_per_write( 0x60, 0x3011 );                                                  // PLL
    I2C_OIS_per_write( 0x65, 0x0080 );                                                  //
    I2C_OIS_per_write( 0x61, 0x8002 );                                                  // VCOON
    I2C_OIS_per_write( 0x61, 0x8003 );                                                  // Circuit ON
    I2C_OIS_per_write( 0x61, 0x8809 );                                                  // PLL ON

	pr_err("%s over\n",__func__ );

}
void	VCOSET1( void )
{
	pr_err("%s\n",__func__ );
    I2C_OIS_per_write( 0x05, 0x000C ); 							// Prepare for PLL clock as master clock
    I2C_OIS_per_write( 0x05, 0x000D ); 							// Change to PLL clock
	pr_err("%s over\n",__func__ );

}


// /////////////////////////////////////////////////////////
// Take picture
// ---------------------------------------------------------
// <Function>
//		This function controls BUSY and CAPTURE signal between SHARP Fedora Board and PIC micon to save image,
// 		and change the saved image file name to the argument.
//
// <Input>
//		char 	*saveFile		change to this file name
//
// <Output>
//		int						status
//
// <Description>
//		2013.03.13	Move from OIS_misc.c
// =========================================================
int	TakePicture(char *saveFile)
{
	// Depend on your system

/*
	int ret;
	char command[512];

	if( saveFile == NULL ){
		DEBUG_printf(("Please set filename."));
		return OIS_PARAMETER_ERROR;
	}

	PortCtrl(0x29, 0x00);

	DEBUG_printf(("Check BUSY = HI, "));
	while( 1 ){
		ret = PortCtrl(0x2B, 0x02);
		if( ret == 1 ){
			break;
		}

		Wait(100);
	}

	DEBUG_printf(("Set CAPTURE = HI, "));
	PortCtrl(0x29, 0x01);

	DEBUG_printf(("Wait BUSY = LO, "));
	while( 1 ){
		ret = PortCtrl(0x2B, 0x02);
		if( ret == 0 ){
			break;
		}

		Wait(100);
	}

	DEBUG_printf(("set CAPTURE = LO, "));
	PortCtrl(0x29, 0x00);

	DEBUG_printf(("Wait BUSY = HI, "));
	while( 1 ){
		ret = PortCtrl(0x2B, 0x02);
		if( ret == 1 ){
			break;
		}

		Wait(100);
	}

	// Rename
	sprintf(command, "IF EXIST %s201*.bmp (ren %s201*.bmp %s)", _STR_AREA_, _STR_AREA_, saveFile);
	ret = system(command);
	if( ret != 0 ){
		DEBUG_printf(("File rename error!! %d\n", ret));
		return OIS_FILE_RENAME_ERROR;
	}

	DEBUG_printf(("Capture & Save done.\n"));
*/
	return OIS_NO_ERROR;
}


// /////////////////////////////////////////////////////////
// Write Data to Slave device via I2C master device
// ---------------------------------------------------------
// <Function>
//		I2C master send these data to the I2C slave device.
//		This function relate to your own circuit.
//
// <Input>
//		OIS_UBYTE	slvadr	I2C slave adr
//		OIS_UBYTE	size	Transfer Size
//		OIS_UBYTE	*dat	data matrix
//
// <Output>
//		none
//
// <Description>
//		[S][SlaveAdr][W]+[dat[0]]+...+[dat[size-1]][P]
//
// =========================================================
int	WR_I2C( OIS_UBYTE slvadr, OIS_UBYTE size, OIS_UBYTE *dat )
{
	/* Please write your source code here. */

	{
		// Sample Code
		//SOutEx(slvadr, dat, size);
    	//pr_err("%s: actuator_ctrl=%p %s\n", __func__,actuator_ctrl,actuator_ctrl->pdev->name);

#if 1
    int rc = 0;
    int loop=1;
    //pr_err("call WR_I2C (slvadr 0x%02X, size 0x%02X, dat 0x%02X, 0x%02X,  0x%02X,  0x%02X,  0x%02X )\n", slvadr, size, dat[0], dat[1], dat[2], dat[3],dat[4]);

    do{

    actuator_ctrl->i2c_client.addr_type = 1;//MSM_CAMERA_I2C_BYTE_ADDR
    rc = actuator_ctrl->i2c_client.i2c_func_tbl->i2c_write_seq(
									&actuator_ctrl->i2c_client,
									(unsigned long)(*dat),
									(uint8_t*)dat+1,
									(uint32_t)(size-1));//MSM_CAMERA_I2C_BYTE_DATA

    if(rc < 0)
    {
    	pr_err("%s:rc = %d write error loop=%d\n", __func__, rc,loop);
    	loop--;
    }
    	}while((rc<0)&&(loop>0));
    	//pr_err("%s:over\n", __func__);
#endif
    return rc;
	}
}


// *********************************************************
// Read Data from Slave device via I2C master device
// ---------------------------------------------------------
// <Function>
//		I2C master read data from the I2C slave device.
//		This function relate to your own circuit.
//
// <Input>
//		OIS_UBYTE	slvadr	I2C slave adr
//		OIS_UBYTE	size	Transfer Size
//		OIS_UBYTE	*dat	data matrix
//
// <Output>
//		OIS_UWORD	16bit data read from I2C Slave device
//
// <Description>
//	if size == 1
//		[S][SlaveAdr][W]+[dat[0]]+         [RS][SlaveAdr][R]+[RD_DAT0]+[RD_DAT1][P]
//	if size == 2
//		[S][SlaveAdr][W]+[dat[0]]+[dat[1]]+[RS][SlaveAdr][R]+[RD_DAT0]+[RD_DAT1][P]
//
// *********************************************************
OIS_UWORD	RD_I2C( OIS_UBYTE slvadr, OIS_UBYTE size, OIS_UBYTE *dat )
{
	OIS_UWORD	read_data = 0;


	/* Please write your source code here. */

	{
		// Sample Code
#if 1
    int rc = 0;
    actuator_ctrl->i2c_client.addr_type = 2;//MSM_CAMERA_I2C_WORD_ADDR
    rc = actuator_ctrl->i2c_client.i2c_func_tbl->i2c_read(
									&actuator_ctrl->i2c_client,
									(unsigned long)((*dat)<<8|(*(dat+1))),
									&read_data,
						            2);
#else
{
		// Sample Code
		//SOutEx(slvadr, dat, size);
		OIS_UBYTE ret[2];
    int rc = 0;
    pr_err("call RD_I2C (0x%02X, 0x%02X, 0x%02X, 0x%02X)\n", slvadr, size, dat[0], dat[1]);//, dat[2], dat[3]);


    	pr_err("%s: actuator_ctrl=%p %s\n", __func__,actuator_ctrl,actuator_ctrl->pdev->name);
#if 1
    actuator_ctrl->i2c_client.addr_type = 2;//MSM_CAMERA_I2C_WORD_ADDR

    rc = actuator_ctrl->i2c_client.i2c_func_tbl->i2c_read_seq(
									&actuator_ctrl->i2c_client,
									(unsigned long)((*dat)<<8|(*(dat+1))),
									(unsigned char *)ret,
						            2);
    if(rc < 0)
    	pr_err("%s:rc = %d write error\n", __func__, rc);
#endif
		read_data = ret[0] * 0x100 + ret[1];

    	pr_err("%s:over\n", __func__);

	}
#endif

	}
  		pr_err("%s addr=0x%lx  value=0x%04X\n",__func__,(unsigned long)((*dat)<<8|(*(dat+1))), read_data);
	return read_data;
}

#if 0
// *********************************************************
// Write Factory Adjusted data to the non-volatile memory
// ---------------------------------------------------------
// <Function>
//		Factory adjusted data are sotred somewhere
//		non-volatile memory.
//
// <Input>
//		_FACT_ADJ	Factory Adjusted data
//
// <Output>
//		none
//
// <Description>
//		You have to port your own system.
//
// *********************************************************
void	store_FADJ_MEM_to_non_volatile_memory( _FACT_ADJ param )
{
	/* 	Write to the non-vollatile memory such as EEPROM or internal of the CMOS sensor... */
}


// *********************************************************
// Read Factory Adjusted data from the non-volatile memory
// ---------------------------------------------------------
// <Function>
//		Factory adjusted data are sotred somewhere
//		non-volatile memory.  I2C master has to read these
//		data and store the data to the OIS controller.
//
// <Input>
//		none
//
// <Output>
//		_FACT_ADJ	Factory Adjusted data
//
// <Description>
//		You have to port your own system.
//
// *********************************************************
_FACT_ADJ	get_FADJ_MEM_from_non_volatile_memory( void )
{
	/* 	Read from the non-vollatile memory such as EEPROM or internal of the CMOS sensor... */

	return FADJ_MEM;		// Note: This return data is for DEBUG.
}
#endif

// *********************************************************
// Wait
// ---------------------------------------------------------
// <Function>
//
// <Input>
//		OIS_ULONG	time	on the micro second time scale
//
// <Output>
//		none
//
// <Description>
//
// *********************************************************
void	Wait_usec( OIS_ULONG time )
{
	/* Please write your source code here. */
#if 0
	{
		// Sample Code

		// Argument of Sleep() is on the msec scale.
		unsigned int msec = time / 1000;

		if (msec == 0){
			msec = 1;
		}
		msleep(msec);
	}
#else
    usleep(time);
#endif
}


// ==> RHM_HT 2013/04/15	Add for DEBUG
// *********************************************************
// Printf for DEBUG
// ---------------------------------------------------------
// <Function>
//
// <Input>
//		const char *format, ...
// 				Same as printf
//
// <Output>
//		none
//
// <Description>
//
// *********************************************************
int debug_print(const char *format, ...)
{
	char str[512];
	int r;
	va_list va;

	int length = (int)strlen(format);

	if(	length >= 512 ){
		printk("length of %s: %d\n", format, length);
		return -1;
	}

	va_start(va, format);
	r = vsprintf(str, format, va);
	va_end(va);

	printk(str);


	return r;
}
// <== RHM_HT 2013/04/15	Add for DEBUG
