/* drivers/misc/pantech_apanic.c
 *
 * Copyright (C) 2011 PANTECH, Co. Ltd.
 * based on drivers/misc/apanic.c
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.      See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/wakelock.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/notifier.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/preempt.h>
#include <asm/cacheflush.h>
#include <asm/system.h>
#include <linux/fb.h>
#include <linux/time.h>

#include <linux/io.h>
//(+) p16652 for change the logging structure 201301xx

#if defined(CONFIG_PANTECH_DEBUG)
#include <mach/pantech_apanic.h> //p14291_121102
#endif

#include "smd_private.h"
#include "modem_notifier.h"

#include <linux/nmi.h>
#include <mach/msm_iomap.h>

#include <linux/kobject.h>
#include <linux/sysfs.h>

#define PANIC_MAGIC    0xDAEDDAED
#define PHDR_VERSION   0x01

struct pantech_log_header{
    uint32_t magic;
    uint32_t version;
    uint32_t *klog_buf_address; 
    uint32_t *klog_end_idx;
    uint32_t *logcat_buf_address;
    uint32_t *logcat_w_off;
    uint32_t logcat_size;
};
// this struct must same, dload_debug_8960.c, logger.c, pantech_apanic.c, printk.c, pantech_phone_info.c

extern struct pantech_log_header *get_pantech_klog_dump_address(void);
extern struct pantech_log_header *get_pantech_logcat_dump_address(void);
static struct pantech_log_header *crash_buf_header = NULL;
//(+) p16652 for change the logging structure 201301xx

#if defined(CONFIG_PANTECH_DEBUG) //p14291_121102
struct pantech_debug_log {
#ifdef CONFIG_PANTECH_DEBUG_SCHED_LOG  //p14291_121102
	atomic_t idx_sched[CONFIG_NR_CPUS];
	struct sched_log sched[CONFIG_NR_CPUS][SCHED_LOG_MAX];
#endif

#ifdef CONFIG_PANTECH_DEBUG_IRQ_LOG  //p14291_121102
	atomic_t idx_irq[CONFIG_NR_CPUS];
	struct irq_log irq[CONFIG_NR_CPUS][SCHED_LOG_MAX];
#endif

#ifdef CONFIG_PANTECH_DEBUG_DCVS_LOG  //p14291_121102
	atomic_t dcvs_log_idx ;
	struct dcvs_debug dcvs_log[DCVS_LOG_MAX] ;	
#endif

#ifdef CONFIG_PANTECH_DEBUG_RPM_LOG  //p14291_121102
	atomic_t rpm_log_idx ;
	struct rpm_debug rpm_set_log[RPM_LOG_MAX] ;	
#endif
};
struct pantech_debug_log *pantech_dbg_log;
static unsigned int pantechdbg_paddr;
static unsigned int pantechdbg_size;
#endif

static int apanic_logging(struct notifier_block *this, unsigned long event, void *ptr)
{
      printk(KERN_EMERG "pantech_apanic: apanic_logging start\n");

#ifdef CONFIG_PREEMPT
      /* Ensure that cond_resched() won't try to preempt anybody */
      add_preempt_count(PREEMPT_ACTIVE);
#endif

      touch_softlockup_watchdog();

      if(crash_buf_header != NULL)
      {
          crash_buf_header->magic = PANIC_MAGIC;
          crash_buf_header->version = PHDR_VERSION;
      }
      else
          printk(KERN_ERR "apanic_logging : crash_buf_header is not initialized!\n");

      flush_cache_all();

#ifdef CONFIG_PREEMPT
      sub_preempt_count(PREEMPT_ACTIVE);
#endif

      return NOTIFY_DONE;
}
//(+, -) p16652 for change the logging structure 201301xx

/*****************************************************
 * PNANTEH APANIC MODULE INIT
 * **************************************************/
static struct notifier_block panic_blk = {
      .notifier_call    = apanic_logging,
};

