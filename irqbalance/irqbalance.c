/*
 * Copyright (C) 2018 TeamNexus
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "irqbalance"
// #define LOG_NDEBUG 0
// #define LOG_VDEBUG 1

#include <cutils/log.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(LOG_VDEBUG) && LOG_VDEBUG
  #define ALOGVV ALOGV
#else
  #define ALOGVV(...) { }
#endif

typedef unsigned char u8;
typedef unsigned long long u64;

typedef struct cpudata {

	// general
	u8 core;
	bool online;

	// cputimes read from /proc/stat
	u64 cputime_user;
	u64 cputime_nice;
	u64 cputime_system;
	u64 cputime_idle;
	u64 cputime_iowait;
	u64 cputime_irq;
	u64 cputime_softirq;
	u64 cputime_steal;
	u64 cputime_guest;
	u64 cputime_guest_nice;

	// utilization
	u64 cpu_util;

} cpudata_t;

static int NR_CPUS = -1;
static int OFFS_CPUS = -1;

static u64 THREAD_DELAY = 0;

static int *irqs;
static int irqs_num;

static int *irq_blacklist;
static int irq_blacklist_num;

static cpudata_t *__cpudata;

static int read_cpudata(cpudata_t *cpudata, int core) {
	FILE *fp = fopen("/proc/stat", "r");
	int coredummy;
	int i, ret;

	if (!fp) {
		return -ENOENT;
	}

	// skip other cpus/non-IRQ CPUs/CPU-total
	for (i = 0; i < (core + OFFS_CPUS + 1); i++) {
		fscanf(fp, "%*[^\n]\n", NULL);
	}

	ret = fscanf(fp, "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n", &coredummy,
		&cpudata->cputime_user,
		&cpudata->cputime_nice,
		&cpudata->cputime_system,
		&cpudata->cputime_idle,
		&cpudata->cputime_iowait,
		&cpudata->cputime_irq,
		&cpudata->cputime_softirq,
		&cpudata->cputime_steal,
		&cpudata->cputime_guest,
		&cpudata->cputime_guest_nice);

	if (ret != 11) {
		if (cpudata->online) {
			ALOGW("%s: --- marking cpu%d as offline\n", __func__, core);
			cpudata->online = false;
		}
		return -EINVAL;
	}

	if (!cpudata->online)
		ALOGI("%s: +++ marking cpu%d as online\n", __func__, core);

	cpudata->online = true;

	fclose(fp);
	return 0;
}

static void irqbalance_load_watchdog(void) {
	int i, ret;

	for (i = 0; i < NR_CPUS; i++) {
		int core = __cpudata[i].core;

		// store current cpudata for calcucation of cpu
		// utilization
		cpudata_t old_cpudata = __cpudata[i];

		// read the current cputimes
		ret = read_cpudata(&__cpudata[i], i);
		if (ret) {
			if (__cpudata[i].online)
				ALOGE("%s: failed to read cpudata for core %d: %s (%d)", __func__, core, strerror(-ret), -ret);
			break;
		}

		#define TOTAL_CPUTIME(_data) \
			( \
				_data.cputime_user + _data.cputime_nice + _data.cputime_system + \
				_data.cputime_iowait + _data.cputime_irq + _data.cputime_softirq + \
				_data.cputime_steal + _data.cputime_guest + _data.cputime_guest_nice \
			)

		u64 old_total_cputime = TOTAL_CPUTIME(old_cpudata);
		u64 cur_total_cputime = TOTAL_CPUTIME(__cpudata[i]);

		u64 cputime_delta = cur_total_cputime - old_total_cputime;
		u64 cputime_total_delta = 
			(cur_total_cputime + __cpudata[i].cputime_idle) - 
			(old_total_cputime + old_cpudata.cputime_idle);

		(&__cpudata[i])->cpu_util = (cputime_delta * 100) / cputime_total_delta;
		ALOGV("%s: +++ core%d: util=%llu (%llu/%llu)\n", __func__, core, __cpudata[i].cpu_util, cputime_delta, cputime_total_delta);
	}
}

static void irqbalance_main() {
	int i, j, n, swp;
	int total_util;
	int irqs_processed;
	int online_cpus;
	char irqpath[255];		
	cpudata_t c_cpudata[NR_CPUS];

	memcpy(c_cpudata, __cpudata, sizeof(cpudata_t) * NR_CPUS);

	n = NR_CPUS;
	do {
		swp = 0;
		for (i = 0; i < (n - 1); ++i) {
			if (c_cpudata[i].cpu_util > c_cpudata[i + 1].cpu_util) {
				cpudata_t tmp = c_cpudata[i];
				c_cpudata[i] = c_cpudata[i + 1];
				c_cpudata[i + 1]= tmp;
				swp = 1;
			}
		}
		n = n - 1;
	} while (swp);

	irqs_processed = 0;
	total_util = 0;
	online_cpus = 0;

	for (i = 0; i < NR_CPUS; i++) {
		total_util += (int)c_cpudata[i].cpu_util;
		if (c_cpudata[i].online)
			online_cpus++;
	}

	for (i = NR_CPUS - 1; i >= 0 && irqs_processed < irqs_num; i--) {
		int online_index = (online_cpus - 1);
		int core = c_cpudata[i].core;
		int util = (int)c_cpudata[i].cpu_util;
		double ratio = util / 100.0;

		if (!c_cpudata[i].online)
			continue;

		int irqs_left = (irqs_num - irqs_processed);
		int irqnum = (int)ceil((irqs_left - (sqrt(ratio) * irqs_left)) / online_index);
		if (online_index == 0)
			irqnum = irqs_left;

		ALOGV("%s: +++ core%d(%02x): util=%d;ratio=%f;irqnum=%d;irqs_processed=%d\n", __func__, core, 1 << core, util, ratio, irqnum, irqs_processed);
		for (j = 0; j < irqnum && irqs_processed < irqs_num; j++) {
			int irqidx = irqs_processed + j;
			sprintf(irqpath, "/proc/irq/%d/smp_affinity", irqs[irqidx]);
			ALOGVV("%s: +++ balancing IRQ %d (%s)\n", __func__, irqs[irqidx], irqpath);

			FILE *irqfp = fopen(irqpath, "w");
			if (irqfp) {
				fprintf(irqfp, "%02x\n", 1 << core);
				fflush(irqfp);
				fclose(irqfp);
			} else {
				ALOGV("%s: --- failed to open IRQ SMP affinity file for IRQ %d\n", __func__, irqs[irqidx]);
			}
		}

		irqs_processed += irqnum;

		if (online_cpus <= 0)
			break;

		if (c_cpudata[i].online)
			online_cpus--;
	}
}

static void irqbalance_loop() {
	struct timespec time_start;
	struct timespec time_end;
	u64 time_start_val;
	u64 time_end_val;
	u64 time_delta;
	u64 sleep_delta;

	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &time_start);
		time_start_val = (time_start.tv_sec * 1e9) + time_start.tv_nsec;

		irqbalance_load_watchdog();
		irqbalance_main();

		clock_gettime(CLOCK_MONOTONIC, &time_end);
		time_end_val = (time_end.tv_sec * 1e9) + time_end.tv_nsec;
		time_delta = (u64)((time_end_val - time_start_val) / 1e3);
		sleep_delta = THREAD_DELAY - time_delta;
		if (THREAD_DELAY < time_delta)
			sleep_delta = 0;

		ALOGV("%s: --- sleeping for %llums\n", __func__, sleep_delta / 1000);
		usleep(sleep_delta);
	}
}

static int read_irqbalance_configuration(void) {
	FILE *fp = fopen("/vendor/etc/irqbalance.conf", "r");
	int i, ret;

	if (!fp) {
		ALOGE("%s: fail to open irqbalance configuration", __func__);
		return -ENOENT;
	}

	while (!feof(fp)) {
		char cmd[5];

		cmd[0] = fgetc(fp);
		if (cmd[0] == '#') {
			fscanf(fp, "%*[^\n]\n", NULL);
			continue;
		}

		fscanf(fp, "%3s", cmd + 1);

		if (!strcmp(cmd, "cpun")) {
			fscanf(fp, "%d\n", &NR_CPUS);
		} else if (!strcmp(cmd, "cpuo")) {
			fscanf(fp, "%d\n", &OFFS_CPUS);
		} else if (!strcmp(cmd, "tdel")) {
			fscanf(fp, "%llu\n", &THREAD_DELAY);
		} else if (!strcmp(cmd, "blck")) {
			int irq_blacklist_sz = 0;
			fscanf(fp, "%d\n", &irq_blacklist_num);

			irq_blacklist_sz = irq_blacklist_num * sizeof(int);
			irq_blacklist = malloc(irq_blacklist_sz);
			if (!irq_blacklist) {
				ALOGE("%s: could not allocate memory for IRQ blacklist (%d bytes)\n", __func__, irq_blacklist_sz);
				return -ENOMEM;
			}
			memset(irq_blacklist, 0, irq_blacklist_sz);

			for (i = 0; i < irq_blacklist_num; i++) {
				ret = fscanf(fp, "%d\n", &irq_blacklist[i]);
				if (ret != 1) {
					ALOGE("%s: could not read given number of blacklisted IRQs: given %d, read %d\n", __func__, irq_blacklist_num, i);
					return -EINVAL;
				}
			}
		} else {
			ALOGE("%s: parsed invalid command: '%s'\n", __func__, cmd);
			return -EINVAL;
		}
	}

	ALOGV("%s: +++ NR_CPUS = %d\n", __func__, NR_CPUS);
	ALOGV("%s: +++ IRQBLCK = %d\n", __func__, irq_blacklist_num);
	for (i = 0; i < irq_blacklist_num; i++) {
		ALOGV("%s: +++  %d\n", __func__, irq_blacklist[i]);
	}

	fclose(fp);
	return 0;
}

static int scan_for_irqs(void) {
	FILE *fp = fopen("/proc/interrupts", "r");
	int i, j, ret, sz, dummy;

	if (!fp) {
		return -ENOENT;
	}

	// skip table header
	fscanf(fp, "%*[^\n]\n", NULL);

	// first, count the valid numeric IRQs
	irqs_num = 0;

	do {
		ret = fscanf(fp, "%d: ", &dummy);
		if (ret == 1) {
			irqs_num++;

			// skip line
			fscanf(fp, "%*[^\n]\n", NULL);
		}
	} while (ret == 1);
	
	// do not allocate memory for blocked IRQs
	irqs_num -= irq_blacklist_num;

	// reverse
	fseek(fp, 0, SEEK_SET);

	// skip table header again
	fscanf(fp, "%*[^\n]\n", NULL);

	// allocate memory
	sz = irqs_num * sizeof(int);
	irqs = malloc(sz);
	if (!irqs) {
		ALOGE("%s: could not allocate memory for IRQ list (%d bytes)\n", __func__, sz);
		return -ENOMEM;
	}

	// finally, read the IRQs
	for (i = 0; i < irqs_num;) {
		fscanf(fp, "%d: ", &irqs[i]);

		for (j = 0; j < irq_blacklist_num; j++) {
			if (irq_blacklist[j] == irqs[i]) {
				ALOGV("%s: --- Skipping IRQ %d: blacklisted\n", __func__, irqs[i]);
				goto skipirq;
			}
		}

		i++;

skipirq:
		// skip line
		fscanf(fp, "%*[^\n]\n", NULL);
	}

	ALOGV("%s: +++ IRQS = %d\n", __func__, irqs_num);
	for (i = 0; i < irqs_num; i++) {
		ALOGV("%s: +++  %d\n", __func__, irqs[i]);
	}

	fclose(fp);
	return 0;
}

int main(int argc, char *argv[]) {
	int i, ret;
	int dtsz;

	ALOGI("%s: initializing irqbalance configuration\n", __func__);
	ret = read_irqbalance_configuration();
	if (ret) {
		return ret;
	}

	ALOGI("%s: scanning for IRQs\n", __func__);
	ret = scan_for_irqs();
	if (ret) {
		return ret;
	}

	ALOGI("%s: allocating memory for CPU data\n", __func__);
	dtsz = sizeof(cpudata_t) * NR_CPUS;

	__cpudata = malloc(dtsz);
	if (!__cpudata) {
		ALOGE("%s: could not allocate memory for CPU data (%d bytes)\n", __func__, dtsz);
		return -ENOMEM;
	}

	memset(__cpudata, 0, dtsz);

	ALOGI("%s: initializing CPU data\n", __func__);
	for (i = 0; i < NR_CPUS; i++) {
		cpudata_t *dt = &__cpudata[i];

		// set core of cpudata - required for after-sort in IRQ balancing
		dt->core = i + OFFS_CPUS;

		ret = read_cpudata(dt, i);
		if (ret) {
			ALOGE("%s: fail to read initial cpudata for core %d", __func__, i);
			return ret;
		}
	}

	ALOGI("%s: starting IRQ balancing loop\n", __func__);
	irqbalance_loop();

	// should never be reached
	ALOGE("%s: reached unreachable code??\n", __func__);
	return 0;
}
