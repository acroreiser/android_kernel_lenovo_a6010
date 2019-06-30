#!/system/bin/sh
# Koffee's (now Hyperloop's) startup script
# running immediatelly after mounting /system
# do not edit!

 /system/bin/dmesg > /data/last_kmsg.log
 /system/bin/sync
 /system/bin/reboot

# currently do nothing #
exit 0