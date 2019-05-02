#define MSM_LIMIT			"msm_limiter"
#define LIMITER_ENABLED			0
#define DEFAULT_SUSPEND_DEFER_TIME	10 
#if defined(CONFIG_ARCH_MSM8916)
#define DEFAULT_SUSPEND_FREQUENCY	998400
#else
#define DEFAULT_SUSPEND_FREQUENCY	1728000
#endif
#if defined(CONFIG_ARCH_APQ8084)
#define DEFAULT_RESUME_FREQUENCY	2649600
#elif defined(CONFIG_ARCH_MSM8916)
#define DEFAULT_RESUME_FREQUENCY	1209600
#else
#define DEFAULT_RESUME_FREQUENCY	2265600
#endif
#if defined(CONFIG_ARCH_MSM8916)
#define DEFAULT_MIN_FREQUENCY		200000
#else
#define DEFAULT_MIN_FREQUENCY		300000
#endif

static struct cpu_limit {
	unsigned int limiter_enabled;
	uint32_t suspend_max_freq;
	uint32_t suspend_min_freq;
	unsigned int suspended;
	unsigned int suspend_defer_time;
	struct delayed_work suspend_work;
	struct work_struct resume_work;
	struct mutex resume_suspend_mutex;
	struct mutex msm_limiter_mutex[4];
	struct notifier_block notif;
} limit = {
	.limiter_enabled = LIMITER_ENABLED,
	.suspend_max_freq = DEFAULT_SUSPEND_FREQUENCY,
	.suspend_min_freq = DEFAULT_MIN_FREQUENCY,
	.suspend_defer_time = DEFAULT_SUSPEND_DEFER_TIME,
};
