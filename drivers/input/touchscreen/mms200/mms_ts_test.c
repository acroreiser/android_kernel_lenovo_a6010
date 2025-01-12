#include "mms200_ts.h"

#ifdef __MMS_LOG_MODE__


static int mms_fs_open(struct inode *node, struct file *fp)
{
	struct mms_ts_info *info;
	struct i2c_client *client;
	struct i2c_msg msg;
	u8 buf[3] = {
		MMS_UNIVERSAL_CMD,
		MMS_CMD_SET_LOG_MODE,
		true,
	};
	if(info->suspended){
		dev_err(&info->client->dev, "Please wake up first\n");
		return -1;
	}
	info = container_of(node->i_cdev, struct mms_ts_info, cdev);
	client = info->client;

	disable_irq(info->irq);
	fp->private_data = info;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = sizeof(buf);

	i2c_transfer(client->adapter, &msg, 1);

	info->log.data = kzalloc(MAX_LOG_LENGTH * 20 + 5, GFP_KERNEL);

	mms_clear_input_data(info);

	return 0;
}

static int mms_fs_release(struct inode *node, struct file *fp)
{
	struct mms_ts_info *info = fp->private_data;

	mms_clear_input_data(info);
	mms_reboot(info);

	kfree(info->log.data);
	enable_irq(info->irq);

	return 0;
}

static void mms_event_handler(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int sz;
	int ret;
	int row_num;
	u8 reg = MMS_INPUT_EVENT;
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = &reg,
			.len = 1,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = info->log.data,
		},

	};
	struct mms_log_pkt {
		u8	marker;
		u8	log_info;
		u8	code;
		u8	element_sz;
		u8	row_sz;
	} __attribute__ ((packed)) *pkt = (struct mms_log_pkt *)info->log.data;

	memset(pkt, 0, sizeof(*pkt));

	if (gpio_get_value(info->pdata->irq_gpio))
		return;

	sz = mms_read_reg(info, MMS_EVENT_PKT_SZ);
	msg[1].len = sz;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev,
			"failed to read %d bytes of data\n",
			sz);
		return;
	}

	if ((pkt->marker & 0xf) == MMS_LOG_EVENT) {
		if ((pkt->log_info & 0x7) == 0x1) {
			pkt->element_sz = 0;
			pkt->row_sz = 0;

			return;
		}

		switch (pkt->log_info >> 4) {
		case LOG_TYPE_U08:
		case LOG_TYPE_S08:
			msg[1].len = pkt->element_sz;
			break;
		case LOG_TYPE_U16:
		case LOG_TYPE_S16:
			msg[1].len = pkt->element_sz * 2;
			break;
		case LOG_TYPE_U32:
		case LOG_TYPE_S32:
			msg[1].len = pkt->element_sz * 4;
			break;
		default:
			dev_err(&client->dev, "invalied log type\n");
			return;
		}

		msg[1].buf = info->log.data + sizeof(struct mms_log_pkt);
		reg = MMS_UNIVERSAL_RESULT;
		row_num = pkt->row_sz ? pkt->row_sz : 1;

		while (row_num--) {
			while (gpio_get_value(info->pdata->irq_gpio))
				;
			ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
			msg[1].buf += msg[1].len;
		};
	} else {
		mms_report_input_data(info, sz, info->log.data);
		memset(pkt, 0, sizeof(*pkt));
	}

	return;
}

static ssize_t mms_fs_read(struct file *fp, char *rbuf, size_t cnt, loff_t *fpos)
{
	struct mms_ts_info *info = fp->private_data;
	struct i2c_client *client = info->client;
	int ret = 0;

	switch (info->log.cmd) {
	case GET_RX_NUM:
		ret = copy_to_user(rbuf, &info->rx_num, 1);
		break;
	case GET_TX_NUM:
		ret = copy_to_user(rbuf, &info->tx_num, 1);
		break;
	case GET_EVENT_DATA:
		mms_event_handler(info);
		/* copy data without log marker */
		ret = copy_to_user(rbuf, info->log.data + 1, cnt);
		break;
	default:
		dev_err(&client->dev, "unknown command\n");
		ret = -EFAULT;
		break;
	}

	return ret;
}

