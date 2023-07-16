#!/bin/sh
if [ -f ".config" ];
then
	cat .config > arch/arm/configs/lineageos_a6010_defconfig
	git add arch/arm/configs/lineageos_a6010_defconfig
	git commit -m "a6010: update defconfig (scripted)"
fi
