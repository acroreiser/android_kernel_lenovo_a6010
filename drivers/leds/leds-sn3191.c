/*
 * drivers/leds/sn3191_leds.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 * sn3191 leds driver
 *
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/fb.h>
#include <linux/debugfs.h>


/****************************************************************************
 * defined
 ***************************************************************************/
#define LED_NAME "rgbled"
#define I2C_MASTER_CLOCK       400

#define SN3191_EXTEND_OFFSET_BIT 8
/*00h*/
#define SN3191_SSD_OFFSET_BIT 0 //software shut down bit; 1:software shut down; 0:working mode
#define SN3191_OUT_EN_OFFSET_BIT 5 //out enable bit; 1:enable out; 0:disable out;

/*01h*/
#define SN3191_HT_OFFSET_BIT 4 //dead time bit; 0:dead on T2; 1:dead on T4;
#define SN3191_RM_OFFSET_BIT 5 //dead mode enable bit; 0:disable; 1:enable;

/*02h*/
#define SN3191_RGB_MODE_OFFSET_BIT 5 //LED mode set; 0:PWM control mode; 1:one programe mode;

/*03h*/
#define SN3191_CS_OFFSET_BIT 2 //current set; 0:42mA; 1:10mA; 2:5mA; 3:30mA; 4:17.5mA;

/*0Ah~0Ch is T0 setting register*/
/*10h~12h is T1 & T2 setting register*/
/*16h~18h is T3 & T4 setting register*/
#define SN3191_T0_OFFSET_BIT 4// 4bit
#define SN3191_T1_OFFSET_BIT 5// 3bit
#define SN3191_T2_OFFSET_BIT 1// 4bit
#define SN3191_T3_OFFSET_BIT 5// 3bit
#define SN3191_T4_OFFSET_BIT 1// 4bit



/*register define*/
#define SN3191_SSD_EN_REG 0x00
#define SN3191_BREATH_MODE_REG 0x01
#define SN3191_LED_MODE_REG 0x02
#define SN3191_CURRENT_SET_REG 0x03
/*04h~06h is PWM level setting register*/
#define SN3191_PWM_BLUE_REG 0x04
#define SN3191_PWM_DATA_REFRESH_REG 0x07
/*0Ahh is T0 setting register*/
#define SN3191_T0_BLUE_REG 0x0A
/*10h is T1 & T2 setting register*/
#define SN3191_T1_T2_BLUE_REG 0x10
/*16h is T3 & T4 setting register*/
#define SN3191_T3_T4_BLUE_REG 0x16


#define SN3191_TIME_REFRESH_REG 0x1C
#define SN3191_LED_OUT_CONTROL_REG 0x1D
#define SN3191_RESET_REG 0x2F

/*register function define*/
#define SN3191_RM_ENABLE 1<<SN3191_RM_OFFSET_BIT
#define SN3191_RM_DISABLE 0<<SN3191_RM_OFFSET_BIT

#define SN3191_RGB_MODE_ENABLE 1<<SN3191_RGB_MODE_OFFSET_BIT
#define SN3191_RGB_MODE_DISABLE 0<<SN3191_RGB_MODE_OFFSET_BIT

#define SN3191_CS_SET_42mA 0<<SN3191_CS_OFFSET_BIT
#define SN3191_CS_SET_10mA 1<<SN3191_CS_OFFSET_BIT
#define SN3191_CS_SET_5mA 2<<SN3191_CS_OFFSET_BIT
#define SN3191_CS_SET_30mA 3<<SN3191_CS_OFFSET_BIT
#define SN3191_CS_SET_17_5mA 4<<SN3191_CS_OFFSET_BIT


#define SN3191_SSD_ENABLE 1<<SN3191_SSD_OFFSET_BIT 
#define SN3191_SSD_DISABLE 0<<SN3191_SSD_OFFSET_BIT //normal work mode

