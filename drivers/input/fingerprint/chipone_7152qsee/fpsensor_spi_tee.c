#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <soc/qcom/scm.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <net/sock.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#else
#include <linux/notifier.h>
#endif

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#endif

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#include "fpsensor_spi_tee.h"

#include "../fp_drv/fp_drv.h" //add by wenguangyu

#define FPSENSOR_SPI_VERSION        "fpsensor_spi_tee_v0.20"

/* debug log setting */
static u8 fpsensor_debug_level = DEBUG_LOG;
/* global variables */
static fpsensor_data_t *g_fpsensor = NULL;

static struct fasync_struct *fasync_queue = NULL;
static uint32_t ftm_finger_status = 0; // 0x01 down 0x02 up

char fpsensor_ver_buf[FPSENSOR_MAX_VER_BUF_LEN] = {0x00};

static int request_irq_ret = 0;
/* -------------------------------------------------------------------- */
/* fingerprint chip hardware configuration                              */
/* -------------------------------------------------------------------- */
static void fpsensor_gpio_free(fpsensor_data_t *fpsensor)
{
	if (request_irq_ret != 0)
		return;

	if (fpsensor->irq_gpio != 0 ) {
		gpio_free(fpsensor->irq_gpio);
		fpsensor->irq_gpio = 0;
	}
	if (fpsensor->reset_gpio != 0) {
		gpio_free(fpsensor->reset_gpio);
		fpsensor->reset_gpio = 0;
	}
	if (fpsensor->power_gpio != 0) {
		gpio_free(fpsensor->power_gpio);
		fpsensor->power_gpio = 0;
	}
}

static void fpsensor_irq_gpio_cfg(fpsensor_data_t *fpsensor)
{
	int error = 0;

	error = gpio_direction_input(fpsensor->irq_gpio);
	if (error) {
		fpsensor_debug(ERR_LOG, "setup fpsensor irq gpio for input failed!error[%d]\n", error);
		return ;
	}

	fpsensor->irq = gpio_to_irq(fpsensor->irq_gpio);
	fpsensor_debug(DEBUG_LOG, "fpsensor irq number[%d]\n", fpsensor->irq);
	if (fpsensor->irq <= 0) {
		fpsensor_debug(ERR_LOG, "fpsensor irq gpio to irq failed!\n");
		return ;
	}

	return;
}

/*
static int fpsensor_request_named_gpio(fpsensor_data_t *fpsensor_dev, const char *label, int *gpio)
{
    struct device *dev = &fpsensor_dev->spi->dev;
    struct device_node *np = dev->of_node;
    int ret = of_get_named_gpio(np, label, 0);

    if (ret < 0)
    {
        fpsensor_debug(ERR_LOG, "failed to get '%s'\n", label);
        return ret;
    }
    *gpio = ret;
    ret = devm_gpio_request(dev, *gpio, label);
    if (ret)
    {
        fpsensor_debug(ERR_LOG, "failed to request gpio %d\n", *gpio);
        return ret;
    }

    fpsensor_debug(ERR_LOG, "%s %d\n", label, *gpio);
    return ret;
}
*/

static int fpsensor_parse_dt(fpsensor_data_t *pdata)
{
	int ret = -1;
	struct device *dev = &pdata->spi->dev;
	struct device_node *np = dev->of_node;

	/* +++reset, irq gpio info+++ */
	pdata->reset_gpio = of_get_named_gpio(np, "qcom,reset-gpio", 0);
	if (pdata->reset_gpio < 0) {
		printk("chipone:reset_gpio  get err!\n");
		return pdata->reset_gpio;
	}

	pdata->irq_gpio = of_get_named_gpio(np, "qcom,irq-gpio", 0);
	if (pdata->irq_gpio < 0) {
		printk("chipone:irq_gpio  get err!\n");
		return pdata->irq_gpio;
	}

	printk("chipone:rst-gpio = %d, irq_pin = %d\n", pdata->reset_gpio, pdata->irq_gpio);


	ret = gpio_request(pdata->irq_gpio, "chipone-irq-gpio");
	if(ret < 0) {
		printk("chipone:irq gpio_request failed ret = %d", ret);
		return ret;
	} else {
		printk("chipone:irq gpio_request ok ret = %d", ret);
	}

	ret = gpio_request(pdata->reset_gpio, "chipone-rst-gpio");
	if(ret < 0) {
		printk("chipone:reset_gpio gpio_request failed ret = %d", ret);
		return ret;
	} else {
		printk("chipone:reset_gpio gpio_request Ok = %d", ret);
	}

	printk("chipone:fpsensor_parse_dt Ok!\n");
	return 0;
}

