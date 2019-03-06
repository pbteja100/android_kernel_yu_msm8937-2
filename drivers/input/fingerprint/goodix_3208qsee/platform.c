/*
 * platform indepent driver interface
 *
 * Coypritht (c) 2017 Goodix
 */
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>

#include "gf_spi.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif

//<copy from 7701> add by yinglong.tang
#define GF_VDD_MIN_UV      2800000
#define GF_VDD_MAX_UV	        2800000
#define GF_VIO_MIN_UV      1800000
#define GF_VIO_MAX_UV      1800000
//<copy from 7701> add by yinglong.tang

int gf_parse_dts(struct gf_dev* gf_dev)
{
	int rc = 0;
	pr_info("gf: gf_parse_dts enter\n");
	/*get reset resource*/
	gf_dev->reset_gpio = of_get_named_gpio(gf_dev->spi->dev.of_node,"qcom,reset-gpio",0);
	if(!gpio_is_valid(gf_dev->reset_gpio)) {
		pr_info("gf: RESET GPIO is invalid.\n");
		return -1;
	}

	rc = gpio_request(gf_dev->reset_gpio, "goodix_reset");
	if(rc) {
		dev_err(&gf_dev->spi->dev, "gf: Failed to request RESET GPIO. rc = %d\n", rc);
		return -1;
	}

	gpio_direction_output(gf_dev->reset_gpio, 1);

	/*get irq resourece*/
	gf_dev->irq_gpio = of_get_named_gpio(gf_dev->spi->dev.of_node,"qcom,irq-gpio",0);
	pr_info("gf::irq_gpio:%d\n", gf_dev->irq_gpio);
	if(!gpio_is_valid(gf_dev->irq_gpio)) {
		pr_info("IRQ GPIO is invalid.\n");
		return -1;
	}

	rc = gpio_request(gf_dev->irq_gpio, "goodix_irq");
	if(rc) {
		dev_err(&gf_dev->spi->dev, "gf: Failed to request IRQ GPIO. rc = %d\n", rc);
		return -1;
	}
	gpio_direction_input(gf_dev->irq_gpio);
	pr_info("gf: gf_parse_dts exit\n");

	return 0;
}

void gf_cleanup(struct gf_dev	* gf_dev)
{
	pr_info("gf:[info] %s\n",__func__);
	if (gpio_is_valid(gf_dev->irq_gpio)) {
		gpio_free(gf_dev->irq_gpio);
		pr_info("gf:remove irq_gpio success\n");
	}
	if (gpio_is_valid(gf_dev->reset_gpio)) {
		gpio_free(gf_dev->reset_gpio);
		pr_info("gf:remove reset_gpio success\n");
	}
}
//<copy from 7701> add by yinglong.tang
int gf_power_ctl(struct gf_dev* gf_dev, bool on)
{
	int rc = 0;

	if (on && (!gf_dev->isPowerOn)) {
		rc = regulator_enable(gf_dev->vdd);
		if (rc) {
			dev_err(&gf_dev->spi->dev,
			        "gf:Regulator vdd enable failed rc=%d\n", rc);
			return rc;
		}

#if 0
		rc = regulator_enable(gf_dev->vio);
		if (rc) {
			dev_err(&gf_dev->spi->dev,
			        "Regulator vio enable failed rc=%d\n", rc);
			regulator_disable(gf_dev->vdd);
			return rc;
		}
#endif
		msleep(10);

		gf_dev->isPowerOn = 1;
	} else if (!on && (gf_dev->isPowerOn)) {

		rc = regulator_disable(gf_dev->vdd);
		if (rc) {
			dev_err(&gf_dev->spi->dev,
			        "gf:Regulator vdd disable failed rc=%d\n", rc);
			return rc;
		}

#if 0
		rc = regulator_disable(gf_dev->vio);
		if (rc) {
			dev_err(&gf_dev->spi->dev,
			        "Regulator vio disable failed rc=%d\n", rc);
		}
#endif

		gf_dev->isPowerOn = 0;
	} else {
		dev_warn(&gf_dev->spi->dev,
		         "gf:Ignore power status change from %d to %d\n",
		         on, gf_dev->isPowerOn);
	}
	return rc;
}
int gf_power_on(struct gf_dev* gf_dev)
{
	int rc = 0;

	msleep(10);
	rc = gf_power_ctl(gf_dev, true);
	pr_info("gf:---- power on ok ----rc=%d\n",rc);

	return rc;
}