#define SN3191_EN_OUT_CLOSE 0<<<SN3191_OUT_EN_OFFSET_BIT
#define SN3191_EN_OUT_OPEN 1<<SN3191_OUT_EN_OFFSET_BIT

/*backlight value analyze*/
#define SN3191_BL_CUR_OFFSET_BIT 24// 3bit

#define SN3191_BL_R_OFFSET_BIT 16// 8bit
#define SN3191_BL_G_OFFSET_BIT 8// 8bit
#define SN3191_BL_B_OFFSET_BIT 0// 8bit
/*delay_on*/
#define SN3191_BL_BT2_OFFSET_BIT 20
#define SN3191_BL_BT3_OFFSET_BIT 24
#define SN3191_BL_BT1_OFFSET_BIT 27
/*delay_off*/
#define SN3191_BL_BT4_OFFSET_BIT 16
#define SN3191_BL_BT0_OFFSET_BIT 20
/*masks*/
#define SN3191_BL_RGB_MASK 0xff
#define SN3191_BL_T0_MASK 0x0f
#define SN3191_BL_T1_MASK 0x07
#define SN3191_BL_T2_MASK 0x0f
#define SN3191_BL_T3_MASK 0x07
#define SN3191_BL_T4_MASK 0x0f

static const char *cur_text[] = {
	"42mA",
	"10mA",
	"5mA",
	"30mA",
	"17.5mA",
};
static const char *t0_t4_text[] = {
	"0s",
	"0.13s",
	"0.26s",
	"0.52s",
	"1.04s",
	"2.08s",
	"4.16s",
	"8.32s",
	"16.64s",
	"33.28s",
	"66.56s",
};
static const char *t1_t3_text[] = {
	"0.13s",
	"0.26s",
	"0.52s",
	"1.04s",
	"2.08s",
	"4.16s",
	"8.32s",
	"16.64s",
};
static const char *t2_text[] = {
	"0s",
	"0.13s",
	"0.26s",
	"0.52s",
	"1.04s",
	"2.08s",
	"4.16s",
	"8.32s",
	"16.64s",
};



static int sn3191_is_init= 0;
static int debug_enable = 1;

#define LEDS_DEBUG(format, args...) do{ \
	if(debug_enable) \
	{\
		printk(KERN_EMERG format,##args);\
	}\
}while(0)

struct sn3191_leds_priv {
	struct led_classdev cdev;
	struct work_struct work;
	int gpio;
	int level;
	int delay_on;
	int delay_off;
	unsigned int reg;
};


/****************************************************************************
 * local functions
 ***************************************************************************/

static int	sn3191_leds_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
//static int  sn3191_leds_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info);
static int  sn3191_leds_i2c_remove(struct i2c_client *client);


//static int __init sn3191_leds_platform_probe(struct platform_device *pdev);
//static int sn3191_leds_platform_remove(struct platform_device *pdev);
////////////////

#define SN3191_I2C_ADDR 0xD0
struct i2c_client *sn3191_i2c_cilent = NULL;

//static struct i2c_board_info __initdata sn3191_i2c_board_info = { I2C_BOARD_INFO("sn3191", (SN3191_I2C_ADDR >> 1))};


static const struct i2c_device_id sn3191_i2c_id[] = {
	{"sn3191",0},
	{}
};
static struct of_device_id lenovo_match_table[] = {
	{.compatible = "lenovo,sn3191"},
	{}
};

static struct i2c_driver sn3191_i2c_driver = {
    .id_table = sn3191_i2c_id,
    .probe = sn3191_leds_i2c_probe,
    .remove = sn3191_leds_i2c_remove,
    .driver = {
		.owner = THIS_MODULE,
		.name = "sn3191",
		.of_match_table = lenovo_match_table,
	},
};