static int fpsensor_get_gpio_dts_info(fpsensor_data_t *fpsensor)
{
	int ret = 0;

	FUNC_ENTRY();
	/*
	    ret = fpsensor_request_named_gpio(fpsensor, "qcom,irq-gpio", &fpsensor->irq_gpio);
	    if (ret)
	    {
	        fpsensor_debug(ERR_LOG, "Failed to request irq GPIO. ret = %d\n", ret);
	        return -1;
	    }

	    // get irq resourece
	    ret = fpsensor_request_named_gpio(fpsensor, "qcom,reset-gpio", &fpsensor->reset_gpio);
	    if (ret)
	    {
	        fpsensor_debug(ERR_LOG, "Failed to request reset GPIO. ret = %d\n", ret);
	        return -1;
	    }

	    // get power resourece
	    ret = fpsensor_request_named_gpio(fpsensor, "fp-gpio-power", &fpsensor->power_gpio);
	    if (ret)
	    {
	        fpsensor_debug(ERR_LOG, "Failed to request power GPIO. ret = %d\n", ret);
	        return -1;
	    }
	    // set power direction output
	    gpio_direction_output(fpsensor->power_gpio, 1);
	    gpio_set_value(fpsensor->power_gpio, 1);
	*/
	// set reset direction output

	request_irq_ret = fpsensor_parse_dt(fpsensor);

	gpio_direction_output(fpsensor->reset_gpio, 1);
	fpsensor_hw_reset(1250);

	return ret;
}

/* delay us after reset */
static void fpsensor_hw_reset(int delay)
{
	FUNC_ENTRY();
	gpio_set_value(g_fpsensor->reset_gpio, 1);

	udelay(100);
	gpio_set_value(g_fpsensor->reset_gpio, 0);

	udelay(1000);
	gpio_set_value(g_fpsensor->reset_gpio, 1);

	if (delay) {
		/* delay is configurable */
		udelay(delay);
	}
	FUNC_EXIT();
	return;
}

static void fpsensor_spi_clk_enable(u8 bonoff)
{
	return ;
}

static void fpsensor_hw_power_enable(u8 onoff)
{
	return ;
}

static void fpsensor_enable_irq(fpsensor_data_t *fpsensor_dev)
{
	FUNC_ENTRY();
	setRcvIRQ(0);
	/* Request that the interrupt should be wakeable */
	if (fpsensor_dev->irq_count == 0) {
		enable_irq(fpsensor_dev->irq);
		fpsensor_dev->irq_count = 1;
	}
	FUNC_EXIT();
	return;
}

static void fpsensor_disable_irq(fpsensor_data_t *fpsensor_dev)
{
	FUNC_ENTRY();

	if (0 == fpsensor_dev->device_available) {
		fpsensor_debug(ERR_LOG, "%s, devices not available\n", __func__);
	} else {
		if (0 == fpsensor_dev->irq_count) {
			fpsensor_debug(ERR_LOG, "%s, irq already disabled\n", __func__);
		} else {
			if (fpsensor_dev->irq)
				disable_irq_nosync(fpsensor_dev->irq);
			fpsensor_dev->irq_count = 0;
			fpsensor_debug(DEBUG_LOG, "%s disable interrupt!\n", __func__);
		}
	}
	setRcvIRQ(0);
	FUNC_EXIT();
	return;
}

