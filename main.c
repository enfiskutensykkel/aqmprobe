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
#include <linux/kernel.h>
#include <linux/types.h>
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


static int qdisc_len = 0;
module_param(qdisc_len, int, 0);
MODULE_PARM_DESC(qdisc_len, "Maximum number of packets enqueued in the qdisc");


static int buf_len = 0;
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buf_len, "Number of buffered event reports before discarding");




/* Report file */
const char filename[] = "aqmprobe";



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

	if (buf_len <= 10 || buf_len > 4096)
	{
		printk(KERN_ERR "Number of buffered event reports must be greater than 10 and less than 4096\n");
		return -EINVAL;
	}


	if (qdisc_len == 0 || qdisc_len > 1000)
	{
		printk(KERN_ERR "Number of possible enqueued packets must greater than 0 and less than 1000");
		return -EINVAL;
	}

	// Initialize message queue
	if (!mq_create(buf_len, qdisc_len))
	{
		return -ENOMEM;
	}

	// Create report file
	if (!fo_init())
	{
		return -ENOMEM;
	}
	
	// Attach probe
	qp_attach(entry_point, concurrent_evts);

	printk(KERN_INFO "Probe registered on Qdisc=%s\n", qdisc);

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
