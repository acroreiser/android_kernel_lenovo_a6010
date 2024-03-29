/*
 * CPUFreq governor based on scheduler-provided CPU utilization data.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpufreq.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/irq_work.h>
#include <linux/slab.h>
#include <trace/events/power.h>

#include "../../drivers/cpufreq/cpufreq_governor.h"
#include "sched.h"

#define SUGOV_KTHREAD_PRIORITY	50

struct sugov_tunables {
	unsigned int up_rate_limit_us;
	unsigned int down_rate_limit_us;
	unsigned long hispeed_freq;
	unsigned int hispeed_load;
};

struct sugov_policy {
	struct cpufreq_policy *policy;

	struct sugov_tunables *tunables;
	struct list_head tunables_hook;

	raw_spinlock_t update_lock;  /* For shared policies */
	u64 last_freq_update_time;
	s64 min_rate_limit_ns;
	s64 up_rate_delay_ns;
	s64 down_rate_delay_ns;
	unsigned int next_freq;

	/* The next fields are only needed if fast switch cannot be used. */
	struct irq_work irq_work;
	struct kthread_work work;
	struct mutex work_lock;
	struct kthread_worker worker;
	struct task_struct *thread;
	bool work_in_progress;

	bool need_freq_update;
};

struct sugov_cpu {
	struct update_util_data update_util;
	struct sugov_policy *sg_policy;

	/* The fields below are only needed when sharing a policy. */
	unsigned long util;
	unsigned long max;
	u64 last_update;

	/* The field below is for single-CPU policies only. */
#ifdef CONFIG_NO_HZ_COMMON
	unsigned long saved_idle_calls;
#endif
};

static DEFINE_PER_CPU(struct sugov_cpu, sugov_cpu);

static struct attribute_group *get_sysfs_attr(void);

/************************ Governor internals ***********************/

static bool sugov_should_update_freq(struct sugov_policy *sg_policy, u64 time)
{
	s64 delta_ns;

	if (unlikely(sg_policy->need_freq_update)) {
		return true;
	}

	delta_ns = time - sg_policy->last_freq_update_time;

	/* No need to recalculate next freq for min_rate_limit_us at least */
	return delta_ns >= sg_policy->min_rate_limit_ns;
}

static bool sugov_up_down_rate_limit(struct sugov_policy *sg_policy, u64 time,
				     unsigned int next_freq)
{
	s64 delta_ns;

	delta_ns = time - sg_policy->last_freq_update_time;

	if (next_freq > sg_policy->next_freq &&
	    delta_ns < sg_policy->up_rate_delay_ns)
			return true;

	if (next_freq < sg_policy->next_freq &&
	    delta_ns < sg_policy->down_rate_delay_ns)
			return true;

	return false;
}

static void sugov_update_commit(struct sugov_policy *sg_policy, u64 time,
				unsigned int next_freq)
{
	struct cpufreq_policy *policy = sg_policy->policy;

	if (sugov_up_down_rate_limit(sg_policy, time, next_freq))
		return;

	if (sg_policy->next_freq != next_freq) {
		sg_policy->next_freq = next_freq;
		sg_policy->last_freq_update_time = time;
		sg_policy->work_in_progress = true;
		irq_work_queue(&sg_policy->irq_work);
	}
}

/**
 * get_next_freq - Compute a new frequency for a given cpufreq policy.
 * @policy: cpufreq policy object to compute the new frequency for.
 * @util: Current CPU utilization.
 * @max: CPU capacity.
 *
 * If the utilization is frequency-invariant, choose the new frequency to be
 * proportional to it, that is
 *
 * next_freq = C * max_freq * util / max
 *
 * Otherwise, approximate the would-be frequency-invariant utilization by
 * util_raw * (curr_freq / max_freq) which leads to
 *
 * next_freq = C * curr_freq * util_raw / max
 *
 * Take C = 1.25 for the frequency tipping point at (util / max) = 0.8.
 */
static unsigned int get_next_freq(struct cpufreq_policy *policy,
				  unsigned long util, unsigned long max)
{
	unsigned int freq = arch_scale_freq_invariant() ?
				policy->cpuinfo.max_freq : policy->cur;
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned long hs_util;
	unsigned long target_freq = (freq + (freq >> 2)) * util / max;

	if(sg_policy->tunables->hispeed_freq == 0 ||
		sg_policy->tunables->hispeed_load == 0)
		return target_freq;

	hs_util = mult_frac(max,
					   sg_policy->tunables->hispeed_load,
					   100);

	if (util >= hs_util &&
		sg_policy->tunables->hispeed_freq > target_freq)
		return sg_policy->tunables->hispeed_freq;
	else
		return target_freq;
}

