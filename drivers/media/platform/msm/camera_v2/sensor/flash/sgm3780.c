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
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/proc_fs.h>
#include "msm_camera_io_util.h"
#include "msm_led_flash.h"
#include "../msm_sensor.h"
#include <linux/delay.h>

#include <soc/qcom/socinfo.h>

#define FLASH_NAME "sub-led-flash"

#define CONFIG_MSMB_CAMERA_DEBUG
#undef CDBG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) printk(fmt, ##args)//pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

struct sgm3780_flash_data {
	int flash_enf;
	int flash_enm;
	int flash_led_mos;//chenglong1 for sisley
};

//chenglong1 for sisley
#define FLASH_LED_MOS_ENABLE GPIO_OUT_LOW
#define FLASH_LED_MOS_DISABLE GPIO_OUT_LOW


static struct msm_led_flash_ctrl_t fctrl;

static int32_t sgm3780_flash_get_subdev_id(struct msm_led_flash_ctrl_t *fctrl,
	void *arg)
{
	uint32_t *subdev_id = (uint32_t *)arg;
	if (!subdev_id) {
		pr_err("%s:%d failed\n", __func__, __LINE__);
		return -EINVAL;
	}
	*subdev_id = fctrl->pdev->id;
	CDBG("%s:%d subdev_id %d\n", __func__, __LINE__, *subdev_id);
	return 0;
}

static ssize_t proc_flash_led_write (struct file *file, const char __user *buf, size_t nbytes, loff_t *ppos)
{
    	char string[nbytes];
	int i;
	struct sgm3780_flash_data *flash_led = (struct sgm3780_flash_data *)fctrl.data;
    	sscanf(buf, "%s", string);
    	if (!strcmp((const char *)string, (const char *)"on"))
    	{
        	CDBG("flash_led on called\n");

#ifdef CONFIG_MACH_SISLEYR
		gpio_set_value_cansleep(
			flash_led->flash_enf,
			GPIO_OUT_HIGH);
		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_LOW);
		gpio_set_value_cansleep(
			flash_led->flash_led_mos,
			FLASH_LED_MOS_ENABLE);//chenglong1 for sisley
#else
		for(i=0;i<16;i++)
		{
			gpio_set_value_cansleep(
				flash_led->flash_enf,
				GPIO_OUT_LOW);
			usleep(200);
			gpio_set_value_cansleep(
				flash_led->flash_enf,
				GPIO_OUT_HIGH);
			CDBG("%s lite%d\n", __func__, i);
		}
		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_LOW);
#endif
	}
    	else if (!strcmp((const char *)string, (const char *)"off"))
    	{
        	CDBG("flash_led off called\n");
        	gpio_set_value_cansleep(
			flash_led->flash_enf,
			GPIO_OUT_LOW);

		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_LOW);
#ifdef CONFIG_MACH_SISLEYR
		gpio_set_value_cansleep(
			flash_led->flash_led_mos,
			FLASH_LED_MOS_DISABLE);//chenglong1 for sisley
#endif
    	}
    	return nbytes;
}
EXPORT_SYMBOL(proc_flash_led_write);

static int32_t sgm3780_flash_led_config(struct msm_led_flash_ctrl_t *fctrl,
	void *data)
{
	int rc = 0,i;
	struct msm_camera_led_cfg_t *cfg = (struct msm_camera_led_cfg_t *)data;
	struct sgm3780_flash_data *flash_led = (struct sgm3780_flash_data *)fctrl->data;
	CDBG("%s called led_state %d\n", __func__, cfg->cfgtype);

	if (!fctrl) {
		pr_err("failed\n");
		return -EINVAL;
	}

	switch (cfg->cfgtype) {
	case MSM_CAMERA_LED_LOW:
		gpio_set_value_cansleep(
			flash_led->flash_enf,
			GPIO_OUT_LOW);

		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_HIGH);
#ifdef CONFIG_MACH_SISLEYR
		gpio_set_value_cansleep(
			flash_led->flash_led_mos,
			FLASH_LED_MOS_ENABLE);//chenglong1 for sisley
#endif
		break;

	case MSM_CAMERA_LED_HIGH:
#ifdef CONFIG_MACH_SISLEYR
		gpio_set_value_cansleep(
			flash_led->flash_enf,
			GPIO_OUT_HIGH);
		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_LOW);
		gpio_set_value_cansleep(
			flash_led->flash_led_mos,
			FLASH_LED_MOS_ENABLE);//chenglong1 for sisley