/* -------------------------------------------------------------------- */
/* file operation function                                              */
/* -------------------------------------------------------------------- */

static irqreturn_t fpsensor_irq(int irq, void *handle)
{
	fpsensor_data_t *fpsensor_dev = (fpsensor_data_t *)handle;

	/* Make sure 'wakeup_enabled' is updated before using it
	** since this is interrupt context (other thread...) */
	smp_rmb();
	wake_lock_timeout(&fpsensor_dev->ttw_wl, msecs_to_jiffies(1000));
	setRcvIRQ(1);
	wake_up_interruptible(&fpsensor_dev->wq_irq_return);
	fpsensor_dev->sig_count++;

	return IRQ_HANDLED;
}

static void setRcvIRQ(int val)
{
	fpsensor_data_t *fpsensor_dev = g_fpsensor;
	// fpsensor_debug(INFO_LOG, "[rickon]: %s befor val :  %d ; set val : %d   \n", __func__, fpsensor_dev-> RcvIRQ, val);
	fpsensor_dev->RcvIRQ = val;
}
#define FPSENSOR_DEV_INFO "chipone_fp"
extern int full_fp_chip_name(const char *name);
extern int full_fp_chip_info(const char *ver_info);

static long fpsensor_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	fpsensor_data_t *fpsensor_dev = NULL;
	int retval = 0;
	unsigned int val = 0;
	int irqf;

	//FUNC_ENTRY();
	fpsensor_debug(INFO_LOG, "[rickon]: fpsensor ioctl cmd : 0x%x \n", cmd );
	fpsensor_dev = (fpsensor_data_t *)filp->private_data;
	//clear cancel flag
	fpsensor_dev->cancel = 0 ;
	switch (cmd) {
	case FPSENSOR_IOC_INIT:
		fpsensor_debug(INFO_LOG, "%s: fpsensor init started======\n", __func__);
		retval = fpsensor_get_gpio_dts_info(fpsensor_dev);
		if (retval) {
			fpsensor_debug(INFO_LOG, "%s: fpsensor_ioctl break\n", __func__);
			break;
		}
		fpsensor_irq_gpio_cfg(fpsensor_dev);
		//regist irq
		irqf = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
		retval = devm_request_threaded_irq(&g_fpsensor->spi->dev, g_fpsensor->irq, fpsensor_irq,
		                                   NULL, irqf, dev_name(&g_fpsensor->spi->dev), g_fpsensor);
		if (retval == 0) {
			fpsensor_debug(INFO_LOG, " irq thread reqquest success!\n");
		} else {
			fpsensor_debug(INFO_LOG, " irq thread request failed , retval =%d \n", retval);
		}
		enable_irq_wake(g_fpsensor->irq);
		fpsensor_dev->device_available = 1;
		fpsensor_dev->irq_count = 0;
		// sunbo: add to avoid "unbalanced enable for IRQ 419" warning - begin
		fpsensor_dev->irq_count = 1;
		fpsensor_disable_irq(fpsensor_dev);
		// sunbo: add to avoid "unbalanced enable for IRQ 419" warning - end
		fpsensor_dev->sig_count = 0;
		fpsensor_debug(INFO_LOG, "%s: fpsensor init finished======\n", __func__);
		break;

	case FPSENSOR_IOC_EXIT:
		fpsensor_disable_irq(fpsensor_dev);
		if (fpsensor_dev->irq) {
			free_irq(fpsensor_dev->irq, fpsensor_dev);
			fpsensor_dev->irq_count = 0;
		}
		fpsensor_dev->device_available = 0;
		fpsensor_gpio_free(fpsensor_dev);
		fpsensor_debug(INFO_LOG, "%s: chipone exit finished======\n", __func__);
		break;

	case FPSENSOR_IOC_RESET:
		fpsensor_debug(INFO_LOG, "%s: chipone chip reset command\n", __func__);
		fpsensor_hw_reset(1250);
		break;

	case FPSENSOR_IOC_ENABLE_IRQ:
		fpsensor_debug(INFO_LOG, "%s: chipone chip ENable IRQ command\n", __func__);
		fpsensor_enable_irq(fpsensor_dev);
		break;

	case FPSENSOR_IOC_DISABLE_IRQ:
		fpsensor_debug(INFO_LOG, "%s: chip disable IRQ command\n", __func__);
		fpsensor_disable_irq(fpsensor_dev);
		break;
	case FPSENSOR_IOC_GET_INT_VAL:
		val = gpio_get_value(fpsensor_dev->irq_gpio);
		if (copy_to_user((void __user *)arg, (void *)&val, sizeof(unsigned int))) {
			fpsensor_debug(ERR_LOG, "Failed to copy data to user\n");
			retval = -EFAULT;
			break;
		}
		retval = 0;
		break;
	case FPSENSOR_IOC_ENABLE_SPI_CLK:
		fpsensor_debug(INFO_LOG, "%s: ENABLE_SPI_CLK ======\n", __func__);
		fpsensor_spi_clk_enable(1);
		break;
	case FPSENSOR_IOC_DISABLE_SPI_CLK:
		fpsensor_debug(INFO_LOG, "%s: DISABLE_SPI_CLK ======\n", __func__);
		fpsensor_spi_clk_enable(0);
		break;
	case FPSENSOR_IOC_ENABLE_POWER:
		fpsensor_debug(INFO_LOG, "%s: FPSENSOR_IOC_ENABLE_POWER ======\n", __func__);
		fpsensor_hw_power_enable(1);
		break;
	case FPSENSOR_IOC_DISABLE_POWER:
		fpsensor_debug(INFO_LOG, "%s: FPSENSOR_IOC_DISABLE_POWER ======\n", __func__);
		fpsensor_hw_power_enable(0);
		break;
	case FPSENSOR_IOC_ENTER_SLEEP_MODE:
		fpsensor_dev->is_sleep_mode = 1;
		break;
	case FPSENSOR_IOC_REMOVE:
		fpsensor_disable_irq(fpsensor_dev);
		if (fpsensor_dev->irq) {
			free_irq(fpsensor_dev->irq, fpsensor_dev);
			fpsensor_dev->irq_count = 0;
		}
		fpsensor_dev->device_available = 0;
		fpsensor_gpio_free(fpsensor_dev);
		fpsensor_dev_cleanup(fpsensor_dev);
		fpsensor_debug(INFO_LOG, "%s remove finished\n", __func__);
		break;

	case FPSENSOR_IOC_CANCEL_WAIT:
		fpsensor_debug(INFO_LOG, "%s: FPSENSOR CANCEL WAIT\n", __func__);
		wake_up_interruptible(&fpsensor_dev->wq_irq_return);
		fpsensor_dev->cancel = 1;
		break;
	case FPSENSOR_IOC_SET_DEV_INFO:
		fpsensor_debug(INFO_LOG, "%s: FPSENSOR SET DEVICE INFO\n", __func__);
		full_fp_chip_name(FPSENSOR_DEV_INFO);
		break;
	case FPSENSOR_IOC_FTM_SET_FINGER_STATE:
		ftm_finger_status = 0;
		if (copy_from_user(&ftm_finger_status, (u32 __user*)arg, sizeof(u32))) {
			fpsensor_debug(ERR_LOG, "Failed to copy data from user\n");
			retval = -EFAULT;
			break;
		}
		// guomingyi add ..
		FP_EVT_REPORT(ftm_finger_status == 1 ? 1 : 2);

		printk(KERN_INFO "%s: FPSENSOR_IOC_FTM_SET_FINGER_STATE: %d\n", __func__, ftm_finger_status);
		if(fasync_queue) {
			kill_fasync(&fasync_queue, SIGIO, POLL_IN);
			printk("%s: kill_fasync to user(ftm_finger_status) !\n", __func__);
		}
		break;
	case FPSENSOR_IOC_FTM_GET_FINGER_STATE:
		if (copy_to_user((u32 __user*)arg, &ftm_finger_status, sizeof(u32))) {
			fpsensor_debug(ERR_LOG, "Failed to copy data to user\n");
			retval = -EFAULT;
			break;
		}
		printk("[guomingyi] %s:%d __put_user(ftm_finger_status):%d\n", __func__,__LINE__, ftm_finger_status);
		break;
	case FPSENSOR_IOC_RELEASE_VERSION:
		if (copy_from_user(fpsensor_ver_buf, (char __user*)arg, FPSENSOR_MAX_VER_BUF_LEN)) {
			fpsensor_debug(ERR_LOG, "fpsensor Failed to copy version from user\n");
			retval = -EFAULT;
			break;
		}
		printk(KERN_INFO "%s: chipone release version info: %s\n", __func__, fpsensor_ver_buf);
		full_fp_chip_info(fpsensor_ver_buf);
		break;
	default:
		fpsensor_debug(ERR_LOG, "fpsensor doesn't support this command(%d)\n", cmd);
		break;
	}

	//FUNC_EXIT();
	return retval;
}