static ssize_t mms_fs_write(struct file *fp, const char *wbuf, size_t cnt, loff_t *fpos)
{
	struct mms_ts_info *info = fp->private_data;
	struct i2c_client *client = info->client;
	u8 *buf;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = cnt,
	};
	int ret = 0;

	mutex_lock(&info->lock);

	if (!info->enabled)
		goto tsp_disabled;
	
	msg.buf = buf = kzalloc(cnt + 1, GFP_KERNEL);

	if ((buf == NULL) || copy_from_user(buf, wbuf, cnt)) {
		dev_err(&client->dev, "failed to read data from user\n");
		ret = -EIO;
		goto out;
	}

	if (cnt == 1) {
		info->log.cmd = *buf;
	} else {
		if (i2c_transfer(client->adapter, &msg, 1) != 1) {
			dev_err(&client->dev, "failed to transfer data\n");
			ret = -EIO;
			goto out;
		}
	}

	ret = 0;

out:
	kfree(buf);
tsp_disabled:
	mutex_unlock(&info->lock);

	return ret;
}

static struct file_operations mms_fops = {
	.owner		= THIS_MODULE,
	.open		= mms_fs_open,
	.release	= mms_fs_release,
	.read		= mms_fs_read,
	.write		= mms_fs_write,
};

int mms_ts_log(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	if (alloc_chrdev_region(&info->mms_dev, 0, 1, "mms_ts")) {
		dev_err(&client->dev, "failed to allocate device region\n");
		return -ENOMEM;
	}

	cdev_init(&info->cdev, &mms_fops);
	info->cdev.owner = THIS_MODULE;

	if (cdev_add(&info->cdev, info->mms_dev, 1)) {
		dev_err(&client->dev, "failed to add ch dev\n");
		return -EIO;
	}
	
	info->class = class_create(THIS_MODULE, "mms_ts");
	device_create(info->class, NULL, info->mms_dev, NULL, "mms_ts");
	
	return 0;
}


#endif


#ifdef __MMS_TEST_MODE__


/*
 * bin_report_read, bin_report_write, bin_sysfs_read, bin_sysfs_write
 * melfas debugging function
 */
static ssize_t bin_report_read(struct file *fp, struct kobject *kobj, struct bin_attribute *attr,
                                char *buf, loff_t off, size_t count)
{
        struct device *dev = container_of(kobj, struct device, kobj);
        struct i2c_client *client = to_i2c_client(dev);
        struct mms_ts_info *info = i2c_get_clientdata(client);
	count = 0;
	if(info->suspended){
		dev_err(&info->client->dev, "Please wake up first\n");
		return -1;
	}
	switch(info->data_cmd){
	case SYS_TXNUM:
		dev_info(&info->client->dev, "tx send %d \n",info->tx_num);
		buf[0]=info->tx_num;
		count =1;
		info->data_cmd = 0;
		break;
	case SYS_RXNUM:
		dev_info(&info->client->dev, "rx send%d\n", info->rx_num);
		buf[0]=info->rx_num;
		count =1;
		info->data_cmd = 0;
		break;
	case SYS_CLEAR:
		dev_info(&info->client->dev, "Input clear\n");
		mms_clear_input_data(info);
		count = 1;
		info->data_cmd = 0;
		break;
	case SYS_ENABLE:
		dev_info(&info->client->dev, "enable_irq  \n");
		enable_irq(info->irq);
		count = 1;
		info->data_cmd = 0;
		break;
	case SYS_DISABLE:
		dev_info(&info->client->dev, "disable_irq  \n");
		disable_irq(info->irq);
		count = 1;
		info->data_cmd = 0;
		break;
	case SYS_INTERRUPT:
		count = gpio_get_value(info->pdata->irq_gpio);
		info->data_cmd = 0;
		break;
	case SYS_RESET:
		mms_reboot(info);
		dev_info(&info->client->dev, "read mms_reboot\n");
		count = 1;
		info->data_cmd = 0;
		break;
	}
	return count;
}

static ssize_t bin_report_write(struct file *fp, struct kobject *kobj, struct bin_attribute *attr,
                                char *buf, loff_t off, size_t count)
{
        struct device *dev = container_of(kobj, struct device, kobj);
        struct i2c_client *client = to_i2c_client(dev);
        struct mms_ts_info *info = i2c_get_clientdata(client);
	if(buf[0]==100){
		mms_report_input_data(info, buf[1], &buf[2]);
	}else{
		info->data_cmd=(int)buf[0];
	}
	return count;
        
}
static struct bin_attribute bin_attr_data = {
        .attr = {
                .name = "report_data",
                .mode = S_IRUGO | S_IWUSR |S_IWGRP,
        },
        .size = PAGE_SIZE,
        .read = bin_report_read,
        .write = bin_report_write,
};

