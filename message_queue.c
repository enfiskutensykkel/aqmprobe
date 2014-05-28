#include "message_queue.h"
#include <linux/sched.h>



/* The message queue */
static struct
{
	size_t             head, // pointer to the first element in the queue
	                   tail, // pointer to the last element in the queue
	                   size, // the total capacity of the queue
                       qlen; // length of a full qdisc
	char*              buff; // pointer to the report buffer
	wait_queue_head_t  wait; // condition variable to wait on when the queue is empty
	u16                fcnt, // flush counter
	                   frst; // flush counter reset value
} mq;



#define MSG(pos) ((struct msg*) (mq.buff + (sizeof(struct msg) + sizeof(struct pkt) * mq.qlen) * (pos)))



static inline void msg_copy(const struct msg* src, struct msg* dst, size_t pkts)
{
	const size_t n = (pkts < src->queue_len ? pkts : src->queue_len);
	size_t i; 

	for (i = 0; i < n; ++i)
	{
		dst->packets[i] = src->packets[i];
	}

	dst->packet = src->packet;
	dst->queue_len = n;
	dst->mark = 0;
}



int mq_create(size_t size, size_t len, u16 flush_count)
{
	char* buffer;
	size_t i;

	size = roundup_pow_of_two(size);
	if ((buffer = kcalloc(size, sizeof(struct msg) + sizeof(struct pkt) * len, GFP_KERNEL)) == NULL)
	{
		printk(KERN_ERR "Insufficient memory for message queue\n");
		return -ENOMEM;
	}

	mq.head = mq.tail = 0;
	mq.fcnt = mq.frst = flush_count;
	mq.size = size;
	mq.qlen = len;
	mq.buff = buffer;
	init_waitqueue_head(&mq.wait);

	for (i = 0; i < size; ++i)
	{
		MSG(i)->mark = 0;
	}

	return 0;
}



void mq_destroy(void)
{
#ifdef DEBUG
	if (mq.tail != mq.head)
	{
		printk(KERN_DEBUG "mq_destroy: queue is not empty on destroy");
	}
#endif

	kfree(mq.buff);
}



int mq_reserve(struct msg** slot)
{
	size_t head, tail, prev, size;
	size = mq.size - 1;

	// FIXME: Use cmpxchg on atomic types instead?
	do
	{
		head = mq.head;
		tail = mq.tail;

		if (((tail - head) & size) >= size)
		{
			*slot = NULL;
			wake_up(&mq.wait);
			return -1;
		}

#ifdef __i386__
		prev = cmpxchg(&mq.tail, tail, (tail + 1) & size);
#else
		prev = cmpxchg64(&mq.tail, tail, (tail + 1) & size);
#endif
	}
	while (prev != tail);

	*slot = MSG(tail);
	return 0;
}



void mq_enqueue(struct msg* slot)
{
	slot->mark = 1;
	wake_up(&mq.wait);
}



void mq_release(struct msg* slot)
{
	slot->mark = 2;
	wake_up(&mq.wait);
}



void mq_signal_waiting(void)
{
	mq.fcnt = 0;
	wake_up(&mq.wait);
}



int mq_dequeue(struct msg* buf, size_t len)
{
	int error;
	int cont;

	do
	{
		error = wait_event_interruptible(mq.wait, !mq.fcnt || (mq.head != mq.tail && MSG(mq.head)->mark));
		cont = MSG(mq.head)->mark == 2;

		if (error != 0)
		{
			printk(KERN_ERR "Unexpected error: %d\n", error);
			return error;
		}

		if (mq.fcnt-- == 0)
		{
			mq.fcnt = mq.frst;
		}

		if (mq.head == mq.tail || !MSG(mq.head)->mark)
		{
			return 1;
		}

		msg_copy(MSG(mq.head), buf, len);
		MSG(mq.head)->mark = 0;
		mq.head = (mq.head + 1) & (mq.size - 1);

	}
	while (cont);

	return 0;
}
