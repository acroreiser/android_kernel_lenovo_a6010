/*
 * Touchscreen common interface
 * add by Lenovo-sw wengjun1
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include "tpd_property.h"

static int tpd_suspend_status_flag = 0;

#ifdef CONFIG_PRODUCT_Z2
void synaptics_set_gesture_ctrl(unsigned int val);
int get_array_flag(void);
#endif

void set_tpd_suspend_status(int mode)
{
	tpd_suspend_status_flag = mode;
#ifdef CONFIG_PRODUCT_Z2
	synaptics_set_gesture_ctrl(mode);
#endif
}
EXPORT_SYMBOL(set_tpd_suspend_status);

int get_tpd_suspend_status(void)
{
	return tpd_suspend_status_flag;
}
EXPORT_SYMBOL(get_tpd_suspend_status);

static ssize_t lenovo_tpd_suspend_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,"%d.\n", get_tpd_suspend_status());
}

#ifdef CONFIG_PRODUCT_Z2
static ssize_t lenovo_tpd_info_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{

    if(!have_correct_setting)
        return sprintf(buf,"[wj]Have not right setting.\n");
    else
        return sprintf(buf,"name=%s; types=%d; fw_num=0x%08x;\n", tpd_info_t->name, tpd_info_t->types, tpd_info_t->fw_num);
}

static ssize_t lenovo_flag_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf,"%d\n", get_array_flag());
}
#endif

static ssize_t lenovo_tpd_suspend_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size)
{
	int mode;
	int res = sscanf(buf, "%d", &mode);
    printk("[wj]mode = %d, res = %d.\n", mode, res);
	if (res != 1)
	{
		printk("%s: [wj]expect 1 numbers\n", __FUNCTION__);
        return -EINVAL;
	}
	else
	{
		set_tpd_suspend_status(mode);
	}
    return res;
}

static struct kobj_attribute lenovo_tpd_suspend_attr = {
    .attr = {
        .name = "tpd_suspend_status",
        .mode = S_IRUGO | S_IWUSR,
    },
    .show = &lenovo_tpd_suspend_show,
	.store = &lenovo_tpd_suspend_store,
};

#ifdef CONFIG_PRODUCT_Z2
static struct kobj_attribute lenovo_tpd_info_attr = {
    .attr = {
        .name = "lenovo_tpd_info",
        .mode = S_IRUGO,
    },
    .show = &lenovo_tpd_info_show,
};
static struct kobj_attribute lenovo_tpd_flag_attr = {
    .attr = {
        .name = "lenovo_flag",
        .mode = S_IRUGO,
    },
    .show = &lenovo_flag_show,
};
#endif

static struct attribute *tpd_properties_attrs[] = {
	&lenovo_tpd_suspend_attr.attr,
#ifdef CONFIG_PRODUCT_Z2
    &lenovo_tpd_flag_attr.attr,
    &lenovo_tpd_info_attr.attr,
#endif
    NULL
};

static struct attribute_group tpd_properties_attr_group = {
    .attrs = tpd_properties_attrs,
};

struct kobject *properties_kobj;

void property_init(void) {
    int ret = 0;
    properties_kobj = kobject_create_and_add("board_properties", NULL);
    if(properties_kobj)
        ret = sysfs_create_group(properties_kobj,&tpd_properties_attr_group);
    if(!properties_kobj || ret)
    printk("failed to create board_properties\n");
}
