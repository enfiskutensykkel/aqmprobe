/*
 * NB! This message queue API only works with a single consumer
 */
#ifndef __AQMPROBE_MQ_H__
#define __AQMPROBE_MQ_H__

#include <net/net_namespace.h>

#define MIN(a, b) ((a) <= (b) ? (a) : (b))



extern int qdisc_len __read_mostly;



struct pkt
{
	struct sockaddr_in src,	// source address      (src IP + src port)
                       dst;	// destination address (dst IP + dst port)
	u16                len;	// the size of the intercepted packet
};

struct msg
{
	u16        mark;      	// reserved by the message queue API
	u16        queue_len; 	// the queue length
	struct pkt packets[0];	// information about the packets in the queue
};

#define MAXSIZE (sizeof(struct msg) + sizeof(struct pkt) * (qdisc_len))
#define MSGSIZE(m) (sizeof(struct msg) + sizeof(struct pkt) * ((m)->queue_len))
		
		

/* Allocate and initialize the message queue */
int mq_create(size_t size, u16 flush_count);



/* Clean up and free the message queue */
void mq_destroy(void);



/* Try to reserve a slot in the message queue for a message */
int mq_reserve(struct msg** slot);



/* Mark the reserved slot as ready */
void mq_enqueue(struct msg* slot);



/* Release the reserved slot instead of enqueueing it */
void mq_release(struct msg* slot);



/* Try to dequeue a message from the message queue and copy it into msg */
int mq_dequeue(struct msg* msg);



/* Signal anyone waiting on the queue that there is no more to read */
void mq_signal_waiting(void);

#endif
