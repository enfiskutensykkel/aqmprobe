#include "qdisc_probe.h"
#include "message_queue.h"
#include <linux/kprobes.h>
#include <net/sch_generic.h>
#include <net/tcp.h>
#include <linux/atomic.h>



/* Count number of events we have to discard because the message queue is full */
static atomic_t miss_counter;



/* Probe function on function invocation
 * Get the arguments to the probed function.
 *
 * Linux kernel function call convention is:
 * 	- i386   : eax, edx, ecx, rest on the stack
 *	- x86_64 : rdi, rsi, rdx, rcx, r8, r9, rest on the stack
 *
 * See: http://stackoverflow.com/questions/22686393/get-a-probed-functions-arguments-in-the-entry-handler-of-a-kretprobe
 */
static int handle_func_invoke(struct kretprobe_instance* ri, struct pt_regs* regs)
{
	struct sk_buff* skb;
	struct Qdisc* sch;
	struct iphdr* ih;
	struct tcphdr* th;
	struct msg* msg;

	// Extract function arguments from registers
#ifdef __i386__
	skb = (struct sk_buff*) regs->ax;
	sch = (struct Qdisc*) regs->dx;
#else
	skb = (struct sk_buff*) regs->di;
	sch = (struct Qdisc*) regs->si;
#endif

	// Check if protocol is TCP
	ih = ip_hdr(skb);
	if (ih->protocol != 6)
	{
		// Ignore non-TCP packet
		*((struct msg**) ri->data) = NULL;
		return 0; 
	}

	// Try to reserve a message queue slot
	if (mq_reserve(&msg))
	{
		// Message queue is full
		*((struct msg**) ri->data) = NULL;
		atomic_inc(&miss_counter);
		return 0; 
	}

	*((struct msg**) ri->data) = msg;

	// Load information about the incomming packet
	th = tcp_hdr(skb);
	msg->packet.src.sin_family = AF_INET;
	msg->packet.src.sin_addr.s_addr = ih->saddr;
	msg->packet.src.sin_port = th->source;
	msg->packet.dst.sin_family = AF_INET;
	msg->packet.dst.sin_addr.s_addr = ih->daddr;
	msg->packet.dst.sin_port = th->dest;
	msg->packet.len = skb->len;

	// Load information about the qdisc

	msg->qlen = skb_queue_len(&sch->q);

	return 0;
}



/* Probe function on function return
 * Get the return value from the probed function.
 *
 * http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/samples/kprobes/kretprobe_example.c
 */
static int handle_func_return(struct kretprobe_instance* ri, struct pt_regs* regs)
{
	struct msg* msg = *((struct msg**) ri->data);
	
	if (msg != NULL)
	{
		if (regs_return_value(regs) == NET_XMIT_DROP)
		{
			mq_enqueue(msg);
		}
		else
		{
			mq_release(msg);
		}
	}

	return 0;
}



static struct kretprobe qp_qdisc_probe =
{
	.handler = handle_func_return,
	.entry_handler = handle_func_invoke,
	.data_size = sizeof(struct msg*),
	.nmissed = 0,
};



void qp_attach(const char* symbol, int max_events)
{
	atomic_set(&miss_counter, 0);
	qp_qdisc_probe.maxactive = max_events;
	qp_qdisc_probe.kp.symbol_name = symbol;
	register_kretprobe(&qp_qdisc_probe);
}



int qp_detach(void)
{
	unregister_kretprobe(&qp_qdisc_probe);
	return qp_qdisc_probe.nmissed + atomic_read(&miss_counter);
}