#if 0
static struct platform_driver sn3191_leds_platform_driver = {
	.driver		= {
		.name	= "leds-sn3191",
		//.owner	= THIS_MODULE,
	},
	.probe		= sn3191_leds_platform_probe,
	.remove		= sn3191_leds_platform_remove,
	//.suspend	= sn3191_leds_platform_suspend,
	//.shutdown   = sn3191_leds_platform_shutdown,
};
#endif
static unsigned int	enable_pin;
struct sn3191_leds_priv *g_sn3191_leds_data[2];
///////////////triger timer
static ssize_t led_delay_on_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_on);
}

static ssize_t led_delay_on_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	unsigned long state;
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	led_blink_set(led_cdev, &state, &led_cdev->blink_delay_off);
	led_cdev->blink_delay_on = state;

	return size;
}

static ssize_t led_delay_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_off);
}

static ssize_t led_delay_off_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	unsigned long state;
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	led_blink_set(led_cdev, &led_cdev->blink_delay_on, &state);
	led_cdev->blink_delay_off = state;

	return size;
}

static DEVICE_ATTR(delay_on, 0644, led_delay_on_show, led_delay_on_store);
static DEVICE_ATTR(delay_off, 0644, led_delay_off_show, led_delay_off_store);

static void timer_trig_activate(struct led_classdev *led_cdev)
{
	int rc;

	led_cdev->trigger_data = NULL;

	rc = device_create_file(led_cdev->dev, &dev_attr_delay_on);
	if (rc)
		return;
	rc = device_create_file(led_cdev->dev, &dev_attr_delay_off);
	if (rc)
		goto err_out_delayon;

	led_blink_set(led_cdev, &led_cdev->blink_delay_on,
		      &led_cdev->blink_delay_off);
	led_cdev->activated = true;

	return;

err_out_delayon:
	device_remove_file(led_cdev->dev, &dev_attr_delay_on);
}

static void timer_trig_deactivate(struct led_classdev *led_cdev)
{
	if (led_cdev->activated) {
		device_remove_file(led_cdev->dev, &dev_attr_delay_on);
		device_remove_file(led_cdev->dev, &dev_attr_delay_off);
		led_cdev->activated = false;
	}

	/* Stop blinking */
	led_set_brightness(led_cdev, LED_OFF);
}

static struct led_trigger timer_led_trigger = {
	.name     = "timer",
	.activate = timer_trig_activate,
	.deactivate = timer_trig_deactivate,
};

static int __init timer_trig_init(void)
{
	return led_trigger_register(&timer_led_trigger);
}

static void __exit timer_trig_exit(void)
{
	led_trigger_unregister(&timer_led_trigger);
}

//////////////

static void sn3191_mdelay(u32 ms)
{
	msleep(ms);
}

static int sn3191_write_reg(u8 addr, u8 para)
{
	struct i2c_msg msg[1];
	u8 data[2];
	int ret;

	data[0] = addr;
	data[1] = para;
	//printk("%s %x %x %x\n",__func__,data[0],data[1],sn3191_i2c_cilent->addr);
	if(sn3191_i2c_cilent != NULL){

	msg[0].addr = 0x68;//sn3191_i2c_cilent->addr;
	msg[0].flags = 0;
	msg[0].buf = data;
	msg[0].len = ARRAY_SIZE(data);

	ret = i2c_transfer(sn3191_i2c_cilent->adapter, msg, ARRAY_SIZE(msg));
	printk("%s,slave=0x%x,add=0x%x,para=0x%x\n",__func__,msg[0].addr,data[0],data[1]);
	return ret;
	}else
		printk("%s no client\n",__func__);
    return 0;
}
//void sn3191_rgb_factory_test(void);