static long fpsensor_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return fpsensor_ioctl(filp, cmd, (unsigned long)(arg));
}

static unsigned int fpsensor_poll(struct file *filp, struct poll_table_struct *wait)
{
	unsigned int ret = 0;

	ret |= POLLIN;
	poll_wait(filp, &g_fpsensor->wq_irq_return, wait);
	if (g_fpsensor->cancel == 1 ) {
		fpsensor_debug(ERR_LOG, " cancle\n");
		ret =  POLLERR;
		g_fpsensor->cancel = 0;
		return ret;
	}
	if ( g_fpsensor->RcvIRQ) {
		fpsensor_debug(ERR_LOG, " get irq\n");
		ret |= POLLRDNORM;
		//guomingyi add .
		FP_EVT_REPORT(HW_EVENT_WAKEUP);
	} else {
		ret = 0;
	}
	return ret;
}

/* -------------------------------------------------------------------- */
/* device function                                                      */
/* -------------------------------------------------------------------- */
static int fpsensor_open(struct inode *inode, struct file *filp)
{
	fpsensor_data_t *fpsensor_dev;

	FUNC_ENTRY();
	fpsensor_dev = container_of(inode->i_cdev, fpsensor_data_t, cdev);
	fpsensor_dev->users++;
	fpsensor_dev->device_available = 1;
	filp->private_data = fpsensor_dev;
	FUNC_EXIT();
	return 0;
}

