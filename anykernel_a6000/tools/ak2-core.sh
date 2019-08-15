# AnyKernel2 Methods (DO NOT CHANGE)
#
# Original and credits: osm0sis @ xda-developers
#
# Modified by Lord Boeffla, 05.12.2016


## start of main script

# set up extracted files and directories
ramdisk=/tmp/anykernel/ramdisk;
bin=/tmp/anykernel/tools;
split_img=/tmp/anykernel/split_img;
patch=/tmp/anykernel/patch;

chmod -R 755 $bin;
mkdir -p $ramdisk $split_img;

if [ "$is_slot_device" == 1 ]; then
	slot=$(getprop ro.boot.slot_suffix 2>/dev/null);
	test ! "$slot" && slot=$(grep -o 'androidboot.slot_suffix=.*$' /proc/cmdline | cut -d\  -f1 | cut -d= -f2);
	test "$slot" && block=$block$slot;
	if [ $? != 0 -o ! -e "$block" ]; then
		ui_print " "; ui_print "Unable to determine active boot slot. Aborting..."; exit 1;
	fi;
fi;

OUTFD=/proc/self/fd/$1;


## internal functions

# ui_print <text>
ui_print()
{
	echo -e "ui_print $1\nui_print" > $OUTFD;
}

# contains <string> <substring>
contains()
{
	test "${1#*$2}" != "$1" && return 0 || return 1;
}

