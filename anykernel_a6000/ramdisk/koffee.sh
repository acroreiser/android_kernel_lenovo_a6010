#!/system/bin/sh
# Koffee's (now Hyperloop's) startup script
# running immediatelly after mounting /system
# do not edit!

# sleep 15

# currently do nothing #

/sbin/busybox mount -o remount,rw /
/sbin/busybox rm /koffee.sh
/sbin/busybox mount -o remount,ro /
exit 0