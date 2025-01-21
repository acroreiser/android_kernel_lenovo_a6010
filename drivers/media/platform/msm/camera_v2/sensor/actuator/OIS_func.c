/////////////////////////////////////////////////////////////////////////////
// File Name	: OIS_func.c
// Function		: Various function for OIS control
// Rule         : Use TAB 4
//
// Copyright(c)	Rohm Co.,Ltd. All rights reserved
//
/***** ROHM Confidential ***************************************************/
//#include <stdio.h>
#define	_USE_MATH_DEFINES							// RHM_HT 2013.03.24	Add for using "M_PI" in math.h (VS2008)
//#include <math.h>
//#include <conio.h>
//#include <ctype.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include "OIS_head.h"
#include "OIS_prog.h"
#include "OIS_coef.h"
#include "OIS_defi.h"
//#include "usb_func.h"

extern	OIS_UWORD	OIS_REQUEST;			// OIS control register.
// ==> RHM_HT 2013.03.04	Change type (OIS_UWORD -> double)
extern	double		OIS_PIXEL[2];			// Just Only use for factory adjustment.
// <== RHM_HT 2013.03.04

// ==> RHM_HT 2013.03.13	add for HALL_SENSE_ADJUST
extern	OIS_WORD	CROP_X;								// x start position for cropping
extern	OIS_WORD	CROP_Y;								// y start position for cropping
extern	OIS_WORD	CROP_WIDTH;							// cropping width
extern	OIS_WORD 	CROP_HEIGHT;						// cropping height
extern	OIS_UBYTE	SLICE_LEVE;							// slice level of bitmap binalization

extern	double		DISTANCE_BETWEEN_CIRCLE;			// distance between center of each circle (vertical and horizontal) [mm]
extern	double		DISTANCE_TO_CIRCLE;					// distance to the circle [mm]
extern	double		D_CF;								// Correction Factor for distance to the circle
// ==> RHM_HT 2013/07/10	Added new user definition variables for DC gain check
extern	OIS_UWORD 	ACT_DRV;							// [mV]: Full Scale of OUTPUT DAC.
extern	OIS_UWORD 	FOCAL_LENGTH;						// [um]: Focal Length 3.83mm
extern	double 		MAX_OIS_SENSE;						// [um/mA]: per actuator difinition (change to absolute value)
extern	double		MIN_OIS_SENSE;						// [um/mA]: per actuator difinition (change to absolute value)
extern	OIS_UWORD 	MAX_COIL_R;							// [ohm]: Max value of coil resistance
extern	OIS_UWORD 	MIN_COIL_R;							// [ohm]: Min value of coil resistance
// <== RHM_HT 2013/07/10	Added new user definition variables
/*+ add ljk ois eeprom data*/
_FACT_ADJ fadj;
/*+end*/
// ==> RHM_HT 2013/11/25	Modified
OIS_UWORD 		u16_ofs_tbl[] = {						// RHM_HT 2013.03.13	[Improvement of Loop Gain Adjust] Change to global variable
// 					0x0FBC,								// 1	For MITSUMI
					0x0DFC,								// 2
// 					0x0C3D,								// 3
					0x0A7D,								// 4
// 					0x08BD,								// 5
					0x06FE,								// 6
// 					0x053E,								// 7
					0x037F,								// 8
// 					0x01BF,								// 9
					0x0000,								// 10
// 					0xFE40,								// 11
					0xFC80,								// 12
// 					0xFAC1,								// 13
					0xF901,								// 14
// 					0xF742,								// 15
					0xF582,								// 16
// 					0xF3C2,								// 17
					0xF203,								// 18
// 					0xF043								// 19
};
// <== RHM_HT 2013/11/25	Modified