static void sn3191_init(void)
{
	int r;
	LEDS_DEBUG("[LED]+%s\n", __func__);
	if (gpio_is_valid(enable_pin)) {
		r = gpio_request(enable_pin, "sn3191_en_gpio");
		if (r) {
			printk(	"unable to request gpio \n");
			goto done;
		}
		r = gpio_direction_output(enable_pin, 1);
		if (r) {
			printk(	"unable to set direction gpio \n");
			goto done;
		}
	} else {
		printk("gpio not provided\n");
		goto done;
	}

	gpio_set_value(enable_pin, 1);/* HPD */
    sn3191_mdelay(10);

    sn3191_write_reg(0x2F, 0);
    sn3191_mdelay(10);
    sn3191_write_reg(0x1D, 0x00);
    sn3191_write_reg(0x03, 0x01 << 2);
	
    sn3191_write_reg(0x04, 170); 
    sn3191_write_reg(0x0A, 0x00);

    sn3191_write_reg(0x10, 0x04 << 1);


    sn3191_write_reg(0x16, 0x04 << 1);  
 
	
    sn3191_write_reg(0x02, 0x01 << 5);
    sn3191_write_reg(0x1C, 0);
    sn3191_write_reg(0x07, 0);	
    sn3191_write_reg(0x00, 0x01);
	sn3191_is_init = 1;
done:
    LEDS_DEBUG("[LED]-%s\n", __func__);
//	sn3191_rgb_factory_test();
}
void sn3191_off(void)
{
	LEDS_DEBUG("[LED]%s\n", __func__);

    sn3191_write_reg(0x1D, 0x00);
    sn3191_write_reg(0x07, 1);	
    sn3191_write_reg(0x00, 0x01);
}

void sn3191_rgb_factory_test(void)
{
	LEDS_DEBUG("[LED]%s\n", __func__);

    sn3191_write_reg(0x00, 0x20);
    sn3191_write_reg(0x02, 0x01 << 5); //RGB mode

    sn3191_write_reg(0x04, 170); //DOUT1,B

    sn3191_write_reg(0x0A, 0x40);	//B
    sn3191_write_reg(0x10, 0x04);
    sn3191_write_reg(0x16, 0x08);  
    sn3191_write_reg(0x1C, 0);
    sn3191_write_reg(0x07, 0);	


    sn3191_write_reg(0x1C, 0); 
    sn3191_write_reg(0x1D, 0x01);
    sn3191_write_reg(0x07, 0);	 
}


static int  sn3191_blink_set(struct led_classdev *led_cdev,
									unsigned long *delay_on,
									unsigned long *delay_off)
{


	struct sn3191_leds_priv *led_data =
		container_of(led_cdev, struct sn3191_leds_priv, cdev);

//	LEDS_DEBUG("[LED]%s delay_on=0x%x, delay_off=0x%x\n", __func__,*delay_on,*delay_off);

	if (*delay_on != led_data->delay_on || *delay_off != led_data->delay_off) {
		led_data->delay_on = *delay_on;
		led_data->delay_off = *delay_off;

	
	}
	
	return 0;
}


