#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include "high_sensitive.h"

BLOCKING_NOTIFIER_HEAD(sensitive_notifier);

int sensitive_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&sensitive_notifier, nb);
}

static ssize_t sensitive_read(struct file *file, char __user *buf,
                              size_t size, loff_t *ppos)
{
	return size;
}

static ssize_t sensitive_write(struct file *filp, const char __user *buf,
                               size_t size, loff_t *ppos)
{
	int enable;
	char tmp[16];

	enable = copy_from_user(tmp, buf, sizeof(tmp));
	enable = !!simple_strtoul(tmp, NULL, 10);
	pr_debug("sensitive_write enable=%d\n", enable);
	blocking_notifier_call_chain(&sensitive_notifier, enable, NULL);
	return size;
}

static const struct file_operations sensitive_ops = {
	.owner = THIS_MODULE,
	.read = sensitive_read,
	.write = sensitive_write,
};

static int __init high_sensitive_init(void)
{
	struct proc_dir_entry * tinno;
	struct proc_dir_entry * tp;
	struct proc_dir_entry * sensitive;
	tinno = proc_mkdir("tinno", NULL);
	if (!tinno)
		goto exit;
	tp = proc_mkdir("tp", tinno);
	if (!tp)
		goto exit;
	sensitive = proc_create("sensitive", 0666, tp, &sensitive_ops);
	if (!sensitive)
		goto exit;
	return 0;

exit:
	pr_debug("fail to create proc entry.\n");
	return -1;
}

late_initcall(high_sensitive_init)