static ssize_t bin_sysfs_read(struct file *fp, struct kobject *kobj , struct bin_attribute *attr,
                          char *buf, loff_t off,size_t count)
{
        struct device *dev = container_of(kobj, struct device, kobj);
        struct i2c_client *client = to_i2c_client(dev);
        struct mms_ts_info *info = i2c_get_clientdata(client);
        struct i2c_msg msg;
        info->client = client;

        msg.addr = client->addr;
        msg.flags = I2C_M_RD ;
        msg.buf = (u8 *)buf;
        msg.len = count;

	switch (count)
	{
		case 65535:
			mms_reboot(info);
			dev_info(&client->dev, "read mms_reboot\n");
			return 0;

		default :
			if(i2c_transfer(client->adapter, &msg, 1) != 1){
	                	dev_err(&client->dev, "failed to transfer data\n");
        	        	mms_reboot(info);
        	        	return 0;
        		}
			break;

	}

        return count;
}

static ssize_t bin_sysfs_write(struct file *fp, struct kobject *kobj, struct bin_attribute *attr,
                                char *buf, loff_t off, size_t count)
{
        struct device *dev = container_of(kobj, struct device, kobj);
        struct i2c_client *client = to_i2c_client(dev);
        struct mms_ts_info *info = i2c_get_clientdata(client);
        struct i2c_msg msg;

        msg.addr =client->addr;
        msg.flags = 0;
        msg.buf = (u8 *)buf;
        msg.len = count;


        if(i2c_transfer(client->adapter, &msg, 1) != 1){
                dev_err(&client->dev, "failed to transfer data\n");
                mms_reboot(info);
                return 0;
        }

        return count;
}

