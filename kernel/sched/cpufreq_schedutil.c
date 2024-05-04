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
#include <linux/module.h>
#include <linux/irq_work.h>
#include <linux/slab.h>
#include <trace/events/power.h>

#include "../../drivers/cpufreq/cpufreq_governor.h"
#include "sched.h"

struct sugov_tunables {
	unsigned int rate_limit_us;
};

struct sugov_policy {
	struct cpufreq_policy *policy;

	struct sugov_tunables *tunables;
	struct list_head tunables_hook;

	raw_spinlock_t update_lock;  /* For shared policies */
	u64 last_freq_update_time;
	s64 freq_update_delay_ns;
	unsigned int next_freq;

	/* The next fields are only needed if fast switch cannot be used. */
	struct irq_work irq_work;
	struct work_struct work;
	struct mutex work_lock;
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
};

static DEFINE_PER_CPU(struct sugov_cpu, sugov_cpu);

static struct attribute_group *get_sysfs_attr(void);

/************************ Governor internals ***********************/

static bool sugov_should_update_freq(struct sugov_policy *sg_policy, u64 time)
{
	s64 delta_ns;

	if (sg_policy->work_in_progress)
		return false;

	if (unlikely(sg_policy->need_freq_update)) {
		sg_policy->need_freq_update = false;
		/*
		 * This happens when limits change, so forget the previous
		 * next_freq value and force an update.
		 */
		sg_policy->next_freq = UINT_MAX;
		return true;
	}

	delta_ns = time - sg_policy->last_freq_update_time;
	return delta_ns >= sg_policy->freq_update_delay_ns;
}

static void sugov_update_commit(struct sugov_policy *sg_policy, u64 time,
				unsigned int next_freq)
{
	struct cpufreq_policy *policy = sg_policy->policy;

	sg_policy->last_freq_update_time = time;

	if (sg_policy->next_freq != next_freq) {
		sg_policy->next_freq = next_freq;
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

	return (freq + (freq >> 2)) * util / max;
}

static void sugov_update_single(struct update_util_data *hook, u64 time,
				unsigned long util, unsigned long max)
{
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned int next_f;

	if (!sugov_should_update_freq(sg_policy, time))
		return;

	next_f = util == ULONG_MAX ? policy->cpuinfo.max_freq :
			get_next_freq(policy, util, max);
	sugov_update_commit(sg_policy, time, next_f);
}

static unsigned int sugov_next_freq_shared(struct sugov_policy *sg_policy,
					   unsigned long util, unsigned long max)
{
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned int max_f = policy->cpuinfo.max_freq;
	u64 last_freq_update_time = sg_policy->last_freq_update_time;
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
		delta_ns = last_freq_update_time - j_sg_cpu->last_update;
		if (delta_ns > TICK_NSEC)
			continue;

		j_util = j_sg_cpu->util;
		if (j_util == ULONG_MAX)
			return max_f;

		j_max = j_sg_cpu->max;
		if (j_util * max > j_max * util) {
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
		next_f = sugov_next_freq_shared(sg_policy, util, max);
		sugov_update_commit(sg_policy, time, next_f);
	}

	raw_spin_unlock(&sg_policy->update_lock);
}

static void sugov_work(struct work_struct *work)
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
	schedule_work_on(smp_processor_id(), &sg_policy->work);
}

/************************** sysfs interface ************************/
static struct sugov_tunables *global_tunables;
static DEFINE_MUTEX(global_tunables_lock);


static ssize_t show_sys_rate_limit_us(struct sugov_tunables *tunables, char *buf)
{
	return sprintf(buf, "%u\n", tunables->rate_limit_us);
}

static ssize_t store_sys_rate_limit_us(struct sugov_tunables *tunables, const char *buf,
				   size_t count)
{
	unsigned int rate_limit_us;
	int cpu;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->rate_limit_us = rate_limit_us;

	for_each_present_cpu(cpu) {
		struct sugov_cpu *sugov_cpu_i = &per_cpu(sugov_cpu, cpu);
		struct sugov_policy *sg_policy_cpu = sugov_cpu_i->sg_policy;

		sg_policy_cpu->tunables->rate_limit_us = rate_limit_us;
		sg_policy_cpu->freq_update_delay_ns = rate_limit_us * NSEC_PER_USEC;
	}

	return count;
}

