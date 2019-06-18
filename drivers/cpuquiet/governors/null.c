/*
 * Copyright (c) 2012-2013 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/cpuquiet.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/pm_qos.h>
#include <linux/jiffies.h>
#include <linux/cpu.h>
#include <linux/sched.h>

#include "../cpuquiet.h"


static void null_stop(void)
{
}

static int null_start(void)
{
	int i = 0;

	for_each_possible_cpu(i)
		{
			cpuquiet_wake_cpu(i, false);
		}

	return 0;
}

static struct cpuquiet_governor null_governor = {
	.name			= "null",
	.start			= null_start,
	.stop			= null_stop,
	.owner			= THIS_MODULE,
};

static int __init init_null(void)
{
	return cpuquiet_register_governor(&null_governor);
}

static void __exit exit_null(void)
{
	cpuquiet_unregister_governor(&null_governor);
}

MODULE_LICENSE("GPL");
#ifdef CONFIG_CPU_QUIET_DEFAULT_GOV_NULL
fs_initcall(init_null);
#else
module_init(init_null);
#endif
module_exit(exit_null);