// <== RHM_HT 2013.03.13
int	E2pDat_Lenovo( uint8_t * memory_data)
{
    pr_err("E2pDat_Lenovo here eeprom \n");
#if 1
    if (memory_data)
    {  //2048  0x800 the first one after 3a eeprom data
        fadj.gl_CURDAT = ( unsigned short )((*(memory_data+2048))|(*(memory_data+2048+1)<<8));
        pr_err("ois gl_CURDAT = 0x%x\n",fadj.gl_CURDAT);
        fadj.gl_HALOFS_X =  ( unsigned short )((*(memory_data+2050))|(*(memory_data+2050+1)<<8));
        pr_err("ois gl_HALOFS_X = 0x%x\n",fadj.gl_HALOFS_X);
        fadj.gl_HALOFS_Y =  ( unsigned short )((*(memory_data+2052))|(*(memory_data+2052+1)<<8));
        pr_err("ois gl_HALOFS_Y = 0x%x\n",fadj.gl_HALOFS_Y);
        fadj.gl_HX_OFS =  ( unsigned short )((*(memory_data+2054))|(*(memory_data+2054+1)<<8));
        pr_err("ois gl_HX_OFS = 0x%x\n",fadj.gl_HX_OFS);
        fadj.gl_HY_OFS =  ( unsigned short )((*(memory_data+2056))|(*(memory_data+2056+1)<<8));
        pr_err("ois gl_HY_OFS = 0x%x\n",fadj.gl_HY_OFS);
        fadj.gl_PSTXOF =  ( unsigned short )((*(memory_data+2058))|(*(memory_data+2058+1)<<8));
        pr_err("ois gl_PSTXOF = 0x%x\n",fadj.gl_PSTXOF);
        fadj.gl_PSTYOF =  ( unsigned short )((*(memory_data+2060))|(*(memory_data+2060+1)<<8));
        pr_err("ois gl_PSTYOF = 0x%x\n",fadj.gl_PSTYOF);
        fadj.gl_GX_OFS = ( unsigned short )((*(memory_data+2062))|(*(memory_data+2062+1)<<8));
        pr_err("ois gl_GX_OFS = 0x%x\n",fadj.gl_GX_OFS);
        fadj.gl_GY_OFS = ( unsigned short )((*(memory_data+2064))|(*(memory_data+2064+1)<<8));
        pr_err("ois gl_GY_OFS = 0x%x\n",fadj.gl_GY_OFS);
        fadj.gl_KgxHG = ( unsigned short )((*(memory_data+2066))|(*(memory_data+2066+1)<<8));
        pr_err("ois gl_KgxHG = 0x%x\n",fadj.gl_KgxHG);
        fadj.gl_KgyHG = ( unsigned short )((*(memory_data+2068))|(*(memory_data+2068+1)<<8));
        pr_err("ois gl_KgyHG = 0x%x\n",fadj.gl_KgyHG);
        fadj.gl_KGXG = ( unsigned short )((*(memory_data+2070))|(*(memory_data+2070+1)<<8));
        pr_err("ois gl_KGXG = 0x%x\n",fadj.gl_KGXG);
        fadj.gl_KGYG = ( unsigned short )((*(memory_data+2072))|(*(memory_data+2072+1)<<8));
        pr_err("ois gl_KGYG = 0x%x\n",fadj.gl_KGYG);
        fadj.gl_SFTHAL_X = ( unsigned short )((*(memory_data+2074))|(*(memory_data+2074+1)<<8));
        pr_err("ois gl_SFTHAL_X = 0x%x\n",fadj.gl_SFTHAL_X);
        fadj.gl_SFTHAL_Y = ( unsigned short )((*(memory_data+2076))|(*(memory_data+2076+1)<<8));
        pr_err("ois gl_SFTHAL_Y = 0x%x\n",fadj.gl_SFTHAL_Y);
        fadj.gl_TMP_X_ = ( unsigned short )((*(memory_data+2078))|(*(memory_data+2078+1)<<8));
        pr_err("ois gl_TMP_X_ = 0x%x\n",fadj.gl_TMP_X_);
        fadj.gl_TMP_Y_ = ( unsigned short )((*(memory_data+2080))|(*(memory_data+2080+1)<<8));
        pr_err("ois gl_TMP_Y_ = 0x%x\n",fadj.gl_TMP_Y_);
        fadj.gl_KgxH0 = ( unsigned short )((*(memory_data+2082))|(*(memory_data+2082+1)<<8));
        pr_err("ois gl_KgxH0 = 0x%x\n",fadj.gl_KgxH0);
        fadj.gl_KgyH0 = ( unsigned short )((*(memory_data+2084))|(*(memory_data+2084+1)<<8));
        pr_err("ois gl_KgyH0 = 0x%x\n",fadj.gl_KgyH0);

    }
#else
    fadj.gl_CURDAT = 0x01CC;
    fadj.gl_HALOFS_X = 0x021E;
    fadj.gl_HALOFS_Y = 0x0235;
    fadj.gl_HX_OFS = 0x03eb;
    fadj.gl_HY_OFS = 0x03ea;
    fadj.gl_PSTXOF = 0x007e;
    fadj.gl_PSTYOF = 0x007e;
    fadj.gl_GX_OFS = 0x0061;
    fadj.gl_GY_OFS = 0xffe3;
    fadj.gl_KgxHG = 0xd84b;
    fadj.gl_KgyHG = 0xd7c9;
    fadj.gl_KGXG = 0x230e;
    fadj.gl_KGYG = 0x2255;
    fadj.gl_SFTHAL_X = 0x021e;
    fadj.gl_SFTHAL_Y = 0x0235;
    fadj.gl_TMP_X_ = 0x0000;
    fadj.gl_TMP_Y_ = 0x0000;
    fadj.gl_KgxH0 = 0x00d7;
    fadj.gl_KgyH0 = 0xff7a;
#endif
    return 0;
}

