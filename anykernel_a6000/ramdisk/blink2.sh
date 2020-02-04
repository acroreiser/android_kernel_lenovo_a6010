#!/system/bin/sh
if [ "$1" == 1 ]; then
	echo '0' > /sys/class/leds/red/brightness;
	echo 'blinking' > /sys/class/leds/green/trigger;
else
	echo 'none' > /sys/class/leds/green/trigger;
	echo '255' > /sys/class/leds/red/brightness;
fi