/******************************
*func:sn3191_proc_backlight_value

level:
 D[7-0] -> B(8bit)
 D[15-8] -> G(8bit)
 D[23-16] -> R(8bit)
 
 0x ff ff ff
     | |  |---B
     | |-----G
     |-------R
     
delay_on:
 D[3-0] -> BT2(4bit)
 D[6-4] -> BT3(3bit)
 D[9-7] -> BT1(3bit)




delay_off:
 D[3-0] -> BT4
 D[7-4] -> BT0


period of time
			    ______			    ______
			   |		 |			   |		 |
	               |		  |			  |		  |
	              |         	   |                |         	   |
	             |               |              |               |
	            |                 |            |                 |
___________|                   |______|                   |______

           T0   |T1 |  T2   |T3|   T4   |T1|    T2  |T3|  T4    	
 
*******************************/
static void sn3191_proc_backlight_value(int level, int delay_on, int delay_off)
{
	int led_cur;
	int pwm_red,pwm_green,pwm_blue;
	int Bt0,Bt1,Bt2,Bt3,Bt4;

	printk("%s level=0x%x delay_on=0x%x delay_off=0x%x\n",__func__,level,delay_on,delay_off);

	led_cur = (level>>SN3191_BL_CUR_OFFSET_BIT)&0x07;
	if(led_cur>4) led_cur=4;
	if(led_cur==0) led_cur=1;//def is 1:10mA
	led_cur=2;//set as 5mA
	pwm_red = (level>>SN3191_BL_R_OFFSET_BIT)&0xff;
	pwm_green = (level>>SN3191_BL_G_OFFSET_BIT)&0xff;	
	pwm_blue = (level>>SN3191_BL_B_OFFSET_BIT)&0xff;

	Bt2=(delay_on>>SN3191_BL_BT2_OFFSET_BIT)&SN3191_BL_T2_MASK;
	if(Bt2>8) Bt2=8;
	Bt3=(delay_on>>SN3191_BL_BT3_OFFSET_BIT)&SN3191_BL_T3_MASK;
	Bt1=(delay_on>>SN3191_BL_BT1_OFFSET_BIT)&SN3191_BL_T1_MASK;

	Bt4=(delay_off>>SN3191_BL_BT4_OFFSET_BIT)&SN3191_BL_T4_MASK;
	if(Bt4>10) Bt4=10;
	Bt0=(delay_off>>SN3191_BL_BT0_OFFSET_BIT)&SN3191_BL_T0_MASK;	
	if(Bt0>10) Bt0=10;

	LEDS_DEBUG("[LED]%s cur:%s rgb:0x%x 0x%x 0x%x;  B:t0=%s t1=%s t2=%s t3=%s t4=%s;\n",
				__func__,
				cur_text[led_cur],
				pwm_red,pwm_green,pwm_blue,
				t0_t4_text[Bt0],t1_t3_text[Bt1],t2_text[Bt2],t1_t3_text[Bt3],t0_t4_text[Bt4]);

	sn3191_write_reg(SN3191_SSD_EN_REG, SN3191_EN_OUT_OPEN | SN3191_SSD_DISABLE);

	sn3191_write_reg(SN3191_BREATH_MODE_REG, 0);//SN3191_RM_DISABLE | SN3191_CSS_OUT_ALL);
	sn3191_write_reg(SN3191_CURRENT_SET_REG, led_cur<<SN3191_CS_OFFSET_BIT);
	
	sn3191_write_reg(SN3191_PWM_BLUE_REG, pwm_blue);


	if((delay_on!=0) && (delay_off!=0)){
	sn3191_write_reg(SN3191_LED_MODE_REG, SN3191_RGB_MODE_ENABLE);
	
	sn3191_write_reg(SN3191_T0_BLUE_REG, Bt0<<SN3191_T0_OFFSET_BIT);
	sn3191_write_reg(SN3191_T1_T2_BLUE_REG, (Bt1<<SN3191_T1_OFFSET_BIT)|(Bt2<<SN3191_T2_OFFSET_BIT));
	sn3191_write_reg(SN3191_T3_T4_BLUE_REG, (Bt3<<SN3191_T3_OFFSET_BIT)|(Bt4<<SN3191_T4_OFFSET_BIT));
	
	}else{
	sn3191_write_reg(SN3191_LED_MODE_REG, SN3191_RGB_MODE_DISABLE);	
	}
	sn3191_write_reg(SN3191_TIME_REFRESH_REG, 0);//hdz
   	sn3191_write_reg(SN3191_PWM_DATA_REFRESH_REG, 0);//hdz

	sn3191_write_reg(SN3191_LED_OUT_CONTROL_REG, 1);//SN3191_LED_OUT_ENABLE_ALL);
	
}

void SN3191_PowerOff_Charging_RGB_LED(unsigned int level)
{
    if(!sn3191_is_init) {
        sn3191_init();
    }
	sn3191_proc_backlight_value(level,0,0);

}
EXPORT_SYMBOL_GPL(SN3191_PowerOff_Charging_RGB_LED);

