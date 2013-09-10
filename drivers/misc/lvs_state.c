#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#define PROC_NAME "lvs_state"
#define MAX_BUFFER_SIZE 64

struct lvs_struct {
	struct proc_dir_entry *lvs_state;
    int stored_count;
    char stored_buffer[MAX_BUFFER_SIZE];
    spinlock_t lvs_spinlock;
};
struct lvs_struct lvs;

static int lvs_state_read(char *buffer, char **start, off_t offset, int count, int *peof, void *dat)
{
    char *page = buffer;
    int rtn_value = 0;
    
    if(offset == 0) {
        pr_info("%s: was read /proc/%s by someone. stored_buffer=%s\n", __func__, PROC_NAME, lvs.stored_buffer);
        spin_lock(&lvs.lvs_spinlock);
        page += sprintf(page, lvs.stored_buffer);
        spin_unlock(&lvs.lvs_spinlock);
        rtn_value = page - buffer;
        pr_info("%s: offset=%ld count=%d peof=%d rtn_value=%d\n", __func__, offset, count, *peof, rtn_value);
    }

    return rtn_value;
}

static int lvs_state_write(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
    pr_info("%s: was written /proc/%s by someone. buffer(%ld)=%s\n", __func__, PROC_NAME, count, buffer);

	spin_lock(&lvs.lvs_spinlock);
    memset(lvs.stored_buffer, 0x00, sizeof(lvs.stored_buffer));
    if(copy_from_user(lvs.stored_buffer, buffer, count)) {
	    spin_unlock(&lvs.lvs_spinlock);
        return -EFAULT;
    }
    lvs.stored_count = count;
	spin_unlock(&lvs.lvs_spinlock);

    return count;
}

static int __init lvs_state_init(void)
{
    pr_info("%s: start lvs_state initialize\n", __func__);

    // create proc entry
	lvs.lvs_state = create_proc_entry(PROC_NAME, S_IFREG|S_IRUGO|S_IWUGO, 0); 
    lvs.lvs_state->read_proc = lvs_state_read;
    lvs.lvs_state->write_proc = lvs_state_write;

    // initialize lvs structure
    lvs.stored_count = 0;
    memset(lvs.stored_buffer, 0x00, sizeof(lvs.stored_buffer));

    // initialize spinlock
	spin_lock_init(&lvs.lvs_spinlock);

    pr_info("%s: end lvs_state initialize\n", __func__);

    return 0;
}
module_init(lvs_state_init);

static void __exit lvs_state_exit(void)
{
    remove_proc_entry("lvs_state", 0);
}
module_exit(lvs_state_exit);
