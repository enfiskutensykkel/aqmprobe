#include "message_queue.h"
#include <linux/sched.h>



/* The message queue */
static struct
{
	size_t             head, // pointer to the first element in the queue
	                   tail, // pointer to the last element in the queue
	                   size; // the total capacity of the queue
	struct msg*        qptr; // pointer to the actual queue
	wait_queue_head_t  wait; // condition variable to wait on when the queue is empty
	u16                fcnt, // flush counter
	                   frst; // flush counter reset value
} mq;



int mq_create(size_t size, u16 flush_count)
{
	struct msg* queue;
	size_t i;

	size = roundup_pow_of_two(size);
	if ((queue = kcalloc(size, sizeof(struct msg), GFP_KERNEL)) == NULL)
	{
		printk(KERN_ERR "Insufficient memory for message queue\n");
		return -ENOMEM;
	}

	mq.head = mq.tail = 0;
	mq.fcnt = mq.frst = flush_count;
	mq.size = size;
	mq.qptr = queue;
	init_waitqueue_head(&mq.wait);

	for (i = 0; i < size; ++i)
	{
		mq.qptr[i].mark = 0;
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

	kfree(mq.qptr);
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
	while (prev == tail);

	*slot = mq.qptr + prev;
	return 0;
}



void mq_enqueue(struct msg* slot)
{
	slot->mark = 1;
	wake_up(&mq.wait);
}



void mq_signal_waiting(void)
{
	mq.fcnt = 0;
	wake_up(&mq.wait);
}



int mq_dequeue(struct msg* buf)
{
	int error;

	error = wait_event_interruptible(mq.wait, !mq.fcnt || (mq.head != mq.tail && mq.qptr[mq.head].mark));

	if (error != 0)
	{
		printk(KERN_ERR "Unexpected error: %d\n", error);
		return error;
	}

	if (mq.fcnt-- == 0)
	{
		mq.fcnt = mq.frst;
	}

	if (mq.head == mq.tail)
	{
#ifdef DEBUG
		printk(KERN_DEBUG "mq_dequeue: flushing queue\n");
#endif
		return 1;
	}

#ifdef DEBUG
	if (mq.qptr[mq.head].mark == 0)
	{
		printk(KERN_ERR "mq_dequeue: dequeuing an unready message\n");
	}
#endif

	mq.qptr[mq.head].mark = 0;
	*buf = mq.qptr[mq.head];
	mq.head = (mq.head + 1) & (mq.size - 1);
	return 0;
}
