#include "message_queue.h"
#include <linux/sched.h>
//#include <asm/cmpxchg.h>

/* The message queue */
static struct
{
	size_t             head, // pointer to the first element in the queue
	                   tail, // pointer to the last element in the queue
	                   size; // the total capacity of the queue
	struct msg*        qptr; // pointer to the actual queue
	wait_queue_head_t  wait; // condition variable to wait on when the queue is empty
	u16                fcnt; // flush counter
} mq;


int mq_create(size_t size)
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
	mq.fcnt = -1;
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
	if (mq.tail != mq.head)
	{
		printk(KERN_ERR "Race condition: queue is not empty on destroy");
	}

	kfree(mq.qptr);
}



int mq_reserve(struct msg** slot)
{
	size_t head, tail, prev, size;
	size = mq.size - 1;

	do
	{
		head = mq.head;
		tail = mq.tail;

		if (((tail - head) & size) >= size)
		{
			*slot = NULL;
			return -1;
		}

#ifdef __i386__
		// TODO: This needs to be tested
		prev = cmpxchg_local(&mq.tail, tail, (tail + 1) & size);
#else
		prev = cmpxchg64_local(&mq.tail, tail, (tail + 1) & size);
#endif
	}
	while (prev == mq.tail);

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
		return error;
	}

	if (mq.head == mq.tail)
	{
		return 1;
	}

	if (mq.qptr[mq.head].mark == 0)
	{
		printk(KERN_ERR "Race condition: dequeuing an unready message\n");
	}

	mq.qptr[mq.head].mark = 0;
	*buf = mq.qptr[mq.head];
	mq.head = (mq.head + 1) & (mq.size - 1);
	return 0;
}
