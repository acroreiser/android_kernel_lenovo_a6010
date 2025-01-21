/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/export.h>
#include "msm_led_flash.h"

#define FLASH_NAME "qcom,led-flash"

#define CONFIG_MSMB_CAMERA_DEBUG
#undef CDBG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

static struct msm_led_flash_ctrl_t fctrl;
static struct i2c_driver ds8540_i2c_driver;
/*//800ma
static struct msm_camera_i2c_reg_array ds8540_init_array[] = {
	{0x00, 0x1E},//75x2+150
	{0x05, 0x68},//turn off led2
	{0x01, 0x90},//400//{0x01, 0x92},//{0x01, 0x94},//{0x01, 0xBF},//{0x01, 0x9A}, //225x2+450
	{0x02, 0x88},////200x2//{0x02, 0x89},//{0x02, 0x8A},//{0x02, 0x9F},//{0x02, 0x8D},
	//{0x03, 0xC0},
	{0x03, 0xE0},
	{0x04, 0x10},//for high current
	{0x05, 0x6F},//enable led2
};
*/
//700ma
static struct msm_camera_i2c_reg_array ds8540_init_array[] = {
	{0x00, 0x1E},//75x2+150
	{0x05, 0x68},//turn off led2
    {0x01, 0x8E},//350
	{0x02, 0x87},////175x2//{0x02, 0x89},//{0x02, 0x8A},//{0x02, 0x9F},//{0x02, 0x8D},
	//{0x03, 0xC0},
	{0x03, 0xE0},
	{0x04, 0x10},//for high current
	{0x05, 0x6F},//enable led2
};

static struct msm_camera_i2c_reg_array ds8540_off_array[] = {
	//{0x0f, 0x00},
	{0x04, 0x00},

};

static struct msm_camera_i2c_reg_array ds8540_release_array[] = {
	//{0x0f, 0x00},
	{0x04, 0x00},
};

static struct msm_camera_i2c_reg_array ds8540_low_array[] = {
	{0x04, 0x00},
};

static struct msm_camera_i2c_reg_array ds8540_high_array[] = {
	{0x04, 0x00},
};

static void __exit msm_flash_ds8540_i2c_remove(void)
{
	i2c_del_driver(&ds8540_i2c_driver);
	return;
}

static const struct of_device_id ds8540_trigger_dt_match[] = {
	{.compatible = "qcom,led-flash", .data = &fctrl},
	{}
};

MODULE_DEVICE_TABLE(of, ds8540_trigger_dt_match);

static const struct i2c_device_id flash_i2c_id[] = {
	{"qcom,led-flash", (kernel_ulong_t)&fctrl},
	{ }
};

static const struct i2c_device_id ds8540_i2c_id[] = {
	{FLASH_NAME, (kernel_ulong_t)&fctrl},
	{ }
};

static int msm_flash_ds8540_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	if (!id) {
		pr_err("msm_flash_ds8540_i2c_probe: id is NULL");
		id = ds8540_i2c_id;
	}

	return msm_flash_i2c_probe(client, id);
}

static struct i2c_driver ds8540_i2c_driver = {
	.id_table = ds8540_i2c_id,
	.probe  = msm_flash_ds8540_i2c_probe,
	.remove = __exit_p(msm_flash_ds8540_i2c_remove),
	.driver = {
		.name = FLASH_NAME,
		.owner = THIS_MODULE,
		.of_match_table = ds8540_trigger_dt_match,
	},
};

static int msm_flash_ds8540_platform_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	match = of_match_device(ds8540_trigger_dt_match, &pdev->dev);
	if (!match)
		return -EFAULT;
	return msm_flash_probe(pdev, match->data);
}

static struct platform_driver ds8540_platform_driver = {
	.probe = msm_flash_ds8540_platform_probe,
	.driver = {
		.name = "qcom,led-flash",
		.owner = THIS_MODULE,
		.of_match_table = ds8540_trigger_dt_match,
	},
};

static int __init msm_flash_ds8540_init_module(void)
{
	int32_t rc = 0;
	CDBG("%s:%d rc %d   1\n", __func__, __LINE__, rc);

	rc = platform_driver_register(&ds8540_platform_driver);
	if (!rc)
		return rc;
	CDBG("%s:%d rc %d\n", __func__, __LINE__, rc);
	return i2c_add_driver(&ds8540_i2c_driver);
}

static void __exit msm_flash_ds8540_exit_module(void)
{
	if (fctrl.pdev)
		platform_driver_unregister(&ds8540_platform_driver);
	else
		i2c_del_driver(&ds8540_i2c_driver);
}

static struct msm_camera_i2c_client ds8540_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
};

static struct msm_camera_i2c_reg_setting ds8540_init_setting = {
	.reg_setting = ds8540_init_array,
	.size = ARRAY_SIZE(ds8540_init_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting ds8540_off_setting = {
	.reg_setting = ds8540_off_array,
	.size = ARRAY_SIZE(ds8540_off_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting ds8540_release_setting = {
	.reg_setting = ds8540_release_array,
	.size = ARRAY_SIZE(ds8540_release_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting ds8540_low_setting = {
	.reg_setting = ds8540_low_array,
	.size = ARRAY_SIZE(ds8540_low_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting ds8540_high_setting = {
	.reg_setting = ds8540_high_array,
	.size = ARRAY_SIZE(ds8540_high_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_led_flash_reg_t ds8540_regs = {
	.init_setting = &ds8540_init_setting,
	.off_setting = &ds8540_off_setting,
	.low_setting = &ds8540_low_setting,
	.high_setting = &ds8540_high_setting,
	.release_setting = &ds8540_release_setting,
};

static struct msm_flash_fn_t ds8540_func_tbl = {
	.flash_get_subdev_id = msm_led_i2c_trigger_get_subdev_id,
	.flash_led_config = msm_led_i2c_trigger_config,
	.flash_led_init = msm_flash_led_init,
	.flash_led_release = msm_flash_led_release,
	.flash_led_off = msm_flash_led_off,
	.flash_led_low = msm_flash_led_low,
	.flash_led_high = msm_flash_led_high,
};

static struct msm_led_flash_ctrl_t fctrl = {
	.flash_i2c_client = &ds8540_i2c_client,
	.reg_setting = &ds8540_regs,
	.func_tbl = &ds8540_func_tbl,
};

/*subsys_initcall(msm_flash_i2c_add_driver);*/
module_init(msm_flash_ds8540_init_module);
module_exit(msm_flash_ds8540_exit_module);
MODULE_DESCRIPTION("ds8540 FLASH");
MODULE_LICENSE("GPL v2");