#if defined(CONFIG_PANTECH_DEBUG) //p14291_121102
#define PANTECH_DBG_MEMORY_BUFFER_BASE 0x88C00000
static int __init __init_pantech_debug_log(void)
{
	int i;
	struct pantech_debug_log *vaddr;
	int size;

	if (pantechdbg_paddr == 0 || pantechdbg_size == 0) {
		pr_info("[PANTECH_DBG] %s: pantech debug buffer not provided. Using reserved memory..\n",
			__func__);
		size = sizeof(struct pantech_debug_log);

		//P14291_121115
		if (size>0x100000){
			pr_info("[PANTECH_DBG] Critical Warning.. SIZE should be less than 1M (size:0x%x)\n",size);
			return -EFAULT;
		}
		else{
			pantechdbg_size=size;
			pantechdbg_paddr=PANTECH_DBG_MEMORY_BUFFER_BASE;
			vaddr = ioremap_nocache(pantechdbg_paddr, pantechdbg_size);
		}
	} else {
		pr_info("[PANTECH_DBG] IOREMAP NOCACHE..\n");
		size = pantechdbg_size;
		vaddr = ioremap_nocache(pantechdbg_paddr, pantechdbg_size);
	}

	pr_info("[PANTECH_DBG] %s: vaddr=0x%x paddr=0x%x size=0x%x "
		"sizeof(struct pantech_debug_log)=0x%x\n", __func__,
		(unsigned int)vaddr, pantechdbg_paddr, pantechdbg_size,
		sizeof(struct pantech_debug_log));

	if ((vaddr == NULL) || (sizeof(struct pantech_debug_log) > size)) {
		pr_info("%s: ERROR! init failed!\n", __func__);
		return -EFAULT;
	}

	for (i = 0; i < CONFIG_NR_CPUS; i++) {
		#ifdef CONFIG_PANTECH_DEBUG_SCHED_LOG  //p14291_121102
		atomic_set(&(vaddr->idx_sched[i]), -1);
		#endif
		#ifdef CONFIG_PANTECH_DEBUG_IRQ_LOG  //p14291_121102
		atomic_set(&(vaddr->idx_irq[i]), -1);
		#endif
	}
	
	#ifdef CONFIG_PANTECH_DEBUG_DCVS_LOG  //p14291_121102
	atomic_set(&(vaddr->dcvs_log_idx), -1);
	#endif

#ifdef CONFIG_PANTECH_DEBUG_RPM_LOG  //p14291_121102
		atomic_set(&(vaddr->rpm_log_idx), -1);
#endif


	pantech_dbg_log = vaddr;

	pr_info("[PANTECH_DBG] %s: init done\n", __func__);

	return 0;
}
#endif

int __init pantech_apanic_init(void)
{
      unsigned size;
      struct pantech_log_header *klog_header, *logcat_log_header;

      klog_header = get_pantech_klog_dump_address();
      logcat_log_header = get_pantech_logcat_dump_address();

      crash_buf_header = (struct pantech_log_header *)smem_get_entry(SMEM_ID_VENDOR2, &size);

      if(!crash_buf_header){
          printk(KERN_ERR "pantech_apanic: no available crash buffer , initial failed\n");
          return 0;
      }
      else
      {
          atomic_notifier_chain_register(&panic_notifier_list, &panic_blk);
          memset(crash_buf_header, 0, sizeof(struct pantech_log_header));
      }

      crash_buf_header->magic=0;
      crash_buf_header->version=0;
      crash_buf_header->klog_buf_address = klog_header->klog_buf_address;
      crash_buf_header->klog_end_idx = klog_header->klog_end_idx;

      crash_buf_header->logcat_buf_address = logcat_log_header->logcat_buf_address;
      crash_buf_header->logcat_w_off = logcat_log_header->logcat_w_off;
      crash_buf_header->logcat_size = logcat_log_header->logcat_size;

	  printk("pantech_apanic : pantech_log_header initialized success for write to SMEM\n");

#if defined(CONFIG_PANTECH_DEBUG) //p14291_121102
	__init_pantech_debug_log();
#endif

      printk(KERN_INFO "apanic_pantech_init\n");
      return 0;
}
//(+, -) p16652 for change the logging structure 201301xx

