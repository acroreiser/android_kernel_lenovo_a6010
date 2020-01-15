#!/system/bin/sh
# Koffee's (now Infernal's) startup script
# running immediatelly after mounting /firmware
# do not edit!


# Start irq balancer daemon
/persist/infernal/sbin/busybox chrt -i 0 /persist/infernal/sbin/irqbalance


# Setup BFQIO CGroup
mkdir /dev/bfqio
/persist/infernal/sbin/busybox mount -t cgroup none /dev/bfqio -o bfqio
sleep 0.5
mkdir /dev/bfqio/rt-display
chmod 0664 /dev/bfqio/*
chmod 0220 /dev/bfqio/cgroup.event_control
chmod 0664 /dev/bfqio/rt-display/*
chmod 0755 /dev/bfqio
chmod 0755 /dev/bfqio/rt-display
chmod 0220 /dev/bfqio/rt-display/cgroup.event_control
chown -R root:system /dev/bfqio
echo 1 > /dev/bfqio/rt-display/bfqio.ioprio_class

# Setup ZRAM
/persist/infernal/sbin/busybox swapoff /dev/block/zram0
sleep 0.5
ZMEM=$(cat /proc/meminfo | grep MemTotal | awk  '{print $2}')
let 'ZMEM=((ZMEM/100)*40)*1024'
echo 1 > /sys/block/zram0/reset
echo 'lz4' > /sys/block/zram0/comp_algorithm
sleep 0.5
echo $ZMEM > /sys/block/zram0/disksize
sleep 0.5
/persist/infernal/sbin/busybox mkswap /dev/block/zram0
sleep 0.5
/persist/infernal/sbin/busybox swapon /dev/block/zram0
echo 100 > /proc/sys/vm/swappiness

# Enavle Laptop mode
echo 1 > /proc/sys/vm/laptop_mode

# Start ureadahead daemon (ported from Ubuntu)
# to speed up booting
mkdir -m 0777 /data/ureadahead
/persist/infernal/sbin/ureadahead --daemon --timeout=35


# Turn /cache to sync mode
/persist/infernal/sbin/busybox mount -o remount,sync /cache

exit 0