/*
 * MELFAS MIT200 Touchscreen Driver for RSP
 *
 * Copyright (C) 2014 MELFAS Inc.
 *
 */

#include "mit200_ts.h"

#define BLOCK_SZ			128


int get_fw_version(struct i2c_client *client, u8 *buf)
{
	u8 cmd[2] = {0x00, MIT_FW_VERSION};
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = cmd,
			.len = 2,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.buf = buf,
			.len = 2,
		},
	};

	return (i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg)) != ARRAY_SIZE(msg));
}

static int mit_isc_check_status(struct mit_ts_info *info)
{
	struct i2c_client *client = info->client;
	int count = 50;
	u8 cmd[6] = ISC_STATUS_READ;
	u8 buf;
	struct i2c_msg msg[]={
		{
			.addr = client->addr,
			.flags = 0,
			.buf = cmd,
			.len = 6,
		},{
			.addr=client->addr,
			.flags = I2C_M_RD,
			.buf = &buf,
			.len = 1,
		}
	};

	while(count--)
	{
		if(i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg)) != ARRAY_SIZE(msg)){
			dev_info(&client->dev,"failed to status read\n");
			return -1;
		}
		if( buf == 0xAD )
		{
			return 0;
		}
		msleep(1);
	}
	dev_info(&client->dev,"failed to status read\n");
	return -1;
}

static int mit_isc_page_program(struct mit_ts_info *info, const u8 *wdata, int addr)
{
	struct i2c_client *client = info->client;
	u8 cmd[BLOCK_SZ + 6] = ISC_PAGE_PROGRAM;

	cmd[4] = (addr&0xFF00)>>8;
	cmd[5] = (addr&0x00FF)>>0;

	memcpy( &cmd[6], wdata, BLOCK_SZ);

	if(i2c_master_send(client, cmd, BLOCK_SZ+6)!=BLOCK_SZ+6){
		dev_err(&client->dev,
			"failed to page program\n");
		return -1;
	}

	if( mit_isc_check_status(info) )
		return -1;


	return 0;
}
	
static int mit_isc_page_read(struct mit_ts_info *info, u8 *rdata, int addr)
{
	struct i2c_client *client = info->client;
	u8 cmd[6] = ISC_FLASH_READ;
	struct i2c_msg msg[]={
		{
			.addr = client->addr,
			.flags = 0,
			.buf = cmd,
			.len = 6,
		},{
			.addr=client->addr,
			.flags = I2C_M_RD,
			.buf = rdata,
			.len = BLOCK_SZ,
		}
	};

	cmd[4] = (addr&0xFF00)>>8;
	cmd[5] = (addr&0x00FF)>>0;

	return (i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg)) != ARRAY_SIZE(msg));
}


static int mit_isc_exit(struct mit_ts_info *info)
{
	struct i2c_client *client = info->client;
	u8 cmd[6] = ISC_EXIT;

	if(i2c_master_send(client, cmd, 6)!= 6){
		dev_err(&client->dev,
			"failed to isc exit\n");
		return -1;
	}

	return 0;
}

//----------------------------------------
//
// Main Functions
//
//----------------------------------------

