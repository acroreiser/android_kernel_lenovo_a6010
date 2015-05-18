obj-y += time.o timer.o hrtimer.o itimer.o posix-timers.o posix-cpu-timers.o
obj-y += timekeeping.o ntp.o clocksource.o jiffies.o timer_list.o
obj-y += timeconv.o posix-clock.o alarmtimer.o

obj-$(CONFIG_GENERIC_CLOCKEVENTS_BUILD)		+= clockevents.o
obj-$(CONFIG_GENERIC_CLOCKEVENTS)		+= tick-common.o
obj-$(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST)	+= tick-broadcast.o tick-broadcast-hrtimer.o
obj-$(CONFIG_GENERIC_SCHED_CLOCK)		+= sched_clock.o
obj-$(CONFIG_TICK_ONESHOT)			+= tick-oneshot.o
obj-$(CONFIG_TICK_ONESHOT)			+= tick-sched.o

$(obj)/time.o: $(objtree)/include/config/