static int fpsensor_release(struct inode *inode, struct file *filp)
{
	fpsensor_data_t *fpsensor_dev;
	int    status = 0;

	FUNC_ENTRY();
	fpsensor_dev = filp->private_data;
	filp->private_data = NULL;

	/*last close??*/
	fpsensor_dev->users--;
	if (fpsensor_dev->users <= 0) {
		fpsensor_debug(INFO_LOG, "%s, disble_irq. irq = %d\n", __func__, fpsensor_dev->irq);
		fpsensor_disable_irq(fpsensor_dev);
	}
	fpsensor_dev->device_available = 0;
	FUNC_EXIT();
	return status;
}

static ssize_t fpsensor_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	fpsensor_debug(ERR_LOG, "Not support read opertion in TEE version\n");
	return -EFAULT;
}

static ssize_t fpsensor_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	fpsensor_debug(ERR_LOG, "Not support write opertion in TEE version\n");
	return -EFAULT;
}

static int fpsensor_fasync(int fd, struct file * filp, int on)
{
	printk("%s enter \n",__func__);
	return fasync_helper(fd, filp, on, &fasync_queue);
}

static const struct file_operations fpsensor_fops = {
	.owner          = THIS_MODULE,
	.write          = fpsensor_write,
	.read           = fpsensor_read,
	.unlocked_ioctl = fpsensor_ioctl,
	.compat_ioctl   = fpsensor_compat_ioctl,
	.open           = fpsensor_open,
	.release        = fpsensor_release,
	.poll           = fpsensor_poll,
	.fasync         = fpsensor_fasync,

};

