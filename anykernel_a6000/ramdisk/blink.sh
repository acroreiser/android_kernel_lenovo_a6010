#!/system/bin/sh
if [ "$1" == 1 ]; then
	echo 'none' > /sys/class/leds/red/trigger;
	echo 'blinking' > /sys/class/leds/green/trigger;
else
	echo 'battery-charging' > /sys/class/leds/red/trigger;
	echo 'battery-full' > /sys/class/leds/green/trigger;

	echo '255' > /sys/class/leds/red/brightness;
fi

