#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
//add by bacon for fp_wake_lock
#include <linux/kobject.h>
#include <linux/debugfs.h>
#include <../kernel/power/power.h>
//add by bacon for fp_wake_lock
#include <linux/jiffies.h>
#include <linux/spi/spi.h>
#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/wakelock.h>
#include <asm/uaccess.h>

#include <linux/sched.h>
#include <linux/cdev.h>

#include <linux/poll.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include "slspi_qsee.h"
#include <linux/regulator/consumer.h>
#include "../fp_drv/fp_drv.h" //add by wenguangyu

#define VERSION_LOG	"Silead fingerprint drvier V0.1"

#define N_SPI_MINORS			32	/* ... up to 256 */

#define SILEAD_FP_NAME "silead_fp"

/*shutdown active/suspend */
#define PINCTRL_STATE_ACTIVE    "fp_active"
#define PINCTRL_STATE_SUSPEND   "fp_suspend"
/*shutdown active/suspend */

#ifdef GSL6313_INTERRUPT_CTRL
#define PINCTRL_STATE_INTERRUPT "fp_interrupt"
#endif

#define SLFP_VIO_CTRL 1

//static struct completion cmd_done_irq;
static DEFINE_MUTEX(silead_factory_mutex);

static int finger_status = 0;

/*
1: tap 2: longpress 3 doubleclick....etc...
typedef enum{
VKEY_TBD = 0,
VKEY_TAP,
VKEY_LONGPRESS,
VKEY_DOUBLECLICK,
VKEY_SLIDEUP,
VKEY_SLIDEDN,
VKEY_SILDELT,
VKEY_SLIDERT
}VKEYResult_t;
*/
static int finger_vkey_result = 0;

static struct fasync_struct *fasync_queue = NULL;
//static int key_status = 0;
//static int factory_status = 0;

#define KEY_FP_INT			KEY_POWER //KEY_WAKEUP // change by customer & framework support
#define KEY_FP_INT2			KEY_1 // change by customer & framework support

#if 0
#define SL_VDD_MIN_UV      2000000
#define SL_VDD_MAX_UV      3300000
#define SL_VIO_MIN_UV      1750000
#define SL_VIO_MAX_UV      1950000
#else
#define SL_VDD_MIN_UV      2800000
#define SL_VDD_MAX_UV      3000000
#define SL_VIO_MIN_UV      1750000
#define SL_VIO_MAX_UV      1950000
#endif

static DECLARE_BITMAP(minors, N_SPI_MINORS);
static wait_queue_head_t silead_poll_wq;

static int irq_enabled = 0;
static int irq_requested = 0;
static int irq_counter = 0;
static int isPowerOn = 0;

static struct spidev_data	*fp_spidev = NULL;
static unsigned int spidev_major = 0;
struct cdev spicdev;

#define REDUCE_REPEAT_IRQ

//#ifndef REDUCE_REPEAT_IRQ
static int g_irq_svc_debounce = 0;
//#endif
//add by matthew start
static int chip_power_off = 0;
//add by matthew end

static int silead_fp_probe(struct platform_device *spi);
static irqreturn_t finger_interrupt_handler(int irq, void *dev);
//add by bacon for fp_wake_lock
static ssize_t fp_wake_lock_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t n);
static ssize_t fp_wake_unlock_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t n);
static ssize_t fp_wake_lock_show(struct kobject *kobj,struct kobj_attribute *attr,char *buf);
static ssize_t fp_wake_unlock_show(struct kobject *kobj,struct kobj_attribute *attr,char *buf);
//add by bacon for fp_wake_lock
int silead_power_init(struct spidev_data *pdata);
static int silead_parse_dt(struct spidev_data *pdata);

int silead_power_ctl(struct spidev_data *pdata, bool on);
int silead_request_irq(struct spidev_data *pdata);


static char silead_gpio_config(struct spidev_data* spidev);
static int silead_gpio_free(struct spidev_data* spidev);

//for lib version
#define SL_MAX_LIB_BUF 64
static char sl_lib_ver_buf[SL_MAX_LIB_BUF] = "unknow";

//for power status detect
//#define POWER_NOTIFY
//static int is_screen_poweroff = 0;

//add by bacon for fp_wake_lock
#define silead_attr(_name) \
	static struct kobj_attribute _name##_attr = {	\
		.attr	= {				\
		                        .name = __stringify(_name),	\
		                        .mode = 0666,			\
		        },					\
		          .show	= _name##_show,			\
		                    .store	= _name##_store,		\
	}


silead_attr(fp_wake_lock);
silead_attr(fp_wake_unlock);

