/*
 * 	aqmprobe - A kernel module to probe Qdiscs for statistics
 *
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
#include <asm/cmpxchg.h>

MODULE_AUTHOR("Jonas Sæther Markussen");
MODULE_DESCRIPTION("qdisc statistics probe");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

/* Qdisc argument to module */
static char* qdisc = NULL;
module_param(qdisc, charp, 0);
MODULE_PARM_DESC(qdisc, "Qdisc to attach to");

/* Maximum number of concurrent events argument to module */
static int max_active = 0;
module_param(max_active, int, 0);
MODULE_PARM_DESC(max_active, "Maximum number of concurrent packet events");

/* Maximum buffer size */
static int buffer_size = 0;
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buffer_size, "Maximum number of buffered packet reports");



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

	u8  entry_ready: 1,		// is this slot in use, used for queueing report entries
		 dropped   : 1; 	// was the packet dropped
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
	dst->entry_ready = src->entry_ready;
	dst->dropped = src->dropped;
}



/* The report log queue */
static struct report* queue = NULL;

/* The report log queue size */
static u32 queue_size __read_mostly = 4096;

/* Report log queue pointers */
static u32 head, tail;

/* Report log queue locks */
static spinlock_t producer_lock, consumer_lock;

/* Report log queue condition variable */
static wait_queue_head_t waiting_queue;

/* Name of the report file (will be located under /proc/net/aqmprobe) */
static const char proc_name[] = "aqmprobe";

/* Flush to user-space application */
static u8 flush_now = 0;



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
	struct sk_buff* skb;
	struct Qdisc* sch;
	struct iphdr* ih;
	struct tcphdr* th;
	struct report* i;

#ifdef __i386__
	// TODO: test this, not sure if arguments should be vice versa
	skb = (struct sk_buff*) regs->ax;
	sch = (struct Qdisc*) regs->dx;
#else
	skb = (struct sk_buff*) regs->di;
	sch = (struct Qdisc*) regs->si;
#endif

	// Check if protocol is TCP
	ih = ip_hdr(skb);
	if (ih->protocol != 0x06)
	{
		*((struct report**) ri->data) = NULL;
		return 0; // ignore packet
	}

	spin_lock_bh(&producer_lock);
	if (((tail - head) & (queue_size - 1)) >= queue_size - 1)
	{
		*((struct report**) ri->data) = NULL;
		spin_unlock_bh(&producer_lock);
		return 1; // queue is full
	}

	i = &queue[tail];
	i->entry_ready = 0;
	tail = (tail + 1) & (queue_size - 1);
	spin_unlock_bh(&producer_lock);

	*((struct report**) ri->data) = i;
	th = tcp_hdr(skb);
	i->src.sin_family = AF_INET;
	i->src.sin_addr.s_addr = ih->saddr;
	i->src.sin_port = th->source;
	i->dst.sin_family = AF_INET;
	i->dst.sin_addr.s_addr = ih->daddr;
	i->dst.sin_port = th->dest;

	i->packet_size = skb->len;
	i->queue_length = skb_queue_len(&sch->q);

	return 0;
}

/* Probe function on function return
 * Get the return value from the probed function.
 *
 * http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/samples/kprobes/kretprobe_example.c
 */
static int return_probe(struct kretprobe_instance* ri, struct pt_regs* regs)
{
	struct report* i = *((struct report**) ri->data);

	if (i != NULL)
	{
		i->dropped = regs_return_value(regs) == NET_XMIT_DROP;
		i->entry_ready = 1;
		wake_up(&waiting_queue);
	}

	return 0;
}

/* Function probe descriptor */
static struct kretprobe qdisc_probe = 
{
	.handler = return_probe,
	.entry_handler = entry_probe,
	.data_size = sizeof(struct report*),
	.maxactive = 0,
};



/* Helper function to dequeue a report entry from the report entry queue */
static int dequeue(struct report* report)
{
	spin_lock_bh(&consumer_lock);
	if (tail == head)
	{
		spin_unlock_bh(&consumer_lock);
		return 0; // queue is empty
	}

	if (queue[head].entry_ready == 0)
	{
		spin_unlock_bh(&consumer_lock);
		return 0; // wait until ready
	}
	copy_report_entry(&queue[head], report);

	queue[head].entry_ready = 0;
	head = (head + 1) & (queue_size - 1);
	spin_unlock_bh(&consumer_lock);
	return 1;
}


/* "read" from the report file
 *
 * This function is invoked when a user-space application attempts to read
 * from the report file. It loads report data into a user-space buffer.
 */
static ssize_t read_report_file(struct file* file, char __user* buf, size_t len, loff_t* ppos)
{
	struct report report;
	size_t count;
	int error;

	if (buf == NULL)
	{
		return -EINVAL;
	}

	if (len < sizeof(struct report))
	{
		return 0;
	}

	for (count = 0; count + sizeof(struct report) <= len; )
	{
		error = wait_event_interruptible(waiting_queue, flush_now || dequeue(&report));

		if (error != 0)
		{
			return error;
		}
		else if (flush_now)
		{
			return count;
		}

		if (copy_to_user(buf + count, &report, sizeof(struct report)))
		{
			return -EFAULT;
		}

		count += sizeof(struct report);
	}

	return count;
}

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

/* "close" the report file handle */
static int close_report_file(struct inode* inode, struct file* file)
{
	return 0;
}

/* proc file operations descriptor */
static const struct file_operations fops =
{
	.owner = THIS_MODULE,
	.open = open_report_file,     // increase open count
	.release = close_report_file, // decrease open count
	.read = read_report_file,     // load user-space buffer
	.llseek = noop_llseek,        // do nothing on seek
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
		printk(KERN_ERR "Number of concurrent packet events must be 1 or greater\n");
		return -EINVAL;
	}
	qdisc_probe.maxactive = max_active;

	if (buffer_size <= 10 || buffer_size > 4096)
	{
		printk(KERN_ERR "Number of buffered packet reports must be greater than 10 and less than 4096\n");
		return -EINVAL;
	}

	spin_lock_init(&producer_lock);
	spin_lock_init(&consumer_lock);

	// Allocate memory for the log queue buffer
	queue_size = roundup_pow_of_two(buffer_size);
	if ((queue = kcalloc(queue_size, sizeof(struct report), GFP_KERNEL)) == NULL)
	{
		printk(KERN_ERR "Not enough memory\n");
		return -ENOMEM;
	}

	// reset position counters and slot markers
	init_waitqueue_head(&waiting_queue);
	head = tail = 0;
	for (i = 0; i < queue_size; ++i)
	{
		(queue + i)->entry_ready = 0;
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
	flush_now = 1;
	wake_up_all(&waiting_queue);

	remove_proc_entry(proc_name, init_net.proc_net);
	unregister_kretprobe(&qdisc_probe);
	kfree(queue);

	printk(KERN_INFO "Unregistering probe, missed %d instances of %s\n", 
			qdisc_probe.nmissed, qdisc_probe.kp.symbol_name);
}
module_exit(aqmprobe_exit);