#else
		for(i=0;i<16;i++)
		{
			gpio_set_value_cansleep(
				flash_led->flash_enf,
				GPIO_OUT_LOW);
			usleep(50);
			gpio_set_value_cansleep(
				flash_led->flash_enf,
				GPIO_OUT_HIGH);
			CDBG("%s lite%d\n", __func__, i);
		}
		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_LOW);
#endif
		break;

	case MSM_CAMERA_LED_OFF:
	case MSM_CAMERA_LED_INIT:
	case MSM_CAMERA_LED_RELEASE:
		gpio_set_value_cansleep(
			flash_led->flash_enf,
			GPIO_OUT_LOW);

		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_LOW);
#ifdef CONFIG_MACH_SISLEYR
		gpio_set_value_cansleep(
			flash_led->flash_led_mos,
			FLASH_LED_MOS_DISABLE);//chenglong1 for sisley
#endif
		break;
	default:
		rc = -EFAULT;
		break;
	}
	CDBG("flash_set_led_state: return %d\n", rc);
	return rc;
}
static void msm_led_torch_brightness_set(struct led_classdev *led_cdev,
                                enum led_brightness value)
{
	struct sgm3780_flash_data *flash_led = (struct sgm3780_flash_data *)fctrl.data;
        if (value > LED_OFF) {
		pr_err("%s torch low\n", __func__);
		gpio_set_value_cansleep(
			flash_led->flash_enf,
			GPIO_OUT_LOW);

		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_HIGH);
        } else {
		pr_err("%s torch off\n", __func__);
		gpio_set_value_cansleep(
			flash_led->flash_enf,
			GPIO_OUT_LOW);

		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_LOW);
        }
};


static void msm_sub_flash_brightness_set(struct led_classdev *led_cdev,
                                enum led_brightness value)
{
	struct sgm3780_flash_data *flash_led = (struct sgm3780_flash_data *)fctrl.data;
        if (value > LED_OFF) {
		pr_err("%s sub flash on\n", __func__);
		gpio_set_value_cansleep(
			flash_led->flash_enf,
			GPIO_OUT_LOW);

		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_HIGH);
                gpio_set_value_cansleep(
			flash_led->flash_led_mos,
			GPIO_OUT_LOW);
        } else {
		pr_err("%s sub flash off\n", __func__);
		gpio_set_value_cansleep(
			flash_led->flash_enf,
			GPIO_OUT_LOW);

		gpio_set_value_cansleep(
			flash_led->flash_enm,
			GPIO_OUT_LOW);
                gpio_set_value_cansleep(
			flash_led->flash_led_mos,
			GPIO_OUT_LOW);
        }
};


static struct led_classdev msm_torch_led = {
        .name                   = "torch-light",
        .brightness_set = msm_led_torch_brightness_set,
        .brightness             = LED_OFF,
};

static struct led_classdev msm_sub_flash_led = {
        .name                   = "sub-flash",
        .brightness_set = msm_sub_flash_brightness_set,
        .brightness             = LED_OFF,
};



static int32_t msm_sgm3780_torch_create_classdev(struct device *dev ,
                                void *data)
{
        int rc;
        msm_led_torch_brightness_set(&msm_torch_led, LED_OFF);
        rc = led_classdev_register(dev, &msm_torch_led);
        if (rc) {
                pr_err("Failed to register led dev. rc = %d\n", rc);
                return rc;
        }

        return 0;
};

static int32_t msm_sgm3780_subflash_create_classdev(struct device *dev ,
                                void *data)

{
        int rc;
        msm_sub_flash_brightness_set(&msm_sub_flash_led, LED_OFF);
        rc = led_classdev_register(dev, &msm_sub_flash_led);
        if (rc) {
                pr_err("Failed to register led dev. rc = %d\n", rc);
                return rc;
        }

        return 0;
};





