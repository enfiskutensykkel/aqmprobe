#ifndef __AQMPROBE_FO_H__
#define __AQMPROBE_FO_H__

extern const char filename[];



/* Create file for user-space applications to read from */
int fo_init(void);

/* Destroy the file */
void fo_destroy(void);

#endif
