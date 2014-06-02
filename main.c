/*
 * 	aqmprobe - A kernel module to probe Qdiscs for drop statistics
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
#include "message_queue.h"
#include "qdisc_probe.h"
#include "file_operations.h"



MODULE_AUTHOR("Jonas Sæther Markussen");
MODULE_DESCRIPTION("Qdisc drop statistics probe");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");


static char* qdisc = "pfifo";
module_param(qdisc, charp, 0);
MODULE_PARM_DESC(qdisc, "Name of the qdisc to attach to and probe for drop statistics");


static int concurrent_evts = 0;
module_param(concurrent_evts, int, 0);
MODULE_PARM_DESC(concurrent_evts, "Number of concurrent events handled before discarding");


int qdisc_len __read_mostly = 0;
module_param(qdisc_len, int, 0);
MODULE_PARM_DESC(qdisc_len, "Maximum number of packets enqueued in the qdisc");


static int buf_len = 0;
module_param(buf_len, int, 0);
MODULE_PARM_DESC(buf_len, "Number of buffered event reports before discarding");


static int flush_freq = 1024;
module_param(flush_freq, int, 0);
MODULE_PARM_DESC(flush_freq, "Number of buffered event reports before triggering file flush");


char* filename = "aqmprobe";
module_param(filename, charp, 0);
MODULE_PARM_DESC(filename, "Filename of the report file");



/* Array of known Qdiscs and their entry points */
static struct { const char* qdisc; const char* entry; } known_qdiscs[] =
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



/* Find the entry point of a certain Qdisc */
static inline const char* qdisc_entry_point(const char* qdisc)
{
	size_t i;
	for (i = 0; i < sizeof(known_qdiscs) / sizeof(known_qdiscs[0]); ++i)
	{
		if (strcmp(known_qdiscs[i].qdisc, qdisc) == 0)
		{
			return known_qdiscs[i].entry;
		}
	}

	return NULL;
}



/* Initialize the module */
static int __init aqmprobe_entry(void)
{
	const char* entry_point;

	// Validate arguments
	if (qdisc == NULL)
	{
		printk(KERN_ERR "Qdisc argument is required\n");
		return -EINVAL;
	}

	if ((entry_point = qdisc_entry_point(qdisc)) == NULL)
	{
		printk(KERN_ERR "Unknown Qdisc: %s\n", qdisc);
		return -EINVAL;
	}

	if (concurrent_evts <= 0)
	{
		printk(KERN_ERR "Number of concurrent drop events must be 1 or greater\n");
		return -EINVAL;
	}

	// TODO: Calculate max values for buf_len and qdisc_len based on what they are combined instead

	if (buf_len <= 10 || buf_len > 1024)
	{
		printk(KERN_ERR "Number of buffered event reports must be in range [10-1024]\n");
		return -EINVAL;
	}

	if (qdisc_len == 0 || qdisc_len > 1000)
	{
		printk(KERN_ERR "Number of possible enqueued packets must be in range [1-1000]\n");
		return -EINVAL;
	}

	if (flush_freq < 1 || flush_freq >= 65536)
	{
		printk(KERN_ERR "Number of buffered packet event reports before triggering file flush must be in range [1-65536]\n");
		return -EINVAL;
	}

	// Initialize message queue
	if (mq_create(buf_len, flush_freq))
	{
		printk(KERN_ERR "Failed to allocate event report buffer\n");
		return -ENOMEM;
	}

	// Create report file
	if (fo_init())
	{
		printk(KERN_ERR "Failed to create report file\n");
		return -ENOMEM;
	}
	
	// Attach probe
	qp_attach(entry_point, concurrent_evts);

#ifdef DEBUG
	printk(KERN_INFO "Probe registered on Qdisc=%s (flush_freq=%d qdisc_len=%d buf_size=%d, sizeof(msg)=%lu sizeof(pkt)=%lu)\n", 
			qdisc, flush_freq, qdisc_len, buf_len, sizeof(struct msg), sizeof(struct pkt));
#else
	printk(KERN_INFO "Probe registered on Qdisc=%s\n", qdisc);
#endif

	return 0;
}
module_init(aqmprobe_entry);



/* Clean up the module */
static void __exit aqmprobe_exit(void)
{
	int nmissed;

    nmissed = qp_detach();
	fo_destroy();
	mq_destroy();
	
	printk(KERN_INFO "Unregistering probe, missed %d instances\n", nmissed);
}
module_exit(aqmprobe_exit);
