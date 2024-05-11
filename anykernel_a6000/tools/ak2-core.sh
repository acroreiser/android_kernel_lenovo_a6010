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

	mkdir /tmp/anykernel/persist
	$bin/busybox mount -t ext4 /dev/block/mmcblk0p24  /tmp/anykernel/persist
	rm -fr /tmp/anykernel/persist/infernal
	umount /tmp/anykernel/persist
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

	$bin/mkbootimg --kernel $kernel --ramdisk $split_img/boot.img-ramdisk.gz $second --cmdline "$cmdline" --board "$board" --base $base --pagesize $pagesize --kernel_offset $kerneloff --ramdisk_offset $ramdiskoff $secondoff --tags_offset $tagsoff --os_version "$osver" --os_patch_level "$oslvl" $dtb --output /tmp/anykernel/boot-new.img;

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
