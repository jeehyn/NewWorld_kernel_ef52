/*
*  drivers/cpufreq/cpufreq_smartdroid.c
*  Auther : NewWorld(cae11cae@naver.com)
*  based on governor Conservative, Authers :
*
*  Copyright (C)  2001 Russell King
*            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
*                      Jun Nakajima <jun.nakajima@intel.com>
*            (C)  2009 Alexander Clouter <alex@digriz.org.uk>
* 
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#ifdef CONFIG_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */
#define FAST_MODE_FREQUENCY_UP_THRESHOLD	(83)
#define FAST_MODE_FREQUENCY_DOWN_THRESHOLD	(23)
#define FAST_MODE_UP_PERSENT			(10)
#define FAST_MODE_DOWN_PERSENT			(5)
#define SLOW_MODE_FREQUENCY_UP_THRESHOLD	(95)
#define SLOW_MODE_FREQUENCY_DOWN_THRESHOLD (35)
#define SLOW_MODE_UP_PERSENT			(5)
#define SLOW_MODE_DOWN_PERSENT			(10)

/*
 * The polling frequency of this governor depends on the capability of
 * the processor. Default polling frequency is 1000 times the transition
 * latency of the processor. The governor will work on any processor with
 * transition latency <= 10mS, using appropriate sampling
 * rate.
 * For CPUs with transition latency > 10mS (mostly drivers with CPUFREQ_ETERNAL)
 * this governor will not work.
 * All times here are in uS.
 */
#define MIN_SAMPLING_RATE_RATIO			(2)

static unsigned int min_sampling_rate;

#define LATENCY_MULTIPLIER			(1000)
#define MIN_LATENCY_MULTIPLIER			(100)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(10)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000 * 1000)
//early suspend varablies
#ifdef CONFIG_EARLYSUSPEND
static unsigned long stored_sampling_rate;
static unsigned long stored_up_threshold;
static unsigned long stored_down_threshold;
#endif
static unsigned int is_early_suspend = 0;
#define DEF_UPCOUNT		(10)
#define DEF_DOWNCOUNT		(4)

static unsigned int upcounter = 0;
static unsigned int downcounter = 0;

static void do_dbs_timer(struct work_struct *work);

struct cpu_dbs_info_s {
	cputime64_t prev_cpu_idle;
	cputime64_t prev_cpu_wall;
	cputime64_t prev_cpu_nice;
	struct cpufreq_policy *cur_policy;
	struct delayed_work work;
	unsigned int down_skip;
	unsigned int requested_freq;
	int cpu;
	unsigned int enable:1;
	/*
	 * percpu mutex that serializes governor limit change with
	 * do_dbs_timer invocation. We do not want do_dbs_timer to run
	 * when user is changing the governor or limits.
	 */
	struct mutex timer_mutex;
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, cs_cpu_dbs_info);

static unsigned int dbs_enable;	/* number of CPUs using this policy */

/*
 * dbs_mutex protects dbs_enable in governor start/stop.
 */
static DEFINE_MUTEX(dbs_mutex);