static struct bin_attribute bin_attr = {
        .attr = {
                .name = "mms_bin",
                .mode =  S_IRUGO | S_IWUSR |S_IWGRP,
        },
        .size = PAGE_SIZE,
        .read = bin_sysfs_read,
        .write = bin_sysfs_write,
};



 int get_cm_test_init(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int cnt = 20;
	int ret = 0;
	u8 sz = 0;
	if(info->suspended){
		dev_err(&info->client->dev, "Please wake up first\n");
		return -1;
	}
	disable_irq(info->irq);
	memset(info->get_data,0,PAGE_SIZE);
	sprintf(info->get_data,"start cm info\n");
	dev_err(&info->client->dev, "get_cm_test_init 11\n");
	ret =i2c_smbus_write_byte_data(info->client, MMS_UNIVERSAL_CMD, MMS_UNIV_ENTER_TEST);
	if(ret!=0){
		dev_err(&client->dev,"i2c failed\n");
		return -1;
	}
	dev_err(&info->client->dev, "get_cm_test_init 22\n");

	do{
		do{
			udelay(100);
		}while (gpio_get_value(info->pdata->irq_gpio));
		sz = mms_read_reg(info, MMS_EVENT_PKT_SZ);
		sz = mms_read_reg(info, MMS_INPUT_EVENT);
		if(sz!=0x0C){
			dev_err(&client->dev,"maker is not 0x0C\n");
		}else{
			dev_info(&client->dev,"Sucess to Enter Test mode\n");
			goto OUT;
		}
		if(!cnt){
			dev_err(&client->dev,"Failed Enter Test mode\n");
			return -1;
		}
	}while(cnt--);
OUT:
	sz = mms_read_reg(info, MMS_UNIVERSAL_RESULT_LENGTH);
	sz = mms_read_reg(info, MMS_UNIVERSAL_RESULT);
	return 0;
}

 int get_cm_test_exit(struct mms_ts_info *info)
{

	struct i2c_client *client = info->client;
	int ret = 0;
	u8 sz = 0;
	char data[25];

	if(i2c_smbus_write_byte_data(info->client, MMS_UNIVERSAL_CMD, MMS_UNIV_EXIT_TEST)){
		dev_err(&client->dev,"i2c failed\n");
		return -1;
	}

	sz = mms_read_reg(info, MMS_UNIVERSAL_RESULT_LENGTH);
	sz = mms_read_reg(info, MMS_UNIVERSAL_RESULT);

	sprintf(data,"\nExit Test Mode\n");
	strcat(info->get_data,data);
	enable_irq(info->irq);

	return ret;
}

 int get_cm_delta(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int r, t;
	int ret = 0;
	int result = 1;
	u8 sz = 0;
	u8 buf[256]={0, };
	u8 reg[4]={ 0, };
	s16 cmdata;
	char data[6];
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = reg,
		},{
			.addr = client->addr,
			.flags = I2C_M_RD,
		},
	};
	printk("cm-delta\n");
	strcat(info->get_data,"\ncm-delta\n\n");
	
	if(i2c_smbus_write_byte_data(info->client, MMS_UNIVERSAL_CMD, MMS_UNIV_TEST_CM)){
		dev_err(&client->dev,"i2c failed\n");
	}

	do{
		udelay(100);
	}while (gpio_get_value(info->pdata->irq_gpio));

	sz = mms_read_reg(info, MMS_UNIVERSAL_RESULT_LENGTH);

	result = mms_read_reg(info, MMS_UNIVERSAL_RESULT);

	if( result == 1 ){
	 	dev_info(&client->dev, "Cm delta test pass\n");
		strcat(info->get_data,"\nCm delta test pass\n\n");
	}else{
		dev_err(&client->dev, "Cm delta test failed\n");
		strcat(info->get_data,"\nCm delta test failed\n");
		ret = -1;
		return ret;
	}
	msleep(1);
	
	printk("\t");

	for(t = 0; t < info->tx_num ; t++){
		printk("[%2d] ",t);
	}
		printk("\n");
	for(r = 0 ; r < info->rx_num; r++)
	{
		printk("[%2d]",r);
		sprintf(data,"[%2d]",r);
		strcat(info->get_data,data);
		memset(data,0,6);

		reg[0] = MMS_UNIVERSAL_CMD;
		reg[1] = MMS_UNIV_GET_DELTA;
		reg[2] = 0xFF;
		reg[3] = r;
		msg[0].len = 4;
		
		if(i2c_transfer(client->adapter, &msg[0],1)!=1){
			dev_err(&client->dev, "Cm delta i2c transfer failed\n");
			ret = -1;
			return ret;
		}
			
		while (gpio_get_value(info->pdata->irq_gpio)){
		}
		
		sz = mms_read_reg(info, MMS_UNIVERSAL_RESULT_LENGTH);
		
		reg[0] =MMS_UNIVERSAL_RESULT;
		msg[0].len = 1;
		msg[1].len = sz;
		msg[1].buf = buf;
		if(i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg))!=ARRAY_SIZE(msg)){
			ret = -1;		
			dev_err(&client->dev, "MMS_UNIVERSAL_RESULT transfer failed\n");
			return ret;
		}
		for(t = 0; t< info->tx_num; t++){
			cmdata = (s16)(buf[2*t] | (buf[2*t+1] << 8));
			printk("%5d",cmdata);
			sprintf(data,"%5d",cmdata);
			strcat(info->get_data,data);
			memset(data,0,6);
		}
		printk("\n");
		sprintf(data,"\n");
		strcat(info->get_data,data);
		memset(data,0,6);
		

	}
	if (info->key_num)
	{
		printk("---key cm delta---\n");
		strcat(info->get_data,"key cm delta\n");
		memset(data,0,6);
		
		reg[0] = MMS_UNIVERSAL_CMD;
		reg[1] = MMS_UNIV_GET_KEY_DELTA;
		reg[2] = 0xFF;
		reg[3] = 0x00;
		msg[0].len = 4;
		
		if(i2c_transfer(client->adapter, &msg[0],1)!=1){
			dev_err(&client->dev, "Cm delta i2c transfer failed\n");
			ret = -1;
			return ret;
		}
			
		while (gpio_get_value(info->pdata->irq_gpio)){
		}
		
		sz = mms_read_reg(info, MMS_UNIVERSAL_RESULT_LENGTH);
		
		reg[0] =MMS_UNIVERSAL_RESULT;
		msg[0].len = 1;
		msg[1].len = sz;
		msg[1].buf = buf;
		if(i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg))!=ARRAY_SIZE(msg)){
			ret = -1;			
			return ret;
		}
		for(t = 0; t< info->key_num; t++){
			cmdata = (s16)(buf[2*t] | (buf[2*t+1] << 8));
			printk("%5d",cmdata);
			sprintf(data,"%5d",cmdata);
			strcat(info->get_data,data);
			memset(data,0,6);
		}
		printk("\n");
		sprintf(data,"\n");
		strcat(info->get_data,data);
		memset(data,0,6);
		
	}
	
	return ret;
}

 int get_cm_abs(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int r, t;
	int ret = 0;
	int result = 1;
	u8 sz = 0;
	u8 buf[256]={0, };
	u8 reg[4]={ 0, };
	s16 cmdata;
	char data[6];
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = reg,
		},{
			.addr = client->addr,
			.flags = I2C_M_RD,
		},
	};
	printk("cm-abs\n");
	strcat(info->get_data,"\ncm-abs\n\n");
	
	
	if(i2c_smbus_write_byte_data(info->client, MMS_UNIVERSAL_CMD, 0x43)){
		dev_err(&client->dev,"i2c failed\n");
	}
	do{
		udelay(100);
	}while (gpio_get_value(info->pdata->irq_gpio));

	sz = i2c_smbus_read_byte_data(info->client, MMS_UNIVERSAL_RESULT_LENGTH);
	result = i2c_smbus_read_byte_data(info->client, MMS_UNIVERSAL_RESULT);

	if( result == 1 ){
	 	dev_info(&client->dev, "Cm abs test pass\n");
		strcat(info->get_data,"\nCm abs test pass\n\n");
	}else{
		dev_err(&client->dev, "Cm abs test failed\n");
		strcat(info->get_data,"\nCm abs test failed\n");
		ret = -1;
		return ret;
	}
	msleep(1);
	
	printk("\t");

	for(t = 0; t < info->tx_num ; t++){
		printk("[%2d] ",t);
	}
		printk("\n");
	for(r = 0 ; r < info->rx_num; r++)
	{
		printk("[%2d]",r);
		sprintf(data,"[%2d]",r);
		strcat(info->get_data,data);
		memset(data,0,6);

		reg[0] = MMS_UNIVERSAL_CMD;
		reg[1] = MMS_UNIV_GET_ABS;
		reg[2] = 0xFF;
		reg[3] = r;
		msg[0].len = 4;
		
		if(i2c_transfer(client->adapter, &msg[0],1)!=1){
			dev_err(&client->dev, "Cm abs i2c transfer failed\n");
			ret = -1;
			return ret;
		}
			
		while (gpio_get_value(info->pdata->irq_gpio)){
		}
		
		sz = i2c_smbus_read_byte_data(info->client, MMS_UNIVERSAL_RESULT_LENGTH);
		
		reg[0] =MMS_UNIVERSAL_RESULT;
		msg[0].len = 1;
		msg[1].len = sz;
		msg[1].buf = buf;
		if(i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg))!=ARRAY_SIZE(msg)){
			ret = -1;			
			return ret;
		}
		for(t = 0; t< info->tx_num; t++){
			cmdata = (s16)(buf[2*t] | (buf[2*t+1] << 8));
			printk("%5d",cmdata);
			sprintf(data,"%5d",cmdata);
			strcat(info->get_data,data);
			memset(data,0,5);
		}
		printk("\n");
		sprintf(data,"\n");
		strcat(info->get_data,data);
		memset(data,0,6);
		memset(data,0,6);

	}
	if (info->key_num)
	{
		printk("---key cm abs---\n");
		strcat(info->get_data,"key cm abs\n");
		memset(data,0,6);
		
		reg[0] = MMS_UNIVERSAL_CMD;
		reg[1] = MMS_UNIV_GET_KEY_ABS;
		reg[2] = 0xFF;
		reg[3] = 0x00;
		msg[0].len = 4;
		
		if(i2c_transfer(client->adapter, &msg[0],1)!=1){
			dev_err(&client->dev, "Cm delta i2c transfer failed\n");
			ret = -1;
			return ret;
		}
			
		while (gpio_get_value(info->pdata->irq_gpio)){
		}
		
		sz = i2c_smbus_read_byte_data(info->client, MMS_UNIVERSAL_RESULT_LENGTH);
		
		reg[0] =MMS_UNIVERSAL_RESULT;
		msg[0].len = 1;
		msg[1].len = sz;
		msg[1].buf = buf;
		if(i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg))!=ARRAY_SIZE(msg)){
			ret = -1;			
			return ret;
		}
		for(t = 0; t< info->key_num; t++){
			cmdata = (s16)(buf[2*t] | (buf[2*t+1] << 8));
			printk("%5d",cmdata);
			sprintf(data,"%5d",cmdata);
			strcat(info->get_data,data);
			memset(data,0,6);
		}
		printk("\n");
		sprintf(data,"\n");
		strcat(info->get_data,data);
		memset(data,0,6);
		
	}
	return ret;
}

 int get_cm_jitter(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int r, t;
	int ret = 0;
	int result = 1;
	u8 sz = 0;
	u8 buf[256]={0, };
	u8 reg[4]={ 0, };
	s16 cmdata;
	char data[6];
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = reg,
		},{
			.addr = client->addr,
			.flags = I2C_M_RD,
		},
	};
	printk("cm-jitter\n");
	strcat(info->get_data,"\ncm-jitter\n\n");
	
	
	if(i2c_smbus_write_byte_data(info->client, MMS_UNIVERSAL_CMD, MMS_UNIV_TEST_JITTER)){
		dev_err(&client->dev,"i2c failed\n");
	}

	do{
		udelay(100);
	}while (gpio_get_value(info->pdata->irq_gpio));

	sz = i2c_smbus_read_byte_data(info->client, MMS_UNIVERSAL_RESULT_LENGTH);

	result = i2c_smbus_read_byte_data(info->client, MMS_UNIVERSAL_RESULT);

	if( result == 1 ){
	 	dev_info(&client->dev, "Cm jitter test pass\n");
		strcat(info->get_data,"\nCm jitter test pass\n\n");
	}else{
		dev_err(&client->dev, "Cm jitter test failed\n");
		strcat(info->get_data,"\nCm jitter test failed\n");
		ret = -1;
		return ret;
	}
	msleep(1);
	
	printk("\t");

	for(t = 0; t < info->tx_num ; t++){
		printk("[%2d] ",t);
	}
		printk("\n");
	for(r = 0 ; r < info->rx_num; r++)
	{
		printk("[%2d]",r);
		sprintf(data,"[%2d]",r);
		strcat(info->get_data,data);
		memset(data,0,6);

		reg[0] = MMS_UNIVERSAL_CMD;
		reg[1] = MMS_UNIV_GET_JITTER;
		reg[2] = 0xFF;
		reg[3] = r;
		msg[0].len = 4;
		
		if(i2c_transfer(client->adapter, &msg[0],1)!=1){
			dev_err(&client->dev, "Cm jitter i2c transfer failed\n");
			ret = -1;
			return ret;
		}
			
		while (gpio_get_value(info->pdata->irq_gpio)){
		}
		
		sz = i2c_smbus_read_byte_data(info->client, MMS_UNIVERSAL_RESULT_LENGTH);
		
		reg[0] =MMS_UNIVERSAL_RESULT;
		msg[0].len = 1;
		msg[1].len = sz;
		msg[1].buf = buf;
		if(i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg))!=ARRAY_SIZE(msg)){
			ret = -1;			
			return ret;
		}
		for(t = 0; t< info->tx_num; t++){
			cmdata = buf[t];
			printk("%5d",cmdata);
			sprintf(data,"%5d",cmdata);
			strcat(info->get_data,data);
			memset(data,0,5);
		}
		printk("\n");
		sprintf(data,"\n");
		strcat(info->get_data,data);
		memset(data,0,6);
		memset(data,0,6);

	}
	if (info->key_num)
	{
		printk("---key cm delta---\n");
		strcat(info->get_data,"key cm jitter\n");
		memset(data,0,6);
		
		reg[0] = MMS_UNIVERSAL_CMD;
		reg[1] = MMS_UNIV_GET_KEY_JITTER;
		reg[2] = 0xFF;
		reg[3] = 0x00;
		msg[0].len = 4;
		
		if(i2c_transfer(client->adapter, &msg[0],1)!=1){
			dev_err(&client->dev, "Cm delta i2c transfer failed\n");
			ret = -1;
			return ret;
		}
			
		while (gpio_get_value(info->pdata->irq_gpio)){
		}
		
		sz = i2c_smbus_read_byte_data(info->client, MMS_UNIVERSAL_RESULT_LENGTH);
		
		reg[0] =MMS_UNIVERSAL_RESULT;
		msg[0].len = 1;
		msg[1].len = sz;
		msg[1].buf = buf;
		if(i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg))!=ARRAY_SIZE(msg)){
			ret = -1;			
			return ret;
		}
		for(t = 0; t< info->key_num; t++){
			cmdata = (buf[t]);
			printk("%5d",cmdata);
			sprintf(data,"%5d",cmdata);
			strcat(info->get_data,data);
			memset(data,0,6);
		}
		printk("\n");
		sprintf(data,"\n");
		strcat(info->get_data,data);
		memset(data,0,6);
		
	}
	return ret;
}

 int get_intensity(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	int r, t;
	int ret = 0;
	u8 sz = 0;
	u8 buf[100]={0, };//42
	u8 reg[4]={ 0, };
	s16 cmdata;
	char data[6];
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = reg,
		},{
			.addr = client->addr,
			.flags = I2C_M_RD,
		},
	};
	if(info->suspended){
		dev_err(&info->client->dev, "Please wake up first\n");
		return -1;
	}
	disable_irq(info->irq);
	memset(info->get_data,0,PAGE_SIZE);
	sprintf(info->get_data,"start intensity\n");
	for(r = 0 ; r < info->rx_num ; r++)
	{
		printk("[%2d]",r);
		sprintf(data,"[%2d]",r);
		strcat(info->get_data,data);
		memset(data,0,6);

		reg[0] = MMS_UNIVERSAL_CMD;
		reg[1] = MMS_UNIV_INTENSITY;
		reg[2] = 0xFF;
		reg[3] = r;

		if(i2c_master_send(client,reg,4) !=4){
			dev_err(&client->dev, "intensity i2c master send failed\n");
			ret = -1;
			enable_irq(info->irq);
			return ret;
		}

		sz = mms_read_reg(info, MMS_UNIVERSAL_RESULT_LENGTH);

		if(sz == -1){
			dev_err(&client->dev, "Result length i2c read failed\n");
			ret = -1;
			enable_irq(info->irq);
			return ret;
		}
		
		reg[0] =MMS_UNIVERSAL_RESULT;
		
		if(i2c_master_send(client,reg,1)!=1){
			dev_err(&client->dev, "intensity i2c master send-2 failed\n");
			ret = -1;
			enable_irq(info->irq);
			return ret;
		}
		dev_err(&client->dev, "ahe 11 i2c_master_recv sz :%d\n",sz);
		if(i2c_master_recv(client,buf,sz)!=sz){
			dev_err(&client->dev, "intensity i2c master recv failed\n");
			ret = -1;
			enable_irq(info->irq);
			return ret;
		}

		sz >>=1;
		for(t = 0 ; t <sz ; t++){		
			cmdata = (s16)(buf[2*t] | (buf[2*t+1] << 8));
			printk("%5d",cmdata);
			sprintf(data,"%5d",cmdata);
			strcat(info->get_data,data);
			memset(data,0,6);
		}
		printk("\n");
		sprintf(data,"\n");
		strcat(info->get_data,data);
		memset(data,0,6);
		
	}

	if (info->key_num)
	{
		printk("---key intensity---\n");
		strcat(info->get_data,"key intensity\n");
		memset(data,0,5);
		
		reg[0] = MMS_UNIVERSAL_CMD;
		reg[1] = MMS_UNIV_KEY_INTENSITY;
		reg[2] = 0xFF;
		reg[3] = 0x00;
		msg[0].len = 4;
		
		if(i2c_transfer(client->adapter, &msg[0],1)!=1){
			dev_err(&client->dev, "Cm delta i2c transfer failed\n");
			ret = -1;
			return ret;
		}
			
		while (gpio_get_value(info->pdata->irq_gpio)){
		}
		
		sz =mms_read_reg(info, MMS_UNIVERSAL_RESULT_LENGTH);
		
		reg[0] =MMS_UNIVERSAL_RESULT;
		msg[0].len = 1;
		msg[1].len = sz;
		msg[1].buf = buf;
		if(i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg))!=ARRAY_SIZE(msg)){
			ret = -1;			
			return ret;
		}
		for(t = 0; t< info->key_num; t++){
			cmdata = (s16)(buf[2*t] | (buf[2*t+1] << 8));
			printk("%5d",cmdata);
			sprintf(data,"%5d",cmdata);
			strcat(info->get_data,data);
			memset(data,0,6);
		}
		printk("\n");
		sprintf(data,"\n");
		strcat(info->get_data,data);
		memset(data,0,6);
		
	}
	enable_irq(info->irq);

	return 0;
}


