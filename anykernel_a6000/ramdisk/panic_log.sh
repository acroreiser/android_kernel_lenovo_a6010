#!/system/bin/sh
# Koffee's (now Hyperloop's) startup script
# running immediatelly after mounting /system
# do not edit!

# Remount fs to sync mode
/system/xbin/busybox mount -o remount,sync /data

# Dump kernel log to file
/system/bin/dmesg > /data/last_kmsg.log

exit 0