static struct dbs_tuners {
	unsigned int sampling_rate;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int down_threshold;
	unsigned int ignore_nice;
	unsigned int up_persent;
	unsigned int down_persent;
	unsigned int fast_mode_up_threshold;
	unsigned int fast_mode_down_threshold;
	unsigned int slow_mode_up_threshold;
	unsigned int slow_mode_down_threshold;
	unsigned int fast_mode_count;
	unsigned int slow_mode_count;
	unsigned int fast_mode_up_persent;
	unsigned int fast_mode_down_persent;
	unsigned int slow_mode_up_persent;
	unsigned int slow_mode_down_persent;
} dbs_tuners_ins = {
	.up_threshold = SLOW_MODE_FREQUENCY_UP_THRESHOLD,
	.down_threshold = SLOW_MODE_FREQUENCY_DOWN_THRESHOLD,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.ignore_nice = 0,
	.up_persent = SLOW_MODE_UP_PERSENT,
	.down_persent = SLOW_MODE_DOWN_PERSENT,
	.fast_mode_up_threshold = FAST_MODE_FREQUENCY_UP_THRESHOLD,
	.fast_mode_down_threshold = FAST_MODE_FREQUENCY_DOWN_THRESHOLD,
	.fast_mode_count = DEF_UPCOUNT,
	.fast_mode_up_persent = FAST_MODE_UP_PERSENT,
	.fast_mode_down_persent = FAST_MODE_DOWN_PERSENT,
	.slow_mode_up_threshold = SLOW_MODE_FREQUENCY_UP_THRESHOLD,
	.slow_mode_down_threshold = SLOW_MODE_FREQUENCY_DOWN_THRESHOLD,
	.slow_mode_count = DEF_DOWNCOUNT,
	.slow_mode_up_persent = SLOW_MODE_UP_PERSENT,
	.slow_mode_down_persent = SLOW_MODE_DOWN_PERSENT,
};
static inline u64 get_cpu_idle_time_jiffy(unsigned int cpu, u64 *wall)
{
	u64 idle_time;
	u64 cur_wall_time;
	u64 busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());

	busy_time  = kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

	idle_time = cur_wall_time - busy_time;
	if (wall)
		*wall = jiffies_to_usecs(cur_wall_time);

	return jiffies_to_usecs(idle_time);
}

static inline cputime64_t get_cpu_idle_time(unsigned int cpu, cputime64_t *wall)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, NULL);

	if (idle_time == -1ULL)
		return get_cpu_idle_time_jiffy(cpu, wall);
	else
		idle_time += get_cpu_iowait_time_us(cpu, wall);

	return idle_time;
}
		
/* keep track of frequency transitions */
static int
dbs_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
		     void *data)
{
	struct cpufreq_freqs *freq = data;
	struct cpu_dbs_info_s *this_dbs_info = &per_cpu(cs_cpu_dbs_info,
							freq->cpu);

	struct cpufreq_policy *policy;

	if (!this_dbs_info->enable)
		return 0;

	policy = this_dbs_info->cur_policy;

	/*
	 * we only care if our internally tracked freq moves outside
	 * the 'valid' ranges of freqency available to us otherwise
	 * we do not change it
	*/
	if (this_dbs_info->requested_freq > policy->max
			|| this_dbs_info->requested_freq < policy->min)
		this_dbs_info->requested_freq = freq->new;

	return 0;
}

static struct notifier_block dbs_cpufreq_notifier_block = {
	.notifier_call = dbs_cpufreq_notifier
};


/************************** sysfs interface ************************/
static ssize_t show_sampling_rate_min(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", min_sampling_rate);
}

define_one_global_ro(sampling_rate_min);

/* cpufreq_smartdroid Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(sampling_down_factor, sampling_down_factor);
show_one(ignore_nice_load, ignore_nice);
show_one(fast_mode_up_threshold, fast_mode_up_threshold);
show_one(fast_mode_down_threshold, fast_mode_down_threshold);
show_one(fast_mode_count, fast_mode_count);
show_one(slow_mode_up_threshold, slow_mode_up_threshold);
show_one(slow_mode_down_threshold, slow_mode_down_threshold);
show_one(slow_mode_count, slow_mode_count);
show_one(fast_mode_up_persent, fast_mode_up_persent);
show_one(fast_mode_down_persent, fast_mode_down_persent);
show_one(slow_mode_up_persent, slow_mode_up_persent);
show_one(slow_mode_down_persent, slow_mode_down_persent);

static ssize_t store_sampling_down_factor(struct kobject *a,
					  struct attribute *b,
					  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;

	dbs_tuners_ins.sampling_down_factor = input;
	return count;
}

static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.sampling_rate = max(input, min_sampling_rate);
	return count;
}

static ssize_t store_ignore_nice_load(struct kobject *a, struct attribute *b,
				      const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == dbs_tuners_ins.ignore_nice) /* nothing to do */
		return count;

	dbs_tuners_ins.ignore_nice = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(cs_cpu_dbs_info, j);
		dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&dbs_info->prev_cpu_wall);
		if (dbs_tuners_ins.ignore_nice)
			dbs_info->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
	}
	return count;
}
int is_correct_persentage(int ret, int input)
{
	if (ret < 1)
		return 1;

	if (input > 100)
		return 2;
	
	return 0;
}