static ssize_t mms_version(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	struct i2c_client *client = info->client;
	u8 ver[3];
	u8 cmd = MMS_FW_VERSION;
	u8 data[255];
	int ret;
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = &cmd,
			.len = 1,
		},{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = ver,
			.len = 3,
		},
	};
	if(info->suspended){
		dev_err(&info->client->dev, "Please wake up first\n");
		return -1;
	}
	memset(info->get_data,0,PAGE_SIZE);
	sprintf(data,"=======firmware version read=======");
	strcat(info->get_data,data);
	disable_irq(info->irq);
	if(i2c_transfer(client->adapter,msg,ARRAY_SIZE(msg))!=ARRAY_SIZE(msg)){
		return -1;
	}
	sprintf(data,"f/w version 0x%x, 0x%x, 0x%x\n",ver[0],ver[1] ,ver[2]);
	strcat(info->get_data,data);
	enable_irq(info->irq);
	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->get_data);
	return ret;
	
}
static DEVICE_ATTR(lenovo_tpd_info, 0664, mms_version, NULL);

static ssize_t mms_intensity(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
//	struct i2c_client *client = info->client;
	int ret;

	info->tx_num = mms_read_reg(info, MMS_TX_NUM);
	info->rx_num = mms_read_reg(info, MMS_RX_NUM);
	info->key_num = mms_read_reg(info, MMS_KEY_NUM);

	dev_info(&info->client->dev, "Intensity Test\n");
	if(get_intensity(info)!=0){
		dev_err(&info->client->dev, "Intensity Test failed\n");
		return -1;
	}
	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->get_data);
	return ret;
}

