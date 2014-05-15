#ifndef __AQMPROBE_QP_H__
#define __AQMPROBE_QP_H__

/* Register the kprobe on the symbol */
void qp_attach(const char* symbol, int max_events);

/* Unregister the kprobe */
void qp_detach(void);

#endif
