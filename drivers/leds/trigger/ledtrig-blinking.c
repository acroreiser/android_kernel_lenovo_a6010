/*
 * LED Blinking Trigger
 *
 * Copyright (C) 2019 Yaroslav Zviezda <acroreiser@gmail.com>
 *
 * Based on Richard Purdie's ledtrig-timer.c and Atsushi Nemoto's ledtrig-heartbeat.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/leds.h>
#include <linux/reboot.h>
#include "../leds.h"

struct blinking_trig_data {
	unsigned int phase;
	struct timer_list timer;
};

static void led_blinking_function(unsigned long data)
{
	struct led_classdev *led_cdev = (struct led_classdev *) data;
	struct blinking_trig_data *blinking_data = led_cdev->trigger_data;
	unsigned long brightness = LED_OFF;

	if(blinking_data->phase == 0)
	{
		brightness = led_cdev->max_brightness;
		blinking_data->phase = 1;
	}
	else
		blinking_data->phase = 0;

	__led_set_brightness(led_cdev, brightness);
	mod_timer(&blinking_data->timer, jiffies + msecs_to_jiffies(750));
}

static void blinking_trig_activate(struct led_classdev *led_cdev)
{
	struct blinking_trig_data *blinking_data;

	blinking_data = kzalloc(sizeof(*blinking_data), GFP_KERNEL);
	if (!blinking_data)
		return;

	led_cdev->trigger_data = blinking_data;
	setup_timer(&blinking_data->timer,
		    led_blinking_function, (unsigned long) led_cdev);
	blinking_data->phase = 0;
	led_blinking_function(blinking_data->timer.data);
	led_cdev->activated = true;
}

static void blinking_trig_deactivate(struct led_classdev *led_cdev)
{
	struct blinking_trig_data *blinking_data = led_cdev->trigger_data;

	if (led_cdev->activated) {
		del_timer_sync(&blinking_data->timer);
		kfree(blinking_data);
		led_cdev->activated = false;
	}
}

static struct led_trigger blinking_led_trigger = {
	.name     = "blinking",
	.activate = blinking_trig_activate,
	.deactivate = blinking_trig_deactivate,
};

static int __init blinking_trig_init(void)
{
	int rc = led_trigger_register(&blinking_led_trigger);

	return rc;
}

static void __exit blinking_trig_exit(void)
{
	led_trigger_unregister(&blinking_led_trigger);
}

module_init(blinking_trig_init);
module_exit(blinking_trig_exit);

MODULE_AUTHOR("Yaroslav Zviezda <acroreiser@gmail.com>");
MODULE_DESCRIPTION("Blinking (based on Heartbeat) LED trigger");
MODULE_LICENSE("GPL");