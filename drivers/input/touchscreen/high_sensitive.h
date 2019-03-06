#ifndef _HIGH_SENSITIVE_H_
#define _HIGH_SENSITIVE_H_

#include <linux/notifier.h>

#define	TP_SENSITIVE_NODE_NAME	"/proc/tinno/tp/sensitive"

int sensitive_notifier_register(struct notifier_block *nb);

//typedef struct tp_sensitive_intf {
//	int (*set_sensitive)(int enable);
//};


#endif