// create and register a char device for fpsensor
static int fpsensor_dev_setup(fpsensor_data_t *fpsensor)
{
	int ret = 0;
	dev_t dev_no = 0;
	struct device *dev = NULL;
	int fpsensor_dev_major = FPSENSOR_DEV_MAJOR;
	int fpsensor_dev_minor = 0;

	FUNC_ENTRY();

	if (fpsensor_dev_major) {
		dev_no = MKDEV(fpsensor_dev_major, fpsensor_dev_minor);
		ret = register_chrdev_region(dev_no, FPSENSOR_NR_DEVS, FPSENSOR_DEV_NAME);
	} else {
		ret = alloc_chrdev_region(&dev_no, fpsensor_dev_minor, FPSENSOR_NR_DEVS, FPSENSOR_DEV_NAME);
		fpsensor_dev_major = MAJOR(dev_no);
		fpsensor_dev_minor = MINOR(dev_no);
		fpsensor_debug(INFO_LOG,"fpsensor device major is %d, minor is %d\n",
		               fpsensor_dev_major, fpsensor_dev_minor);
	}

	if (ret < 0) {
		fpsensor_debug(ERR_LOG,"can not get device major number %d\n", fpsensor_dev_major);
		goto out;
	}

	cdev_init(&fpsensor->cdev, &fpsensor_fops);
	fpsensor->cdev.owner = THIS_MODULE;
	fpsensor->cdev.ops   = &fpsensor_fops;
	fpsensor->devno      = dev_no;
	ret = cdev_add(&fpsensor->cdev, dev_no, FPSENSOR_NR_DEVS);
	if (ret) {
		fpsensor_debug(ERR_LOG,"add char dev for fpsensor failed\n");
		goto release_region;
	}

	fpsensor->class = class_create(THIS_MODULE, FPSENSOR_CLASS_NAME);
	if (IS_ERR(fpsensor->class)) {
		fpsensor_debug(ERR_LOG,"create fpsensor class failed\n");
		ret = PTR_ERR(fpsensor->class);
		goto release_cdev;
	}

	dev = device_create(fpsensor->class, &fpsensor->spi->dev, dev_no, fpsensor, FPSENSOR_DEV_NAME);
	if (IS_ERR(dev)) {
		fpsensor_debug(ERR_LOG,"create device for fpsensor failed\n");
		ret = PTR_ERR(dev);
		goto release_class;
	}
	FUNC_EXIT();
	return ret;

release_class:
	class_destroy(fpsensor->class);
	fpsensor->class = NULL;
release_cdev:
	cdev_del(&fpsensor->cdev);
release_region:
	unregister_chrdev_region(dev_no, FPSENSOR_NR_DEVS);
out:
	FUNC_EXIT();
	return ret;
}

// release and cleanup fpsensor char device
static void fpsensor_dev_cleanup(fpsensor_data_t *fpsensor)
{
	FUNC_ENTRY();

	cdev_del(&fpsensor->cdev);
	unregister_chrdev_region(fpsensor->devno, FPSENSOR_NR_DEVS);
	device_destroy(fpsensor->class, fpsensor->devno);
	class_destroy(fpsensor->class);

	FUNC_EXIT();
}