//  *****************************************************
//  **** Program Download Function
//  *****************************************************
ADJ_STS		func_PROGRAM_DOWNLOAD( void ){	// RHM_HT 2013/04/15	Change "typedef" of return value

	OIS_UWORD	sts;						// RHM_HT 2013/04/15	Change "typedef".
	pr_err("%s \n", __func__ );
	download( 0, 0 );
	if(1){
	// Program Download
	//msleep(1);
	sts = I2C_OIS_mem__read( _M_OIS_STS );	// Check Status
	pr_err("%s over FDL sts :   0x%x\n\n",__func__, sts );
	if( (sts != 0xFFFF) && ( sts & 0x0004 ) == 0x0004 ){	// If return value is 0xFFFF, the equation is successful, so such condition should be removed.
		// ==> RHM_HT 2013/07/10	Added
		//OIS_UWORD u16_dat;

		//u16_dat = I2C_OIS_mem__read( _M_FIRMVER );
		//pr_err("Firm Ver :      %4d\n\n", u16_dat );
		// <== RHM_HT 2013/07/10	Added

		return ADJ_OK;						// Success				RHM_HT 2013/04/15	Change return value.
	}
	else{
		return PROG_DL_ERR;					// FAIL					RHM_HT 2013/04/15	Change return value.
        //return ADJ_OK;
	}
    }
        return ADJ_OK;

}


// ==> RHM_HT 2013/11/26	Reverted
//  *****************************************************
//  **** COEF Download function
//  *****************************************************
void	func_COEF_DOWNLOAD( OIS_UWORD u16_coef_type ){

	OIS_UWORD u16_i, u16_dat;
	OIS_UWORD	sts;						// RHM_HT 2013/04/15	Change "typedef".

	download( 1, u16_coef_type );			// COEF Download
	sts = I2C_OIS_mem__read( _M_CEFTYP );	// Check Status
	pr_err("FDL sts :  0x%x,  except 0xF03C\n\n", sts );

    if(0)
    {
	for( u16_i = 1; u16_i <= 256; u16_i++){
		u16_dat = I2C_OIS_mem__read( u16_i-1 );
		//  memory dump around start area and end area
		if( ( u16_i < 3 ) || ( u16_i > 253 ) ){
			pr_err("M[%.2x] %.4X\n",u16_i-1, u16_dat );
		}
		//
		if( u16_i == 128 ){
			pr_err("  ... \n" );
		}
		// Coef type
		if( (u16_i-1) == _M_CEFTYP ){
			pr_err("COEF     M[%.2x] %.4X\n",u16_i-1, u16_dat );
		}
	}
	}
}
// <== RHM_HT 2013/11/26	Reverted


