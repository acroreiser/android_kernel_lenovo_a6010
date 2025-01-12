#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/switch.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

struct hall_switch_data {
	struct platform_device *pdev;
	struct switch_dev sdev;
	struct regulator *vdd;
	struct pinctrl *hall_pinctrl;
	struct pinctrl_state *hall_pinctrl_on;
	struct pinctrl_state *hall_pinctrl_off;
	int gpio;
	int irq;
};

#define DEBUG_HALL 1

static irqreturn_t hall_switch_threaded_irq(int irq, void *data)
{
	struct hall_switch_data *hall_data = (struct hall_switch_data *)data;
	int state = gpio_get_value(hall_data->gpio);

	if (DEBUG_HALL)
		pr_info("%s: state=%d, old_state=%d\n"
			,__func__, state, hall_data->sdev.state);
	switch_set_state(&hall_data->sdev, !state);

	if (state)
		irq_set_irq_type(hall_data->irq, IRQF_TRIGGER_LOW | IRQF_ONESHOT);
	else
		irq_set_irq_type(hall_data->irq, IRQF_TRIGGER_HIGH | IRQF_ONESHOT);

	return IRQ_HANDLED;
}

static int hall_switch_show(struct seq_file *m, void *v)
{
	struct hall_switch_data *hall_data = (struct hall_switch_data *)m->private;
	int state = gpio_get_value(hall_data->gpio);

	seq_printf(m, "%d\n",state);

	return 0;
}

static int hall_switch_open(struct inode *inode, struct file *file)
{
	return single_open(file, hall_switch_show, PDE_DATA(inode));
}

static const struct file_operations hall_switch_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= hall_switch_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#if 1
static int hall_regulator_conf(struct hall_switch_data *hall_data, int on)
{
	int ret;

	if (on) {
		hall_data->vdd = regulator_get(&hall_data->pdev->dev, "vdd");
		if (IS_ERR(hall_data->vdd)) {
			pr_err("%s: Failed to get vdd regulator\n", __func__);
			return -1;
		}

		if (regulator_count_voltages(hall_data->vdd) > 0) {
			ret = regulator_set_voltage(hall_data->vdd, 1800000, 1800000);
			if (ret) {
				regulator_put(hall_data->vdd);
				pr_err( "%s: Failed to set regulator voltage %d\n", __func__, ret);
				return -1;
			}
		} else {
			pr_err("%s: regulator count voltage err\n", __func__);
		}
	} else {
		if (regulator_count_voltages(hall_data->vdd) > 0)
			regulator_set_voltage(hall_data->vdd, 0, 1800000);
		regulator_put(hall_data->vdd);
	}

	pr_info("%s: regulator config %s ok\n", __func__, on?"on":"off");
	return 0;
}
#endif

static int hall_gpio_conf(struct hall_switch_data *hall_data)
{
	int ret = 0;

	ret = of_get_named_gpio(hall_data->pdev->dev.of_node, "lenovo,irq-gpio", 0);
	if (ret < 0 || !gpio_is_valid(ret)) {
		pr_err("%s:Failed to get gpio %d!!!\n", __func__, ret);
		return -1;
	}
	hall_data->gpio = ret;

	ret = gpio_request(hall_data->gpio, hall_data->pdev->name);
	if (ret < 0) {
		pr_err("%s:Failed to request gpio %d!!!\n", __func__, ret);
		return -1;
	}
	
	ret = gpio_direction_input(hall_data->gpio);
	if (ret < 0) {
		pr_err("%s:Failed to config gpio %d!!!\n", __func__, ret);
		gpio_free(hall_data->gpio);
		return -1;
	}

	pr_info("%s:gpio config ok %d!!!\n", __func__, hall_data->gpio);
	return 0;
}

static int hall_pinctrl_conf(struct hall_switch_data *hall_data, int on)
{
	struct pinctrl_state *pins_state;
	int ret = 0;

	if (!hall_data->hall_pinctrl) {
		hall_data->hall_pinctrl = devm_pinctrl_get(&hall_data->pdev->dev);
		if (IS_ERR_OR_NULL(hall_data->hall_pinctrl)) {
			pr_err("%s: Failed to get pinctrl %ld!!!\n", __func__, PTR_ERR(hall_data->hall_pinctrl));
			return -1;
		}

		hall_data->hall_pinctrl_on = pinctrl_lookup_state(hall_data->hall_pinctrl, "hall_pinctrl_on");
		if (IS_ERR_OR_NULL(hall_data->hall_pinctrl_on)) {
			hall_data->hall_pinctrl = NULL;
			pr_err("%s: Failed to get pinctrl state on %ld!!!\n", __func__, PTR_ERR(hall_data->hall_pinctrl_on));
			return -1;
		}

		hall_data->hall_pinctrl_off = pinctrl_lookup_state(hall_data->hall_pinctrl, "hall_pinctrl_off");
		if (IS_ERR_OR_NULL(hall_data->hall_pinctrl_off)) {
			hall_data->hall_pinctrl = NULL;
			pr_err("%s:Failed to get pinctrl state off %ld!!!\n", __func__, PTR_ERR(hall_data->hall_pinctrl_off));
			return -1;
		}
	}

	pins_state = on ? hall_data->hall_pinctrl_on: hall_data->hall_pinctrl_off;
	ret = pinctrl_select_state(hall_data->hall_pinctrl, pins_state);
	if (ret) {
		pr_err("%s: Failed to select pinctrl state %d!!!\n", __func__, ret);
		return -1;
	}

	pr_info("%s:  pinctrl config %s ok\n", __func__, on?"on":"off");
	return 0;
}

