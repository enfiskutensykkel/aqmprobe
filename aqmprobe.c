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
#include <linux/skbuff.h>
#include <net/net_namespace.h>
#include <net/pkt_sched.h>
#include <net/sch_generic.h>
#include <net/tcp.h>
#include <asm/ptrace.h>

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

static int max_active = 0;
module_param(max_active, int, 0);
MODULE_PARM_DESC(max_active, "Maximum number of concurrent events");

/* report object */
struct report
{
	struct sockaddr_in src;
	struct sockaddr_in dst;
	u16 slot;
	u16 plen;
	u32 qlen;
	u16 drop;
};

/* the report log queue */
static struct report* queue = NULL;

/* the report log queue size */
static u32 queue_size __read_mostly = 4096;

/* report log queue pointers */
static u32 head, tail;

/* report log queue condition variable */
static wait_queue_head_t waiting_queue;

static inline int load_addr(struct report* report, struct sk_buff* skb)
{
	struct iphdr* ih;
	struct tcphdr* th;
	struct in_addr tmp_addr;

	ih = ip_hdr(skb);
	if (ih->protocol != 8)
	{
		// Not IP protocol
		return 1;
	}

	th = tcp_hdr(skb);

	report->src.sin_family = AF_INET;
	report->dst.sin_family = AF_INET;

	tmp_addr.s_addr = ih->saddr;
	report->src.sin_addr = tmp_addr;
	report->src.sin_port = th->source;

	tmp_addr.s_addr = ih->daddr;
	report->dst.sin_addr = tmp_addr;
	report->dst.sin_port = th->dest;

	return 0;
}

/* Probe function on entry
 * Get the arguments to the probed function
 * Because of Linux syscall calling convention, the arguments are in eax/rax, 
 * edx/rdx and ecx/rcx.
 *
 * http://stackoverflow.com/questions/22686393/get-a-probed-functions-arguments-in-the-entry-handler-of-a-kretprobe
 */
static int entry_handler(struct kretprobe_instance* ri, struct pt_regs* regs)
{
	struct report* instance; 
	struct sk_buff* skb;
	struct Qdisc* sch;

	skb = (struct sk_buff*) regs->ax;
	sch = (struct Qdisc*) regs->dx;

	instance = (struct report*) ri->data;

	if (!load_addr(instance, skb))
	{
		// Not TCP segment
		return 0;
	}

	instance->slot = 0;
	printk(KERN_INFO "%d\n", ntohs(instance->dst.sin_port));
	return 0;
}


/* Probe function on return
 * Get the return value from the probed function.
 *
 * http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/samples/kprobes/kretprobe_example.c
 */
static int return_handler(struct kretprobe_instance* ri, struct pt_regs* regs)
{
	struct report* instance = (struct report*) ri->data;
	int retv = regs_return_value(regs);

	instance->drop = retv == NET_XMIT_DROP;

	return 0;
}



/* function probe descriptor */
static struct kretprobe qdisc_probe = 
{
	.handler = return_handler,
	.entry_handler = entry_handler,
	.data_size = sizeof(struct report),
	.maxactive = 20,
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
	.llseek = noop_llseek,
};

/* Initialize the module */
static int __init aqmprobe_entry(void)
{
	u32 i;

	// Validate arguments
	if (qdisc == NULL)
	{
		printk(KERN_ERR "Qdisc is required\n");
		return -EINVAL;
	}

	if ((qdisc_probe.kp.symbol_name = entry_point(qdisc)) == NULL)
	{
		printk(KERN_ERR "Unknown Qdisc: %s\n", qdisc);
		return -EINVAL;
	}

	if (max_active <= 0)
	{
		printk(KERN_ERR "Number of concurrent events must be 1 or greater\n");
		return -EINVAL;
	}
	qdisc_probe.maxactive = max_active;


	// Allocate memory for the log queue buffer
	queue_size = roundup_pow_of_two(queue_size);
	if ((queue = kcalloc(queue_size, sizeof(struct report), GFP_KERNEL)) == NULL)
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

	register_kretprobe(&qdisc_probe);

	printk(KERN_INFO "Probe registered on Qdisc=%s, reporting instances of %s\n", 
			qdisc, qdisc_probe.kp.symbol_name);
	return 0;
}
module_init(aqmprobe_entry);

/* Clean up the module */
static void __exit aqmprobe_exit(void)
{
	remove_proc_entry(proc_name, init_net.proc_net);
	unregister_kretprobe(&qdisc_probe);
	kfree(queue);

	printk(KERN_INFO "Unregistering probe, missed %d instances of %s\n", 
			qdisc_probe.nmissed, qdisc_probe.kp.symbol_name);
}
module_exit(aqmprobe_exit);