//  *****************************************************
//  **** Download the data
//  *****************************************************
void	download( OIS_UWORD u16_type, OIS_UWORD u16_coef_type ){

	// Data Transfer Size per one I2C access
	#define		DWNLD_TRNS_SIZE		(32)
	OIS_UBYTE	temp[DWNLD_TRNS_SIZE+1];
	OIS_UWORD	block_cnt;
	OIS_UWORD	total_cnt;
	OIS_UWORD	lp;
	OIS_UWORD	n;
	OIS_UWORD	u16_i;
	if	( u16_type == 0 ){
		n		= DOWNLOAD_BIN_LEN;
	}
	else{
		n = DOWNLOAD_COEF_LEN;								// RHM_HT 2013/07/10	Modified
	}
	block_cnt	= n / DWNLD_TRNS_SIZE + 1;
	total_cnt	= block_cnt;
    pr_err("download +\n" );

	while( 1 ){
		// Residual Number Check
		if( block_cnt == 1 ){
			lp = n % DWNLD_TRNS_SIZE;
		}
		else{
			lp = DWNLD_TRNS_SIZE;
		}

		// Transfer Data set
		if( lp != 0 ){
			if(	u16_type == 0 ){
				temp[0] = _OP_FIRM_DWNLD;
				for( u16_i = 1; u16_i <= lp; u16_i += 1 ){
					temp[ u16_i ] = DOWNLOAD_BIN[ ( total_cnt - block_cnt ) * DWNLD_TRNS_SIZE + u16_i - 1 ];
				}

			}
			else{
				temp[0] = _OP_COEF_DWNLD;
				for( u16_i = 1; u16_i <= lp; u16_i += 1 ){
					temp[u16_i] = DOWNLOAD_COEF[(total_cnt - block_cnt) * DWNLD_TRNS_SIZE + u16_i -1];	// RHM_HT 2013/07/10	Modified
				}
			}
			// Data Transfer
 			WR_I2C( _SLV_OIS_, lp + 1, temp );
		}

		// Block Counter Decrement
		block_cnt = block_cnt - 1;
		if( block_cnt == 0 ){
			break;
		}
	}

}


int SET_FADJ_PARAM( const _FACT_ADJ *param )
{
    int rc = -1;
	//*********************
	// HALL ADJUST
	//*********************
	// Set Hall Current DAC   value that is FACTORY ADJUSTED
	I2C_OIS_per_write( _P_30_ADC_CH0, param->gl_CURDAT );
	// Set Hall     PreAmp Offset   that is FACTORY ADJUSTED
	I2C_OIS_per_write( _P_31_ADC_CH1, param->gl_HALOFS_X );
	I2C_OIS_per_write( _P_32_ADC_CH2, param->gl_HALOFS_Y );
	// Set Hall-X/Y PostAmp Offset  that is FACTORY ADJUSTED
	I2C_OIS_mem_write( _M_X_H_ofs, param->gl_HX_OFS );
	I2C_OIS_mem_write( _M_Y_H_ofs, param->gl_HY_OFS );
	// Set Residual Offset          that is FACTORY ADJUSTED
	I2C_OIS_per_write( _P_39_Ch3_VAL_1, param->gl_PSTXOF );
	I2C_OIS_per_write( _P_3B_Ch3_VAL_3, param->gl_PSTYOF );

	//*********************
	// DIGITAL GYRO OFFSET
	//*********************
	I2C_OIS_mem_write( _M_Kgx00, param->gl_GX_OFS );
	I2C_OIS_mem_write( _M_Kgy00, param->gl_GY_OFS );
	I2C_OIS_mem_write( _M_TMP_X_, param->gl_TMP_X_ );
	I2C_OIS_mem_write( _M_TMP_Y_, param->gl_TMP_Y_ );

	//*********************
	// HALL SENSE
	//*********************
	// Set Hall Gain   value that is FACTORY ADJUSTED
	I2C_OIS_mem_write( _M_KgxHG, param->gl_KgxHG );
	I2C_OIS_mem_write( _M_KgyHG, param->gl_KgyHG );
	// Set Cross Talk Canceller
	I2C_OIS_mem_write( _M_KgxH0, param->gl_KgxH0 );
	I2C_OIS_mem_write( _M_KgyH0, param->gl_KgyH0 );

	//*********************
	// LOOPGAIN
	//*********************
	I2C_OIS_mem_write( _M_KgxG, param->gl_KGXG );
	I2C_OIS_mem_write( _M_KgyG, param->gl_KGYG );

	// Position Servo ON ( OIS OFF )
	rc = I2C_OIS_mem_write( _M_EQCTL, 0x0C0C ); //server-on lens move to center
    return rc;
}


