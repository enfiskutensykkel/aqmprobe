#include "qdisc_probe.h"
#include "message_queue.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <net/net_namespace.h>
#include <net/pkt_sched.h>
#include <net/sch_generic.h>
#include <net/tcp.h>
#include <asm/ptrace.h>



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
	struct msg* message;

#ifdef __i386__
	// TODO: This needs to be tested
#else
#endif
}

static struct kretprobe qp_qdisc_probe =
{
	.handler = handle_func_return,
	.entry_handler = handle_func_invoke,
	.data_size = sizeof(struct msg*),
};