#if defined(CONFIG_PANTECH_DEBUG)
#ifdef CONFIG_PANTECH_DEBUG_SCHED_LOG  //p14291_121102
void __pantech_debug_task_sched_log(int cpu, struct task_struct *task,
						char *msg)
{
	unsigned i;

	if (!pantech_dbg_log)
		return;

	if (!task && !msg)
		return;

	i = atomic_inc_return(&(pantech_dbg_log->idx_sched[cpu]))
		& (SCHED_LOG_MAX - 1);
	pantech_dbg_log->sched[cpu][i].time = cpu_clock(cpu);
	if (task) {
		strncpy(pantech_dbg_log->sched[cpu][i].comm, task->comm,
			sizeof(pantech_dbg_log->sched[cpu][i].comm));
		pantech_dbg_log->sched[cpu][i].pid = task->pid;
	} else {
		strncpy(pantech_dbg_log->sched[cpu][i].comm, msg,
			sizeof(pantech_dbg_log->sched[cpu][i].comm));
		pantech_dbg_log->sched[cpu][i].pid = -1;
	}
}
void pantech_debug_task_sched_log_short_msg(char *msg)
{
	__pantech_debug_task_sched_log(smp_processor_id(), NULL, msg);
}
void pantech_debug_task_sched_log(int cpu, struct task_struct *task)
{
	__pantech_debug_task_sched_log(cpu, task, NULL);
}
#endif

#ifdef CONFIG_PANTECH_DEBUG_IRQ_LOG  //p14291_121102
void pantech_debug_irq_sched_log(unsigned int irq, void *fn, int en, unsigned long long start_time)
{
	int cpu = smp_processor_id();
	unsigned i;

	if (!pantech_dbg_log)
		return;

	i = atomic_inc_return(&(pantech_dbg_log->idx_irq[cpu]))
		& (SCHED_LOG_MAX - 1);
	pantech_dbg_log->irq[cpu][i].time = start_time;
	pantech_dbg_log->irq[cpu][i].end_time = cpu_clock(cpu);
	pantech_dbg_log->irq[cpu][i].elapsed_time =
	pantech_dbg_log->irq[cpu][i].end_time - start_time;
	pantech_dbg_log->irq[cpu][i].irq = irq;
	pantech_dbg_log->irq[cpu][i].fn = (void *)fn;
	pantech_dbg_log->irq[cpu][i].en = en;
	pantech_dbg_log->irq[cpu][i].preempt_count = preempt_count();
	pantech_dbg_log->irq[cpu][i].context = &cpu;
}
#endif

#ifdef CONFIG_PANTECH_DEBUG_DCVS_LOG  //p14291_121102
void pantech_debug_dcvs_log(int cpu_no, unsigned int prev_freq,
						unsigned int new_freq)
{
	unsigned int i;
	if (!pantech_dbg_log)
		return;

	i = atomic_inc_return(&(pantech_dbg_log->dcvs_log_idx)) 
		& (DCVS_LOG_MAX - 1);
	pantech_dbg_log->dcvs_log[i].cpu_no = cpu_no;
	pantech_dbg_log->dcvs_log[i].prev_freq = prev_freq;
	pantech_dbg_log->dcvs_log[i].new_freq = new_freq;
	pantech_dbg_log->dcvs_log[i].time = cpu_clock(cpu_no);
}
#endif

#ifdef CONFIG_PANTECH_DEBUG_RPM_LOG  //p14291_121102
void pantech_debug_rpm_log(unsigned int set, unsigned int id, unsigned int value)
{
	int cpu = smp_processor_id();
	unsigned int i;
	
	if (!pantech_dbg_log)
		return;

	i = atomic_inc_return(&(pantech_dbg_log->rpm_log_idx)) 
		& (RPM_LOG_MAX - 1);
	pantech_dbg_log->rpm_set_log[i].set = set;
	pantech_dbg_log->rpm_set_log[i].id = id;
	pantech_dbg_log->rpm_set_log[i].value = value;
	pantech_dbg_log->rpm_set_log[i].time = cpu_clock(cpu);
}
#endif


static int __init pantech_dbg_setup(char *str)
{
	unsigned size = memparse(str, &str);

	pr_emerg("%s: str=%s\n", __func__, str);

	printk("pantech_dbg_setup\n");
	printk("%s: str=%s\n", __func__, str);

	if (size && (size == roundup_pow_of_two(size)) && (*str == '@')) {
		pantechdbg_paddr = (unsigned int)memparse(++str, NULL);
		pantechdbg_size = size;
	}

	printk("%s: pantechdbg_paddr = 0x%x\n", __func__, pantechdbg_paddr);
	printk("%s: pantechdbg_size = 0x%x\n", __func__, pantechdbg_size);

	return 1;
}

__setup("pantech_dbg=", pantech_dbg_setup);
#endif /* CONFIG_PANTECH_DEBUG */

module_init(pantech_apanic_init);

MODULE_AUTHOR("Pantech ls4 part1>");
MODULE_DESCRIPTION("Pantech errlogging driver");
MODULE_LICENSE("GPL v2");
