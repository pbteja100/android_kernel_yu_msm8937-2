#ifndef __GF_SPI_H
#define __GF_SPI_H

#include <linux/types.h>
#include <linux/notifier.h>
/**********************************************************/
enum FP_MODE {
	GF_IMAGE_MODE = 0,
	GF_KEY_MODE,
	GF_SLEEP_MODE,
	GF_FF_MODE,
	GF_DEBUG_MODE = 0x56
};

struct gf_key {
	unsigned int key;
	int value;
};


struct gf_key_map {
	char *name;
	unsigned short val;
};

#define  GF_IOC_MAGIC         'g'
#define  GF_IOC_DISABLE_IRQ	_IO(GF_IOC_MAGIC, 0)
#define  GF_IOC_ENABLE_IRQ	_IO(GF_IOC_MAGIC, 1)
#define  GF_IOC_SETSPEED    _IOW(GF_IOC_MAGIC, 2, unsigned int)
#define  GF_IOC_RESET       _IO(GF_IOC_MAGIC, 3)
#define  GF_IOC_COOLBOOT    _IO(GF_IOC_MAGIC, 4)
#define  GF_IOC_SENDKEY    _IOW(GF_IOC_MAGIC, 5, struct gf_key)
#define  GF_IOC_CLK_READY  _IO(GF_IOC_MAGIC, 6)
#define  GF_IOC_CLK_UNREADY  _IO(GF_IOC_MAGIC, 7)
#define  GF_IOC_PM_FBCABCK  _IO(GF_IOC_MAGIC, 8)
#define  GF_IOC_POWER_ON   _IO(GF_IOC_MAGIC, 9)
#define  GF_IOC_POWER_OFF  _IO(GF_IOC_MAGIC, 10)
#define  GF_IOC_REQUEST_IRQ  _IO(GF_IOC_MAGIC, 11)
// release info.
#define GF_IOC_RELEASEVERSION 	_IOW(GF_IOC_MAGIC, 12, char*)

#define  GFX1XM_IOC_FTM	_IOW(GF_IOC_MAGIC, 101, int)
#define  GFX1XM_IOC_SET_MODE	_IOW(GF_IOC_MAGIC, 102, int)


#define  GF_IOC_MAXNR    14

//#define AP_CONTROL_CLK       1
/*#define  USE_PLATFORM_BUS     */
#define  USE_SPI_BUS	1
#define GF_FASYNC   1	/*If support fasync mechanism.*/
#define GF_NET_EVENT_IRQ 0
//#define GF_NETLINK_ENABLE
#define GF_NET_EVENT_FB_BLACK 1
#define GF_NET_EVENT_FB_UNBLACK 2


struct gf_dev {
	dev_t devt;
	struct list_head device_entry;
#if defined(USE_SPI_BUS)
	struct spi_device *spi;
#elif defined(USE_PLATFORM_BUS)
	struct platform_device *spi;
#endif
	struct clk *core_clk;
	struct clk *iface_clk;

	struct input_dev *input;
	/* buffer is NULL unless this device is open (users > 0) */
	unsigned users;
	signed irq_gpio;
	signed reset_gpio;
	signed pwr_gpio;
	int irq;
	int irq_enabled;
	int clk_enabled;
#ifdef GF_FASYNC
	struct fasync_struct *async;
#endif
	struct notifier_block notifier;
	char device_available;
	char fb_black;
	//TINNO BEGIN
	u8           isPowerOn;
	struct regulator *vdd;
	struct regulator *vio;
	//signed fpid_gpio;
	//TINNO END
};

int gf_parse_dts(struct gf_dev* gf_dev);
void gf_cleanup(struct gf_dev *gf_dev);

int gf_power_on(struct gf_dev *gf_dev);
int gf_power_off(struct gf_dev *gf_dev);

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms);
int gf_irq_num(struct gf_dev *gf_dev);
//<copy from 7701> add by yinglong.tang
extern int gf_power_ctl(struct gf_dev* gf_dev, bool on);
extern int gf_power_init(struct gf_dev* gf_dev);
extern int gf_power_deinit(struct gf_dev* gf_dev);
//<copy from 7701> add by yinglong.tang

void sendnlmsg(char *message);
int netlink_init(void);
void netlink_exit(void);
#endif /*__GF_SPI_H*/
