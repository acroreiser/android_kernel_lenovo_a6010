#!/system/bin/sh
# Koffee's (now Hyperloop's) startup script
# running immediatelly after mounting /system
# do not edit!
/system/xbin/busybox mount -o remount,sync /data
 /system/bin/dmesg > /data/last_kmsg.log

# currently do nothing #
exit 0