static ssize_t store_fast_mode_up_persent(struct kobject *a, struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	int compare;
	ret = sscanf(buf, "%u", &input);
	
	compare = is_correct_persentage(ret, input);
	if(compare == 1)
		return -EINVAL;
	
	else if (compare == 2)
		input = 100;

	dbs_tuners_ins.fast_mode_up_persent = input;
	return count;
}
static ssize_t store_fast_mode_down_persent(struct kobject *a, struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	int compare;
	ret = sscanf(buf, "%u", &input);
	
	compare = is_correct_persentage(ret, input);
	if(compare == 1)
		return -EINVAL;
	
	else if (compare == 2)
		input = 100;
	dbs_tuners_ins.fast_mode_down_persent = input;
	return count;
}
static ssize_t store_slow_mode_up_persent(struct kobject *a, struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	int compare;
	ret = sscanf(buf, "%u", &input);
	
	compare = is_correct_persentage(ret, input);
	if(compare == 1)
		return -EINVAL;
	
	else if (compare == 2)
		input = 100;
	dbs_tuners_ins.slow_mode_up_persent = input;
	return count;
}
static ssize_t store_slow_mode_down_persent(struct kobject *a, struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	int compare;
	ret = sscanf(buf, "%u", &input);
	
	compare = is_correct_persentage(ret, input);
	if(compare == 1)
		return -EINVAL;
	
	else if (compare == 2)
		input = 100;
	dbs_tuners_ins.slow_mode_down_persent = input;
	return count;
}
static ssize_t store_fast_mode_up_threshold(struct kobject *a, struct attribute *b, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 100 ||
			input <= dbs_tuners_ins.fast_mode_down_threshold)
		return -EINVAL;

	dbs_tuners_ins.fast_mode_up_threshold = input;
	return count;
}
static ssize_t store_fast_mode_down_threshold(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	/* cannot be lower than 11 otherwise freq will not fall */
	if (ret != 1 || input < 11 || input > 100 ||
			input >= dbs_tuners_ins.fast_mode_up_threshold)
		return -EINVAL;

	dbs_tuners_ins.fast_mode_down_threshold = input;
	return count;
}
static ssize_t store_slow_mode_up_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 100 ||
			input <= dbs_tuners_ins.slow_mode_down_threshold)
		return -EINVAL;

	dbs_tuners_ins.slow_mode_up_threshold = input;
	return count;
}
static ssize_t store_slow_mode_down_threshold(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	/* cannot be lower than 11 otherwise freq will not fall */
	if (ret != 1 || input < 11 || input > 100 ||
			input >= dbs_tuners_ins.slow_mode_up_threshold)
		return -EINVAL;

	dbs_tuners_ins.slow_mode_down_threshold = input;
	return count;
}
static ssize_t store_slow_mode_count(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if(input < 1 || input >= 100)
		return -EINVAL;

	dbs_tuners_ins.slow_mode_count = input;
	upcounter = 0;
	downcounter = 0;
	return count;
}
static ssize_t store_fast_mode_count(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if(input < 1 || input >= 100)
		return -EINVAL;

	dbs_tuners_ins.fast_mode_count = input;
	upcounter = 0;
	downcounter = 0;
	return count;
}
define_one_global_rw(sampling_rate);
define_one_global_rw(sampling_down_factor);
define_one_global_rw(ignore_nice_load);
define_one_global_rw(fast_mode_up_threshold);
define_one_global_rw(fast_mode_down_threshold);
define_one_global_rw(fast_mode_count);
define_one_global_rw(fast_mode_up_persent);
define_one_global_rw(fast_mode_down_persent);
define_one_global_rw(slow_mode_up_threshold);
define_one_global_rw(slow_mode_down_threshold);
define_one_global_rw(slow_mode_count);
define_one_global_rw(slow_mode_up_persent);
define_one_global_rw(slow_mode_down_persent);

