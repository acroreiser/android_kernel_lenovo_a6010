/*
 * mms_ts.h - Platform data for Melfas MMS-series touch driver
 *
 * Copyright (C) 2013 Melfas Inc.
 * Author: DVK team <dvk@melfas.com>
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef _LINUX_MMS_TOUCH_H
#define _LINUX_MMS_TOUCH_H

struct mms_ts_platform_data {
	int	max_x;
	int	max_y;
/*
	int	gpio_sda;
	int	gpio_scl;
	int	gpio_resetb;		//Interrupt pin
	int	gpio_vdd_en;
*/
//ahe add 
	int	irq_gpio;
	u32 irq_gpio_flags;
	int	reset_gpio;
	u32 reset_gpio_flags;
	int	ctp_gpio;
	u32 ctp_gpio_flags;
};

#endif /* _LINUX_MMS_TOUCH_H */