static DEVICE_ATTR(intensity, 0664, mms_intensity, NULL);

static ssize_t mms_cmdelta(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	//struct i2c_client *client = info->client;
	int ret;
	dev_info(&info->client->dev, "cm delta Test\n");
/*
	info->tx_num = mms_read_reg(info, MMS_TX_NUM);
	info->rx_num = mms_read_reg(info, MMS_RX_NUM);
	info->key_num = mms_read_reg(info, MMS_KEY_NUM);
*/
	info->tx_num = mms_read_reg(info, MMS_TX_NUM);
	info->rx_num = mms_read_reg(info, MMS_RX_NUM);
	info->key_num = mms_read_reg(info, MMS_KEY_NUM);
	dev_info(&info->client->dev, "cm delta Test 11\n");
	if(get_cm_test_init(info)){
		dev_info(&info->client->dev, "Failed\n");
		return -EAGAIN;
	}
	dev_info(&info->client->dev, "cm delta Test 22 init_ok\n");
	if(get_cm_delta(info)){
		return -EAGAIN;
	}
	dev_info(&info->client->dev, "cm delta Test 33 get_cm_delta OK\n");
	if(get_cm_test_exit(info)){
		return -EAGAIN;
	}
	dev_info(&info->client->dev, "cm delta Test 33 exit OK\n");	
	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->get_data);
	memset(info->get_data,0,4096);
	return ret;
}
static DEVICE_ATTR(cmdelta, 0664, mms_cmdelta, NULL);