static struct attribute *dbs_attributes[] = {
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&sampling_down_factor.attr,
	&ignore_nice_load.attr,
	&fast_mode_up_threshold.attr,
	&fast_mode_down_threshold.attr,
	&fast_mode_count.attr,
	&slow_mode_up_threshold.attr,
	&slow_mode_down_threshold.attr,
	&slow_mode_count.attr,
	&fast_mode_up_persent.attr,
	&fast_mode_down_persent.attr,
	&slow_mode_up_persent.attr,
	&slow_mode_down_persent.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "smartdroid",
};


/************************** sysfs end ************************/
//early suspend tweak :D
#ifdef CONFIG_EARLYSUSPEND
static void cpufreq_smartdroid_early_suspend(struct early_suspend *h)
{
	mutex_lock(&dbs_mutex);
	is_early_suspend = 1;
	stored_sampling_rate = dbs_tuners_ins.sampling_rate;
	dbs_tuners_ins.sampling_rate = stored_sampling_rate * 6;
	stored_up_threshold = dbs_tuners_ins.up_threshold;
	stored_down_threshold = dbs_tuners_ins.down_threshold;
	dbs_tuners_ins.down_threshold = 70;
	dbs_tuners_ins.up_threshold = 99;
	mutex_unlock(&dbs_mutex);
}

static void cpufreq_smartdroid_late_resume(struct early_suspend *h)
{
	mutex_lock(&dbs_mutex);
	is_early_suspend = 0;
	dbs_tuners_ins.sampling_rate = stored_sampling_rate;
	dbs_tuners_ins.up_threshold = stored_up_threshold;
	dbs_tuners_ins.down_threshold = stored_down_threshold;
	mutex_unlock(&dbs_mutex);
}

