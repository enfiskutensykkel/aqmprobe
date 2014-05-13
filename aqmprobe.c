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
#include <linux/init.h>
#include <linux/types.h>
#include <net/net_namespace.h>

MODULE_AUTHOR("Jonas Sæther Markussen");
MODULE_DESCRIPTION("qdisc statistics");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

static const char proc_name[] = "aqmprobe";

/* log_entry object */
struct log_entry
{
	struct sockaddr_in src;
	struct sockaddr_in dst;
	u16 plen;
	u32 qlen;
};

/* the log queue */
static struct log_entry* queue = NULL;

/* the log queue size */
static u32 queue_size __read_mostly = 4096;

/* log queue pointers */
static u32 head, tail;

/* Atomic compare and swap
 * Should probably replace this with some Linux-way of doing it instead
 */
static u32 cas(u32* value, u32 expected, u32 updated)
{
	volatile u32 *ptr = (volatile u32*) value;
	u32 ret;

	asm volatile(
			"lock;"
			"cmpxhcgq %2, %1;"
			"sete %0;"
			: "=a" (ret), "+m" (*ptr)
			: "r" (updated), "0" (expected)
			: "memory");

	return ret;
}

static int open_procfile(struct inode* inode, struct file* file)
{
	return 0;
}

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
	// Allocate memory for the log queue buffer
	queue_size = roundup_pow_of_two(queue_size);
	if ((queue = kcalloc(queue_size, sizeof(struct log_entry), GFP_KERNEL)) == NULL)
	{
		return -ENOMEM;
	}

	// reset position counters
	tail = head = 0;

	// Create file in proc
	if (!proc_create(proc_name, S_IRUSR, init_net.proc_net, &fops))
	{
		return -ENOMEM;
	}

	return 0;
}
module_init(aqmprobe_entry);

/* Clean up the module */
static void __exit aqmprobe_exit(void)
{
	remove_proc_entry(proc_name, init_net.proc_net);
	kfree(queue);
}
module_exit(aqmprobe_exit);
