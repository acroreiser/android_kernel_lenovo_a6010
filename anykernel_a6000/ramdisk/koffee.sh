#!/system/bin/sh
# Koffee's (now Infernal's) startup script
# running immediatelly after mounting /firmware
# do not edit!

# Enavle Laptop mode
echo 1 > /proc/sys/vm/laptop_mode

# Start ureadahead daemon (ported from Ubuntu)
# to speed up booting
mkdir -m 0777 /data/ureadahead
/persist/infernal/sbin/ureadahead --daemon --timeout=35

# Turn /cache to sync mode
/persist/infernal/sbin/busybox mount -o remount,sync /cache

exit 0