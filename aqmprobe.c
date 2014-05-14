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

/* Qdisc argument to module */
static char* qdisc = NULL;
module_param(qdisc, charp, 0);
MODULE_PARM_DESC(qdisc, "Qdisc to attach to");

/* Maximum number of concurrent events argument to module */
static int max_active = 0;
module_param(max_active, int, 0);
MODULE_PARM_DESC(max_active, "Maximum number of concurrent events");

/* Name of the report file (will be located under /proc/net/aqmprobe) */
static const char proc_name[] = "aqmprobe";

static struct { const char* qdisc; const char* entry; } known_entry_point_symbols[] =
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

static inline const char* entry_point_symbol_name(const char* qdisc)
{
	u32 i;
	for (i = 0; i < sizeof(known_entry_point_symbols) / sizeof(known_entry_point_symbols[0]); ++i)
	{
		if (strcmp(known_entry_point_symbols[i].qdisc, qdisc) == 0)
		{
			return known_entry_point_symbols[i].entry;
		}
	}

	return NULL;
}



/* Report entry type */
struct report
{
	struct sockaddr_in src; // TCP source information
	struct sockaddr_in dst;	// TCP destination information
	
	u16 packet_size;		// packet size (ethernet frame size)
	u32 queue_length;		// Qdisc length

	u8  slot_taken: 1,		// is this slot in use, used for queueing report entries
		dropped   : 1, 		// was the packet dropped
		is_tcp    : 1;		// does the packet belong to a TCP flow (if not, ignore it)
};

static inline void copy_sockaddr_in(const struct sockaddr_in* src, struct sockaddr_in* dst)
{
	dst->sin_family = src->sin_family;
	dst->sin_addr.s_addr = src->sin_addr.s_addr;
	dst->sin_port = src->sin_port;
}

/* Helper function to copy a repord entry */
static inline void copy_report_entry(const struct report* src, struct report* dst)
{
	copy_sockaddr_in(&src->src, &dst->src);
	copy_sockaddr_in(&src->dst, &dst->dst);

	dst->packet_size = src->packet_size;
	dst->queue_length = src->queue_length;
	dst->slot_taken = src->slot_taken;
	dst->dropped = src->dropped;
	dst->is_tcp = src->is_tcp;
}



/* The report log queue */
static struct report* queue = NULL;

/* The report log queue size */
static u32 queue_size __read_mostly = 4096;

/* Report log queue pointers */
static atomic_t head, tail;

/* Helper function to enqueue a report entry in the report entry queue */
static inline int enqueue(const struct report* report)
{
	u64 curr_tail, curr_head, prev_tail, idx;

	do
	{
		curr_head = atomic_read(&head);
		curr_tail = atomic_read(&tail);

		if (curr_tail >= curr_head + queue_size)
		{
			return 0; // queue is full
		}

		prev_tail = atomic_cmpxchg(&tail, curr_tail, (curr_tail + 1) & (queue_size - 1));
	}
	while (curr_tail == prev_tail);

	idx = curr_tail & (queue_size - 1);
	copy_report_entry(report, &queue[idx]);
	queue[idx].slot_taken = 1;

	return 1;
}

/* Helper function to dequeue a report entry from the report entry queue */
static inline int dequeue(struct report* report)
{
	u64 idx;
	u64 curr_head = atomic_read(&head);

	if (curr_head == atomic_read(&tail))
	{
		return 0; // queue is empty
	}

	idx = curr_head & (queue_size - 1);
	while (queue[idx].slot_taken == 0);

	copy_report_entry(&queue[idx], report);

	queue[idx].slot_taken = 0;
	atomic_set(&head, (curr_head + 1) & (queue_size -1));
	
	return 1;
}



/* Helper function to load TCP connection information into a report entry */
static inline int load_addr(struct report* report, struct sk_buff* skb)
{
	struct iphdr* ih;
	struct tcphdr* th;

	ih = ip_hdr(skb);
	if (ih->protocol != 0x06)
	{
		return 0;
	}

	th = tcp_hdr(skb);

	report->src.sin_family = AF_INET;
	report->src.sin_addr.s_addr = ih->saddr;
	report->src.sin_port = th->source;

	report->dst.sin_family = AF_INET;
	report->dst.sin_addr.s_addr = ih->daddr;
	report->dst.sin_port = th->dest;

	return 1;
}




