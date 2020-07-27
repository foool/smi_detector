/*  * A simple SMI detector. Use this module to detect large system latencies
    * introduced by the presence of vendor BIOS SMI (System Management Interrupts)
    * somehow gone awry. We do this by hogging all of the CPU(s) for configurable
    * time intervals, looking to see if something stole time from us. Therefore,
    * obviously, you should NEVER use this module in a production environment.
    * 
    * Copyright (C) 2008 Jon Masters, Red Hat, Inc. <jcm@redhat.com>
    * 
    * Licensed under the GNU General Public License, version 2.0.
    * 
    */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/stop_machine.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define smi_version "0.2.1"
#define SMI_BANNER "SMI Detector: "
#define DEFAULT_MS_PER_SAMPLE 1
#define DEFAULT_MS_SEP_SAMPLE 5000
#define DEFAULT_SMI_THRESHOLD 1

    MODULE_AUTHOR("Jon Masters <jcm@redhat.com>");
    MODULE_DESCRIPTION("A simple SMI detector");
    MODULE_LICENSE("GPL");

    static int debug = 0;
    static int enabled = 0;
    static int threshold = 0;

    module_param(debug, int, 0);
    module_param(enabled, int, 0);
    module_param(threshold, int, 0);

    struct task_struct *smi_kthread = NULL;

    struct smdata_struct {

        u64 last_sample;
        u64 max_sample;
        u64 smi_count;
        u64 threshold;
        ktime_t last_spike;
        u64 frequency;

        atomic_t pending;
        wait_queue_head_t wq;

    } smdata;

struct dentry *smi_debug_dir = NULL;            /* SMI debugfs directory */
struct dentry *smi_debug_max = NULL;            /* maximum TSC delta */
struct dentry *smi_debug_smi = NULL;            /* SMI detect count */
struct dentry *smi_debug_sample_ms = NULL;      /* sample size ms */
struct dentry *smi_debug_interval_ms = NULL;    /* interval size ms */
struct dentry *smi_debug_sample = NULL;     /* raw SMI samples (us) */
struct dentry *smi_debug_threshold = NULL;  /* latency threshold (us) */
struct dentry *smi_debug_frequency_us = NULL;   /* avg smi spike interval (us) */

u32 smi_sample_ms   = DEFAULT_MS_PER_SAMPLE;    /* sample size ms */
u32 smi_interval_ms = DEFAULT_MS_SEP_SAMPLE;    /* interval size ms */

/*
 *  * smi_get_sample -  Used to repeatedly capture the CPU TSC (or similar),
 *   *                      looking for potential SMIs. Called under stop_machine.
 *    */
static int smi_get_sample(void *data)
{
    ktime_t start, t1, t2, spike;
    s64 diff, total = 0;
    u64 sample = 0;
    struct smdata_struct *smi_data = (struct smdata_struct *)data;

    start = ktime_get(); /* start timestamp */

    do {

        t1 = ktime_get();
        t2 = ktime_get();

        total = ktime_to_us(ktime_sub(t2, start));

        diff = ktime_to_us(ktime_sub(t2, t1));
        if (diff < 0) {
            printk(KERN_ERR SMI_BANNER "time running backwards\n");
            return 1;
        }
        if (diff > sample)
            sample = diff; /* only want highest value per sample */

        if (diff > smi_data->threshold)
            spike = t1;

    } while (total <= USEC_PER_MSEC*smi_sample_ms);

    smi_data->last_sample = sample;

    if (sample > smi_data->threshold) {
        u64 tmp;

        smi_data->smi_count++;
        tmp = ktime_to_us(ktime_sub(spike, smi_data->last_spike));

        if (smi_data->smi_count > 2)
            smi_data->frequency = (smi_data->frequency + tmp) / 2;
        else 
            if (smi_data->smi_count == 2)
                smi_data->frequency = tmp;

        smi_data->last_spike = spike;
    }

    atomic_set(&smi_data->pending,1);

    if (sample > smi_data->max_sample)
        smi_data->max_sample = sample;

    return 0;
}