int mit_flash_fw(struct mit_ts_info *info, const u8 *fw_data, size_t fw_size)
{
	struct i2c_client *client = info->client;
	int addr;
	int retires = 3;
	u8 ver[VERSION_NUM];
	u8 target[VERSION_NUM];
	u8 cmpdata[BLOCK_SZ];
	u8 init_data[BLOCK_SZ];	
	memset(init_data, 0xFF, sizeof(init_data));
MIT_DEBUG("%s",__func__);	
	if(memcmp("T2H0", &fw_data[fw_size - 16], 4)){
		dev_err(&client->dev, "ERROR : F/W file is not for MIT200\n");
		goto out;
	}
	
	while (retires--) {
		if (!get_fw_version(client, ver))
			break;
		else
			mit_reboot(info);
	}
	mit_reboot(info);

	if (retires < 0) {
		dev_warn(&client->dev, "failed to obtain version info from chip f/w\n");
		memset(ver, 0xff, sizeof(ver));
	} else {
		print_hex_dump(KERN_INFO, "mit_ts f/w ver : ", DUMP_PREFIX_NONE, 16, 1,
				ver, VERSION_NUM, false);
	}

	target[0] = fw_data[fw_size - 6];
	target[1] = fw_data[fw_size - 5];
	dev_info(&client->dev, "binary ver : %x %x\n", target[0], target[1]);
	if(ver[0] == 0x51 && ver[1] == 0x99){
		dev_err(&client->dev, "51 99 fw not update\n");
		return 0;
	}
	if(ver[0] == target[0] && ver[1] >= target[1]){
		dev_err(&client->dev, "f/w is already up-to-date\n");
		return 0;
	}
	if( fw_size % BLOCK_SZ != 0 )
	{
		dev_err(&client->dev, "f/w size mismatch\n");
		goto out;
	}

	if( mit_isc_page_program(info, &init_data[0], 0) ){
		goto out;
	}
	
	for( addr=fw_size-BLOCK_SZ;addr>=0;addr-=BLOCK_SZ )
	{
		if( mit_isc_page_program(info, &fw_data[addr], addr) ){
			goto out;
		}
		if( mit_isc_page_read(info,cmpdata, addr) ){
			goto out;		
		}
		if(memcmp(&fw_data[addr],cmpdata,BLOCK_SZ)){
				print_hex_dump(KERN_ERR, "mit fw wr : ",
						DUMP_PREFIX_OFFSET, 16, 1,
						&fw_data[addr], BLOCK_SZ, false);

				print_hex_dump(KERN_ERR, "mit fw rd : ",
						DUMP_PREFIX_OFFSET, 16, 1,
						cmpdata, BLOCK_SZ, false);
				dev_err(&client->dev, "flash verify failed\n");
				goto out;
		}
	}

	if(mit_isc_exit(info)){
		goto out;
	}

	mit_reboot(info);

	if (get_fw_version(client, ver)) {
		dev_err(&client->dev, "failed to obtain version after flash\n");
		goto out;
	} else {
		if (ver[0] != target[0] || ver[1] != target[1]) {
			dev_err(&client->dev, "version mismatch after flash. 0x%x != 0x%x , 0x%x != 0x%x\n",ver[0], target[0], ver[1], target[1]);
			goto out;
		}
	}
	dev_info(&client->dev, "f/w is updated successfully\n");
	return 0;
out:
	mit_reboot(info);
	dev_err(&client->dev, "failed to f/w update\n");
	return -1;
}
int mit_flash_fw_force(struct mit_ts_info *info, const u8 *fw_data, size_t fw_size)
{
	struct i2c_client *client = info->client;
	int addr;
	int retires = 3;
	u8 ver[VERSION_NUM];
	u8 target[VERSION_NUM];
	u8 cmpdata[BLOCK_SZ];
	u8 init_data[BLOCK_SZ];	
	memset(init_data, 0xFF, sizeof(init_data));
MIT_DEBUG("%s",__func__);	
	if(memcmp("T2H0", &fw_data[fw_size - 16], 4)){
		dev_err(&client->dev, "ERROR : F/W file is not for MIT200\n");
		goto out;
	}
	
	while (retires--) {
		if (!get_fw_version(client, ver))
			break;
		else
			mit_reboot(info);
	}
	mit_reboot(info);

	if (retires < 0) {
		dev_warn(&client->dev, "failed to obtain version info from chip f/w\n");
		memset(ver, 0xff, sizeof(ver));
	} else {
		print_hex_dump(KERN_INFO, "mit_ts f/w ver : ", DUMP_PREFIX_NONE, 16, 1,
				ver, VERSION_NUM, false);
	}

	target[0] = fw_data[fw_size - 6];
	target[1] = fw_data[fw_size - 5];
	dev_info(&client->dev, "binary ver : %x %x\n", target[0], target[1]);
	if( fw_size % BLOCK_SZ != 0 )
	{
		dev_err(&client->dev, "f/w size mismatch\n");
		goto out;
	}

	if( mit_isc_page_program(info, &init_data[0], 0) ){
		goto out;
	}
	
	for( addr=fw_size-BLOCK_SZ;addr>=0;addr-=BLOCK_SZ )
	{
		if( mit_isc_page_program(info, &fw_data[addr], addr) ){
			goto out;
		}
		if( mit_isc_page_read(info,cmpdata, addr) ){
			goto out;		
		}
		if(memcmp(&fw_data[addr],cmpdata,BLOCK_SZ)){
				print_hex_dump(KERN_ERR, "mit fw wr : ",
						DUMP_PREFIX_OFFSET, 16, 1,
						&fw_data[addr], BLOCK_SZ, false);

				print_hex_dump(KERN_ERR, "mit fw rd : ",
						DUMP_PREFIX_OFFSET, 16, 1,
						cmpdata, BLOCK_SZ, false);
				dev_err(&client->dev, "flash verify failed\n");
				goto out;
		}
	}

	if(mit_isc_exit(info)){
		goto out;
	}

	mit_reboot(info);

	if (get_fw_version(client, ver)) {
		dev_err(&client->dev, "failed to obtain version after flash\n");
		goto out;
	} else {
		if (ver[0] != target[0] || ver[1] != target[1]) {
			dev_err(&client->dev, "version mismatch after flash. 0x%x != 0x%x , 0x%x != 0x%x\n",ver[0], target[0], ver[1], target[1]);
			goto out;
		}
	}
	dev_info(&client->dev, "f/w is updated successfully\n");
	return 0;
out:
	mit_reboot(info);
	dev_err(&client->dev, "failed to f/w update\n");
	return -1;
}