#ifdef CONFIG_NO_HZ_COMMON
static bool sugov_cpu_is_busy(struct sugov_cpu *sg_cpu)
{
	unsigned long idle_calls = tick_nohz_get_idle_calls();
	bool ret = idle_calls == sg_cpu->saved_idle_calls;

	sg_cpu->saved_idle_calls = idle_calls;
	return ret;
}
#else
static inline bool sugov_cpu_is_busy(struct sugov_cpu *sg_cpu) { return false; }
#endif /* CONFIG_NO_HZ_COMMON */

static void sugov_update_single(struct update_util_data *hook, u64 time,
				unsigned long util, unsigned long max)
{
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned int next_f;
	bool busy;

	if (!sugov_should_update_freq(sg_policy, time))
		return;

	busy = sugov_cpu_is_busy(sg_cpu);

	/*
	 * Do not reduce the frequency if the CPU has not been idle
	 * recently, as the reduction is likely to be premature then.
	 */
	if (busy && next_f < sg_policy->next_freq &&
		   sg_policy->next_freq != UINT_MAX)
		next_f = sg_policy->next_freq;
	else
		next_f = util == ULONG_MAX ? policy->cpuinfo.max_freq :
				get_next_freq(policy, util, max);

	sugov_update_commit(sg_policy, time, next_f);
}

static unsigned int sugov_next_freq_shared(struct sugov_policy *sg_policy,
					   unsigned long util, unsigned long max, u64 time)
{
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned int max_f = policy->cpuinfo.max_freq;
	unsigned int j;

	if (util == ULONG_MAX)
		return max_f;

	for_each_cpu(j, policy->cpus) {
		struct sugov_cpu *j_sg_cpu;
		unsigned long j_util, j_max;
		s64 delta_ns;

		if (j == smp_processor_id())
			continue;

		j_sg_cpu = &per_cpu(sugov_cpu, j);
		/*
		 * If the CPU utilization was last updated before the previous
		 * frequency update and the time elapsed between the last update
		 * of the CPU utilization and the last frequency update is long
		 * enough, don't take the CPU into account as it probably is
		 * idle now.
		 */
		delta_ns = time - j_sg_cpu->last_update;
		if (delta_ns > TICK_NSEC)
			continue;

		j_util = j_sg_cpu->util;
		if (j_util == ULONG_MAX)
			return max_f;

		j_max = j_sg_cpu->max;
		if (j_util * max >= j_max * util) {
			util = j_util;
			max = j_max;
		}
	}

	return get_next_freq(policy, util, max);
}

static void sugov_update_shared(struct update_util_data *hook, u64 time,
				unsigned long util, unsigned long max)
{
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	unsigned int next_f;

	raw_spin_lock(&sg_policy->update_lock);

	sg_cpu->util = util;
	sg_cpu->max = max;
	sg_cpu->last_update = time;

	if (sugov_should_update_freq(sg_policy, time)) {
		next_f = sugov_next_freq_shared(sg_policy, util, max, time);
		sugov_update_commit(sg_policy, time, next_f);
	}

	raw_spin_unlock(&sg_policy->update_lock);
}

static void sugov_work(struct kthread_work *work)
{
	struct sugov_policy *sg_policy = container_of(work, struct sugov_policy, work);

	mutex_lock(&sg_policy->work_lock);
	__cpufreq_driver_target(sg_policy->policy, sg_policy->next_freq,
				CPUFREQ_RELATION_L);
	mutex_unlock(&sg_policy->work_lock);

	sg_policy->work_in_progress = false;
}

static void sugov_irq_work(struct irq_work *irq_work)
{
	struct sugov_policy *sg_policy;

	sg_policy = container_of(irq_work, struct sugov_policy, irq_work);

	/*
	 * For Real Time and Deadline tasks, schedutil governor shoots the
	 * frequency to maximum. And special care must be taken to ensure that
	 * this kthread doesn't result in that.
	 *
	 * This is (mostly) guaranteed by the work_in_progress flag. The flag is
	 * updated only at the end of the sugov_work() and before that schedutil
	 * rejects all other frequency scaling requests.
	 *
	 * Though there is a very rare case where the RT thread yields right
	 * after the work_in_progress flag is cleared. The effects of that are
	 * neglected for now.
	 */
	kthread_queue_work(&sg_policy->worker, &sg_policy->work);
}

