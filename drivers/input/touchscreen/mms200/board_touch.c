/*
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/gpio_event.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/platform_data/mms_ts.h>
#include <asm/mach-types.h>

#include "board-tuna.h"
#include "mux.h"

#define GPIO_TOUCH_EN		19
#define GPIO_TOUCH_IRQ		46

/* touch is on i2c3 */
#define GPIO_TOUCH_SCL		130
#define GPIO_TOUCH_SDA		131

static struct mms_ts_platform_data mms_ts_pdata = {
	.max_x		= 720,
	.max_y		= 1280,
	.gpio_resetb	= GPIO_TOUCH_IRQ,
	.gpio_vdd_en	= GPIO_TOUCH_EN,
	.gpio_scl	= GPIO_TOUCH_SCL,
	.gpio_sda	= GPIO_TOUCH_SDA,
};

static struct i2c_board_info __initdata tuna_i2c3_boardinfo_final[] = {
	{
		I2C_BOARD_INFO("mms_ts", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.platform_data = &mms_ts_pdata,
		.irq = OMAP_GPIO_IRQ(GPIO_TOUCH_IRQ),
	},
};


void __init omap4_tuna_input_init(void)
{
	gpio_request(GPIO_TOUCH_IRQ, "tsp_int_n");
	gpio_direction_input(GPIO_TOUCH_IRQ);
	omap_mux_init_gpio(GPIO_TOUCH_IRQ,
			   OMAP_PIN_INPUT_PULLUP);
	gpio_request(GPIO_TOUCH_EN, "tsp_en");
	gpio_direction_output(GPIO_TOUCH_EN, 1);
	omap_mux_init_gpio(GPIO_TOUCH_EN, OMAP_PIN_OUTPUT);
	gpio_request(GPIO_TOUCH_SCL, "ap_i2c3_scl");
	gpio_request(GPIO_TOUCH_SDA, "ap_i2c3_sda");

	i2c_register_board_info(3, tuna_i2c3_boardinfo_final,
		ARRAY_SIZE(tuna_i2c3_boardinfo_final));

}
