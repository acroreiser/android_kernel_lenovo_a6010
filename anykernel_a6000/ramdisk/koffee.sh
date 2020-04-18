#!/system/bin/sh
# Koffee's (now Infernal's) startup script
# running immediatelly after mounting /firmware
# do not edit!

# Enavle Laptop mode
echo 1 > /proc/sys/vm/laptop_mode

# Turn /cache to sync mode
/persist/infernal/sbin/busybox mount -o remount,sync /cache

exit 0