static ssize_t show_rate_limit_us(struct sugov_policy *sg_policy, char *buf)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	return sprintf(buf, "%u\n", tunables->rate_limit_us);
}

static ssize_t store_rate_limit_us(struct sugov_policy *sg_policy, const char *buf,
				   size_t count)
{
	struct sugov_tunables *tunables = sg_policy->tunables;
	unsigned int rate_limit_us;
	int cpu;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->rate_limit_us = rate_limit_us;
	sg_policy->freq_update_delay_ns = rate_limit_us * NSEC_PER_USEC;

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

show_store_gov_pol_sys(rate_limit_us);

#define gov_sys_pol_attr_rw(_name)					\
	gov_sys_attr_rw(_name);						\
	gov_pol_attr_rw(_name)

gov_sys_pol_attr_rw(rate_limit_us);



/* One Governor instance for entire system */
static struct attribute *sugov_attributes_gov_sys[] = {
	&rate_limit_us_gov_sys.attr,
	NULL
};


static struct attribute_group sugov_attr_group_gov_sys = {
	.attrs = sugov_attributes_gov_sys,
	.name = "schedutil",
};

static struct attribute *sugov_attributes_gov_pol[] = {
	&rate_limit_us_gov_pol.attr,
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

static struct cpufreq_governor schedutil_gov;

static struct sugov_policy *sugov_policy_alloc(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy;

	sg_policy = kzalloc(sizeof(*sg_policy), GFP_KERNEL);
	if (!sg_policy)
		return NULL;

	sg_policy->policy = policy;
	init_irq_work(&sg_policy->irq_work, sugov_irq_work);
	INIT_WORK(&sg_policy->work, sugov_work);
	mutex_init(&sg_policy->work_lock);
	raw_spin_lock_init(&sg_policy->update_lock);
	return sg_policy;
}

static void sugov_policy_free(struct sugov_policy *sg_policy)
{
	mutex_destroy(&sg_policy->work_lock);
	kfree(sg_policy);
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
	unsigned int lat;
	int ret = 0;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	sg_policy = sugov_policy_alloc(policy);
	if (!sg_policy)
		return -ENOMEM;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto free_sg_policy;
		}
		policy->governor_data = sg_policy;
		sg_policy->tunables = global_tunables;

		goto out;
	}

	tunables = sugov_tunables_alloc(sg_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto free_sg_policy;
	}

	tunables->rate_limit_us = LATENCY_MULTIPLIER;
	lat = policy->cpuinfo.transition_latency / NSEC_PER_USEC;
	if (lat)
		tunables->rate_limit_us *= lat;

	policy->governor_data = sg_policy;
	sg_policy->tunables = tunables;

	ret = sysfs_create_group(get_governor_parent_kobj(policy),
			get_sysfs_attr());
	if (ret) {
		if (!have_governor_per_policy()) {
			global_tunables = NULL;
			cpufreq_put_global_kobject();
		}
		goto fail;
	}

 out:
	mutex_unlock(&global_tunables_lock);

	return 0;

 fail:
	policy->governor_data = NULL;
	sugov_tunables_free(tunables);

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

	sugov_policy_free(sg_policy);
	return 0;
}

static int sugov_start(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	sg_policy->freq_update_delay_ns = LATENCY_MULTIPLIER * NSEC_PER_USEC;
	sg_policy->last_freq_update_time = 0;
	sg_policy->next_freq = UINT_MAX;
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
	cancel_work_sync(&sg_policy->work);
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

static struct cpufreq_governor schedutil_gov = {
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
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &schedutil_gov;
}

fs_initcall(sugov_module_init);
#else
module_init(sugov_module_init);
#endif
module_exit(sugov_module_exit);