//  *****************************************************
//  **** Scence parameter
//  *****************************************************
ADJ_STS	func_SET_SCENE_PARAM(OIS_UBYTE u16_scene, OIS_UBYTE u16_mode, OIS_UBYTE filter, OIS_UBYTE range, const _FACT_ADJ *param)	// RHM_HT 2013/04/15	Change "typedef" of return value
{
	OIS_UWORD u16_i;
	OIS_UWORD u16_dat;

// ==> RHM_HT 2013/11/25	Modified
	OIS_UBYTE u16_adr_target[3]        = { _M_Kgxdr, _M_X_LMT, _M_X_TGT,  };

	OIS_UWORD u16_dat_SCENE_NIGHT_1[3] = { 0x7FFE,   0x38E0,   0x0830,    };	// 1.3 deg	16dps
	OIS_UWORD u16_dat_SCENE_NIGHT_2[3] = { 0x7FFC,   0x38E0,   0x0830,    };	// 1.3 deg	16dps
	OIS_UWORD u16_dat_SCENE_NIGHT_3[3] = { 0x7FFA,   0x38E0,   0x0830,    };	// 1.3 deg	16dps

	OIS_UWORD u16_dat_SCENE_D_A_Y_1[3] = { 0x7FFE,   0x38E0,   0x1478,    };	// 1.3 deg	40dps
	OIS_UWORD u16_dat_SCENE_D_A_Y_2[3] = { 0x7FFA,   0x38E0,   0x1478,    };	// 1.3 deg	40dps
	OIS_UWORD u16_dat_SCENE_D_A_Y_3[3] = { 0x7FF0,   0x38E0,   0x1478,    };	// 1.3 deg	40dps

	OIS_UWORD u16_dat_SCENE_SPORT_1[3] = { 0x7FFE,   0x38E0,   0x1EB4,    };	// 1.3 deg	60dps
	OIS_UWORD u16_dat_SCENE_SPORT_2[3] = { 0x7FF0,   0x38E0,   0x1EB4,    };	// 1.3 deg	60dps
	OIS_UWORD u16_dat_SCENE_SPORT_3[3] = { 0x7FE0,   0x38E0,   0x1EB4,    };	// 1.3 deg	60dps

	OIS_UWORD u16_dat_SCENE_TEST___[3] = { 0x7FF0,   0x7FFF,   0x7FFF,    };	// Limmiter OFF
// <== RHM_HT 2013/11/25	Modified

	OIS_UWORD *u16_dat_SCENE_;

	OIS_UBYTE	size_SCENE_tbl = sizeof( u16_dat_SCENE_NIGHT_1 ) / sizeof(OIS_UWORD);

	// Disable OIS ( position Servo is not disable )
	u16_dat = I2C_OIS_mem__read( _M_EQCTL );
	u16_dat = ( u16_dat &  0xFEFE );
	I2C_OIS_mem_write( _M_EQCTL, u16_dat );

	// Scene parameter select
	switch( u16_scene ){
		case _SCENE_NIGHT_1 : u16_dat_SCENE_ = u16_dat_SCENE_NIGHT_1;	DEBUG_printf(("+???f???f???f???f???f???f???f???f???f???f+\n+---_SCENE_NIGHT_1---+\n+???f???f???f???f???f???f???f???f???f???f+\n"));		break;
		case _SCENE_NIGHT_2 : u16_dat_SCENE_ = u16_dat_SCENE_NIGHT_2;	DEBUG_printf(("+???f???f???f???f???f???f???f???f???f???f+\n+---_SCENE_NIGHT_2---+\n+???f???f???f???f???f???f???f???f???f???f+\n"));		break;
		case _SCENE_NIGHT_3 : u16_dat_SCENE_ = u16_dat_SCENE_NIGHT_3;	DEBUG_printf(("+???f???f???f???f???f???f???f???f???f???f+\n+---_SCENE_NIGHT_3---+\n+???f???f???f???f???f???f???f???f???f???f+\n"));		break;
		case _SCENE_D_A_Y_1 : u16_dat_SCENE_ = u16_dat_SCENE_D_A_Y_1;	DEBUG_printf(("+????????????????????+\n+---_SCENE_D_A_Y_1---+\n+????????????????????+\n"));		break;
		case _SCENE_D_A_Y_2 : u16_dat_SCENE_ = u16_dat_SCENE_D_A_Y_2;	DEBUG_printf(("+????????????????????+\n+---_SCENE_D_A_Y_2---+\n+????????????????????+\n"));		break;
		case _SCENE_D_A_Y_3 : u16_dat_SCENE_ = u16_dat_SCENE_D_A_Y_3;	DEBUG_printf(("+????????????????????+\n+---_SCENE_D_A_Y_3---+\n+????????????????????+\n"));		break;
		case _SCENE_SPORT_1 : u16_dat_SCENE_ = u16_dat_SCENE_SPORT_1;	DEBUG_printf(("+????????????????????+\n+---_SCENE_SPORT_1---+\n+????????????????????+\n"));		break;
		case _SCENE_SPORT_2 : u16_dat_SCENE_ = u16_dat_SCENE_SPORT_2;	DEBUG_printf(("+????????????????????+\n+---_SCENE_SPORT_2---+\n+????????????????????+\n"));		break;
		case _SCENE_SPORT_3 : u16_dat_SCENE_ = u16_dat_SCENE_SPORT_3;	DEBUG_printf(("+????????????????????+\n+---_SCENE_SPORT_3---+\n+????????????????????+\n"));		break;
		case _SCENE_TEST___ : u16_dat_SCENE_ = u16_dat_SCENE_TEST___;	DEBUG_printf(("+********************+\n+---dat_SCENE_TEST___+\n+********************+\n"));		break;
		default             : u16_dat_SCENE_ = u16_dat_SCENE_TEST___;	DEBUG_printf(("+********************+\n+---dat_SCENE_TEST___+\n+********************+\n"));		break;
	}

	// Set parameter to the OIS controller
	for( u16_i = 0; u16_i < size_SCENE_tbl; u16_i += 1 ){
		I2C_OIS_mem_write( u16_adr_target[u16_i],          	u16_dat_SCENE_[u16_i]   );
	}
	for( u16_i = 0; u16_i < size_SCENE_tbl; u16_i += 1 ){
		I2C_OIS_mem_write( u16_adr_target[u16_i] + 0x80,	u16_dat_SCENE_[u16_i] );
	}

	// Set/Reset Notch filter
	if ( filter == 1 ) {					// Disable Filter
		u16_dat = I2C_OIS_mem__read( _M_EQCTL );
		u16_dat |= 0x4000;
		I2C_OIS_mem_write( _M_EQCTL, u16_dat );
	}
	else{									// Enable Filter
		u16_dat = I2C_OIS_mem__read( _M_EQCTL );
		u16_dat &= 0xBFFF;
		I2C_OIS_mem_write( _M_EQCTL, u16_dat );
	}
	// Clear the register of the OIS controller
	I2C_OIS_mem_write( _M_wDgx02, 0x0000 );
	I2C_OIS_mem_write( _M_wDgx03, 0x0000 );
	I2C_OIS_mem_write( _M_wDgx06, 0x7FFF );
	I2C_OIS_mem_write( _M_Kgx15,  0x0000 );

	I2C_OIS_mem_write( _M_wDgy02, 0x0000 );
	I2C_OIS_mem_write( _M_wDgy03, 0x0000 );
	I2C_OIS_mem_write( _M_wDgy06, 0x7FFF );
	I2C_OIS_mem_write( _M_Kgy15,  0x0000 );

	// Set the pre-Amp offset value (X and Y)
// ==> RHM_HT 2013/11/25	Modified
	if	( range == 1 ) {
		I2C_OIS_per_write( _P_31_ADC_CH1, param->gl_SFTHAL_X );
		I2C_OIS_per_write( _P_32_ADC_CH2, param->gl_SFTHAL_Y );
	}
	else{
		I2C_OIS_per_write( _P_31_ADC_CH1, param->gl_HALOFS_X );
		I2C_OIS_per_write( _P_32_ADC_CH2, param->gl_HALOFS_Y );
	}
    pr_err("+set gl_HALOFS_X =0x%x  gl_HALOFS_Y=0x%x\n",param->gl_HALOFS_X,param->gl_HALOFS_Y );

// <== RHM_HT 2013/11/25	Modified

	// Enable OIS (if u16_mode = 1)
	if(	( u16_mode == 1 ) ){
		u16_dat = I2C_OIS_mem__read( _M_EQCTL );
		u16_dat = ( u16_dat |  0x0101 );
		I2C_OIS_mem_write( _M_EQCTL, u16_dat );
		DEBUG_printf(("SET : EQCTL:%.4x\n", u16_dat ));
	}
	else{														// ==> RHM_HT 2013.03.23	Add for OIS controll
		u16_dat = I2C_OIS_mem__read( _M_EQCTL );
		u16_dat = ( u16_dat &  0xFEFE );
		I2C_OIS_mem_write( _M_EQCTL, u16_dat );
		DEBUG_printf(("SET : EQCTL:%.4x\n", u16_dat ));
	}															// <== RHM_HT 2013.03.23	Add for OIS controll

	return ADJ_OK;												// RHM_HT 2013/04/15	Change return value
}


