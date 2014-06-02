#include "file_operations.h"
#include "message_queue.h"
#include <linux/module.h>
#include <linux/sched.h>



static spinlock_t open_count_guard;

static int open_count __read_mostly = 0;



static int handle_open_file(struct inode* inode, struct file* file)
{
	spin_lock_bh(&open_count_guard);
	if (open_count == 0)
	{
		open_count = 1;
		spin_unlock_bh(&open_count_guard);
		printk(KERN_INFO "File open: /proc/net/%s\n", filename);
		return 0;
	}
	spin_unlock_bh(&open_count_guard);

	// File was already opened
#ifdef DEBUG
	printk(KERN_DEBUG "Forcing flush of busy file: /proc/net/%s\n", filename);
#endif
	mq_signal_waiting();
	return -EBUSY;
}



static int handle_close_file(struct inode* inode, struct file* file)
{
	spin_lock_bh(&open_count_guard);
	printk(KERN_INFO "File close: /proc/net/%s\n", filename);
	open_count = 0;
	spin_unlock_bh(&open_count_guard);
	return 0;
}



static ssize_t handle_read_file(struct file* file, char __user* buf, size_t len, loff_t* ppos)
{
	struct msg* message;
	ssize_t count;
	int err;

	if (buf == NULL)
	{
		return -EINVAL;
	}

	if ((message = kcalloc(1, MAXSIZE, GFP_KERNEL)) == NULL)
	{
		printk(KERN_ERR "Insufficient memory for write buffer\n");
		return -ENOMEM;
	}

	if (len < MAXSIZE)
	{
		printk(KERN_ERR "User-space buffer is too small\n");
		kfree(message);
		return 0;
	}

	for (count = 0; count + MAXSIZE <= len; )
	{
		err = mq_dequeue(message);

		if (err != 0)
		{
#ifdef DEBUG
			printk(KERN_DEBUG "Flushing %ld bytes to user-space\n", count);
#endif
			// Force flush file
			kfree(message);
			return err < 0 ? err : count;
		}

#ifdef DEBUG
		if (message->queue_len == 0)
		{
			printk(KERN_DEBUG "ERROR STATE: Qdisc length is zero, yet message was marked as enqueued...\n");
			continue;
		}
#endif

		if (message->queue_len != (qdisc_len - 1))
		{
			printk(KERN_INFO "Queue length is shorter than qdisc_len value (queue_len=%u qdisc_len=%u)\n", message->queue_len, qdisc_len);
		}

		if (copy_to_user(buf + count, message, MSGSIZE(message)))
		{
			printk(KERN_ERR "Failed to copy message to user-space buffer\n");
			return -EFAULT;
		}

		count += MSGSIZE(message);
	}

#ifdef DEBUG
	printk(KERN_DEBUG "Flushing %ld bytes to user-space\n", count);
#endif
	kfree(message);
	return count;
}



static const struct file_operations fo_file_operations =
{
	.owner = THIS_MODULE,
	.open = handle_open_file,
	.release = handle_close_file,
	.read = handle_read_file,
	.llseek = noop_llseek,
};



int fo_init(void)
{
	spin_lock_init(&open_count_guard);

	if (!proc_create(filename, S_IRUSR, init_net.proc_net, &fo_file_operations))
	{
		printk(KERN_ERR "Failed to create file: /proc/net/%s\n", filename);
		return -1;
	}

	printk(KERN_INFO "Created file: /proc/net/%s\n", filename);
	return 0;
}



void fo_destroy(void)
{
	// Ensure that nobody can reopen the file while we're closing it
	do
	{
		mq_signal_waiting();
		
		spin_lock_bh(&open_count_guard);
		if (open_count == 0)
		{
			break; // we're not releasing the lock here on purpose
		}
		spin_unlock_bh(&open_count_guard);

	} while (1);

	remove_proc_entry(filename, init_net.proc_net);
	spin_unlock_bh(&open_count_guard);
	printk(KERN_INFO "Removing file: /proc/net/%s\n", filename);
}
