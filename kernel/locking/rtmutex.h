
static inline void rt_mutex_print_deadlock(struct rt_mutex_waiter *w)
{
	WARN(1, "rtmutex deadlock detected\n");
}
