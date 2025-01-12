/*
 *  Copyright (C) 2009 Samsung Electronics
 *  Minkyu Kang <mk7.kang@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MAX17058_BATTERY_H_
#define __MAX17058_BATTERY_H_

#ifdef CONFIG_MULTI_BATTERY
#define MAX17058_BAT_ID_VOL		1800

//bat res (K)
#define MAX17058_BAT_ID_RES		10
#define MAX17058_BAT_1_RES		10
#define MAX17058_BAT_2_RES		100

#define MAX17058_BAT_STR_DEFAULT	"10"
#define MAX17058_BAT_STR_1			"10"
#define MAX17058_BAT_STR_2			"100"
#define MAX17058_BAT_STR_ERR		"NULL"

enum MAX17058_BAT_ID 
{
	MAX17058_BAT_DEFAULT = 1,
	MAX17058_BAT_1 = 1,
	MAX17058_BAT_2 = 2,		
};
#endif

struct max17058_platform_data {
	int r_bat;
	int irq_gpio;
	//ini file
	int ini_rcomp;
	int ini_temp_co_hot;
	int ini_temp_co_cold;
	int ini_soccheck_a;
	int ini_soccheck_b;
	int ini_ocv_test;
	int ini_bits;
	unsigned char ini_model_data[64];	
#ifdef CONFIG_MULTI_BATTERY	
	int ini_rcomp_2;
	int ini_temp_co_hot_2;
	int ini_temp_co_cold_2;
	int ini_soccheck_a_2;
	int ini_soccheck_b_2;
	int ini_ocv_test_2;
	int ini_bits_2;
	unsigned char ini_model_data_2[64];	
#endif
	//ini file end
	int (*battery_online)(void);
	int (*charger_online)(void);
	int (*charger_enable)(void);
};

#endif