/*
 *  * smi_kthread_fn - Used to periodically sample the CPU TSC via smi_get_sample.
 *   *                  We use stop_machine, which intentionally introduces latency.
 *    */

static int smi_kthread_fn(void *data)
{
    int err = 0;
    struct smdata_struct *smi_data = (struct smdata_struct *)data;

    while (!kthread_should_stop()) {
        /* upstream this is stop_machine now */     
        err = stop_machine(smi_get_sample, smi_data, 0);

        wake_up(&smi_data->wq);

        if (msleep_interruptible(smi_interval_ms))
            goto out;
    }

out:
    return 0;
}

static int smi_debug_sample_fopen(struct inode *inode, struct file *filp)
{

    filp->private_data = (void *)&smdata;

    return 0;   
}
static ssize_t smi_debug_sample_fread(struct file *filp, char __user *ubuf,
        size_t cnt, loff_t *ppos)
{
    int len;
    char buf[64];
    struct smdata_struct *smi_data = filp->private_data;

    wait_event_interruptible(smi_data->wq, atomic_read(&smi_data->pending));
    atomic_set(&smi_data->pending,0);
    len = sprintf(buf, "%08llx\n", smi_data->last_sample);
    copy_to_user(ubuf, buf, len);

    return len;
}

static struct file_operations smi_sample_fops = {
    .open       = smi_debug_sample_fopen,
    .read       = smi_debug_sample_fread,
    .owner      = THIS_MODULE,
};

int smi_detector_init(void)
{

    printk(KERN_INFO SMI_BANNER "version %s\n", smi_version);
    if (!enabled) {
        printk(KERN_INFO SMI_BANNER "please reload with enabled=1\n");
        return -1;
    }

    smdata.last_sample = 0;
    smdata.max_sample = 0;
    smdata.smi_count = 0;
    smdata.frequency = 0;

    if (!threshold)
        smdata.threshold = DEFAULT_SMI_THRESHOLD;
    else
        smdata.threshold = threshold;

    init_waitqueue_head(&smdata.wq);
    atomic_set(&smdata.pending, 0);

    smi_debug_dir = debugfs_create_dir("smi_detector", NULL);

    smi_debug_sample_ms = debugfs_create_u32("ms_per_sample",
            0644, smi_debug_dir, &smi_sample_ms);
    smi_debug_interval_ms = debugfs_create_u32("ms_between_samples",
            0644, smi_debug_dir, &smi_interval_ms);
    smi_debug_max = debugfs_create_u64("max_sample_us",
            0644, smi_debug_dir, &smdata.max_sample);
    smi_debug_smi = debugfs_create_u64("smi_count",
            0644, smi_debug_dir, &smdata.smi_count);
    smi_debug_sample = debugfs_create_file("sample_us",
            0444, smi_debug_dir, &smdata, &smi_sample_fops);
    smi_debug_frequency_us = debugfs_create_u64("avg_smi_interval_us",
            0444, smi_debug_dir, &smdata.frequency);
    smi_debug_threshold = debugfs_create_u64("latency_threshold_us",
            0444, smi_debug_dir, &smdata.threshold);


    smi_kthread = kthread_run(smi_kthread_fn, &smdata, "smi_detector");

    return 0;

}

void smi_detector_exit(void)
{
    kthread_stop(smi_kthread);

    debugfs_remove(smi_debug_sample_ms);
    debugfs_remove(smi_debug_interval_ms);
    debugfs_remove(smi_debug_max);
    debugfs_remove(smi_debug_smi);
    debugfs_remove(smi_debug_frequency_us);
    debugfs_remove(smi_debug_threshold);
    debugfs_remove(smi_debug_sample);
    debugfs_remove(smi_debug_dir);
}

module_init(smi_detector_init);
module_exit(smi_detector_exit);