static struct attribute * g[] = {
	&fp_wake_lock_attr.attr,
	&fp_wake_unlock_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};

struct wakelock {
	char			*name;
	struct wakeup_source	ws;
};

static struct wakelock * g_wakelock_list[10] = {
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

static DEFINE_MUTEX(wakelocks_lock);

static ssize_t fp_wake_lock_show(struct kobject *kobj,
                                 struct kobj_attribute *attr,
                                 char *buf)
{
	//return pm_show_wakelocks(buf, true);

	char *str = buf;
	char *end = buf + PAGE_SIZE;
	int i;

	mutex_lock(&wakelocks_lock);

	for(i=0; i<10; i++) {
		if(g_wakelock_list[i]!=NULL) {
			str += scnprintf(str, end - str, "%s ", g_wakelock_list[i]->name);
		}
	}
	if (str > buf)
		str--;

	str += scnprintf(str, end - str, "\n");

	mutex_unlock(&wakelocks_lock);
	return (str - buf);
}

static ssize_t fp_wake_lock_store(struct kobject *kobj,
                                  struct kobj_attribute *attr,
                                  const char *buf, size_t n)
{
//	int error = pm_wake_lock(buf);
//	return error ? error : n;

	int i, j;
	int ret= -1;
	char * wl_name;
	struct wakelock *wl;

	wl_name = kstrndup(buf, n, GFP_KERNEL);
	if (!wl_name) {
		return -ENOMEM;
	}

	mutex_lock(&wakelocks_lock);
	for(j=0; j<10; j++) {
		if(g_wakelock_list[j]!=NULL) {
			if(strcmp(g_wakelock_list[j]->name,buf) == 0) {
				wl = g_wakelock_list[j];
				ret = n;
				break;
			}
		}
	}

	if (j == 10) {
		wl = kzalloc(sizeof(*wl), GFP_KERNEL);
		if (!wl)
			return -ENOMEM;

		wl->name = wl_name;
		wl->ws.name = wl_name;
		wakeup_source_add(&wl->ws);

		for(i=0; i<10; i++) {
			if(g_wakelock_list[i]==NULL) {
				g_wakelock_list[i] = wl;
				ret = n;
				break;
			}
		}
	}

	__pm_stay_awake(&wl->ws);
	mutex_unlock(&wakelocks_lock);

	// SL_LOGD("fp_wake_lock_store ret = %d\n", ret);
	return ret;
}

static ssize_t fp_wake_unlock_show(struct kobject *kobj,
                                   struct kobj_attribute *attr,
                                   char *buf)
{
	//return pm_show_wakelocks(buf, fasle);

	char *str = buf;
	char *end = buf + PAGE_SIZE;
	int i;

	mutex_lock(&wakelocks_lock);

	for(i=0; i<10; i++) {
		if(g_wakelock_list[i]!=NULL) {
			str += scnprintf(str, end - str, "%s ", g_wakelock_list[i]->name);
		}
	}
	if (str > buf)
		str--;

	str += scnprintf(str, end - str, "\n");

	mutex_unlock(&wakelocks_lock);
	return (str - buf);
}

static ssize_t fp_wake_unlock_store(struct kobject *kobj,
                                    struct kobj_attribute *attr,
                                    const char *buf, size_t n)
{
//	int error = pm_wake_unlock(buf);
//	return error ? error : n;
	struct wakelock *wl;
	int ret = -1;
	int i;

	mutex_lock(&wakelocks_lock);
	for(i=0; i<10; i++) {
		if(g_wakelock_list[i]!=NULL) {
			if(strcmp(g_wakelock_list[i]->name,buf)==0) {
				wl = g_wakelock_list[i];
				__pm_relax(&wl->ws);
				wakeup_source_remove(&wl->ws);
				kfree(wl->name);
				kfree(wl);
				g_wakelock_list[i] = NULL;
				ret = n;
				break;
			}
		}
	}

	mutex_unlock(&wakelocks_lock);
	//SL_LOGD("fp_wake_unlock_store ret = %d\n", ret);
	return ret;
}
//add by bacon for fp_wake_unlock

void silead_irq_enable(struct spidev_data *pdata)
{
	unsigned long irqflags = 0;
	printk("[guomingyi] IRQ Enable = %d.\n", pdata->int_irq);

	if(irq_requested == 0) {
		// printk("[guomingyi] %s, silead_request_irq\n", __func__);
		silead_request_irq(pdata);
	}

	spin_lock_irqsave(&pdata->spi_lock, irqflags);
	if (irq_enabled == 0 && irq_counter == 0) {
		irq_counter = 1;
		enable_irq(pdata->int_irq);
		irq_enabled = 1;
		printk("[guomingyi] enable_irq, irq_enabled = %d\n", irq_enabled);
		//#ifndef REDUCE_REPEAT_IRQ
		g_irq_svc_debounce = 1;
		mdelay(5);
		g_irq_svc_debounce = 0;
		//#endif
	} else {
		printk("[guomingyi] %s, irq has enabled!\n", __func__);
	}
	spin_unlock_irqrestore(&pdata->spi_lock, irqflags);
}

void silead_irq_disable(struct spidev_data *pdata)
{
	unsigned long irqflags;
	printk("[guomingyi] IRQ Disable = %d.\n", pdata->int_irq);

	if(irq_requested == 0) {
		return;
	}

	spin_lock_irqsave(&pdata->spi_lock, irqflags);
	if (irq_enabled && irq_counter>0) {
		irq_counter = 0;
		disable_irq(pdata->int_irq);
		irq_enabled = 0;
		printk("[guomingyi] %s, disable_irq\n", __func__);
	} else {
		printk("[guomingyi] %s, irq has disabled\n", __func__);
	}
	spin_unlock_irqrestore(&pdata->spi_lock, irqflags);
}

int silead_request_irq(struct spidev_data *pdata)
{
	int err;
	int irq_flags;

	if(irq_requested) {
		return 0;
	}

	irq_requested = 1;
	irq_enabled = 0;
	irq_counter = 0;
	spin_lock_init(&pdata->spi_lock);

	irq_flags = /*IRQF_NO_SUSPEND | */ IRQF_TRIGGER_RISING | IRQF_ONESHOT;
	DBG_MSG(MSG_INFO, "%s  Interrupt  %d  wake up is %d"
	        "irq flag is 0x%X\n",
	        __func__,pdata->int_irq, pdata->wakeup,irq_flags);

	err = request_irq(pdata->int_irq, finger_interrupt_handler,
	                  irq_flags, "silead_fp", pdata);

	if (err) {
		printk("Failed to request IRQ %d.\n", err);
		irq_requested = 0;
		return -1;
	}

	enable_irq_wake(pdata->int_irq);
	disable_irq(pdata->int_irq);
	return 0;
}

static int spidev_reset_hw(struct spidev_data *spidev)
{
	gpio_direction_output(spidev->shutdown_gpio, 0);
	mdelay(3);
	gpio_direction_output(spidev->shutdown_gpio, 1);
	mdelay(3);
	return 0;
}

static int spidev_shutdown_hw(struct spidev_data *spidev)
{
	gpio_direction_output(spidev->shutdown_gpio, 0);
	mdelay(3);
	//gpio_direction_output(spidev->shutdown_gpio, 1);
	//mdelay(3);
	return 0;
}


/*shutdown active/suspend */
/*IRQ wake-up control */
/*--------------------------------------------------------------------------
 * work function
 *--------------------------------------------------------------------------*/
#ifdef GSL6313_INTERRUPT_CTRL
static void finger_interrupt_work(struct work_struct *work)
{
	struct spidev_data *spidev = container_of(work, struct spidev_data, int_work);

	char*   env_ext[2] = {"SILEAD_FP_EVENT=IRQ", NULL};
	//#ifndef REDUCE_REPEAT_IRQ
	char*					env_ext_forged[2] = {"SILEAD_FP_EVENT=IRQ_FORGED", NULL};
	//#endif
	DBG_MSG(MSG_TRK, "irq bottom half spidev_irq_work enter \n");
	//#ifndef REDUCE_REPEAT_IRQ
	if(g_irq_svc_debounce) {
		kobject_uevent_env(&spidev->spi->dev.kobj, KOBJ_CHANGE, env_ext_forged);
		return;
	}
	g_irq_svc_debounce = 1;
	//#endif
	kobject_uevent_env(&spidev->spi->dev.kobj, KOBJ_CHANGE, env_ext );
}
#endif

static irqreturn_t finger_interrupt_handler(int irq, void *dev)
{
	int value;
	//struct timex txc;
	struct spidev_data *spidev = dev;
	//add by matthew start
	if(chip_power_off) {
		return IRQ_HANDLED;
	}
	//add by matthew end

#ifdef REDUCE_REPEAT_IRQ
	spidev_shutdown_hw(spidev);
#endif

	//do_gettimeofday(&(txc.time));
	//DBG_MSG(MSG_TRK, "txc.time.tv_sec=%ld,txc.time.tv_usec=%ld \n",txc.time.tv_sec,txc.time.tv_usec);
	DBG_MSG(MSG_TRK, "S interrupt top half has entered!\n");
// guomingyi add.
	FP_EVT_REPORT(HW_EVENT_WAKEUP);
	wake_lock_timeout(&spidev->wake_lock, 10*HZ);

	value = gpio_get_value_cansleep(spidev->int_wakeup_gpio);
	DBG_MSG(MSG_TRK, "S IRQ %d , GPIO %d state is %d\n",
	        irq,spidev->int_wakeup_gpio,value);
	DBG_MSG(MSG_TRK, "state is %d\n", value);
#ifdef IRQ_SVC_DEBOUNCE
	if(!atomic_read(&spidev->irq_svc_debounce)) {
		atomic_set(&spidev->irq_svc_debounce, 1);
#endif

		queue_work(spidev->int_wq,&spidev->int_work);
		irq_counter = 0;
		irq_enabled = 0;
#ifdef IRQ_SVC_DEBOUNCE
	}
#endif

	return IRQ_HANDLED;
}


static struct class *spidev_class;

int silead_power_deinit(struct spidev_data *pdata);

static int silead_fp_remove(struct platform_device *spi)
{
	struct spidev_data *spidev = platform_get_drvdata(spi);

	/* make sure ops on existing fds can abort cleanly */
	spin_lock_irq(&spidev->spi_lock);
	spidev->spi = NULL;

	if (spidev->int_irq) {
		free_irq(spidev->int_irq, spidev);
		irq_enabled = 0;
		irq_requested = 0;
		irq_counter = 0;
	}

	gpio_free(spidev->int_wakeup_gpio);
	gpio_free(spidev->shutdown_gpio);

	silead_power_deinit(spidev);

	platform_set_drvdata(spi, NULL);
	spin_unlock_irq(&spidev->spi_lock);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int silead_fp_suspend(struct device *dev)
{
	DBG_MSG(MSG_INFO, "silead_fp suspend!\n");
	return 0;
}

static int silead_fp_resume(struct device *dev)
{
	DBG_MSG(MSG_INFO, "silead_fp resume!\n");
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(silead_fp_pm_ops, silead_fp_suspend, silead_fp_resume);

static struct of_device_id silead_of_match[] = {
	{ .compatible = "qcom,fingerprint",},
	{ },
};

static struct platform_driver silead_fp_driver = {
	.driver = {
		.name 	= "silead_fp",
		.owner = THIS_MODULE,
		.pm 	= &silead_fp_pm_ops,
		.of_match_table = silead_of_match,
	},
	.probe 	= silead_fp_probe,
	.remove = silead_fp_remove,
};


static long spidev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct spidev_data	*spidev;
	int retval = 0;
	//int ret = -1;
	//int err = 0;
	/* Check type and command number */
	if (_IOC_TYPE(cmd) != SPI_IOC_MAGIC)
		return -ENOTTY;

	spidev = (struct spidev_data *)filp->private_data;

	mutex_lock(&spidev->buf_lock);
	switch (cmd) {
	case SPI_HW_RESET :
		DBG_MSG(MSG_INFO, "SPI_HW_RESET called\n");
		spidev_reset_hw(spidev);
		//add by matthew start
		if(chip_power_off) {
			mdelay(1);
			chip_power_off = 0;
		}
		//add by matthew end
		break;

	case SPI_HW_SHUTDOWN:
		DBG_MSG(MSG_INFO, "SPI_HW_SHUTDOWN called\n");
		//add by matthew start
		chip_power_off = 1;
		//add by matthew end
		spidev_shutdown_hw(spidev);
		//add by matthew start
		silead_irq_enable(spidev);
		//add by matthew end
		break;

	case SPI_OPEN_CLOCK:
		DBG_MSG(MSG_INFO, "SPI_OPEN_CLOCK called\n");
		break;

	case SPI_HW_POWEROFF:
		DBG_MSG(MSG_INFO, "SPI_HW_POWEROFF called\n");
		silead_power_ctl(spidev, 0);
		break;

	case SPI_HW_POWERON:
		DBG_MSG(MSG_INFO, "SPI_HW_POWERON called\n");
		silead_power_ctl(spidev, 1);
		break;

	case SPI_HW_SET_APP_VER: {
		int ret = copy_from_user(sl_lib_ver_buf, (char *)arg, SL_MAX_LIB_BUF);
		if (!ret) {
			sl_lib_ver_buf[SL_MAX_LIB_BUF-1] = '\0';
			full_fp_chip_info(sl_lib_ver_buf);
		}
		full_fp_chip_name(SILEAD_FP_NAME);
	}
	break;

	case SPI_HW_IRQ_ENABLE:
		DBG_MSG(MSG_INFO,"[guomingyi]--SPI_HW_IRQ_ENABLE:%d\n", (int)arg);
		if(arg) {
#ifdef IRQ_SVC_DEBOUNCE
			atomic_set(&spidev->irq_svc_debounce, 0);
#endif
			silead_irq_enable(spidev);
		} else {
			silead_irq_disable(spidev);
		}
		break;

	case SPI_HW_IRQ_REQUEST:
		DBG_MSG(MSG_INFO,"[guomingyi]--SPI_HW_IRQ_REQUEST \n");
		silead_request_irq(spidev);
		break;

	case SPI_HW_FINGER_STATE_INFO:

		finger_status = (int)arg;
		printk("[guomingyi]--finger_status:%d\n", (int)arg);
		FP_EVT_REPORT(finger_status == 1 ? 1 : 2);// guomingyi add.
		if(fasync_queue) {
			kill_fasync(&fasync_queue, SIGIO, POLL_IN);
			printk("[guomingyi]-----kill_fasync to user(finger_status) !-----\n");
		} else {
			printk("[guomingyi]-----fasync_queue-is null(finger_status)!----\n");
		}

		break;

	case IOCTL_FINGER_STATE_INFO:
		retval = __put_user(finger_status, (__u32 __user *)arg);
		if(retval) {
			printk("[guomingyi]__put_user(finger_status) err: %d\n", retval);
		}
		break;

	case IOCTL_HW_GPIO_REQUEST:

		DBG_MSG(MSG_INFO,"silead:--IOCTL_HW_GPIO_REQUEST\n");

		if(silead_power_init(spidev) < 0) {
			DBG_MSG(MSG_INFO,"silead:silead_power_init failed\n");
			break;
		}
		silead_power_ctl(spidev, 1);
		if(silead_parse_dt(spidev) < 0) {
			DBG_MSG(MSG_INFO,"silead_parse_dt failed\n");
			break;
		}

		silead_gpio_config(spidev);

		irq_enabled = 0;
		irq_counter = 0;
		break;

	case IOCTL_HW_GPIO_FREE:
		DBG_MSG(MSG_INFO,"silead:--IOCTL_HW_GPIO_FREE\n");
		silead_power_ctl(spidev, 0);
		silead_power_deinit(spidev);
		silead_gpio_free(spidev);
		break;


	case SPI_HW_FINGER_VKEY_RESULT:
		finger_vkey_result = (int)arg;
		printk("[guomingyi]--SPI_HW_FINGER_VKEY_RESULT:%d\n", finger_vkey_result);
		retval = __put_user(finger_vkey_result, (__u32 __user *)arg);
		if(retval) {
			printk("[guomingyi]__put_user err: %d\n", retval);
		}

		break;

	case IOCTL_FINGER_VKEY_RESULT:
		retval = __put_user(finger_vkey_result, (__u32 __user *)arg);
		if(retval) {
			printk("[guomingyi]__put_user err: %d\n", retval);
		}
		if(fasync_queue) {
			kill_fasync(&fasync_queue, SIGIO, POLL_IN);
			printk("[guomingyi]-----kill_fasync to user(finger_vkey_result) !-----\n");
		} else {
			printk("[guomingyi]-----fasync_queue-is null(finger_vkey_result)!----\n");
		}
		break;

	default:
		break;
	}

	mutex_unlock(&spidev->buf_lock);
	return retval;
}

static unsigned int spidev_poll(struct file *file, poll_table *wait)
{
	int mask=0;
	poll_wait(file, &silead_poll_wq, wait);

	return mask;
}
static int spidev_fp_fasync(int fd, struct file * filp, int on)
{
	printk("[guomingyi]:%s enter \n",__func__);
	return fasync_helper(fd, filp, on, &fasync_queue);
}

//<copy from 7701> add by yinglong.tang
int silead_power_ctl(struct spidev_data *pdata, bool on)
{
	int rc = 0;


	DBG_MSG(MSG_INFO,"silead:--silead_power_ctl\n");

	if (on && (!isPowerOn)) {
		rc = regulator_enable(pdata->vdd);
		if (rc) {
			printk("SLCODE Regulator vdd enable failed rc=%d\n", rc);
			return rc;
		}

#ifdef SLFP_VIO_CTRL
		rc = regulator_enable(pdata->vio);
		if (rc) {
			printk("SLCODE Regulator vio enable failed rc=%d\n", rc);
			regulator_disable(pdata->vdd);
			return rc;
		}
#endif
		msleep(10);

		isPowerOn = 1;
		printk(" set PowerOn ok !\n");
	} else if (!on && (isPowerOn)) {

		rc = regulator_disable(pdata->vdd);
		if (rc) {
			printk("SLCODE Regulator vdd disable failed rc=%d\n", rc);
			return rc;
		}

#ifdef SLFP_VIO_CTRL
		rc = regulator_disable(pdata->vio);
		if (rc) {
			printk("SLCODE Regulator vio disable failed rc=%d\n", rc);
		}
#endif

		isPowerOn = 0;
		printk(" set PowerDown !ok \n");
	} else {
		printk(	"SLCODE Ignore power status change from %d to %d\n",
		        on, isPowerOn);
	}
	return rc;
}

int silead_power_init(struct spidev_data *pdata)
{
	int ret = -1;

	pdata->vdd = regulator_get(&pdata->spi->dev, "vdd");
	if (IS_ERR(pdata->vdd)) {
		ret = PTR_ERR(pdata->vdd);
		printk("SLCODE Regulator get failed vdd ret=%d\n", ret);
		return ret;
	}

	if (regulator_count_voltages(pdata->vdd) > 0) {
		ret = regulator_set_voltage(pdata->vdd, SL_VDD_MIN_UV,
		                            SL_VDD_MAX_UV);
		if (ret) {
			printk("SLCODE Regulator set_vtg failed vdd ret=%d\n", ret);
			goto reg_vdd_put;
		}
	}

#ifdef SLFP_VIO_CTRL
	pdata->vio = regulator_get(&pdata->spi->dev, "vio");
	if (IS_ERR(pdata->vio)) {
		ret = PTR_ERR(pdata->vio);
		printk("SLCODE Regulator get failed vio ret=%d\n", ret);
		goto reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(pdata->vio) > 0) {
		ret = regulator_set_voltage(pdata->vio,
		                            SL_VIO_MIN_UV,
		                            SL_VIO_MAX_UV);
		if (ret) {
			printk("SLCODE Regulator set_vtg failed vio ret=%d\n", ret);
			goto reg_vio_put;
		}
	}
#endif

	DBG_MSG(MSG_INFO,"SLCODE Regulator set_vtg OK vdd ret=%d \n", ret);
	return 0;

#ifdef SLFP_VIO_CTRL
reg_vio_put:
	regulator_put(pdata->vio);
reg_vdd_set_vtg:
	if (regulator_count_voltages(pdata->vdd) > 0)
		regulator_set_voltage(pdata->vdd, 0, SL_VDD_MAX_UV);
#endif
reg_vdd_put:
	regulator_put(pdata->vdd);
	return ret;
}

int silead_power_deinit(struct spidev_data *pdata)
{
	int ret = 0;

	DBG_MSG(MSG_INFO,"silead:--silead_power_deinit\n");

	if (pdata->vdd) {
		if (regulator_count_voltages(pdata->vdd) > 0)
			regulator_set_voltage(pdata->vdd, 0, SL_VDD_MAX_UV);

		regulator_disable(pdata->vdd);
		isPowerOn = 0;

		regulator_put(pdata->vdd);
	}

#ifdef SLFP_VIO_CTRL
	if (pdata->vio) {
		if (regulator_count_voltages(pdata->vio) > 0)
			regulator_set_voltage(pdata->vio, 0, SL_VIO_MAX_UV);

		regulator_disable(pdata->vio);
		regulator_put(pdata->vio);
	}
#endif

	return ret;
}

#ifdef POWER_NOTIFY
static int silead_fb_state_chg_callback(struct notifier_block *nb,
                                        unsigned long val, void *data)
{
	struct fb_event *evdata = data;
	unsigned int blank;
	struct spidev_data *fp = container_of(nb, struct spidev_data, notifier);

	if (val != FB_EARLY_EVENT_BLANK) {
		return 0;
	}

	if (evdata && evdata->data && val == FB_EARLY_EVENT_BLANK && fp) {
		blank = *(int *)(evdata->data);
		switch (blank) {
		case FB_BLANK_POWERDOWN:
			is_screen_poweroff = 1;
			complete(&cmd_done_irq);
			//is_interrupt |= 2;
			//wake_up(&fp->slfp_wait);
			break;
		case FB_BLANK_UNBLANK:
			is_screen_poweroff = 0;
			break;
		default:
			printk("%s defalut\n", __func__);
			break;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block silead_noti_block = {
	.notifier_call = silead_fb_state_chg_callback,
};
#endif


static int silead_gpio_free(struct spidev_data* spidev)
{
	if(irq_requested == 1) {
		free_irq(spidev->int_irq, spidev);
		printk("silead:free irq success\n");
		irq_requested = 0;
		spidev->int_irq = 0;
	}
	if (gpio_is_valid(spidev->shutdown_gpio)) {
		gpio_free(spidev->shutdown_gpio);
		spidev->shutdown_gpio = 0;
		printk("silead:remove shutdown_gpio success\n");
	}

	if (gpio_is_valid(spidev->int_wakeup_gpio)) {
		gpio_free(spidev->int_wakeup_gpio);
		spidev->int_wakeup_gpio = 0;
		printk("silead:remove int_wakeup_gpio success\n");
	}
	DBG_MSG(MSG_INFO,"silead:%s free res success\n",__func__);
	return 0;
}


static char silead_gpio_config(struct spidev_data* spidev)
{
	int ret = -1;

	ret =  gpio_request(spidev->shutdown_gpio, "reset-gpio");//spidev->shutdown_gpio
	if(ret < 0) {
		printk("silead:reset gpio_request failed ret = %d", ret);
		ret = -ENODEV;
		return ret;
	}
	{
		printk("silead:%s() RST%d request success, err=0x%x.\n", __func__, spidev->shutdown_gpio, ret);
		gpio_direction_output(spidev->shutdown_gpio, 0);
		mdelay(10);
		gpio_direction_output(spidev->shutdown_gpio, 1);
		mdelay(10);
		printk("silead:%s() Reset ...\n", __func__);
	}

	ret = gpio_request(spidev->int_wakeup_gpio, "irq-gpio");
	if(ret < 0) {
		printk("silead:irq gpio_request failed ret = %d", ret);
		ret = -ENODEV;
		return ret;
	}

	DBG_MSG(MSG_INFO,"silead:irq gpio_request ret = %d", ret);
	{
		gpio_direction_input(spidev->int_wakeup_gpio);
		spidev->int_irq = gpio_to_irq(spidev->int_wakeup_gpio);
		DBG_MSG(MSG_INFO,"silead:%s() IRQ(%d) = ISR(%d) request success, err=0x%x.\n", __func__, spidev->int_wakeup_gpio, spidev->int_irq, ret);
	}

	return ret;
}

static int silead_parse_dt(struct spidev_data *pdata)
{
	struct device *dev = &pdata->spi->dev;
	struct device_node *np = dev->of_node;

	/* +++reset, irq gpio info+++ */
	pdata->shutdown_gpio = of_get_named_gpio(np, "qcom,reset-gpio", 0);
	printk("shutdown_gpio = %d\n", pdata->shutdown_gpio);
	if (pdata->shutdown_gpio < 0)
		return pdata->shutdown_gpio;

	pdata->int_wakeup_gpio = of_get_named_gpio(np, "qcom,irq-gpio", 0);
	printk("int_wakeup_gpio = %d\n", pdata->int_wakeup_gpio);
	if (pdata->int_wakeup_gpio < 0)
		return pdata->int_wakeup_gpio;

	printk("rst-gpio = %d, irq_pin = %d\n", pdata->shutdown_gpio, pdata->int_wakeup_gpio);

	return 0;
}


#ifdef CONFIG_COMPAT
static long spidev_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return spidev_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define spidev_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int spidev_open(struct inode *inode, struct file *filp)
{
	filp->private_data = fp_spidev;
	return 0;
}

static int spidev_release(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static const struct file_operations spidev_fops = {
	.owner =	THIS_MODULE,
	/* REVISIT switch to aio primitives, so that userspace
	 * gets more complete API coverage.  It'll simplify things
	 * too, except for the locking.
	 */
	//.write =	spidev_write,
	//.read =		spidev_read,
	.unlocked_ioctl = spidev_ioctl,
	.compat_ioctl = spidev_compat_ioctl,
	.open =		spidev_open,
	.release =	spidev_release,
	.poll			= spidev_poll,
	.fasync 		= spidev_fp_fasync,
};

static int silead_fp_probe(struct platform_device *spi)
{
	struct spidev_data	*spidev;
	int			status;
	int         error;
	unsigned long		minor;
	//int ret;
	dev_t devno;

	DBG_MSG(MSG_INFO, "S1\n");

	if (read_fpId_pin_value(&spi->dev, "qcom,fpid-gpio") == 1 /*HIGH*/) {
		// full_fp_chip_name(SILEAD_FP_NAME);
	} else {
		DBG_MSG(MSG_ERR, "%s, not detect silead hw!\n", __func__);
		return -1;
	}

	/* Claim our 256 reserved device numbers.  Then register a class
	 * that will key udev/mdev to add/remove /dev nodes.  Last, register
	 * the driver which manages those device numbers.
	 */
	BUILD_BUG_ON(N_SPI_MINORS > 256);

	status = alloc_chrdev_region(&devno, 0,255, "sileadfp");
	if(status <0 ) {
		DBG_MSG(MSG_INFO, "alloc_chrdev_region error\n");
		return status;
	}

	spidev_major = MAJOR(devno);
	cdev_init(&spicdev, &spidev_fops);
	spicdev.owner = THIS_MODULE;
	status = cdev_add(&spicdev,MKDEV(spidev_major, 0),N_SPI_MINORS);
	if(status != 0) {
		DBG_MSG(MSG_INFO, "cdev_add error\n");
		return status;
	}

	spidev_class = class_create(THIS_MODULE, "spidev");
	if (IS_ERR(spidev_class)) {
		DBG_MSG(MSG_INFO, "class_create error\n");
		unregister_chrdev(spidev_major, silead_fp_driver.driver.name);
		status =  PTR_ERR(spidev_class);
		return status;
	}

	/* Allocate driver data */
	spidev = kzalloc(sizeof(*spidev), GFP_KERNEL);
	if (!spidev) {
		class_destroy(spidev_class);
		unregister_chrdev(spidev_major, silead_fp_driver.driver.name);
		return -ENOMEM;
	}

	/* Initialize the driver data */
	spidev->spi = spi;

	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *dev;

		spidev->devt = MKDEV(spidev_major, minor);
		dev = device_create(spidev_class, &spi->dev, spidev->devt,
		                    spidev, "silead_fp_dev");
		status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	} else {
		DBG_MSG(MSG_ERR, "no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
	}

	/*
		ret = silead_power_init(spidev);
		if(ret < 0)
		{
			printk("silead_power_init failed ret = %d\n", ret);
			ret = -ENODEV;
		}
		silead_power_ctl(spidev, 1);

		ret = silead_parse_dt(spidev);
		if(ret < 0) {
			printk("silead_parse_dt failed ret = %d\n", ret);
			ret = -ENODEV;
		}
	*/
	DBG_MSG(MSG_INFO, "S2\n");

	/* Init Poll Wait */
	init_waitqueue_head(&silead_poll_wq);

	mutex_init(&spidev->buf_lock);
	wake_lock_init(&spidev->wake_lock, WAKE_LOCK_SUSPEND, "fp_wake_lock");

	spidev->int_wq= create_singlethread_workqueue("int_silead_wq");
	INIT_WORK(&spidev->int_work, finger_interrupt_work);


	//silead_request_irq(spidev);

#ifdef POWER_NOTIFY
	spidev->notifier = silead_noti_block;
	fb_register_client(&spidev->notifier);
#endif

	//add by bacon for fp_wake_lock
	power_kobj = kobject_create_and_add("silead", NULL);
	if (!power_kobj) {
		class_destroy(spidev_class);
		unregister_chrdev(spidev_major, silead_fp_driver.driver.name);
		kfree(spidev);
		return -ENOMEM;
	}
	error = sysfs_create_group(power_kobj, &attr_group);
	if (error) {
		class_destroy(spidev_class);
		unregister_chrdev(spidev_major, silead_fp_driver.driver.name);
		kfree(spidev);
		return error;
	}
	//add by bacon for fp_wake_lock

	if (status == 0) {
		platform_set_drvdata(spi, spidev);
		fp_spidev = spidev;
	} else {
		class_destroy(spidev_class);
		unregister_chrdev(spidev_major, silead_fp_driver.driver.name);
		kfree(spidev);
		fp_spidev = NULL;
	}

	return status;
}

static int __init silead_fp_init(void)
{
	int status = 0;
	//dev_t devno;

	DBG_MSG(MSG_INFO, "S\n");

	status = platform_driver_register(&silead_fp_driver);
	if (status < 0) {
		printk("platform_driver_register error\n");
	}

	return status;
}

static void __exit silead_fp_exist(void)
{
	cdev_del(&spicdev);
	platform_driver_unregister(&silead_fp_driver);
	if(fp_spidev != NULL) {
		class_destroy(spidev_class);
		unregister_chrdev(spidev_major, silead_fp_driver.driver.name);
	}
}

module_init(silead_fp_init);
module_exit(silead_fp_exist);

MODULE_AUTHOR("EricLiu <ericclliu@fih-foxconn.com>");
MODULE_DESCRIPTION("driver to control Silead fingerprint module");
MODULE_VERSION(VERSION_LOG);
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spidev");