BOOTBLK="/dev/block/mmcblk0p22"
# dump boot and extract ramdisk
dump_boot()
{
	dd if=$BOOTBLK of=/tmp/anykernel/boot.img;

	$bin/unpackbootimg -i /tmp/anykernel/boot.img -o $split_img;
	if [ $? != 0 ]; then
		ui_print " ";
		ui_print "Dumping/splitting image failed. Aborting...";
		exit 1;
	fi;

	mv -f $ramdisk /tmp/anykernel/rdtmp;
	mkdir -p $ramdisk;
	cd $ramdisk;
	gunzip -c $split_img/boot.img-ramdisk.gz | cpio -i;

	if [ $? != 0 -o -z "$(ls $ramdisk)" ]; then
		ui_print " ";
		ui_print "Unpacking ramdisk failed. Aborting...";
		exit 1;
	fi;

	ui_print " ** Doing some magic with ramdisk...";
	ui_print " ";
	###KOFFEE_EARLY_SCRIPT###
	###ENHANCEIO###
	###PANIC_LOG_ON_FS###

	ui_print "* Applying fixup for 800MHz stuck on interactive governor (for old ROMs like AEX 6.1)";
	sed -i s/'1 800000:90'/'1 200000:90'/ /tmp/anykernel/ramdisk/init.qcom.power.rc
	
	COMPZRAM=$(cat /tmp/anykernel/ramdisk/init.target.rc | grep /comp_ | awk '{print $3}')
	sed  "s/[[:<:]]comp_algorithm $COMPZRAM[[:>:]]/comp_algorithm lz4/" -i /tmp/anykernel/ramdisk/init.target.rc

	ui_print " "
	ui_print "* Tuning disk i/o subsystem"
	sed "s/read_ahead_kb 64/read_ahead_kb 2048/g" -i /tmp/anykernel/ramdisk/init.target.rc
	sed "s/read_ahead_kb 128/read_ahead_kb 2048/g" -i /tmp/anykernel/ramdisk/init.target.rc
	sed "/scheduler noop/d" -i /tmp/anykernel/ramdisk/init.target.rc
	IOSCHED=$(cat /tmp/anykernel/ramdisk/init.target.rc | grep "/scheduler" | awk '{print $3}')
	sed  "s/[[:<:]]scheduler $IOSCHED[[:>:]]/scheduler bfq/" -i /tmp/anykernel/ramdisk/init.target.rc

	ui_print " "
	ui_print "* Tuning virtual memory subsystem"
	sed "s/lmk_fast_run 0/lmk_fast_run 1/g" -i /tmp/anykernel/ramdisk/init.target.rc
	sed "s/swappiness 35/swappiness 100/g" -i /tmp/anykernel/ramdisk/init.target.rc
	sed "s/vfs_cache_pressure 25/vfs_cache_pressure 0/g" -i /tmp/anykernel/ramdisk/init.target.rc
	sed "s/vfs_cache_pressure 65/vfs_cache_pressure 0/g" -i /tmp/anykernel/ramdisk/init.target.rc

	if [ "$(cat /proc/meminfo | head -n 1 | awk '{print $2}')" -lt "1100000" ]; then
		ui_print " ";
		ui_print "* 1/8 model detected, additionally tweaking ramdisk";
		ui_print " ";
		ui_print "* We want to get more RAM... Lets go to do it!";
		sed "s/$ZRAMSIZE/zramsize=419430400/" -i /tmp/anykernel/ramdisk/fstab.qcom

		sed "s/nr_requests 300/nr_requests 512/g" -i /tmp/anykernel/ramdisk/init.target.rc
		sed "s/nr_requests 128/nr_requests 512/g" -i /tmp/anykernel/ramdisk/init.target.rc
	else
		ui_print " ";
		ui_print "* 2/16 model detected, additionally tweaking ramdisk";
		ui_print " ";
		ui_print "* We want to get more RAM... Lets go to do it!";
		sed "s/nr_requests 300/nr_requests 1024/g" -i /tmp/anykernel/ramdisk/init.target.rc
		sed "s/nr_requests 128/nr_requests 1024/g" -i /tmp/anykernel/ramdisk/init.target.rc
		ZRAMSIZE=$(cat /tmp/anykernel/ramdisk/fstab.qcom | grep block/zram0 | awk '{print $5}')
		sed "s/$ZRAMSIZE/zramsize=734003200/" -i /tmp/anykernel/ramdisk/fstab.qcom
	fi	


	if [ -d $ramdisk/su ]; then
			ui_print "  SuperSu systemless detected...";
			ui_print " ";

			SAVE_IFS=$IFS;
			IFS=";"
			for filename in $supersu_exclusions; do 
				rm -f /tmp/anykernel/rdtmp/$filename
			done
			IFS=$SAVE_IFS;
	fi;

	cp -af /tmp/anykernel/rdtmp/* $ramdisk;
}

# repack ramdisk then build and write image
write_boot()
{
	cd $split_img;
	cmdline=`cat *-cmdline`;
	board=`cat *-board`;
	base=`cat *-base`;
	pagesize=`cat *-pagesize`;
	kerneloff=`cat *-kerneloff`;
	ramdiskoff=`cat *-ramdiskoff`;
	tagsoff=`cat *-tagsoff`;
	osver=`cat *-osversion`;
	oslvl=`cat *-oslevel`;

	if [ -f *-second ]; then
		second=`ls *-second`;
		second="--second $split_img/$second";
		secondoff=`cat *-secondoff`;
		secondoff="--second_offset $secondoff";
	fi;

	if [ -f /tmp/anykernel/zImage-dtb ]; then
		kernel=/tmp/anykernel/zImage-dtb;
	else
		kernel=`ls *-zImage`;
		kernel=$split_img/$kernel;
	fi;

	chmod -R 0755 /tmp/anykernel/ramdisk/sbin/*;

	if [ -f "$bin/mkbootfs" ]; then
		$bin/mkbootfs /tmp/anykernel/ramdisk | gzip > /tmp/anykernel/ramdisk-new.cpio.gz;
	else
		cd $ramdisk;
		find . | cpio -H newc -o | gzip > /tmp/anykernel/ramdisk-new.cpio.gz;
	fi;

	if [ $? != 0 ]; then
		ui_print " ";
		ui_print "Repacking ramdisk failed. Aborting...";
		exit 1;
	fi;

	$bin/mkbootimg --kernel $kernel --ramdisk /tmp/anykernel/ramdisk-new.cpio.gz $second --cmdline "$cmdline" --board "$board" --base $base --pagesize $pagesize --kernel_offset $kerneloff --ramdisk_offset $ramdiskoff $secondoff --tags_offset $tagsoff --os_version "$osver" --os_patch_level "$oslvl" $dtb --output /tmp/anykernel/boot-new.img;

	if [ $? != 0 ]; then
		ui_print " ";
		ui_print "Repacking image failed. Aborting...";
		exit 1;
	elif [ `wc -c < /tmp/anykernel/boot-new.img` -gt `wc -c < /tmp/anykernel/boot.img` ]; then
		ui_print " ";
		ui_print "New image larger than boot partition. Aborting...";
		exit 1;
	fi;

	if [ -f "/data/custom_boot_image_patch.sh" ]; then
		ash /data/custom_boot_image_patch.sh /tmp/anykernel/boot-new.img;
		if [ $? != 0 ]; then
			ui_print " ";
			ui_print "User script execution failed. Aborting...";
			exit 1;
		fi;
	fi;

	dd if=/tmp/anykernel/boot-new.img of=$BOOTBLK;
}

# backup_file <file>
backup_file()
{ 
	cp $1 $1~;
}

# replace_string <file> <if search string> <original string> <replacement string>
replace_string()
{
	if [ -z "$(grep "$2" $1)" ]; then
		sed -i "s;${3};${4};" $1;
	fi;
}

# replace_section <file> <begin search string> <end search string> <replacement string>
replace_section()
{
	S1=$(echo ${2} | sed 's/\//\\\//g'); # escape forward slashes
	S2=$(echo ${3} | sed 's/\//\\\//g'); # escape forward slashes
	line=`grep -n "$2" $1 | head -n1 | cut -d: -f1`;
	sed -i "/${S1}/,/${S2}/d" $1;
	sed -i "${line}s;^;${4}\n;" $1;
}

# remove_section <file> <begin search string> <end search string>
remove_section() 
{
	S1=$(echo ${2} | sed 's/\//\\\//g'); # escape forward slashes
	S2=$(echo ${3} | sed 's/\//\\\//g'); # escape forward slashes
	sed -i "/${S1}/,/${S2}/d" $1;
}

# insert_line <file> <if search string> <before|after> <line match string> <inserted line>
insert_line() 
{
	if [ -z "$(grep "$2" $1)" ]; then
		case $3 in
			before) offset=0;;
			after) offset=1;;
		esac;

		line=$((`grep -n "$4" $1 | head -n1 | cut -d: -f1` + offset));
		sed -i "${line}s;^;${5}\n;" $1;
	fi;
}

# replace_line <file> <line replace string> <replacement line>
replace_line()
{
	if [ ! -z "$(grep "$2" $1)" ]; then
		line=`grep -n "$2" $1 | head -n1 | cut -d: -f1`;
		sed -i "${line}s;.*;${3};" $1;
	fi;
}

# remove_line <file> <line match string>
remove_line()
{
	if [ ! -z "$(grep "$2" $1)" ]; then
		line=`grep -n "$2" $1 | head -n1 | cut -d: -f1`;
		sed -i "${line}d" $1;
	fi;
}

# prepend_file <file> <if search string> <patch file>
prepend_file()
{
	if [ -z "$(grep "$2" $1)" ]; then
		echo "$(cat $patch/$3 $1)" > $1;
	fi;
}

# insert_file <file> <if search string> <before|after> <line match string> <patch file>
insert_file()
{
	if [ -z "$(grep "$2" $1)" ]; then
		case $3 in
			before) offset=0;;
			after) offset=1;;
		esac;

		line=$((`grep -n "$4" $1 | head -n1 | cut -d: -f1` + offset));
		sed -i "${line}s;^;\n;" $1;
		sed -i "$((line - 1))r $patch/$5" $1;
	fi;
}

# append_file <file> <if search string> <patch file>
append_file()
{
	if [ -z "$(grep "$2" $1)" ]; then
		echo -ne "\n" >> $1;
		cat $patch/$3 >> $1;
		echo -ne "\n" >> $1;
	fi;
}

# replace_file <file> <permissions> <patch file>
replace_file()
{
	cp -pf $patch/$3 $1;
	chmod $2 $1;
}

# patch_fstab <fstab file> <mount match name> <fs match type> <block|mount|fstype|options|flags> <original string> <replacement string>
patch_fstab()
{
	entry=$(grep "$2" $1 | grep "$3");

	if [ -z "$(echo "$entry" | grep "$6")" ]; then
		case $4 in
			block) part=$(echo "$entry" | awk '{ print $1 }');;
			mount) part=$(echo "$entry" | awk '{ print $2 }');;
			fstype) part=$(echo "$entry" | awk '{ print $3 }');;
			options) part=$(echo "$entry" | awk '{ print $4 }');;
			flags) part=$(echo "$entry" | awk '{ print $5 }');;
		esac;

		newentry=$(echo "$entry" | sed "s;${part};${6};");
		sed -i "s;${entry};${newentry};" $1;
	fi;
}

# comment_line <file> <search string>
comment_line()
{
	S1=$(echo ${2} | sed 's/\//\\\//g'); # escape forward slashes
	sed -i "/${S1}/ s/^#*/#/" $1;
}

# comment_line <file> <begin search string> <end search string>
comment_section()
{
	S1=$(echo ${2} | sed 's/\//\\\//g'); # escape forward slashes
	S2=$(echo ${3} | sed 's/\//\\\//g'); # escape forward slashes
	sed -i "/${S1}/,/${S2}/ s/^#*/#/" $1;
}

## end methods
