#!/bin/bash

VERSION=0.9
DEFCONFIG=lineageos_a6010_defconfig
TOOLCHAIN=../../../prebuilts/gcc/linux-x86/arm/arm-eabi/bin/arm-none-eabi-
KCONFIG=false
CUST_CONF=no
BUILD_NUMBER=1
DEVICE=a6010
KCONF_REPLACE=false
KERNEL_NAME="Eclipse"
SKIP_MODULES=true
DONTPACK=false
USER=$USER
DATE=`date`
BUILD_HOST=`hostname`
BUILD_PATH=`pwd`
CLEAN=false
VERBOSE=0
INDTB=-dtb

usage() {
	echo "$KERNEL_NAME build script v$VERSION"
	echo `date`
	echo "Written by acroreiser <acroreiser@gmail.com>"
	echo ""
    echo "Usage:"
    echo ""
	echo "make-koffee.sh [-d/-D/-O <file>] [-K] [-U <user>] [-N <BUILD_NUMBER>] [-k] [-R] [-V] -t <toolchain_prefix> -d <defconfig>"
	echo ""
	echo "Main options:"
	echo "	-K 			call Kconfig (use only with ready config!)"
	echo "	-d 			defconfig for the kernel. Will try to use already generated .config if not specified"
	echo "	-S 			set device codename (a6000 for Lenovo a6000/a6010, m0 for i9300 or t03g for n7100)"
	echo "	-O <file> 			external/other defconfig."
	echo "	-t <toolchain_prefix> 			toolchain prefix for custom toolchain (default GCC 4.7)"
	echo ""
	echo "Extra options:"
	echo "	-j <number_of_cpus> 			set number of CPUs to use"
	echo "	-k 			make only zImage, do not pack into zip"
	echo "	-C 			cleanup before building"
	echo "	-R 			save your arguments to reuse (just run make-koffee.sh on next builds)"
	echo "	-U <username> 			set build user"
	echo "	-H <hostname> 			set build host"
	echo "	-N <release_number> 			set release number"
	echo "	-V 					 			verbose output"
	echo "	-v 			show build script version"
	echo "	-h 			show this help"
	
}

prepare() 
{
 	make -j4 clean
}

make_config() 
{
	if [ "$CUST_CONF" != "no" ]; then
		cp $CUST_CONF `pwd`/.config
		echo "Using custom configuration from $CUST_CONF"
		DEFCONFIG=custom
	else
		if [ ! -f "`pwd`/.config" ] || [ "$KCONF_REPLACE" = true ]; then
			make ARCH=arm $JOBS $DEFCONFIG &>/dev/null
		fi
	fi
	if [ "$KCONFIG" = "true" ]; then
		make ARCH=arm $JOBS menuconfig
	fi
}

build_kernel()
{
	make ARCH=arm KBUILD_BUILD_VERSION=$BUILD_NUMBER $JOBS KBUILD_BUILD_USER=$USER KBUILD_BUILD_HOST=$BUILD_HOST CROSS_COMPILE=$TOOLCHAIN V=$VERBOSE zImage$INDTB
	if [ $? -eq 0 ]; then
		return 0
	else
		return 1
	fi
}

build_modules()
{
	make ARCH=arm KBUILD_BUILD_VERSION=$BUILD_NUMBER $JOBS KBUILD_BUILD_USER=$USER KBUILD_BUILD_HOST=$BUILD_HOST CROSS_COMPILE=$TOOLCHAIN V=$VERBOSE modules
	if [ $? -eq 0 ]; then
		return 0
	else
		return 1
	fi
}

build_dtbs()
{
	make ARCH=arm KBUILD_BUILD_VERSION=$BUILD_NUMBER $JOBS KBUILD_BUILD_USER=$USER KBUILD_BUILD_HOST=$BUILD_HOST CROSS_COMPILE=$TOOLCHAIN V=$VERBOSE dtbs
	if [ $? -eq 0 ]; then
		return 0
	else
		return 1
	fi
}