static void sn3191_led_work(struct work_struct *work)
{
	struct sn3191_leds_priv	*led_data =
		container_of(work, struct sn3191_leds_priv, work);

if((led_data->level)==0)
	sn3191_off();
else
	sn3191_proc_backlight_value(led_data->level,led_data->delay_on,led_data->delay_off);
}

void sn3191_led_set(struct led_classdev *led_cdev,enum led_brightness value)
{


	struct sn3191_leds_priv *led_data =
		container_of(led_cdev, struct sn3191_leds_priv, cdev);
	LEDS_DEBUG("[LED]%s value=%d\n", __func__,value);	

    if(sn3191_i2c_cilent == NULL) {
        printk("sn3191_i2c_cilent null\n");
        return;
    }
    cancel_work_sync(&led_data->work);
	led_data->level = value;
        
    if(!sn3191_is_init) {
        sn3191_init();
    }
    schedule_work(&led_data->work);
}

/*for factory test*/
static void sn3191_led_work_test(struct work_struct *work)
{
	struct sn3191_leds_priv	*led_data =
		container_of(work, struct sn3191_leds_priv, work);

if((led_data->level)==0)
	sn3191_off();
else
	sn3191_rgb_factory_test();
}

/*for factory test*/

void sn3191_led_set_test(struct led_classdev *led_cdev,enum led_brightness value)
{

	struct sn3191_leds_priv *led_data =
		container_of(led_cdev, struct sn3191_leds_priv, cdev);
	LEDS_DEBUG("[LED]%s value=%d\n", __func__,value);	

    if(sn3191_i2c_cilent == NULL) {
        printk("sn3191_i2c_cilent null\n");
        return;
    }
    cancel_work_sync(&led_data->work);
	led_data->level = value;
        
    if(!sn3191_is_init) {
        sn3191_init();
    }
    schedule_work(&led_data->work);
}
/* Check for availability of qca199x_ NFC controller hardware */
static int hw_check(struct i2c_client *client, unsigned short curr_addr)
{
	int r = 0;
	unsigned char buf = 0;

	client->addr = curr_addr;
	/* Set-up Addr 0. No data written */
	r = i2c_master_send(client, &buf, 1);
	if (r < 0)
		goto err_presence_check;
	buf = 0;
	/* Read back from Addr 0 */
	r = i2c_master_recv(client, &buf, 1);
	if (r < 0)
		goto err_presence_check;

	r = 0;
	return r;

err_presence_check:
	r = -ENXIO;
	dev_err(&client->dev,
		"nfc-nci nfcc_presence check - no NFCC available\n");
	return r;
}

static int  sn3191_leds_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret,i,r;
	unsigned int temp_value=0;
	struct device_node *node = client->dev.of_node;

	LEDS_DEBUG("[LED]%s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(" probe: need I2C_FUNC_I2C\n");
		return -ENODEV;
	}
	sn3191_i2c_cilent = client;
	if(node){
		enable_pin = of_get_named_gpio(node, "lenovo,en-gpio", 0);
		if ((!gpio_is_valid(enable_pin))){
			printk("[JX]!!ERROR!! gpio unvalid\n");
			goto err;
		
		r = of_property_read_u32(node, "reg", &temp_value);
		if (r)
			goto err;
	}else
		printk("%s no node",__func__);
	printk("%s %x\n",__func__,temp_value);
	
	hw_check(sn3191_i2c_cilent,g_sn3191_leds_data[0]->reg);
	ret = led_classdev_register(&client->dev, &g_sn3191_leds_data[0]->cdev);
	if (ret)
		goto err;
	ret = led_classdev_register(&client->dev, &g_sn3191_leds_data[1]->cdev);
	if (ret)
		goto err;
	
    sn3191_init();
	}
	return ret;