//  *****************************************************
//  **** Write to the Peripheral register < 82h >
//  **** ------------------------------------------------
//  **** OIS_UBYTE	adr	Peripheral Address
//  **** OIS_UWORD	dat	Write data
//  *****************************************************
void	I2C_OIS_per_write( OIS_UBYTE u08_adr, OIS_UWORD u16_dat ){

	OIS_UBYTE	out[4];

	out[0] = _OP_Periphe_RW;
	out[1] = u08_adr;
	out[2] = ( u16_dat      ) & 0xFF;
	out[3] = ( u16_dat >> 8 ) & 0xFF;

	WR_I2C( _SLV_OIS_, 4, out );
}

//  *****************************************************
//  **** Write to the Memory register < 84h >
//  **** ------------------------------------------------
//  **** OIS_UBYTE	adr	Memory Address
//  **** OIS_UWORD	dat	Write data
//  *****************************************************
int	I2C_OIS_mem_write( OIS_UBYTE u08_adr, OIS_UWORD u16_dat){

	OIS_UBYTE	out[4];
    int rc = -1;
	out[0] = _OP_Memory__RW;
	out[1] = u08_adr;
	out[2] = ( u16_dat      ) & 0xFF;
	out[3] = ( u16_dat >> 8 ) & 0xFF;

	rc = WR_I2C( _SLV_OIS_, 4, out );
	return rc;
}