make_flashable()
{
	# copy anykernel template over
	if [ "$DEVICE" == "a6010" ] || [ "$DEVICE" == "a6000" ]; then
		cp -R $BUILD_PATH/anykernel_a6000/* $REPACK_PATH
		echo "--------------------------------------" > $REPACK_PATH/info.txt
		echo "| Build  date:    $DATE" >> $REPACK_PATH/info.txt
		echo "| Configuration file:    $DEFCONFIG" >> $REPACK_PATH/info.txt
		echo "| Release:    $BVERN" >> $REPACK_PATH/info.txt
		echo "| Building for:    $DEVICE" >> $REPACK_PATH/info.txt
		echo "| Build  user:    $USER" >> $REPACK_PATH/info.txt
		echo "| Build  host:    $BUILD_HOST" >> $REPACK_PATH/info.txt
		echo "| Build  toolchain:    $TVERSION" >> $REPACK_PATH/info.txt
		echo "| Kernel security patch level: $(cat patchlevel.txt)" >> $REPACK_PATH/info.txt
	else
		echo "Attempt to create flashable for an unknown device. Aborting..."
		exit 1
	fi

	cd $REPACK_PATH
	# delete placeholder files
	find . -name placeholder -delete

	# copy kernel image
	cp $BUILD_PATH/arch/arm/boot/zImage$INDTB $REPACK_PATH/zImage$INDTB

	# replace variables in anykernel script
	cd $REPACK_PATH
	KERNELNAME="Flashing $KERNEL_NAME"
	sed -i "s;###kernelname###;${KERNELNAME};" META-INF/com/google/android/update-binary;
	COPYRIGHT=$(echo '(c) acroreiser, 2022')
	sed -i "s;###copyright###;${COPYRIGHT};" META-INF/com/google/android/update-binary;
	BUILDINFO="Release ${BUILD_NUMBER}, $DATE"
	sed -i "s;###buildinfo###;${BUILDINFO};" META-INF/com/google/android/update-binary;

	# create zip file
	zip -r9 ${KERNEL_NAME}-r${BUILD_NUMBER}.zip * -x ${KERNEL_NAME}-r${BUILD_NUMBER}.zip

	# sign recovery zip if there are keys available
	if [ -f "$BUILD_PATH/tools_boeffla/testkey.x509.pem" ]; then
		java -jar $BUILD_PATH/tools_boeffla/signapk.jar -w $BUILD_PATH/tools_boeffla/testkey.x509.pem $BUILD_PATH/tools_boeffla/testkey.pk8 ${KERNEL_NAME}-r${BUILD_NUMBER}.zip ${KERNEL_NAME}-r${BUILD_NUMBER}-signed.zip
		cp ${KERNEL_NAME}-r${BUILD_NUMBER}-signed.zip $BUILD_PATH/${KERNEL_NAME}-r${BUILD_NUMBER}-${DEVICE}-signed.zip
		md5sum $BUILD_PATH/${KERNEL_NAME}-r${BUILD_NUMBER}-${DEVICE}-signed.zip > $BUILD_PATH/${KERNEL_NAME}-r${BUILD_NUMBER}-${DEVICE}-signed.zip.md5
	else
		cp ${KERNEL_NAME}-r${BUILD_NUMBER}.zip $BUILD_PATH/${KERNEL_NAME}-r${BUILD_NUMBER}-${DEVICE}.zip
		md5sum $BUILD_PATH/${KERNEL_NAME}-r${BUILD_NUMBER}-${DEVICE}.zip > $BUILD_PATH/${KERNEL_NAME}-r${BUILD_NUMBER}-${DEVICE}.zip.md5
	fi

	cd $BUILD_PATH
	return 0
}

# Pre

while getopts "hvO:j:Kd:kB:S:N:CU:H:t:V" opt
do
case $opt in
	h) usage; exit 0;;
	v) echo $VERSION; exit 0;;
	t) TOOLCHAIN=$OPTARG;;
	j) THREADS=$OPTARG;;
	O) CUST_CONF=$OPTARG; DEFCONFIG=custom; KCONF_REPLACE=true;;
	C) CLEAN=true;;
	N) BUILD_NUMBER=$OPTARG;;
	H) BUILD_HOST=$OPTARG;;
	V) VERBOSE=1;;
	S) DEVICE=$OPTARG;;
	K) KCONFIG=true;;
	k) DONTPACK=true;;
	d) DEFCONFIG="$OPTARG"; KCONF_REPLACE=true;;
	U) USER=$OPTARG;;
	*) usage; exit 0;;
esac
done

if [ -z $THREADS ]; then
	THREADS=$(nproc --all)
fi

JOBS="-j$THREADS"


if [ -d "`pwd`/.tmpzip" ]; then
	rm -fr "`pwd`/.tmpzip"
fi
REPACK_PATH=`pwd`/.tmpzip
mkdir -p $REPACK_PATH

# ENTRY POINT
echo "Koffee build script v$VERSION"
echo $DATE

if [ "$DEFCONFIG" == "" ];
then
	if [ ! -f "$BUILD_PATH/.config" ]; then
		echo "FATAL: No config specified!" 
		echo "*** BUILD FAILED ***"
		exit 1 
	fi
	DEFCONFIG=".config"
fi


if [ -z $TOOLCHAIN ]; then
	echo "FATAL: No toolchain prefix specified!" 
	echo "*** BUILD FAILED ***"
	exit 1
fi

if [ "$CLEAN" = "true" ]; then
	prepare &>/dev/null
fi 
if [ "$DEFCONFIG" != ".config" ] || [ "$KCONFIG" == "true" ]; then
	make_config
fi
TVERSION=$(${TOOLCHAIN}gcc --version | grep gcc)

if [ -z $BUILD_NUMBER ]; then 
	if [ -f "`pwd`/.version" ]; then
		BVERN=$(cat `pwd`/.version)
	else
		BVERN=1
	fi
else
	BVERN=$BUILD_NUMBER
fi

if [ $? -eq 0 ]; then
	echo "--------------------------------------"
	echo "| Build  date:	$DATE"
	echo "| Configuration file:	$DEFCONFIG"
	echo "| Release:	$BVERN"
	echo "| Building for:	$DEVICE"
	echo "| Build  user:	$USER"
	echo "| Build  host:	$BUILD_HOST"
	echo "| Build  toolchain:	$TVERSION"
	echo "| Number of threads:	$THREADS"
	echo "--------------------------------------"
else
	echo "*** CONFIGURATION FAILED ***"
	exit 1
fi
echo "*** NOW WE GO! ***"
echo "---- Stage 1: Building the kernel ----"
build_kernel
if [ $? -eq 0 ]; then
	echo "*** Kernel is ready! ***"
else
	echo "*** zImage BUILD FAILED ***"
	exit 1
fi

#echo "---- Stage 2: Building the Device Tree ----"
# make the kernel backward-compatible Oreo and Pie
#	build_dtbs
		
if [ "$SKIP_MODULES" = "false" ]; then
	echo "---- Stage 3: Building modules ----"
	build_modules
	if [ $? -eq 0 ]; then
		echo "*** Modules is ready! ***"
	else
		echo "*** MODULE BUILD FAILED ***"
		exit 1
	fi
else
	echo "---- Stage 3(skipped): Building modules ----"
fi

if [ "$DONTPACK" = "false" ]; then
	echo "---- Stage 4: Packing all stuff ----"
	make_flashable
	if [ $? -eq 0 ]; then
		echo "--------------------------------------"
		cat $REPACK_PATH/info.txt
		echo "> Flashable ZIP: $(ls | grep ${KERNEL_NAME}-r${BUILD_NUMBER}-${DEVICE} | grep .zip | head -n 1)"
		echo "> MD5sum: $(ls | grep ${KERNEL_NAME}-r${BUILD_NUMBER}-${DEVICE} | grep .md5)"
		echo "--------------------------------------"
		echo "*** $KERNEL_NAME is ready! ***"
	else
		echo "*** KOFFEE ZIP BUILD FAILED ***"
		exit 1
	fi
	rm -fr .tmpzip
else
	echo "---- Stage 3(skipped): Packing all stuff ----"
	echo "--------------------------------------"
	echo "--------------------------------------"
	echo "| Build  date:	$DATE"
	echo "| Configuration file:	$DEFCONFIG"
	echo "| Release:	$BVERN"
	echo "| Building for:	$DEVICE"
	echo "| Build  user:	$USER"
	echo "| Build  host:	$BUILD_HOST"
	echo "| Build  toolchain:	$TVERSION"
	echo "| Number of threads:	$THREADS"
	echo "> zImage: arch/arm/boot/zImage"
	echo "--------------------------------------"
	echo "*** $KERNEL_NAME is ready! ***"
fi
