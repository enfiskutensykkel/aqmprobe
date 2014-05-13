/*
 *  Copyright (C) 2014 Jonas Sæther Markussen <jonassm@ifi.uio.no>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/types.h>
#include <net/net_namespace.h>

// http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/samples/kprobes/kretprobe_example.c
// https://www.kernel.org/doc/Documentation/kprobes.txt

MODULE_AUTHOR("Jonas Sæther Markussen");
MODULE_DESCRIPTION("qdisc statistics");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

static const char proc_name[] = "aqmprobe";

static struct { const char* qdisc; const char* entry; } known_entry_points[] =
{
	{
		.qdisc = "pfifo",
		.entry = "pfifo_enqueue"
	},
	{
		.qdisc = "bfifo",
		.entry = "bfifo_enqueue"
	}
};

static inline const char* entry_point(const char* qdisc)
{
	u32 i;
	for (i = 0; i < sizeof(known_entry_points) / sizeof(known_entry_points[0]); ++i)
	{
		if (strcmp(known_entry_points[i].qdisc, qdisc) == 0)
		{
			return known_entry_points[i].entry;
		}
	}

	return NULL;
}

static char* qdisc = NULL;
module_param(qdisc, charp, 0);
MODULE_PARM_DESC(qdisc, "Qdisc to attach to");

/* log_entry object */
struct log_entry
{
	struct sockaddr_in src;
	struct sockaddr_in dst;
	u16 slot;
	u16 plen;
	u32 qlen;
};

/* the log queue */
static struct log_entry* queue = NULL;

/* the log queue size */
static u32 queue_size __read_mostly = 4096;

/* log queue pointers */
static u32 head, tail;

/* log queue condition variable */
static wait_queue_head_t waiting_queue;

static int entry_handler(struct kretprobe_instance* ri, struct pt_regs* regs)
{

}

static struct kretprobe qdisc_kretprobe = 
{
	.handler = return_handler,
	.entry_handler = entry_handler
};

/* "open" the proc file */
static int open_procfile(struct inode* inode, struct file* file)
{
	return 0;
}

/* Read from the proc file
 * read log entries and output to the user space buffer
 */
static ssize_t read_procfile(struct file* file, char __user* buf, size_t len, loff_t* ppos)
{
	if (buf == NULL)
	{
		return -EINVAL;
	}

	return 0;
}

/* proc file operations descriptor */
static const struct file_operations fops =
{
	.owner = THIS_MODULE,
	.open = open_procfile,
	.read = read_procfile,
	.llseek = noop_llseek
};

/* Initialize the module */
static int __init aqmprobe_entry(void)
{
	u32 i;
	const char* entry;

	// Validate arguments
	if (qdisc == NULL)
	{
		printk(KERN_ERR "Qdisc is required\n");
		return -EINVAL;
	}

	if ((entry = entry_point(qdisc)) == NULL)
	{
		printk(KERN_ERR "Unknown Qdisc: %s\n", qdisc);
		return -EINVAL;
	}

	// Allocate memory for the log queue buffer
	queue_size = roundup_pow_of_two(queue_size);
	if ((queue = kcalloc(queue_size, sizeof(struct log_entry), GFP_KERNEL)) == NULL)
	{
		printk(KERN_ERR "Not enough memory\n");
		return -ENOMEM;
	}

	// reset position counters and slot markers
	tail = head = 0;
	for (i = 0; i < queue_size; ++i)
	{
		(queue + i)->slot = 0;
	}

	init_waitqueue_head(&waiting_queue);

	// Create file in proc
	if (!proc_create(proc_name, S_IRUSR, init_net.proc_net, &fops))
	{
		printk(KERN_ERR "Failed to create file /proc/%s\n", proc_name);
		return -ENOMEM;
	}

	printk(KERN_INFO "Probe registered on Qdisc=%s\n", qdisc);
	return 0;
}
module_init(aqmprobe_entry);

/* Clean up the module */
static void __exit aqmprobe_exit(void)
{
	printk(KERN_INFO "Unregistering probe\n");
	remove_proc_entry(proc_name, init_net.proc_net);
	kfree(queue);
}
module_exit(aqmprobe_exit);
