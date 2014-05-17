#include "qdisc_probe.h"
#include "message_queue.h"
#include <linux/kprobes.h>
#include <net/sch_generic.h>
#include <net/tcp.h>
//#include <asm/ptrace.h>



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

	printk(KERN_INFO "skb=%p sch=%p\n", skb, sch);
	
	printk(KERN_INFO "qdisc len=%d\n", skb_queue_len(&sch->q));

	*((struct msg**) ri->data) = NULL;
	return 1;
#else
	skb = (struct sk_buff*) regs->di;
	sch = (struct Qdisc*) regs->si;
#endif

	// Check if protocol is TCP
	ih = ip_hdr(skb);
	if (ih->protocol != 6)
	{
		*((struct msg**) ri->data) = NULL;
		return 1; // ignore packet
	}

	// Try to reserve a message queue slot
	if (mq_reserve(&msg))
	{
		*((struct msg**) ri->data) = NULL;
		return 1; // queue is full
	}
	*((struct msg**) ri->data) = msg;

	// Set message data
	th = tcp_hdr(skb);
	msg->src.sin_family = AF_INET;
	msg->src.sin_addr.s_addr = ih->saddr;
	msg->src.sin_port = th->source;
	msg->dst.sin_family = AF_INET;
	msg->dst.sin_addr.s_addr = ih->daddr;
	msg->dst.sin_port = th->dest;
	msg->plen = skb->len;
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
	
#ifdef DEBUG
	if (msg == NULL)
	{
		printk(KERN_ERR "handle_func_return: This shouldn't happen");
		return 0;
	}
#endif

	// FIXME: Return value might be in regs->orig_eax
	msg->drop = regs_return_value(regs) == NET_XMIT_DROP;
	mq_enqueue(msg);
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
	qp_qdisc_probe.maxactive = max_events;
	qp_qdisc_probe.kp.symbol_name = symbol;
	register_kretprobe(&qp_qdisc_probe);
}



int qp_detach(void)
{
	unregister_kretprobe(&qp_qdisc_probe);
	return qp_qdisc_probe.nmissed;
}
