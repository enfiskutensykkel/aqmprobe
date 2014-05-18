/*
 * NB! This message queue API only works with a single consumer
 */
#ifndef __AQMPROBE_MQ_H__
#define __AQMPROBE_MQ_H__

#include <net/net_namespace.h>

struct msg
{
	struct sockaddr_in  src, // source address      (src IP + src port)
					    dst; // destination address (dst IP + dst port)

	u32                qlen; // the length of the qdisc when packet was intercepted
	u16                plen; // the size of the intercepted packet
	u8                 drop; // was the intercepted packet dropped
	u8                 mark; // reserved by the message queue API
};



/* Allocate and initialize the message queue */
int mq_create(size_t size, u16 flush_count);



/* Clean up and free the message queue */
void mq_destroy(void);



/* Try to reserve a slot in the message queue for a message */
int mq_reserve(struct msg** slot);



/* Mark the reserved slot as ready */
void mq_enqueue(struct msg* slot);



/* Try to dequeue a message from the message queue and copy it into msg */
int mq_dequeue(struct msg* msg);



/* Signal anyone waiting on the queue that there is no more to read */
void mq_signal_waiting(void);

#endif