static const struct of_device_id sgm3780_platform_dt_match[] = {
	{.compatible = "qcom,sub-led-flash"},
	{}
};

MODULE_DEVICE_TABLE(of, sgm3780_platform_dt_match);

static struct platform_driver sgm3780_platform_driver = {
	.driver = {
		.name = FLASH_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sgm3780_platform_dt_match,
	},
};

static const struct file_operations proc_flash_led_operations = {
	.owner	= THIS_MODULE,
	.write	= proc_flash_led_write,
};

static int msm_flash_sgm3780_platform_probe(struct platform_device *pdev)
{
	int rc = 0;
	int i;
	struct device_node *of_node = pdev->dev.of_node;
	struct gpio *gpio_tbl = NULL;
	struct sgm3780_flash_data *flash_led = NULL;
	uint16_t *gpio_array = NULL;
	uint16_t gpio_array_size = 0;
	uint32_t *val_array = NULL;

	struct proc_dir_entry * rcdir;
	/*lenovo-sw zhangjiano modify for flash 2014.09.22 begin*/
#ifdef CONFIG_MACH_SISLEYR
	rcdir = proc_create_data("CTP_FLASH_CTRL", S_IFREG | S_IWUGO | S_IWUSR, NULL, &proc_flash_led_operations, NULL);
        if(rcdir == NULL)
        {
		CDBG("proc_create_data fail\n");
	}
#else
	rcdir = proc_create_data("CTP_FLASH_CTRL1", S_IFREG | S_IWUGO | S_IWUSR, NULL, &proc_flash_led_operations, NULL);
        if(rcdir == NULL)
	{
		CDBG("proc_create_data fail\n");
	}
#endif
	/*lenovo-sw zhangjiano modify for flash 2014.09.22 end*/
	CDBG("%s:%d Enter\n", __func__, __LINE__);
	if (!of_node) {
		pr_err("of_node NULL\n");
		return -EFAULT;
	}

	rc = of_property_read_u32(of_node, "cell-index", &pdev->id);
	if (rc < 0) {
		CDBG("%s failed %d\n", __func__, __LINE__);
		return -EFAULT;
	}

	gpio_array_size = of_gpio_count(of_node);
        CDBG("%s gpio count %d\n", __func__, gpio_array_size);

	gpio_tbl = kzalloc(sizeof(struct gpio) * gpio_array_size,
               	GFP_KERNEL);
	if (!gpio_tbl) {
               	CDBG("%s failed %d\n", __func__, __LINE__);
		return -ENOMEM;
	}

	fctrl.pdev = pdev;

	flash_led = devm_kzalloc(&pdev->dev, sizeof(struct sgm3780_flash_data),
		GFP_KERNEL);
	if (!flash_led) {
		CDBG("%s:%d Unable to allocate memory\n",
			__func__, __LINE__);
               	rc = -ENOMEM;
                goto ERROR1;
	}

        if (gpio_array_size) {

                gpio_array = kzalloc(sizeof(uint16_t) * gpio_array_size,
                        GFP_KERNEL);
                if (!gpio_array) {
                        pr_err("%s failed %d\n", __func__, __LINE__);
                        rc = -ENOMEM;
                        goto ERROR2;
                }

                for (i = 0; i < gpio_array_size; i++) {
                        gpio_array[i] = of_get_gpio(of_node, i);
                        CDBG("%s gpio_array[%d] = %d\n", __func__, i,
                                 gpio_array[i]);
                }


		val_array = kzalloc(sizeof(uint32_t) * gpio_array_size, GFP_KERNEL);
		if (!val_array) {
                	pr_err("%s failed %d\n", __func__, __LINE__);
                	return -ENOMEM;
                        goto ERROR3;
        	}

		rc = of_property_read_u32_array(of_node, "qcom,gpio-req-tbl-num",
                	val_array, gpio_array_size);
        	if (rc < 0) {
                	pr_err("%s failed %d\n", __func__, __LINE__);
                	goto ERROR3;
        	}

                for (i = 0; i < gpio_array_size; i++) {
                	if (val_array[i] >= gpio_array_size) {
                        	pr_err("%s gpio req tbl index %d invalid\n",
                                	__func__, val_array[i]);
                		rc = -ENOMEM;
                		goto ERROR3;
                	}
                	gpio_tbl[i].gpio = gpio_array[val_array[i]];
                	CDBG("%s gpio_tbl[%d].gpio = %d ,val_array[%d] = %d\n", __func__, i,
                        	gpio_tbl[i].gpio, i, val_array[i]);
        	}

		rc = of_property_read_u32_array(of_node, "qcom,gpio-req-tbl-flags",
                	val_array, gpio_array_size);
        	if (rc < 0) {
                	pr_err("%s failed %d\n", __func__, __LINE__);
                	goto ERROR3;
        	}
                for (i = 0; i < gpio_array_size; i++) {
                	gpio_tbl[i].flags = val_array[i];
                	CDBG("%s gpio_tbl[%d].flags = %ld\n", __func__, i,
                        	gpio_tbl[i].flags);
        	}

                for (i = 0; i < gpio_array_size; i++) {
                	rc = of_property_read_string_index(of_node,
                        	"qcom,gpio-req-tbl-label", i,
                        	&gpio_tbl[i].label);
                	CDBG("%s gpio_tbl[%d].label = %s\n", __func__, i,
                        	gpio_tbl[i].label);
                	if (rc < 0) {
                        	pr_err("%s failed %d\n", __func__, __LINE__);
                        	goto ERROR3;
                	}
        	}
		kfree(val_array);
	}

	rc = msm_camera_request_gpio_table(
                gpio_tbl,
                gpio_array_size, 1);
	if (rc) {
                CDBG("%s: fail to request gpio\n", __func__);
        }

	flash_led->flash_enf = gpio_tbl[0].gpio;
	flash_led->flash_enm = gpio_tbl[1].gpio;

#ifdef CONFIG_MACH_SISLEYR
	flash_led->flash_led_mos = gpio_tbl[2].gpio;//chenglong1 for sisley
        CDBG("%s enf %d, enm %d, led_mos %d \n", __func__, flash_led->flash_enf, flash_led->flash_enm,
			                                       flash_led->flash_led_mos);
#endif
	gpio_set_value_cansleep(flash_led->flash_enf,
		GPIO_OUT_LOW);
	gpio_set_value_cansleep(flash_led->flash_enm,
		GPIO_OUT_LOW);

	fctrl.data = flash_led;

	rc = msm_led_flash_create_v4lsubdev(pdev, &fctrl);
	if(rc){
		dev_err(&pdev->dev,
			"failed to create sub dev");
		goto ERROR4;
	}
 	msm_sgm3780_torch_create_classdev(&pdev->dev, NULL);

#ifdef CONFIG_MACH_SISLEYR
	printk("enter sisley create sub flash dev\n");
	msm_sgm3780_subflash_create_classdev(&pdev->dev, NULL);
#endif

	kfree(gpio_array);
	kfree(gpio_tbl);
	CDBG("%s:%d Exit\n", __func__, __LINE__);
	return 0;
ERROR4:
	kfree(val_array);
ERROR3:
	kfree(gpio_array);
ERROR2:
	devm_kfree(&pdev->dev, flash_led);
ERROR1:
	kfree(gpio_tbl);
	return rc;
}

static int __init msm_flash_sgm3780_init_module(void)
{
	CDBG("%s:%d Enter\n", __func__, __LINE__);
	return platform_driver_probe(&sgm3780_platform_driver,
		msm_flash_sgm3780_platform_probe);
}


static struct msm_flash_fn_t sgm3780_func_tbl = {
	.flash_get_subdev_id = sgm3780_flash_get_subdev_id,
	.flash_led_config = sgm3780_flash_led_config,
};

static struct msm_led_flash_ctrl_t fctrl = {
	.func_tbl = &sgm3780_func_tbl,
};

module_init(msm_flash_sgm3780_init_module);
MODULE_DESCRIPTION("sgm3780 FLASH");
MODULE_LICENSE("GPL v2");