static ssize_t mms_cmabs(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	//struct i2c_client *client = info->client;
	int ret;
	dev_info(&info->client->dev, "cm delta Test\n");

	info->tx_num = mms_read_reg(info, MMS_TX_NUM);
	info->rx_num = mms_read_reg(info, MMS_RX_NUM);
	info->key_num = mms_read_reg(info, MMS_KEY_NUM);

	get_cm_test_init(info);
	get_cm_abs(info);
	get_cm_test_exit(info);
	
	
	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->get_data);
	memset(info->get_data,0,4096);
	return ret;
}
static DEVICE_ATTR(cmabs, 0664, mms_cmabs, NULL);

static ssize_t mms_cmjitter(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mms_ts_info *info = dev_get_drvdata(dev);
	//struct i2c_client *client = info->client;
	int ret;

	info->tx_num = mms_read_reg(info, MMS_TX_NUM);
	info->rx_num = mms_read_reg(info, MMS_RX_NUM);
	info->key_num = mms_read_reg(info, MMS_KEY_NUM);

	dev_info(&info->client->dev, "cm delta Test\n");
	get_cm_test_init(info);
	get_cm_jitter(info);
	get_cm_test_exit(info);
	
	ret = snprintf(buf,PAGE_SIZE,"%s\n",info->get_data);
	memset(info->get_data,0,4096);
	return ret;
}
static DEVICE_ATTR(cmjitter, 0664, mms_cmjitter, NULL);