static struct early_suspend cpufreq_smartdroid_early_suspend_info = {
	.suspend = cpufreq_smartdroid_early_suspend,
	.resume = cpufreq_smartdroid_late_resume,
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB+1,
};
#endif
static void update_gov_tunable(int flag)
{
	if(is_early_suspend == 1)
			return;
	if(flag)	//Fast
	{
		dbs_tuners_ins.down_threshold = dbs_tuners_ins.fast_mode_down_threshold;
		dbs_tuners_ins.up_threshold = dbs_tuners_ins.up_threshold;
		dbs_tuners_ins.up_persent = dbs_tuners_ins.fast_mode_up_persent;
		dbs_tuners_ins.down_persent = dbs_tuners_ins.fast_mode_down_persent;
	}
	else	//slow
	{
		dbs_tuners_ins.down_threshold = dbs_tuners_ins.slow_mode_down_threshold;
		dbs_tuners_ins.up_threshold = dbs_tuners_ins.slow_mode_up_threshold;
		dbs_tuners_ins.up_persent = dbs_tuners_ins.slow_mode_up_persent;
		dbs_tuners_ins.down_persent = dbs_tuners_ins.slow_mode_down_persent;
	}
}
static void dbs_check_cpu(struct cpu_dbs_info_s *this_dbs_info)
{
	unsigned int load = 0;
	unsigned int max_load = 0;
	unsigned int freq_target;

	struct cpufreq_policy *policy;
	unsigned int j;

	policy = this_dbs_info->cur_policy;

	/*
	 * Every sampling_rate, we check, if current idle time is less
	 * than 40% (default), then we try to increase frequency
	 * Every sampling_rate*sampling_down_factor, we check, if current
	 * idle time is more than 80%, then we try to decrease frequency
	 *
	 * Any frequency increase takes it to the maximum frequency.
	 * Frequency reduction happens at minimum steps of
	 * 5% (default) of maximum frequency
	 */

	/* Get Absolute Load */
	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info_s *j_dbs_info;
		cputime64_t cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;

		j_dbs_info = &per_cpu(cs_cpu_dbs_info, j);

		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time);

		wall_time = (unsigned int)
			(cur_wall_time - j_dbs_info->prev_cpu_wall);
		j_dbs_info->prev_cpu_wall = cur_wall_time;

		idle_time = (unsigned int)
			(cur_idle_time - j_dbs_info->prev_cpu_idle);
		j_dbs_info->prev_cpu_idle = cur_idle_time;

		if (dbs_tuners_ins.ignore_nice) {
			u64 cur_nice;
			unsigned long cur_nice_jiffies;

			cur_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE] -
					 j_dbs_info->prev_cpu_nice;
			/*
			 * Assumption: nice time between sampling periods will
			 * be less than 2^32 jiffies for 32 bit sys
			 */
			cur_nice_jiffies = (unsigned long)
					cputime64_to_jiffies64(cur_nice);

			j_dbs_info->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
			idle_time += jiffies_to_usecs(cur_nice_jiffies);
		}

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		load = 100 * (wall_time - idle_time) / wall_time;

		if (load > max_load)
			max_load = load;
	}

	if (dbs_tuners_ins.fast_mode_up_persent == 0 ||
		dbs_tuners_ins.fast_mode_down_persent == 0 ||
		dbs_tuners_ins.slow_mode_up_persent == 0 ||
		dbs_tuners_ins.slow_mode_down_persent == 0)
		return;

	/* Check for frequency increase */
	if (max_load > dbs_tuners_ins.up_threshold) {
		upcounter++;
		/* check if counter is max */
		if (upcounter == dbs_tuners_ins.fast_mode_count){
			update_gov_tunable(0);
			upcounter = 0;
			if(downcounter != 0)
				downcounter--;
		}
		this_dbs_info->down_skip = 0;

		/* if we are already at full speed then break out early */
		if (this_dbs_info->requested_freq == policy->max)
			return;

		freq_target = (dbs_tuners_ins.up_persent * policy->max) / 100;

		/* max freq cannot be less than 100. But who knows.... */
		if (unlikely(freq_target == 0))
			freq_target = 5;

		this_dbs_info->requested_freq += freq_target;
		if (this_dbs_info->requested_freq > policy->max)
			this_dbs_info->requested_freq = policy->max;

		__cpufreq_driver_target(policy, this_dbs_info->requested_freq,
			CPUFREQ_RELATION_H);
		return;
	}

	/*
	 * The optimal frequency is the frequency that is the lowest that
	 * can support the current CPU usage without triggering the up
	 * policy. To be safe, we focus 10 points under the threshold.
	 */
	if (max_load < (dbs_tuners_ins.down_threshold - 10)) {
		downcounter++;
		/* check if counter is 5 */
		if (downcounter == dbs_tuners_ins.slow_mode_count){
			update_gov_tunable(1);
			downcounter = 0;

			if(upcounter != 0)
				upcounter--;
		}
		freq_target = (dbs_tuners_ins.down_persent * policy->max) / 100;

		this_dbs_info->requested_freq -= freq_target;
		if (this_dbs_info->requested_freq < policy->min)
			this_dbs_info->requested_freq = policy->min;

		/*
		 * if we cannot reduce the frequency anymore, break out early
		 */
		if (policy->cur == policy->min)
			return;

		__cpufreq_driver_target(policy, this_dbs_info->requested_freq,
				CPUFREQ_RELATION_H);
		return;
	}
}


static void do_dbs_timer(struct work_struct *work)
{
	struct cpu_dbs_info_s *dbs_info =
		container_of(work, struct cpu_dbs_info_s, work.work);
	unsigned int cpu = dbs_info->cpu;

	/* We want all CPUs to do sampling nearly on same jiffy */
	int delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	delay -= jiffies % delay;

	mutex_lock(&dbs_info->timer_mutex);

	dbs_check_cpu(dbs_info);

	schedule_delayed_work_on(cpu, &dbs_info->work, delay);
	mutex_unlock(&dbs_info->timer_mutex);
}