/* Probe function on function invocation
 * Get the arguments to the probed function.
 *
 * Linux kernel function call convention is:
 * 	- i386   : eax, edx, ecx, rest on the stack
 *	- x86_64 : rdi, rsi, rdx, rcx, r8, r9, rest on the stack
 *
 * See: http://stackoverflow.com/questions/22686393/get-a-probed-functions-arguments-in-the-entry-handler-of-a-kretprobe
 */
static int entry_probe(struct kretprobe_instance* ri, struct pt_regs* regs)
{
	struct report* i; 
	struct sk_buff* skb;
	struct Qdisc* sch;

#ifdef __i386__
	// TODO: test this, not sure if arguments should be vice versa
	skb = (struct sk_buff*) regs->ax;
	sch = (struct Qdisc*) regs->dx;
#else
	skb = (struct sk_buff*) regs->di;
	sch = (struct Qdisc*) regs->si;
#endif

	i = (struct report*) ri->data;
	i->is_tcp = 0;

	// Check if packet is actually part of a TCP flow
	if (load_addr(i, skb))
	{
		i->slot_taken = 0;
		i->dropped = 1;
		i->is_tcp = 1;

		i->packet_size = skb->len;
		i->queue_length = skb_queue_len(&sch->q);
	}

	return 0;
}

/* Probe function on function return
 * Get the return value from the probed function.
 *
 * http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/samples/kprobes/kretprobe_example.c
 */
static int return_probe(struct kretprobe_instance* ri, struct pt_regs* regs)
{
	struct report* i = (struct report*) ri->data;

	if (i->is_tcp)
	{
		i->dropped = regs_return_value(regs) == NET_XMIT_DROP;
		return !enqueue(i);
	}

	return 0;
}

/* Function probe descriptor */
static struct kretprobe qdisc_probe = 
{
	.handler = return_probe,
	.entry_handler = entry_probe,
	.data_size = sizeof(struct report),
	.maxactive = 20,
};



/* "open" the report file handle
 *
 * This function is invoked when a user-space application attempts to open
 * the report file.
 *
 * The path to the file should be /proc/net/aqmprobe
 */
static int open_report_file(struct inode* inode, struct file* file)
{
	return 0;
}

/* "read" from the report file
 *
 * This function is invoked when a user-space application attempts to read
 * from the report file. It loads report data into a user-space buffer.
 */
static ssize_t read_report_file(struct file* file, char __user* buf, size_t len, loff_t* ppos)
{
	struct report report;

	if (buf == NULL)
	{
		return -EINVAL;
	}

	if (len < sizeof(struct report))
	{
		return 0;
	}

	if (!dequeue(&report))
	{
		return 0;
	}

	if (copy_to_user(buf, &report, sizeof(struct report)))
	{
		return -EFAULT;
	}

	printk(KERN_INFO "srcport=%u dstport=%u ps=%u ql=%u dropped=%d\n",
			ntohs(report.src.sin_port),
			ntohs(report.dst.sin_port),
			report.packet_size,
			report.queue_length,
			report.dropped);

	return sizeof(struct report);
}

/* proc file operations descriptor */
static const struct file_operations fops =
{
	.owner = THIS_MODULE,
	.open = open_report_file,	// do dummy action on open
	.read = read_report_file,	// load user-space buffer
	.llseek = noop_llseek,	 	// do nothing on seek
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

	if ((qdisc_probe.kp.symbol_name = entry_point_symbol_name(qdisc)) == NULL)
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
	atomic_set(&tail, 0);
	atomic_set(&head, 0);
	for (i = 0; i < queue_size; ++i)
	{
		(queue + i)->slot_taken = 0;
	}

	// Create file /proc/net/aqmprobe
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
