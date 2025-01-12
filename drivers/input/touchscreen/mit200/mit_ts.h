/*
 * MELFAS MIT200 Touchscreen Driver - Platform Data
 *
 * Copyright (C) 2014 MELFAS Inc.
 *
 */

#ifndef _LINUX_MIT_TOUCH_H
#define _LINUX_MIT_TOUCH_H


struct mit_ts_platform_data {
	int	max_x;
	int	max_y;
	int	irq_gpio;
	u32 irq_gpio_flags;
	int	reset_gpio;
	u32 reset_gpio_flags;
	int	ctp_gpio;
	u32 ctp_gpio_flags;
};

#endif /* _LINUX_MIT_TOUCH_H */