//  *****************************************************
//  **** Read from the Peripheral register < 82h >
//  **** ------------------------------------------------
//  **** OIS_UBYTE	adr	Peripheral Address
//  **** OIS_UWORD	dat	Read data
//  *****************************************************
OIS_UWORD	I2C_OIS_per__read( OIS_UBYTE u08_adr ){

	OIS_UBYTE	u08_dat[2];

	u08_dat[0] = _OP_Periphe_RW;	// Op-code
	u08_dat[1] = u08_adr;			// target address

	return RD_I2C( _SLV_OIS_, 2, u08_dat );
}


//  *****************************************************
//  **** Read from the Memory register < 84h >
//  **** ------------------------------------------------
//  **** OIS_UBYTE	adr	Memory Address
//  **** OIS_UWORD	dat	Read data
//  *****************************************************
OIS_UWORD	I2C_OIS_mem__read( OIS_UBYTE u08_adr){

	OIS_UBYTE	u08_dat[2];

	u08_dat[0] = _OP_Memory__RW;	// Op-code
	u08_dat[1] = u08_adr;			// target address

	return RD_I2C( _SLV_OIS_, 2, u08_dat );
}


//  *****************************************************
//  **** Special Command 8Ah
// 		_cmd_8C_EI			0	// 0x0001
// 		_cmd_8C_DI			1	// 0x0002
//  *****************************************************
void	I2C_OIS_spcl_cmnd( OIS_UBYTE u08_on, OIS_UBYTE u08_dat ){

	if( ( u08_dat == _cmd_8C_EI ) ||
		( u08_dat == _cmd_8C_DI )    ){

		OIS_UBYTE out[2];

		out[0] = _OP_SpecialCMD;
		out[1] = u08_dat;

		WR_I2C( _SLV_OIS_, 2, out );
	}
}


//  *****************************************************
//  **** F0-F3h Command NonAssertClockStretch Function
//  *****************************************************
void	I2C_OIS_F0123_wr_( OIS_UBYTE u08_dat0, OIS_UBYTE u08_dat1, OIS_UWORD u16_dat2 ){

	OIS_UBYTE out[5];

	out[0] = 0xF0;
	out[1] = u08_dat0;
	out[2] = u08_dat1;
	out[3] = u16_dat2 / 256;
	out[4] = u16_dat2 % 256;

	WR_I2C( _SLV_OIS_, 5, out );

}

OIS_UWORD	I2C_OIS_F0123__rd( void ){

	OIS_UBYTE	u08_dat;

	u08_dat = 0xF0;				// Op-code

	return RD_I2C( _SLV_OIS_, 1, &u08_dat );
}


// -----------------------------------------------------------
// -----------------------------------------------------------
// -----------------------------------------------------------