/************************** sysfs interface ************************/
static struct sugov_tunables *global_tunables;
static DEFINE_MUTEX(global_tunables_lock);
static DEFINE_MUTEX(min_rate_lock);


static void update_min_rate_limit_us(struct sugov_policy *sg_policy)
{
	mutex_lock(&min_rate_lock);
	sg_policy->min_rate_limit_ns = min(sg_policy->up_rate_delay_ns,
					   sg_policy->down_rate_delay_ns);
	mutex_unlock(&min_rate_lock);
}

static ssize_t show_sys_up_rate_limit_us(struct sugov_tunables *tunables, char *buf)
{
	return sprintf(buf, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t store_sys_up_rate_limit_us(struct sugov_tunables *tunables, const char *buf,
				   size_t count)
{
	unsigned int up_rate_limit_us;
	int cpu;

	if (kstrtouint(buf, 10, &up_rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = up_rate_limit_us;

	for_each_present_cpu(cpu) {
		struct sugov_cpu *sugov_cpu_i = &per_cpu(sugov_cpu, cpu);
		struct sugov_policy *sg_policy_cpu = sugov_cpu_i->sg_policy;

		sg_policy_cpu->tunables->up_rate_limit_us = up_rate_limit_us;
		sg_policy_cpu->up_rate_delay_ns = up_rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_us(sg_policy_cpu);
	}

	return count;
}

static ssize_t show_up_rate_limit_us(struct sugov_policy *sg_policy, char *buf)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	return sprintf(buf, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t store_up_rate_limit_us(struct sugov_policy *sg_policy, const char *buf,
				   size_t count)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	unsigned int up_rate_limit_us;
	int cpu;

	if (kstrtouint(buf, 10, &up_rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = up_rate_limit_us;
	sg_policy->up_rate_delay_ns = up_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_us(sg_policy);

	return count;
}

static ssize_t show_sys_down_rate_limit_us(struct sugov_tunables *tunables, char *buf)
{
	return sprintf(buf, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t store_sys_down_rate_limit_us(struct sugov_tunables *tunables, const char *buf,
				   size_t count)
{
	unsigned int down_rate_limit_us;
	int cpu;

	if (kstrtouint(buf, 10, &down_rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = down_rate_limit_us;

	for_each_present_cpu(cpu) {
		struct sugov_cpu *sugov_cpu_i = &per_cpu(sugov_cpu, cpu);
		struct sugov_policy *sg_policy_cpu = sugov_cpu_i->sg_policy;

		sg_policy_cpu->tunables->down_rate_limit_us = down_rate_limit_us;
		sg_policy_cpu->down_rate_delay_ns = down_rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_us(sg_policy_cpu);
	}

	return count;
}

static ssize_t show_down_rate_limit_us(struct sugov_policy *sg_policy, char *buf)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	return sprintf(buf, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t store_down_rate_limit_us(struct sugov_policy *sg_policy, const char *buf,
				   size_t count)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	unsigned int down_rate_limit_us;
	int cpu;

	if (kstrtouint(buf, 10, &down_rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = down_rate_limit_us;
	sg_policy->down_rate_delay_ns = down_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_us(sg_policy);

	return count;
}

static ssize_t show_sys_hispeed_freq(struct sugov_tunables *tunables, char *buf)
{
	return sprintf(buf, "%lu\n", tunables->hispeed_freq);
}

static ssize_t store_sys_hispeed_freq(struct sugov_tunables *tunables, const char *buf,
				   size_t count)
{
	long unsigned int hispeed_freq;
	int cpu;

	if (kstrtoul(buf, 0, &hispeed_freq))
		return -EINVAL;

	for_each_present_cpu(cpu) {
		struct sugov_cpu *sugov_cpu_i = &per_cpu(sugov_cpu, cpu);
		struct sugov_policy *sg_policy_cpu = sugov_cpu_i->sg_policy;


		if (hispeed_freq != 0 && hispeed_freq < sg_policy_cpu->policy->cpuinfo.min_freq)
			hispeed_freq = sg_policy_cpu->policy->cpuinfo.min_freq;
		else if (hispeed_freq > sg_policy_cpu->policy->cpuinfo.max_freq)
			hispeed_freq = sg_policy_cpu->policy->cpuinfo.max_freq;

		sg_policy_cpu->tunables->hispeed_freq = hispeed_freq;
	}
	tunables->hispeed_freq = hispeed_freq;

	return count;
}

static ssize_t show_hispeed_freq(struct sugov_policy *sg_policy, char *buf)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	return sprintf(buf, "%lu\n", tunables->hispeed_freq);
}

static ssize_t store_hispeed_freq(struct sugov_policy *sg_policy, const char *buf,
				   size_t count)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	struct sugov_cpu *sugov_cpu_i = &per_cpu(sugov_cpu, 0);
	long unsigned int hispeed_freq;
	int cpu;

	if (kstrtoul(buf, 0, &hispeed_freq))
		return -EINVAL;


	if (hispeed_freq == 0)
	{
		tunables->hispeed_freq = 0;
		return count;
	}

	if (hispeed_freq < sg_policy->policy->cpuinfo.min_freq)
		hispeed_freq = sg_policy->policy->cpuinfo.min_freq;

	if (hispeed_freq > sg_policy->policy->cpuinfo.max_freq)
		hispeed_freq = sg_policy->policy->cpuinfo.max_freq;

	tunables->hispeed_freq = hispeed_freq;

	return count;
}

static ssize_t show_sys_hispeed_load(struct sugov_tunables *tunables, char *buf)
{
	return sprintf(buf, "%u\n", tunables->hispeed_load);
}

static ssize_t store_sys_hispeed_load(struct sugov_tunables *tunables, const char *buf,
				   size_t count)
{
	unsigned int hispeed_load;
	int cpu;

	if (kstrtouint(buf, 0, &hispeed_load))
		return -EINVAL;

	if (hispeed_load > 100 || hispeed_load < 5)
		return -EINVAL;

	for_each_present_cpu(cpu) {
		struct sugov_cpu *sugov_cpu_i = &per_cpu(sugov_cpu, cpu);
		struct sugov_policy *sg_policy_cpu = sugov_cpu_i->sg_policy;

		sg_policy_cpu->tunables->hispeed_load = hispeed_load;
	}
	tunables->hispeed_load = hispeed_load;

	return count;
}

static ssize_t show_hispeed_load(struct sugov_policy *sg_policy, char *buf)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	return sprintf(buf, "%u\n", tunables->hispeed_load);
}

static ssize_t store_hispeed_load(struct sugov_policy *sg_policy, const char *buf,
				   size_t count)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	struct sugov_cpu *sugov_cpu_i = &per_cpu(sugov_cpu, 0);
	unsigned int hispeed_load;
	int cpu;

	if (kstrtouint(buf, 0, &hispeed_load))
		return -EINVAL;

	if (hispeed_load > 100 || hispeed_load < 5)
		return -EINVAL;

	tunables->hispeed_load = hispeed_load;

	return count;
}

/*
 * Create show/store routines
 * - sys: One governor instance for complete SYSTEM
 * - pol: One governor instance per struct cpufreq_policy
 */
#define show_gov_pol_sys(file_name)					\
static ssize_t show_##file_name##_gov_sys				\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return show_sys_##file_name(global_tunables, buf);			\
}									\
									\
static ssize_t show_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, char *buf)				\
{									\
	return show_##file_name(policy->governor_data, buf);		\
}

#define store_gov_pol_sys(file_name)					\
static ssize_t store_##file_name##_gov_sys				\
(struct kobject *kobj, struct attribute *attr, const char *buf,		\
	size_t count)							\
{									\
	return store_sys_##file_name(global_tunables, buf, count);		\
}									\
									\
static ssize_t store_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, const char *buf, size_t count)		\
{									\
	return store_##file_name(policy->governor_data, buf, count);	\
}

#define show_store_gov_pol_sys(file_name)				\
show_gov_pol_sys(file_name);						\
store_gov_pol_sys(file_name)

show_store_gov_pol_sys(up_rate_limit_us);
show_store_gov_pol_sys(down_rate_limit_us);
show_store_gov_pol_sys(hispeed_freq);
show_store_gov_pol_sys(hispeed_load);

#define gov_sys_pol_attr_rw(_name)					\
	gov_sys_attr_rw(_name);						\
	gov_pol_attr_rw(_name)

gov_sys_pol_attr_rw(up_rate_limit_us);
gov_sys_pol_attr_rw(down_rate_limit_us);
gov_sys_pol_attr_rw(hispeed_freq);
gov_sys_pol_attr_rw(hispeed_load);

/* One Governor instance for entire system */
static struct attribute *sugov_attributes_gov_sys[] = {
	&up_rate_limit_us_gov_sys.attr,
	&down_rate_limit_us_gov_sys.attr,
	&hispeed_freq_gov_sys.attr,
	&hispeed_load_gov_sys.attr,
	NULL
};


static struct attribute_group sugov_attr_group_gov_sys = {
	.attrs = sugov_attributes_gov_sys,
	.name = "schedutil",
};

static struct attribute *sugov_attributes_gov_pol[] = {
	&up_rate_limit_us_gov_pol.attr,
	&down_rate_limit_us_gov_pol.attr,
	&hispeed_freq_gov_pol.attr,
	&hispeed_load_gov_pol.attr,
	NULL
};

static struct attribute_group sugov_attr_group_gov_pol = {
	.attrs = sugov_attributes_gov_pol,
	.name = "schedutil",
};

static struct attribute_group *get_sysfs_attr(void)
{
	if (have_governor_per_policy())
		return &sugov_attr_group_gov_pol;
	else
		return &sugov_attr_group_gov_sys;
}

/********************** cpufreq governor interface *********************/

static struct sugov_policy *sugov_policy_alloc(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy;

	sg_policy = kzalloc(sizeof(*sg_policy), GFP_KERNEL);
	if (!sg_policy)
		return NULL;

	sg_policy->policy = policy;
	init_irq_work(&sg_policy->irq_work, sugov_irq_work);
	mutex_init(&sg_policy->work_lock);
	raw_spin_lock_init(&sg_policy->update_lock);
	return sg_policy;
}

static void sugov_policy_free(struct sugov_policy *sg_policy)
{
	mutex_destroy(&sg_policy->work_lock);
	kfree(sg_policy);
}

static int sugov_kthread_create(struct sugov_policy *sg_policy)
{
	struct task_struct *thread;
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO / 2 };
	struct cpufreq_policy *policy = sg_policy->policy;
	int ret;

	kthread_init_work(&sg_policy->work, sugov_work);
	kthread_init_worker(&sg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &sg_policy->worker,
				"sugov:%d",
				cpumask_first(policy->related_cpus));
	if (IS_ERR(thread)) {
		pr_err("failed to create sugov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setscheduler_nocheck(thread, SCHED_FIFO, &param);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set SCHED_FIFO\n", __func__);
		return ret;
	}

	sg_policy->thread = thread;
	kthread_bind_mask(thread, policy->related_cpus);
	wake_up_process(thread);

	return 0;
}

static void sugov_kthread_stop(struct sugov_policy *sg_policy)
{
	kthread_flush_worker(&sg_policy->worker);
	kthread_stop(sg_policy->thread);
}

static struct sugov_tunables *sugov_tunables_alloc(struct sugov_policy *sg_policy)
{
	struct sugov_tunables *tunables;

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (tunables) {
		if (!have_governor_per_policy())
			global_tunables = tunables;
	}
	return tunables;
}

static void sugov_tunables_free(struct sugov_tunables *tunables)
{
	if (!have_governor_per_policy())
		global_tunables = NULL;

	kfree(tunables);
}

static int sugov_init(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy;
	struct sugov_tunables *tunables;
	int ret = 0;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	sg_policy = sugov_policy_alloc(policy);
	if (!sg_policy)
		return -ENOMEM;

	ret = sugov_kthread_create(sg_policy);
	if (ret)
		goto free_sg_policy;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		policy->governor_data = sg_policy;
		sg_policy->tunables = global_tunables;

		goto out;
	}

	tunables = sugov_tunables_alloc(sg_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto stop_kthread;
	}

	if (policy->up_transition_delay_us && policy->down_transition_delay_us) {
		tunables->up_rate_limit_us = policy->up_transition_delay_us;
		tunables->down_rate_limit_us = policy->down_transition_delay_us;
	} else {
		unsigned int lat;

                tunables->up_rate_limit_us = LATENCY_MULTIPLIER;
                tunables->down_rate_limit_us = LATENCY_MULTIPLIER;
		lat = policy->cpuinfo.transition_latency / NSEC_PER_USEC;
		if (lat) {
                        tunables->up_rate_limit_us *= lat;
                        tunables->down_rate_limit_us *= lat;
                }
	}

	tunables->hispeed_freq = policy->max;
	tunables->hispeed_load = 85;

	policy->governor_data = sg_policy;
	sg_policy->tunables = tunables;

	ret = sysfs_create_group(get_governor_parent_kobj(policy),
			get_sysfs_attr());
	if (ret) {
		if (!have_governor_per_policy()) {
			global_tunables = NULL;
		}
		goto fail;
	}

 out:
	mutex_unlock(&global_tunables_lock);

	return 0;

 fail:
	policy->governor_data = NULL;
	sugov_tunables_free(tunables);

 stop_kthread:
	sugov_kthread_stop(sg_policy);

 free_sg_policy:
	mutex_unlock(&global_tunables_lock);

	sugov_policy_free(sg_policy);
	pr_err("cpufreq: schedutil governor initialization failed (error %d)\n", ret);
	return ret;
}

static int sugov_exit(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	struct sugov_tunables *tunables = sg_policy->tunables;

	mutex_lock(&global_tunables_lock);
	sysfs_remove_group(get_governor_parent_kobj(policy),
			get_sysfs_attr());

	policy->governor_data = NULL;

	sugov_tunables_free(tunables);

	mutex_unlock(&global_tunables_lock);

	sugov_kthread_stop(sg_policy);
	sugov_policy_free(sg_policy);
	return 0;
}

static int sugov_start(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	sg_policy->up_rate_delay_ns =
		sg_policy->tunables->up_rate_limit_us * NSEC_PER_USEC;
	sg_policy->down_rate_delay_ns =
		sg_policy->tunables->down_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_us(sg_policy);
	sg_policy->last_freq_update_time = 0;
	sg_policy->next_freq = 0;
	sg_policy->work_in_progress = false;
	sg_policy->need_freq_update = false;

	for_each_cpu(cpu, policy->cpus) {
		struct sugov_cpu *sg_cpu = &per_cpu(sugov_cpu, cpu);

		sg_cpu->sg_policy = sg_policy;
		if (policy_is_shared(policy)) {
			sg_cpu->util = ULONG_MAX;
			sg_cpu->max = 0;
			sg_cpu->last_update = 0;
			cpufreq_add_update_util_hook(cpu, &sg_cpu->update_util,
						     sugov_update_shared);
		} else {
			cpufreq_add_update_util_hook(cpu, &sg_cpu->update_util,
						     sugov_update_single);
		}
	}
	return 0;
}

static int sugov_stop(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_sched();

	irq_work_sync(&sg_policy->irq_work);
	kthread_cancel_work_sync(&sg_policy->work);
	return 0;
}

static int sugov_limits(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;

	if (1) {
		mutex_lock(&sg_policy->work_lock);

		if (policy->max < policy->cur)
			__cpufreq_driver_target(policy, policy->max,
						CPUFREQ_RELATION_H);
		else if (policy->min > policy->cur)
			__cpufreq_driver_target(policy, policy->min,
						CPUFREQ_RELATION_L);

		mutex_unlock(&sg_policy->work_lock);
	}

	sg_policy->need_freq_update = true;
	return 0;
}

int sugov_governor(struct cpufreq_policy *policy, unsigned int event)
{
	if (event == CPUFREQ_GOV_POLICY_INIT) {
		return sugov_init(policy);
	} else if (policy->governor_data) {
		switch (event) {
		case CPUFREQ_GOV_POLICY_EXIT:
			return sugov_exit(policy);
		case CPUFREQ_GOV_START:
			return sugov_start(policy);
		case CPUFREQ_GOV_STOP:
			return sugov_stop(policy);
		case CPUFREQ_GOV_LIMITS:
			return sugov_limits(policy);
		}
	}
	return -EINVAL;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL
static
#endif
 struct cpufreq_governor schedutil_gov = {
	.name = "schedutil",
	.governor = sugov_governor,
	.owner = THIS_MODULE,
};

static int __init sugov_module_init(void)
{
	return cpufreq_register_governor(&schedutil_gov);
}

static void __exit sugov_module_exit(void)
{
	cpufreq_unregister_governor(&schedutil_gov);
}

MODULE_AUTHOR("Rafael J. Wysocki <rafael.j.wysocki@intel.com>");
MODULE_DESCRIPTION("Utilization-based CPU frequency selection");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL
fs_initcall(sugov_module_init);
#else
module_init(sugov_module_init);
#endif
module_exit(sugov_module_exit);
