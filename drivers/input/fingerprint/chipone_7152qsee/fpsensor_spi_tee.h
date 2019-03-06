#ifndef __FPSENSOR_SPI_TEE_H
#define __FPSENSOR_SPI_TEE_H

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/wait.h>

#define FPSENSOR_DEV_NAME           "chipone_fp"
#define FPSENSOR_CLASS_NAME         "chipone_fp"
#define FPSENSOR_DEV_MAJOR          255
#define N_SPI_MINORS                32    /* ... up to 256 */
#define FPSENSOR_NR_DEVS            1

#define ERR_LOG     (0)
#define INFO_LOG    (1)
#define DEBUG_LOG   (2)
#define fpsensor_debug(level, fmt, args...) do { \
		if (fpsensor_debug_level >= level) {\
			printk( "[chipone_fp] " fmt, ##args); \
		} \
	} while (0)
#define FUNC_ENTRY()  fpsensor_debug(DEBUG_LOG, "%s, %d, entry\n", __func__, __LINE__)
#define FUNC_EXIT()   fpsensor_debug(DEBUG_LOG, "%s, %d, exit\n", __func__, __LINE__)

/**********************IO Magic**********************/
#define FPSENSOR_IOC_MAGIC    0xf0    //CHIP

/* define commands */
#define FPSENSOR_IOC_INIT                       _IOWR(FPSENSOR_IOC_MAGIC,0,unsigned int)
#define FPSENSOR_IOC_EXIT                       _IOWR(FPSENSOR_IOC_MAGIC,1,unsigned int)
#define FPSENSOR_IOC_RESET                      _IOWR(FPSENSOR_IOC_MAGIC,2,unsigned int)
#define FPSENSOR_IOC_ENABLE_IRQ                 _IOWR(FPSENSOR_IOC_MAGIC,3,unsigned int)
#define FPSENSOR_IOC_DISABLE_IRQ                _IOWR(FPSENSOR_IOC_MAGIC,4,unsigned int)
#define FPSENSOR_IOC_GET_INT_VAL                _IOWR(FPSENSOR_IOC_MAGIC,5,unsigned int)
#define FPSENSOR_IOC_DISABLE_SPI_CLK            _IOWR(FPSENSOR_IOC_MAGIC,6,unsigned int)
#define FPSENSOR_IOC_ENABLE_SPI_CLK             _IOWR(FPSENSOR_IOC_MAGIC,7,unsigned int)
#define FPSENSOR_IOC_ENABLE_POWER               _IOWR(FPSENSOR_IOC_MAGIC,8,unsigned int)
#define FPSENSOR_IOC_DISABLE_POWER              _IOWR(FPSENSOR_IOC_MAGIC,9,unsigned int)
/* fp sensor has change to sleep mode while screen off */
#define FPSENSOR_IOC_ENTER_SLEEP_MODE           _IOWR(FPSENSOR_IOC_MAGIC,11,unsigned int)
#define FPSENSOR_IOC_REMOVE                     _IOWR(FPSENSOR_IOC_MAGIC,12,unsigned int)
#define FPSENSOR_IOC_CANCEL_WAIT                _IOWR(FPSENSOR_IOC_MAGIC,13,unsigned int)
#define FPSENSOR_IOC_SET_DEV_INFO               _IOWR(FPSENSOR_IOC_MAGIC,14,unsigned int)
#define FPSENSOR_IOC_FTM_SET_FINGER_STATE       _IOWR(FPSENSOR_IOC_MAGIC,15,unsigned int)
#define FPSENSOR_IOC_FTM_GET_FINGER_STATE       _IOWR(FPSENSOR_IOC_MAGIC,16,unsigned int)
#define FPSENSOR_IOC_RELEASE_VERSION            _IOWR(FPSENSOR_IOC_MAGIC,17, char *)

#define FPSENSOR_IOC_MAXNR    32  /* THIS MACRO IS NOT USED NOW... */
#define FPSENSOR_MAX_VER_BUF_LEN  64

typedef struct {
	dev_t devno;
	struct class *class;
	struct cdev cdev;
	struct platform_device *spi;

	unsigned int users;
	u8 device_available;    /* changed during fingerprint chip sleep and wakeup phase */
	// struct early_suspend early_suspend;
	u8 probe_finish;
	u8 irq_count;
	/* bit24-bit32 of signal count */
	/* bit16-bit23 of event type, 1: key down; 2: key up; 3: fp data ready; 4: home key */
	/* bit0-bit15 of event type, buffer status register */
	u32 event_type;
	u8 sig_count;
	u8 is_sleep_mode;
	volatile unsigned int RcvIRQ;
	//irq
	int irq;
	int irq_gpio;
	int reset_gpio;
	int power_gpio;
	struct wake_lock ttw_wl;
	//wait queue
	wait_queue_head_t wq_irq_return;
	int cancel;
} fpsensor_data_t;

static void fpsensor_dev_cleanup(fpsensor_data_t *fpsensor);
static void fpsensor_hw_reset(int delay);
static void setRcvIRQ(int val);

#endif    /* __FPSENSOR_SPI_TEE_H */