int gf_power_off(struct gf_dev* gf_dev)
{
	int rc = 0;
	rc = gf_power_ctl(gf_dev, false);
	pr_info("gf:---- power off ----rc=%d\n",rc);
	return rc;
}

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms)
{
	if(gf_dev == NULL) {
		pr_info("gf:gf_hw_reset Input buff is NULL.\n");
		return -1;
	}
	gpio_direction_output(gf_dev->reset_gpio, 1);
	gpio_set_value(gf_dev->reset_gpio, 0);
	mdelay(3);
	gpio_set_value(gf_dev->reset_gpio, 1);
	mdelay(delay_ms);
	return 0;
}

int gf_irq_num(struct gf_dev *gf_dev)
{
	if(gf_dev == NULL) {
		pr_info("gf: gf_irq_num Input buff is NULL.\n");
		return -1;
	} else {
		pr_info("gf: gf_irq_num\n");
		return gpio_to_irq(gf_dev->irq_gpio);
	}
}

int gf_power_init(struct gf_dev* gf_dev)
{
	int ret = 0;

	gf_dev->vdd = regulator_get(&gf_dev->spi->dev, "vdd");
	if (IS_ERR(gf_dev->vdd)) {
		ret = PTR_ERR(gf_dev->vdd);
		dev_err(&gf_dev->spi->dev,
		        "gf:Regulator get failed vdd ret=%d\n", ret);
		return ret;
	}

	if (regulator_count_voltages(gf_dev->vdd) > 0) {
		ret = regulator_set_voltage(gf_dev->vdd, GF_VDD_MIN_UV,
		                            GF_VDD_MAX_UV);
		if (ret) {
			dev_err(&gf_dev->spi->dev,
			        "gf:Regulator set_vtg failed vdd ret=%d\n", ret);
			goto reg_vdd_put;
		}
	}

#if 0
	gf_dev->vio = regulator_get(&gf_dev->spi->dev, "vio");
	if (IS_ERR(gf_dev->vio)) {
		ret = PTR_ERR(gf_dev->vio);
		dev_err(&gf_dev->spi->dev,
		        "Regulator get failed vio ret=%d\n", ret);
		goto reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(gf_dev->vio) > 0) {
		ret = regulator_set_voltage(gf_dev->vio,
		                            GF_VIO_MIN_UV,
		                            GF_VIO_MAX_UV);
		if (ret) {
			dev_err(&gf_dev->spi->dev,
			        "Regulator set_vtg failed vio ret=%d\n", ret);
			goto reg_vio_put;
		}
	}
#endif

	return 0;

#if 0
reg_vio_put:
	regulator_put(gf_dev->vio);
reg_vdd_set_vtg:
	if (regulator_count_voltages(gf_dev->vdd) > 0)
		regulator_set_voltage(gf_dev->vdd, 0, GF_VDD_MAX_UV);
#endif
reg_vdd_put:
	regulator_put(gf_dev->vdd);
	return ret;
}

int gf_power_deinit(struct gf_dev* gf_dev)
{
	int ret = 0;

	if (gf_dev->vdd) {
		if (regulator_count_voltages(gf_dev->vdd) > 0)
			regulator_set_voltage(gf_dev->vdd, 0, GF_VDD_MAX_UV);

		regulator_disable(gf_dev->vdd);
		regulator_put(gf_dev->vdd);
	}

#if 0
	if (gf_dev->vio) {
		if (regulator_count_voltages(gf_dev->vio) > 0)
			regulator_set_voltage(gf_dev->vio, 0, GF_VIO_MAX_UV);

		regulator_disable(gf_dev->vio);
		regulator_put(gf_dev->vio);
	}
#endif

	return ret;
}