static int fpsensor_probe(struct platform_device *pdev)
{
	int status = 0;
	fpsensor_data_t *fpsensor_dev = NULL;

	// FUNC_ENTRY();

	fpsensor_debug(ERR_LOG, "new entry, %s", __func__);


	if (read_fpId_pin_value(&pdev->dev, "qcom,fpid-gpio") == __LOW) {
		fpsensor_debug(ERR_LOG, "%s, not chipone,return.\n", __func__);
		return -1;
	}

	/* Allocate driver data */
	fpsensor_dev = kzalloc(sizeof(*fpsensor_dev), GFP_KERNEL);
	if (!fpsensor_dev) {
		status = -ENOMEM;
		fpsensor_debug(ERR_LOG, "%s, Failed to alloc memory for fpsensor device.\n", __func__);
		goto out;
	}

	/* Initialize the driver data */
	g_fpsensor = fpsensor_dev;
	fpsensor_dev->spi               = pdev ;
	fpsensor_dev->device_available  = 0;
	fpsensor_dev->probe_finish      = 0;
	fpsensor_dev->users             = 0;
	fpsensor_dev->irq               = 0;
	fpsensor_dev->power_gpio        = 0;
	fpsensor_dev->reset_gpio        = 0;
	fpsensor_dev->irq_gpio          = 0;
	fpsensor_dev->irq_count         = 0;
	fpsensor_dev->sig_count         = 0;

	//fpsensor_parse_dt(fpsensor_dev);

	/* setup a char device for fpsensor */
	status = fpsensor_dev_setup(fpsensor_dev);
	if (status) {
		fpsensor_debug(ERR_LOG, "fpsensor setup char device failed, %d", status);
		goto release_drv_data;
	}
	init_waitqueue_head(&fpsensor_dev->wq_irq_return);
	wake_lock_init(&g_fpsensor->ttw_wl, WAKE_LOCK_SUSPEND, "fpsensor_ttw_wl");
	fpsensor_dev->probe_finish = 1;
	fpsensor_dev->is_sleep_mode = 0;
	fpsensor_dev->device_available = 1;

	// fpsensor_spi_clk_enable(1);
	fpsensor_debug(INFO_LOG, "%s probe finished, normal driver version: %s\n", __func__,
	               FPSENSOR_SPI_VERSION);
	goto out;

release_drv_data:
	kfree(fpsensor_dev);
	fpsensor_dev = NULL;
out:
	FUNC_EXIT();
	return status;
}

static int fpsensor_remove(struct platform_device *pdev)
{
	fpsensor_data_t *fpsensor_dev = g_fpsensor;

	FUNC_ENTRY();
	fpsensor_spi_clk_enable(0);
	fpsensor_disable_irq(fpsensor_dev);
	if (fpsensor_dev->irq)
		free_irq(fpsensor_dev->irq, fpsensor_dev);

	fpsensor_gpio_free(fpsensor_dev);
	fpsensor_dev_cleanup(fpsensor_dev);
	wake_lock_destroy(&fpsensor_dev->ttw_wl);
	kfree(fpsensor_dev);
	g_fpsensor = NULL;

	FUNC_EXIT();
	return 0;
}

static int fpsensor_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int fpsensor_resume( struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id fpsensor_of_match[] = {
	{ .compatible = "qcom,fingerprint-chipone" },
	{}
};
MODULE_DEVICE_TABLE(of, fpsensor_of_match);
#endif


static struct platform_driver fpsensor_driver = {
	.driver = {
		.name = FPSENSOR_DEV_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(fpsensor_of_match),
#endif
	},
	.probe = fpsensor_probe,
	.remove = fpsensor_remove,
	.suspend = fpsensor_suspend,
	.resume = fpsensor_resume,
};

static int __init fpsensor_init(void)
{
	int status;

	fpsensor_debug(ERR_LOG, "---fpsensor_init---\n");
	status = platform_driver_register(&fpsensor_driver);
	if (status < 0) {
		fpsensor_debug(ERR_LOG, "%s, Failed to register SPI driver.\n", __func__);
	}

	return status;
}
module_init(fpsensor_init);

static void __exit fpsensor_exit(void)
{
	platform_driver_unregister(&fpsensor_driver);
}
module_exit(fpsensor_exit);

MODULE_AUTHOR("xhli");
MODULE_DESCRIPTION(" Fingerprint chip TEE driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:fpsensor-drivers");
