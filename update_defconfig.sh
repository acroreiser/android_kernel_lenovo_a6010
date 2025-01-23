#!/bin/sh
if [ -f ".config" ];
then
	if [ -f "arch/arm/configs/lineageos_$1_defconfig" ];
	then
		cat .config > arch/arm/configs/lineageos_$1_defconfig
		git add arch/arm/configs/lineageos_$1_defconfig
		git commit -m "$1: update defconfig (scripted)"
	else
		echo "no defconfig for $1, nothing to do"
	fi
else
	echo "no .config, nothing new"
fi