static struct attribute *mms_attrs_test[] = {
	&dev_attr_lenovo_tpd_info.attr,
	&dev_attr_intensity.attr,
	&dev_attr_cmdelta.attr,
	&dev_attr_cmabs.attr,
	&dev_attr_cmjitter.attr,
	NULL,
};

static const struct attribute_group mms_attr_test_group = {
	.attrs = mms_attrs_test,
};

int mms_sysfs_test_mode(struct mms_ts_info *info)
{
	struct i2c_client *client = info->client;
	if (sysfs_create_group(&client->dev.kobj, &mms_attr_test_group)) {
		dev_err(&client->dev, "failed to create sysfs group\n");
		return -EAGAIN;
	}
	if(sysfs_create_bin_file(&client->dev.kobj ,&bin_attr)){
		dev_err(&client->dev, "failed to create sysfs symlink\n");
		return -EAGAIN;
	}

	if(sysfs_create_bin_file(&client->dev.kobj ,&bin_attr_data)){
		dev_err(&client->dev, "failed to create sysfs symlink\n");
		return -EAGAIN;
	}
	info->get_data=kzalloc(sizeof(u8)*4096,GFP_KERNEL);

	return 0;
}

void mms_sysfs_remove(struct mms_ts_info *info)
{
	sysfs_remove_group(&info->client->dev.kobj, &mms_attr_test_group);
	sysfs_remove_bin_file(&info->client->dev.kobj, &bin_attr);
	sysfs_remove_bin_file(&info->client->dev.kobj, &bin_attr_data);
	kfree(info->get_data);
	return;
}
#endif