err:
	i2c_del_driver(&sn3191_i2c_driver);
	for (i = 1; i >=0; i--) {
		if (!g_sn3191_leds_data[i])
			continue;
		led_classdev_unregister(&g_sn3191_leds_data[i]->cdev);
		cancel_work_sync(&g_sn3191_leds_data[i]->work);
		kfree(g_sn3191_leds_data[i]);
		g_sn3191_leds_data[i] = NULL;
	}

	return ret;
 }

static int  sn3191_leds_i2c_remove(struct i2c_client *client)
{
   
   LEDS_DEBUG("[LED]%s\n", __func__);
    return 0;
}



/***********************************************************************************
* please add platform device in mt_devs.c
*
************************************************************************************/



MODULE_DEVICE_TABLE(of, lenovo_match_table);




static int __init sn3191_leds_init(void)
{
	int ret=0;
	int i;
	
	LEDS_DEBUG("[LED]%s\n", __func__);

	g_sn3191_leds_data[0] = kzalloc(sizeof(struct sn3191_leds_priv), GFP_KERNEL);
	if (!g_sn3191_leds_data[0]) {
		ret = -ENOMEM;
		goto err;
	}

	g_sn3191_leds_data[0]->cdev.name = LED_NAME;
	g_sn3191_leds_data[0]->cdev.brightness_set = sn3191_led_set;
	g_sn3191_leds_data[0]->cdev.max_brightness = 0xffffffff;
	g_sn3191_leds_data[0]->cdev.blink_set = sn3191_blink_set;
	INIT_WORK(&g_sn3191_leds_data[0]->work, sn3191_led_work);
//	g_sn3191_leds_data[0]->gpio = GPIO80;//GPIO_LED_EN;
	g_sn3191_leds_data[0]->level = 0;
	
	//ret = led_classdev_register(&pdev->dev, &g_sn3191_leds_data[0]->cdev);
	//if (ret)
	//	goto err;

	//for factory test
	g_sn3191_leds_data[1] = kzalloc(sizeof(struct sn3191_leds_priv), GFP_KERNEL);
	if (!g_sn3191_leds_data[1]) {
		ret = -ENOMEM;
		goto err;
	}

	g_sn3191_leds_data[1]->cdev.name = "test-led";
	g_sn3191_leds_data[1]->cdev.brightness_set = sn3191_led_set_test;
	g_sn3191_leds_data[1]->cdev.max_brightness = 0xff;
	INIT_WORK(&g_sn3191_leds_data[1]->work, sn3191_led_work_test);
//	g_sn3191_leds_data[1]->gpio = GPIO80;//GPIO_LED_EN;
	g_sn3191_leds_data[1]->level = 0;
	
	//ret = led_classdev_register(&pdev->dev, &g_sn3191_leds_data[1]->cdev);
	//end for factory test
	
	//if (ret)
	//	goto err;

	if(i2c_add_driver(&sn3191_i2c_driver))
	{
		printk("add i2c driver error %s\n",__func__);
		goto err;
	} 
	timer_trig_init();
	return 0;
	
err:

	for (i = 1; i >=0; i--) {
			if (!g_sn3191_leds_data[i])
				continue;
			//led_classdev_unregister(&g_sn3191_leds_data[i]->cdev);
			cancel_work_sync(&g_sn3191_leds_data[i]->work);
			kfree(g_sn3191_leds_data[i]);
			g_sn3191_leds_data[i] = NULL;
		}

	return ret;
}

static void __exit sn3191_leds_exit(void)
{
	i2c_del_driver(&sn3191_i2c_driver);
	timer_trig_exit();
}

module_param(debug_enable, int,0644);

module_init(sn3191_leds_init);
module_exit(sn3191_leds_exit);

MODULE_AUTHOR("jixu@lenovo.com");
MODULE_DESCRIPTION("sn3191 led driver");
MODULE_LICENSE("GPL");