static int hall_switch_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct hall_switch_data *hall_data;

	pr_info("%s start\n", __func__);

	if (!pdev->dev.of_node) {
		pr_err("%s:no of_node!!!\n", __func__);
		return -EINVAL;
	}

	hall_data = kzalloc(sizeof(struct hall_switch_data), GFP_KERNEL);
	if (!hall_data)
		return -ENOMEM;

	hall_data->pdev = pdev;
	hall_data->sdev.name = "hall";
	hall_data->sdev.index = 0;
	hall_data->sdev.state = 0;

	ret = switch_dev_register(&hall_data->sdev);
	if(ret < 0) {
		pr_err("%s:Failed to register switch device %d!!!\n", __func__, ret);
		goto err_switch_dev_register;
	}

#if 1
	ret = hall_regulator_conf(hall_data, 1);
	if(ret < 0) {
		goto err_config_regulator;
	}
#endif

	ret = hall_gpio_conf(hall_data);
	if(ret < 0) {
		goto err_config_gpio;
	}
	// initial state of switch_dev should be determined by gpio value
	switch_set_state(&hall_data->sdev, !gpio_get_value(hall_data->gpio));

	ret = hall_pinctrl_conf(hall_data, 1);
	if(ret < 0) {
		goto err_config_pinctrl;
	}

	hall_data->irq = gpio_to_irq(hall_data->gpio);

	platform_set_drvdata(pdev, hall_data);

	ret = request_threaded_irq(hall_data->irq, NULL, hall_switch_threaded_irq
		, (gpio_get_value(hall_data->gpio) ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH) | IRQF_ONESHOT
		, pdev->name, hall_data);
	if (ret < 0) {
		pr_err("%s:Failed to request irq %d!!!\n", __func__, ret);
		goto err_request_irq;
	}

	proc_create_data("driver/hall", 0444, NULL, &hall_switch_proc_fops, hall_data);

	enable_irq_wake(hall_data->irq);

	pr_info("%s end. irq=%d, dev_state=%d\n", __func__, hall_data->irq, hall_data->sdev.state);
	return 0;

err_request_irq:
	hall_pinctrl_conf(hall_data, 0);
err_config_pinctrl:
	gpio_free(hall_data->gpio);
err_config_gpio:
	hall_regulator_conf(hall_data, 0);
err_config_regulator:
	switch_dev_unregister(&hall_data->sdev);
err_switch_dev_register:
	kfree(hall_data);

	return ret;
}

static int hall_switch_remove(struct platform_device *pdev)
{
	struct hall_switch_data *hall_data = platform_get_drvdata(pdev);

	pr_info("%s\n", __func__);
	free_irq(hall_data->irq, hall_data);
	if (gpio_is_valid(hall_data->gpio));
		gpio_free(hall_data->gpio);
	if (hall_data->hall_pinctrl)
		hall_pinctrl_conf(hall_data, 0);
	hall_regulator_conf(hall_data, 0);
	switch_dev_unregister(&hall_data->sdev);
	kfree(hall_data);

	return 0;
}

static struct of_device_id hall_dt_match[] = {
	{.compatible = "lenovo,hall",},
	{},
};

static struct platform_driver hall_switch_driver = {
	.probe		= hall_switch_probe,
	.remove		= hall_switch_remove,
	.driver		= {
		.name	= "switch-hall",
		.owner	= THIS_MODULE,
		.of_match_table = hall_dt_match,
	},
};

static int __init hall_switch_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&hall_switch_driver);
	pr_info("%s:  ret=%d\n", __func__,ret);
	return ret;
}

static void __exit hall_switch_exit(void)
{
	pr_info("%s\n", __func__);
	platform_driver_unregister(&hall_switch_driver);
}

module_init(hall_switch_init);
module_exit(hall_switch_exit);

MODULE_AUTHOR("renxm1 <renxm1@lenovo.com>");
MODULE_DESCRIPTION("HALL Switch driver");
MODULE_LICENSE("GPL");