static inline void dbs_timer_init(struct cpu_dbs_info_s *dbs_info)
{
	/* We want all CPUs to do sampling nearly on same jiffy */
	int delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);
	delay -= jiffies % delay;

	dbs_info->enable = 1;
	INIT_DELAYED_WORK_DEFERRABLE(&dbs_info->work, do_dbs_timer);
	schedule_delayed_work_on(dbs_info->cpu, &dbs_info->work, delay);
}

static inline void dbs_timer_exit(struct cpu_dbs_info_s *dbs_info)
{
	dbs_info->enable = 0;
	cancel_delayed_work_sync(&dbs_info->work);
}

static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				   unsigned int event)
{
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *this_dbs_info;
	unsigned int j;
	int rc;

	this_dbs_info = &per_cpu(cs_cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy->cur))
			return -EINVAL;

		mutex_lock(&dbs_mutex);

		for_each_cpu(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;
			j_dbs_info = &per_cpu(cs_cpu_dbs_info, j);
			j_dbs_info->cur_policy = policy;

			j_dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&j_dbs_info->prev_cpu_wall);
			if (dbs_tuners_ins.ignore_nice)
				j_dbs_info->prev_cpu_nice =
						kcpustat_cpu(j).cpustat[CPUTIME_NICE];
		}
		this_dbs_info->down_skip = 0;
		this_dbs_info->requested_freq = policy->cur;

		mutex_init(&this_dbs_info->timer_mutex);
		dbs_enable++;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 1) {
			unsigned int latency;
			/* policy latency is in nS. Convert it to uS first */
			latency = policy->cpuinfo.transition_latency / 1000;
			if (latency == 0)
				latency = 1;

			rc = sysfs_create_group(cpufreq_global_kobject,
						&dbs_attr_group);
			if (rc) {
				mutex_unlock(&dbs_mutex);
				return rc;
			}

			/*
			 * conservative does not implement micro like ondemand
			 * governor, thus we are bound to jiffes/HZ
			 */
			min_sampling_rate =
				MIN_SAMPLING_RATE_RATIO * jiffies_to_usecs(10);
			/* Bring kernel and HW constraints together */
			min_sampling_rate = max(min_sampling_rate,
					MIN_LATENCY_MULTIPLIER * latency);
			dbs_tuners_ins.sampling_rate =
				max(min_sampling_rate,
				    latency * LATENCY_MULTIPLIER);

			cpufreq_register_notifier(
					&dbs_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
		}
		mutex_unlock(&dbs_mutex);

		dbs_timer_init(this_dbs_info);

		break;

	case CPUFREQ_GOV_STOP:
		dbs_timer_exit(this_dbs_info);

		mutex_lock(&dbs_mutex);
		dbs_enable--;
		mutex_destroy(&this_dbs_info->timer_mutex);

		/*
		 * Stop the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 0)
			cpufreq_unregister_notifier(
					&dbs_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);

		mutex_unlock(&dbs_mutex);
		if (!dbs_enable)
			sysfs_remove_group(cpufreq_global_kobject,
					   &dbs_attr_group);

		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&this_dbs_info->timer_mutex);
		if (policy->max < this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
					policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
					policy->min, CPUFREQ_RELATION_L);
		mutex_unlock(&this_dbs_info->timer_mutex);

		break;
	}
	return 0;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_SMARTDROID
static
#endif
struct cpufreq_governor cpufreq_gov_smartdroid = {
	.name			= "smartdroid",
	.governor		= cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
#ifdef CONFIG_EARLYSUSPEND
	register_early_suspend(&cpufreq_smartdroid_early_suspend_info);
#endif
	return cpufreq_register_governor(&cpufreq_gov_smartdroid);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_smartdroid);
}


MODULE_AUTHOR("NewWorld <cae11cae@naver.com>");
MODULE_DESCRIPTION("'cpufreq_smartdroid' - A smartdroid governor based on conservative");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_SMARTDROID
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
