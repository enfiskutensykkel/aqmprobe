#ifndef __AQMPROBE_FO_H__
#define __AQMPROBE_FO_H__

#include <linux/types.h>

extern char* filename;



/* Create file for user-space applications to read from */
int fo_init(size_t queue_len);

/* Destroy the file */
void fo_destroy(void);

#endif
