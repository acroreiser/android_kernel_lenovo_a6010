#!/system/xbin/bash
# Koffee's (Hyperloop) startup script
# running immediatelly after mounting /system
# do not edit!

tc qdisc add dev rmnet_data0 root fq_codel
tc qdisc add dev wlan0 root fq_codel

/sbin/busybox mount -o remount,rw /
/sbin/busybox rm /koffee.sh
/sbin/busybox mount -o remount,ro /
exit 0