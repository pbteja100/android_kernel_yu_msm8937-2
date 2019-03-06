/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2010-2017, FocalTech Systems, Ltd., all rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*****************************************************************************
*
* File Name: focaltech_core.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-08
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include "../high_sensitive.h"

#include "focaltech_core.h"


#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#define FTS_SUSPEND_LEVEL 1     /* Early-suspend level */
#endif

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define FTS_DRIVER_NAME                     "fts_ts"
#define INTERVAL_READ_REG                   20  //interval time per read reg unit:ms
#define TIMEOUT_READ_REG                    300 //timeout of read reg unit:ms
#if FTS_POWER_SOURCE_CUST_EN
#define FTS_VTG_MIN_UV                      2600000
#define FTS_VTG_MAX_UV                      3300000
#define FTS_I2C_VTG_MIN_UV                  1800000
#define FTS_I2C_VTG_MAX_UV                  1800000
#endif
#define FTS_READ_TOUCH_BUFFER_DIVIDED       0
/*****************************************************************************
* Global variable or extern global variabls/functions
******************************************************************************/
struct i2c_client *fts_i2c_client;
struct fts_ts_data *fts_wq_data;
struct input_dev *fts_input_dev;

#ifdef CONFIG_FT5XX_TGESTURE_FUNCTION
#if FTS_GESTURE_EN
struct input_dev *ft5xx_key_dev;
extern int bEnTGesture;
#endif
#endif

#if FTS_DEBUG_EN
int g_show_log = 1;
#else
//int g_show_log = 0;
int g_show_log = 1;
#endif

#if (FTS_DEBUG_EN && (FTS_DEBUG_LEVEL == 2))
char g_sz_debug[1024] = {0};
#endif

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
static void fts_release_all_finger(void);
static int fts_ts_suspend(struct device *dev);
static int fts_ts_resume(struct device *dev);

#define DEBUG_LOG_MSG_TO_FILE 0
//#ifdef DEBUG_LOG_MSG_TO_FILE
#define FILE_TP_LOG "/sdcard/tp_debug.log"
static struct file *filep = (struct file*)-1;

//can be exported for other tp driver
bool tpdebug_log_on(void)
{
#ifdef CONFIG_TINNO_KE_LOG_CTRL
	extern char * module_parser_mask(char *module);
	char *mask = module_parser_mask("tpdebug");
	if (mask) {
		if (!strncmp(mask, "1", 1))
			return true;
		else
			return false;
	}
#endif
	return DEBUG_LOG_MSG_TO_FILE;
}

static int fts_log_msg(const char* fmt, ...)
{
	va_list args;
	int ret = 0;
	char buf[512] = {0};
	mm_segment_t fs;
	struct timex  txc;
	struct rtc_time tm;

	if (!tpdebug_log_on()) {
//		printk("%s: disabled\n", __func__);
		if (!IS_ERR(filep)) {
			filp_close(filep, NULL);
			filep = (struct file*)-1;
		}
		return ret;
	}
//		printk("%s: enabled\n", __func__);
	if (IS_ERR(filep)) {
		filep = filp_open(FILE_TP_LOG, O_CREAT | O_RDWR | O_TRUNC, 0);
		if (IS_ERR(filep)) {
			printk("fail to open "FILE_TP_LOG"\n");
			return -1;
		}
	}

	do_gettimeofday(&(txc.time));
	rtc_time_to_tm(txc.time.tv_sec, &tm);
	va_start(args, fmt);
	ret = sprintf(buf, "%04d%02d%02d-%02d:%02d:%02d ",
	              tm.tm_year+1900, tm.tm_mon, tm.tm_mday,
	              tm.tm_hour, tm.tm_min, tm.tm_sec);
	ret += vsnprintf(buf+ret, (sizeof buf)-ret, fmt, args);
	va_end(args);

	if (filep->f_inode->i_size > 10 * 1024 * 1024) {
		filp_close(filep, NULL);
		fs = get_fs();
		set_fs(KERNEL_DS);
		sys_truncate(FILE_TP_LOG, 0);
		set_fs(fs);
		filep = filp_open(FILE_TP_LOG, O_CREAT | O_RDWR, 0);
	}
	ret = vfs_write(filep, buf, ret, &filep->f_pos);

	return ret;
}

static void fts_dump_hex_to_log(char* buf, int len)
{
	u8 pr_buf[256] = { 0 };

	if (!tpdebug_log_on()) {
//		printk("%s: disabled\n", __func__);
		if (!IS_ERR(filep)) {
			filp_close(filep, NULL);
			filep = (struct file*)-1;
		}
		return;
	}
//	printk("%s: enabled\n", __func__);
	while (len > 0) {
		hex_dump_to_buffer(buf, len, 16, 1, pr_buf, 256, false);
		buf += 16;
		len -= 16;
		fts_log_msg("%s\n", pr_buf);
	}
}
//#else
//static int fts_log_msg(const char* fmt, ...) { return 0; }
//static void fts_dump_hex_to_log(char* buf, int len) { }
//#endif

/*****************************************************************************
*  Name: fts_wait_tp_to_valid
*  Brief:   Read chip id until TP FW become valid, need call when reset/power on/resume...
*           1. Read Chip ID per INTERVAL_READ_REG(20ms)
*           2. Timeout: TIMEOUT_READ_REG(300ms)
*  Input:
*  Output:
*  Return: 0 - Get correct Device ID
*****************************************************************************/
int fts_wait_tp_to_valid(struct i2c_client *client)
{
	int ret = 0;
	int cnt = 0;
	u8  reg_value = 0;

	do {
		ret = fts_i2c_read_reg(client, FTS_REG_CHIP_ID, &reg_value);
		if ((ret < 0) || (reg_value != chip_types.chip_idh)) {
			FTS_INFO("TP Not Ready, ReadData = 0x%x", reg_value);
		} else if (reg_value == chip_types.chip_idh) {
			FTS_INFO("TP Ready, Device ID = 0x%x", reg_value);
			return 0;
		}
		cnt++;
		msleep(INTERVAL_READ_REG);
	} while ((cnt * INTERVAL_READ_REG) < TIMEOUT_READ_REG);

	ret = fts_ft5x46_get_vendor_id_flash(client, &reg_value);
	if (ret == 0) {
		FTS_INFO("Vendor ID = 0x%x", reg_value);
		return 0;
	} else {
		FTS_INFO("fts_ft5x46_get_vendor_id_flash failed.");
	}
	/* error: not get correct reg data */
	return -1;
}

/*****************************************************************************
*  Name: fts_recover_state
*  Brief: Need execute this function when reset
*  Input:
*  Output:
*  Return:
*****************************************************************************/
void fts_tp_state_recovery(struct i2c_client *client)
{
	/* wait tp stable */
	fts_wait_tp_to_valid(client);
	/* recover TP charger state 0x8B */
	/* recover TP glove state 0xC0 */
	/* recover TP cover state 0xC1 */
	fts_ex_mode_recovery(client);
	/* recover TP gesture state 0xD0 */
#ifdef CONFIG_FT5XX_TGESTURE_FUNCTION
#if FTS_GESTURE_EN
	fts_gesture_recovery(client);
#endif
#endif
}


/*****************************************************************************
*  Name: fts_reset_proc
*  Brief: Execute reset operation
*  Input: hdelayms - delay time unit:ms
*  Output:
*  Return:
*****************************************************************************/
int fts_reset_proc(int hdelayms)
{
	gpio_direction_output(fts_wq_data->pdata->reset_gpio, 0);
	msleep(20);
	gpio_direction_output(fts_wq_data->pdata->reset_gpio, 1);
	msleep(hdelayms);

	return 0;
}

/*****************************************************************************
*  Name: fts_irq_disable
*  Brief: disable irq
*  Input:
*   sync:
*  Output:
*  Return:
*****************************************************************************/
void fts_irq_disable(void)
{
	unsigned long irqflags;
	spin_lock_irqsave(&fts_wq_data->irq_lock, irqflags);

	if (!fts_wq_data->irq_disable) {
		disable_irq_nosync(fts_wq_data->client->irq);
		fts_wq_data->irq_disable = 1;
	}

	spin_unlock_irqrestore(&fts_wq_data->irq_lock, irqflags);
}

/*****************************************************************************
*  Name: fts_irq_enable
*  Brief: enable irq
*  Input:
*  Output:
*  Return:
*****************************************************************************/
void fts_irq_enable(void)
{
	unsigned long irqflags = 0;
	spin_lock_irqsave(&fts_wq_data->irq_lock, irqflags);

	if (fts_wq_data->irq_disable) {
		enable_irq(fts_wq_data->client->irq);
		fts_wq_data->irq_disable = 0;
	}

	spin_unlock_irqrestore(&fts_wq_data->irq_lock, irqflags);
}

/*****************************************************************************
*  Name: fts_input_dev_init
*  Brief: input dev init
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fts_input_dev_init( struct i2c_client *client, struct fts_ts_data *data,  struct input_dev *input_dev, struct fts_ts_platform_data *pdata)
{
	int  err, len;

	FTS_FUNC_ENTER();

	/* Init and register Input device */
	input_dev->name = FTS_DRIVER_NAME;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;

	input_set_drvdata(input_dev, data);
	i2c_set_clientdata(client, data);

	__set_bit(EV_KEY, input_dev->evbit);
	if (data->pdata->have_key) {
		FTS_DEBUG("set key capabilities");
		for (len = 0; len < data->pdata->key_number; len++) {
			input_set_capability(input_dev, EV_KEY, data->pdata->keys[len]);
		}
	}
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

#if FTS_MT_PROTOCOL_B_EN
	input_mt_init_slots(input_dev, pdata->max_touch_number, INPUT_MT_DIRECT);
#else
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 0x0f, 0, 0);
#endif
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, pdata->x_min, pdata->x_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, pdata->y_min, pdata->y_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 0xFF, 0, 0);
#if FTS_REPORT_PRESSURE_EN
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 0xFF, 0, 0);
#endif

	err = input_register_device(input_dev);
	if (err) {
		FTS_ERROR("Input device registration failed");
		goto free_inputdev;
	}

	FTS_FUNC_EXIT();

	return 0;

free_inputdev:
	input_free_device(input_dev);
	FTS_FUNC_EXIT();
	return err;

}

/*****************************************************************************
* Power Control
*****************************************************************************/
#if FTS_POWER_SOURCE_CUST_EN
static int fts_power_source_init(struct fts_ts_data *data)
{
	int rc;

	FTS_FUNC_ENTER();

	data->vdd = regulator_get(&data->client->dev, "vdd");
	if (IS_ERR(data->vdd)) {
		rc = PTR_ERR(data->vdd);
		FTS_ERROR("Regulator get failed vdd rc=%d", rc);
	}

	if (regulator_count_voltages(data->vdd) > 0) {
		rc = regulator_set_voltage(data->vdd, FTS_VTG_MIN_UV, FTS_VTG_MAX_UV);
		if (rc) {
			FTS_ERROR("Regulator set_vtg failed vdd rc=%d", rc);
			goto reg_vdd_put;
		}
	}

	data->vcc_i2c = regulator_get(&data->client->dev, "vcc_i2c");
	if (IS_ERR(data->vcc_i2c)) {
		rc = PTR_ERR(data->vcc_i2c);
		FTS_ERROR("Regulator get failed vcc_i2c rc=%d", rc);
		goto reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(data->vcc_i2c) > 0) {
		rc = regulator_set_voltage(data->vcc_i2c, FTS_I2C_VTG_MIN_UV, FTS_I2C_VTG_MAX_UV);
		if (rc) {
			FTS_ERROR("Regulator set_vtg failed vcc_i2c rc=%d", rc);
			goto reg_vcc_i2c_put;
		}
	}

	FTS_FUNC_EXIT();
	return 0;

reg_vcc_i2c_put:
	regulator_put(data->vcc_i2c);
reg_vdd_set_vtg:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, FTS_VTG_MAX_UV);
reg_vdd_put:
	regulator_put(data->vdd);
	FTS_FUNC_EXIT();
	return rc;
}

static int fts_power_source_ctrl(struct fts_ts_data *data, int enable)
{
	int rc;

	FTS_FUNC_ENTER();
	if (enable) {
		rc = regulator_enable(data->vdd);
		if (rc) {
			FTS_ERROR("Regulator vdd enable failed rc=%d", rc);
		}

		rc = regulator_enable(data->vcc_i2c);
		if (rc) {
			FTS_ERROR("Regulator vcc_i2c enable failed rc=%d", rc);
		}
	} else {
		rc = regulator_disable(data->vdd);
		if (rc) {
			FTS_ERROR("Regulator vdd disable failed rc=%d", rc);
		}
		rc = regulator_disable(data->vcc_i2c);
		if (rc) {
			FTS_ERROR("Regulator vcc_i2c disable failed rc=%d", rc);
		}
	}
	FTS_FUNC_EXIT();
	return 0;
}

#endif


/*****************************************************************************
*  Reprot related
*****************************************************************************/
/*****************************************************************************
*  Name: fts_release_all_finger
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static void fts_release_all_finger(void)
{
#if FTS_MT_PROTOCOL_B_EN
	unsigned int finger_count=0;
#endif

	mutex_lock(&fts_wq_data->report_mutex);
#if FTS_MT_PROTOCOL_B_EN
	for (finger_count = 0; finger_count < fts_wq_data->pdata->max_touch_number; finger_count++) {
		input_mt_slot(fts_input_dev, finger_count);
		input_mt_report_slot_state(fts_input_dev, MT_TOOL_FINGER, false);
	}
#else
	input_mt_sync(fts_input_dev);
#endif
	input_report_key(fts_input_dev, BTN_TOUCH, 0);
	input_sync(fts_input_dev);
	mutex_unlock(&fts_wq_data->report_mutex);
}


#if (FTS_DEBUG_EN && (FTS_DEBUG_LEVEL == 2))
static void fts_show_touch_buffer(u8 *buf, int point_num)
{
	int len = point_num * FTS_ONE_TCH_LEN;
	int count = 0;
	int i;

	memset(g_sz_debug, 0, 1024);
	if (len > (POINT_READ_BUF-3)) {
		len = POINT_READ_BUF-3;
	} else if (len == 0) {
		len += FTS_ONE_TCH_LEN;
	}
	count += sprintf(g_sz_debug, "%02X,%02X,%02X", buf[0], buf[1], buf[2]);
	for (i = 0; i < len; i++) {
		count += sprintf(g_sz_debug+count, ",%02X", buf[i+3]);
	}
	FTS_DEBUG("buffer: %s", g_sz_debug);
}
#endif

static int fts_input_dev_report_key_event(struct ts_event *event, struct fts_ts_data *data)
{
	int i;

	if (data->pdata->have_key) {
		if ( (1 == event->touch_point || 1 == event->point_num) &&
		     (event->au16_y[0] == data->pdata->key_y_coord)) {

			if (event->point_num == 0) {
				FTS_DEBUG("Keys All Up!");
				for (i = 0; i < data->pdata->key_number; i++) {
					input_report_key(data->input_dev, data->pdata->keys[i], 0);
				}
			} else {
				for (i = 0; i < data->pdata->key_number; i++) {
					if (event->au16_x[0] > (data->pdata->key_x_coords[i] - FTS_KEY_WIDTH) &&
					    event->au16_x[0] < (data->pdata->key_x_coords[i] + FTS_KEY_WIDTH)) {

						if (event->au8_touch_event[i]== 0 ||
						    event->au8_touch_event[i] == 2) {
							input_report_key(data->input_dev, data->pdata->keys[i], 1);
//                            FTS_DEBUG("Key%d(%d, %d) DOWN!", i, event->au16_x[0], event->au16_y[0]);
						} else {
							input_report_key(data->input_dev, data->pdata->keys[i], 0);
//                            FTS_DEBUG("Key%d(%d, %d) Up!", i, event->au16_x[0], event->au16_y[0]);
						}
						break;
					}
				}
			}
			input_sync(data->input_dev);
			return 0;
		}
	}

	return -1;
}

#if FTS_MT_PROTOCOL_B_EN
static int fts_input_dev_report_b(struct ts_event *event, struct fts_ts_data *data)
{
	int i = 0;
	int uppoint = 0;
	int touchs = 0;
	for (i = 0; i < event->touch_point; i++) {
		bool pt_active;
		if (event->au8_finger_id[i] >= data->pdata->max_touch_number) {
			break;
		}

#ifdef CONFIG_PROJECT_HS3
		{
			int  TINNO_TP_BORDER_TOUCH = 16;    // tinnotp border touch function
			const struct fts_ts_platform_data *pdata = data->pdata;

			/*************tinnotp border touch function****************/
			//printk("tinnotp border touch function  x=%d, y=%d ,sensor_max_x=%d\n", x ,y,rmi4_data->sensor_max_x);
			if(TINNO_TP_BORDER_TOUCH > 0) {
				if(event->au16_x[i] > (pdata->x_max -TINNO_TP_BORDER_TOUCH)) {
					if(event->au16_x[i] > (pdata->x_max -TINNO_TP_BORDER_TOUCH/2)) {
//				printk("tinnotp border touch function  drop point, border %d\n", TINNO_TP_BORDER_TOUCH);
						continue;
					} else {
						event->au16_x[i]= event->au16_x[i] + event->au16_x[i] +TINNO_TP_BORDER_TOUCH - pdata->x_max;
						printk("tinnotp border touch function  x=%d\n", event->au16_x[i]);
					}
				}

				if((event->au16_x[i]< TINNO_TP_BORDER_TOUCH)&&( event->au16_y[i] > 0)) {
					if(event->au16_x[i] < (TINNO_TP_BORDER_TOUCH/2)) {
//				printk("tinnotp border touch function  drop point\n");
						continue;
					} else {
						event->au16_x[i] = event->au16_x[i] + event->au16_x[i] -TINNO_TP_BORDER_TOUCH;
//				printk("tinnotp border touch function  x=%d\n", event->au16_x[i]);
					}
				}
			}
			/*************tinnotp border touch function****************/
		}
#endif

		input_mt_slot(data->input_dev, event->au8_finger_id[i]);

		if (event->au8_touch_event[i] == FTS_TOUCH_DOWN || event->au8_touch_event[i] == FTS_TOUCH_CONTACT) {
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, true);

#if FTS_REPORT_PRESSURE_EN
			if (event->pressure[i] <= 0) {
				FTS_ERROR("[B]Illegal pressure: %d", event->pressure[i]);
				event->pressure[i] = 1;
			}
			input_report_abs(data->input_dev, ABS_MT_PRESSURE, event->pressure[i]);
#endif

			if (event->area[i] <= 0) {
				FTS_ERROR("[B]Illegal touch-major: %d", event->area[i]);
				event->area[i] = 1;
			}
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->area[i]);

			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->au16_x[i]);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->au16_y[i]);
			touchs |= BIT(event->au8_finger_id[i]);
			data->touchs |= BIT(event->au8_finger_id[i]);

			//          FTS_DEBUG("[B]P%d(%d, %d)[p:%d,tm:%d] DOWN!", event->au8_finger_id[i], event->au16_x[i],
			//                    event->au16_y[i], event->pressure[i], event->area[i]);
			pt_active = true;
		} else {
			uppoint++;
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, false);
			data->touchs &= ~BIT(event->au8_finger_id[i]);
//           FTS_DEBUG("[B]P%d UP!", event->au8_finger_id[i]);
			pt_active = false;
		}
		fts_log_msg("fts %s bit %d data->touchs=%2x touchs=%2x (%d %d)\n",
		            pt_active ? "  active" : "inactive",
		            event->au8_finger_id[i],
		            data->touchs, touchs,
		            event->au16_x[i], event->au16_y[i]);
	}

	if (unlikely(data->touchs ^ touchs)) {
		fts_log_msg("fts touchs not matched\n");
		for (i = 0; i < data->pdata->max_touch_number; i++) {
			if (BIT(i) & (data->touchs ^ touchs)) {
				FTS_DEBUG("[B]P%d UP!", i);
				input_mt_slot(data->input_dev, i);
				input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, false);
			}
		}
	}

	data->touchs = touchs;
	if (event->touch_point == uppoint) {
		FTS_DEBUG("Points All Up!");
		input_report_key(data->input_dev, BTN_TOUCH, 0);
	} else {
		input_report_key(data->input_dev, BTN_TOUCH, event->touch_point > 0);
	}

	input_sync(data->input_dev);

	return 0;

}

#else
static int fts_input_dev_report_a(struct ts_event *event,struct fts_ts_data *data)
{
	int i =0;
	int uppoint = 0;
	int touchs = 0;

	for (i = 0; i < event->touch_point; i++) {

		if (event->au8_touch_event[i] == FTS_TOUCH_DOWN || event->au8_touch_event[i] == FTS_TOUCH_CONTACT) {
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->au8_finger_id[i]);
#if FTS_REPORT_PRESSURE_EN
			if (event->pressure[i] <= 0) {
				FTS_ERROR("[B]Illegal pressure: %d", event->pressure[i]);
				event->pressure[i] = 1;
			}
			input_report_abs(data->input_dev, ABS_MT_PRESSURE, event->pressure[i]);
#endif

			if (event->area[i] <= 0) {
				FTS_ERROR("[B]Illegal touch-major: %d", event->area[i]);
				event->area[i] = 1;
			}
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->area[i]);

			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->au16_x[i]);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->au16_y[i]);

			input_mt_sync(data->input_dev);

			FTS_DEBUG("[B]P%d(%d, %d)[p:%d,tm:%d] DOWN!", event->au8_finger_id[i], event->au16_x[i],
			          event->au16_y[i], event->pressure[i], event->area[i]);
		} else {
			uppoint++;
		}
	}

	data->touchs = touchs;
	if (event->touch_point == uppoint) {
		FTS_DEBUG("Points All Up!");
		input_report_key(data->input_dev, BTN_TOUCH, 0);
		input_mt_sync(data->input_dev);
	} else {
		input_report_key(data->input_dev, BTN_TOUCH, event->touch_point > 0);
	}

	input_sync(data->input_dev);

	return 0;
}
#endif

/*****************************************************************************
*  Name: fts_read_touchdata
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fts_read_touchdata(struct fts_ts_data *data)
{
	u8 buf[POINT_READ_BUF] = { 0 };
	u8 pointid = FTS_MAX_ID;
	int ret = -1;
	int i;
	struct ts_event * event = &(data->event);

#ifdef CONFIG_FT5XX_TGESTURE_FUNCTION
#if FTS_GESTURE_EN
	if (bEnTGesture) {
		u8 state;
		if (data->suspended) {
			fts_i2c_read_reg(data->client, FTS_REG_GESTURE_EN, &state);
			if (state ==1) {
				fts_gesture_readdata(data->client);
				return 1;
			}
		}
	}
#endif
#endif

#if FTS_PSENSOR_EN
	if ( (fts_sensor_read_data(data) != 0) && (data->suspended == 1) ) {
		return 1;
	}
#endif


#if FTS_READ_TOUCH_BUFFER_DIVIDED
	memset(buf, 0xFF, POINT_READ_BUF);
	memset(event, 0, sizeof(struct ts_event));

	buf[0] = 0x00;
	ret = fts_i2c_read(data->client, buf, 1, buf, (3 + FTS_ONE_TCH_LEN));
	if (ret < 0) {
		FTS_ERROR("%s read touchdata failed.", __func__);
		return ret;
	}
	event->touch_point = 0;
	event->point_num=buf[FTS_TOUCH_POINT_NUM] & 0x0F;
	if (event->point_num > data->pdata->max_touch_number)
		event->point_num = data->pdata->max_touch_number;

	if (event->point_num > 1) {
		buf[9] = 0x09;
		fts_i2c_read(data->client, buf+9, 1, buf+9, (event->point_num - 1) * FTS_ONE_TCH_LEN);
	}
#else
	ret = fts_i2c_read(data->client, buf, 1, buf, POINT_READ_BUF);
	if (ret < 0) {
		FTS_ERROR("[B]Read touchdata failed, ret: %d", ret);
		return ret;
	}

	fts_dump_hex_to_log(buf, POINT_READ_BUF);

#if FTS_POINT_REPORT_CHECK_EN
	fts_point_report_check_queue_work();
#endif

	memset(event, 0, sizeof(struct ts_event));
	event->point_num = buf[FTS_TOUCH_POINT_NUM] & 0x0F;
	if (event->point_num > data->pdata->max_touch_number)
		event->point_num = data->pdata->max_touch_number;
	event->touch_point = 0;
#endif

#if (FTS_DEBUG_EN && (FTS_DEBUG_LEVEL == 2))
	fts_show_touch_buffer(buf, event->point_num);
#endif

	for (i = 0; i < data->pdata->max_touch_number; i++) {
		pointid = (buf[FTS_TOUCH_ID_POS + FTS_ONE_TCH_LEN * i]) >> 4;
		if (pointid >= FTS_MAX_ID)
			break;
		else
			event->touch_point++;

		event->au16_x[i] =
		    (s16) (buf[FTS_TOUCH_X_H_POS + FTS_ONE_TCH_LEN * i] & 0x0F) <<
		    8 | (s16) buf[FTS_TOUCH_X_L_POS + FTS_ONE_TCH_LEN * i];
		event->au16_y[i] =
		    (s16) (buf[FTS_TOUCH_Y_H_POS + FTS_ONE_TCH_LEN * i] & 0x0F) <<
		    8 | (s16) buf[FTS_TOUCH_Y_L_POS + FTS_ONE_TCH_LEN * i];
		event->au8_touch_event[i] =
		    buf[FTS_TOUCH_EVENT_POS + FTS_ONE_TCH_LEN * i] >> 6;
		event->au8_finger_id[i] =
		    (buf[FTS_TOUCH_ID_POS + FTS_ONE_TCH_LEN * i]) >> 4;
		event->area[i] =
		    (buf[FTS_TOUCH_AREA_POS + FTS_ONE_TCH_LEN * i]) >> 4;
		event->pressure[i] =
		    (s16) buf[FTS_TOUCH_PRE_POS + FTS_ONE_TCH_LEN * i];

		if (0 == event->area[i])
			event->area[i] = 0x09;

		if (0 == event->pressure[i])
			event->pressure[i] = 0x3f;

		if ((event->au8_touch_event[i]==0 || event->au8_touch_event[i]==2)&&(event->point_num==0)) {
			FTS_DEBUG("abnormal touch data from fw");
			return -1;
		}
	}
	if(event->touch_point == 0) {
		return -1;
	}
	return 0;
}

/*****************************************************************************
*  Name: fts_report_value
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static void fts_report_value(struct fts_ts_data *data)
{
	struct ts_event *event = &data->event;


	FTS_DEBUG("point number: %d, touch point: %d", event->point_num,
	          event->touch_point);

	if (0 == fts_input_dev_report_key_event(event, data)) {
		return;
	}

#if FTS_MT_PROTOCOL_B_EN
	fts_input_dev_report_b(event, data);
#else
	fts_input_dev_report_a(event, data);
#endif


	return;

}

/*****************************************************************************
*  Name: fts_ts_interrupt
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static irqreturn_t fts_ts_interrupt(int irq, void *dev_id)
{
	struct fts_ts_data *fts_ts = dev_id;
	int ret = -1;

	if (!fts_ts) {
		FTS_ERROR("[INTR]: Invalid fts_ts");
		return IRQ_HANDLED;
	}

#if FTS_ESDCHECK_EN
	fts_esdcheck_set_intr(1);
#endif

	ret = fts_read_touchdata(fts_wq_data);

	if (ret == 0) {
		mutex_lock(&fts_wq_data->report_mutex);
		fts_report_value(fts_wq_data);
		mutex_unlock(&fts_wq_data->report_mutex);
	}

#if FTS_ESDCHECK_EN
	fts_esdcheck_set_intr(0);
#endif

	return IRQ_HANDLED;
}

/*****************************************************************************
*  Name: fts_gpio_configure
*  Brief: Configure IRQ&RESET GPIO
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fts_gpio_configure(struct fts_ts_data *data)
{
	int err = 0;

	FTS_FUNC_ENTER();
	/* request irq gpio */
	if (gpio_is_valid(data->pdata->irq_gpio)) {
		err = gpio_request(data->pdata->irq_gpio, "fts_irq_gpio");
		if (err) {
			FTS_ERROR("[GPIO]irq gpio request failed");
			goto err_irq_gpio_req;
		}

		err = gpio_direction_input(data->pdata->irq_gpio);
		if (err) {
			FTS_ERROR("[GPIO]set_direction for irq gpio failed");
			goto err_irq_gpio_dir;
		}
	}
	/* request reset gpio */
	if (gpio_is_valid(data->pdata->reset_gpio)) {
		err = gpio_request(data->pdata->reset_gpio, "fts_reset_gpio");
		if (err) {
			FTS_ERROR("[GPIO]reset gpio request failed");
			goto err_irq_gpio_dir;
		}

//        err = gpio_direction_output(data->pdata->reset_gpio, 1);
		err = gpio_direction_output(data->pdata->reset_gpio, 0);
		if (err) {
			FTS_ERROR("[GPIO]set_direction for reset gpio failed");
			goto err_reset_gpio_dir;
		}
		msleep(10);
	}

	FTS_FUNC_EXIT();
	return 0;

err_reset_gpio_dir:
	if (gpio_is_valid(data->pdata->reset_gpio))
		gpio_free(data->pdata->reset_gpio);
err_irq_gpio_dir:
	if (gpio_is_valid(data->pdata->irq_gpio))
		gpio_free(data->pdata->irq_gpio);
err_irq_gpio_req:
	FTS_FUNC_EXIT();
	return err;
}


/*****************************************************************************
*  Name: fts_get_dt_coords
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fts_get_dt_coords(struct device *dev, char *name,
                             struct fts_ts_platform_data *pdata)
{
	u32 coords[FTS_COORDS_ARR_SIZE];
	struct property *prop;
	struct device_node *np = dev->of_node;
	int coords_size, rc;

	prop = of_find_property(np, name, NULL);
	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;


	coords_size = prop->length / sizeof(u32);
	if (coords_size != FTS_COORDS_ARR_SIZE) {
		FTS_ERROR("invalid %s", name);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(np, name, coords, coords_size);
	if (rc && (rc != -EINVAL)) {
		FTS_ERROR("Unable to read %s", name);
		return rc;
	}

	if (!strcmp(name, "focaltech,display-coords") || !strcmp(name, "focaltech,display-coords-old")) {
		pdata->x_min = coords[0];
		pdata->y_min = coords[1];
		pdata->x_max = coords[2];
		pdata->y_max = coords[3];
	} else {
		FTS_ERROR("unsupported property %s", name);
		return -EINVAL;
	}

	return 0;
}

/*****************************************************************************
*  Name: fts_parse_dt
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fts_parse_dt(struct device *dev, struct fts_ts_platform_data *pdata)
{
	int rc;
	struct device_node *np = dev->of_node;
	u32 temp_val;

	FTS_FUNC_ENTER();

	rc = fts_get_dt_coords(dev, "focaltech,display-coords", pdata);
	if (rc)
		FTS_ERROR("Unable to get display-coords");

	/* key */
	pdata->have_key = of_property_read_bool(np, "focaltech,have-key");
	if (pdata->have_key) {
		rc = of_property_read_u32(np, "focaltech,key-number", &pdata->key_number);
		if (rc) {
			FTS_ERROR("Key number undefined!");
		}
		rc = of_property_read_u32_array(np, "focaltech,keys",
		                                pdata->keys, pdata->key_number);
		if (rc) {
			FTS_ERROR("Keys undefined!");
		}
		rc = of_property_read_u32(np, "focaltech,key-y-coord", &pdata->key_y_coord);
		if (rc) {
			FTS_ERROR("Key Y Coord undefined!");
		}
		rc = of_property_read_u32_array(np, "focaltech,key-x-coords",
		                                pdata->key_x_coords, pdata->key_number);
		if (rc) {
			FTS_ERROR("Key X Coords undefined!");
		}
		FTS_DEBUG("%d: (%d, %d, %d), [%d, %d, %d][%d]",
		          pdata->key_number, pdata->keys[0], pdata->keys[1], pdata->keys[2],
		          pdata->key_x_coords[0], pdata->key_x_coords[1], pdata->key_x_coords[2],
		          pdata->key_y_coord);
	}

	/* reset, irq gpio info */
	pdata->reset_gpio = of_get_named_gpio_flags(np, "focaltech,reset-gpio", 0, &pdata->reset_gpio_flags);
	if (pdata->reset_gpio < 0) {
		FTS_ERROR("Unable to get reset_gpio");
	}

	pdata->irq_gpio = of_get_named_gpio_flags(np, "focaltech,irq-gpio", 0, &pdata->irq_gpio_flags);
	if (pdata->irq_gpio < 0) {
		FTS_ERROR("Unable to get irq_gpio");
	}

	rc = of_property_read_u32(np, "focaltech,max-touch-number", &temp_val);
	if (!rc) {
		pdata->max_touch_number = temp_val;
		FTS_DEBUG("max_touch_number=%d", pdata->max_touch_number);
	} else {
		FTS_ERROR("Unable to get max-touch-number");
		pdata->max_touch_number = FTS_MAX_POINTS;
	}

	/*fw auto update flag*/

	pdata->fw_auto_update = of_property_read_bool(np, "focaltech,fw-auto-update");
	if (pdata->fw_auto_update)
		FTS_INFO("fw_auto_update: enabled");
	else
		FTS_INFO("fw_auto_update: disabled");

	FTS_FUNC_EXIT();
	return 0;
}

#if defined(CONFIG_FB)

#ifdef CONFIG_TINNO_DRV_OPTIMIZE
static void fb_notify_resume_work(struct work_struct *work)
{
	fts_ts_resume(&fts_wq_data->client->dev);
}
#endif

/*****************************************************************************
*  Name: fb_notifier_callback
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fb_notifier_callback(struct notifier_block *self,
                                unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct fts_ts_data *fts_data =
	    container_of(self, struct fts_ts_data, fb_notif);
	FTS_ERROR("TINNO: fb_notifier_callback TP");//add temp log for IAAO-250.
#ifdef CONFIG_TINNO_DRV_OPTIMIZE
	if (evdata && evdata->data &&
	    fts_data && fts_data->client) {
		blank = evdata->data;
		if (event == FB_EVENT_BLANK && *blank == FB_BLANK_UNBLANK) {
			schedule_work(&fts_data->fb_notify_work);
		} else if (event == FB_EARLY_EVENT_BLANK && *blank == FB_BLANK_POWERDOWN) {
			flush_work(&fts_data->fb_notify_work);
			fts_ts_suspend(&fts_data->client->dev);
		}
	}
#else
	if (evdata && evdata->data && event == FB_EVENT_BLANK &&
	    fts_data && fts_data->client) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK)
			fts_ts_resume(&fts_data->client->dev);
		else if (*blank == FB_BLANK_POWERDOWN)
			fts_ts_suspend(&fts_data->client->dev);
	}
#endif
	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
/*****************************************************************************
*  Name: fts_ts_early_suspend
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static void fts_ts_early_suspend(struct early_suspend *handler)
{
	struct fts_ts_data *data = container_of(handler,
	                                        struct fts_ts_data,
	                                        early_suspend);

	fts_ts_suspend(&data->client->dev);
}

/*****************************************************************************
*  Name: fts_ts_late_resume
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static void fts_ts_late_resume(struct early_suspend *handler)
{
	struct fts_ts_data *data = container_of(handler,
	                                        struct fts_ts_data,
	                                        early_suspend);

	fts_ts_resume(&data->client->dev);
}
#endif

static int  fts_ts_compability(struct device *dev, struct fts_ts_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	int rc = 0;

	/*ID gpio info*/
	#ifndef CONFIG_PROJECT_C800
	pdata->id_gpio = of_get_named_gpio_flags(np, "focaltech,id-gpio", 0, &pdata->id_gpio_flags);
	if (pdata->id_gpio < 0) {
		FTS_ERROR("Unable to get id_gpio");
	}
	/* request reset gpio */
		if (gpio_is_valid(pdata->id_gpio)) {
		rc= gpio_request(pdata->id_gpio, "fts_id_gpio");
		if (rc) {
			FTS_ERROR("[GPIO]id gpio request failed");
			//            goto err_irq_gpio_dir;
		}
			rc = gpio_direction_input(pdata->id_gpio);
		if (rc) {
			FTS_ERROR("[GPIO]set_direction for id gpio failed");
			}
	}
	if (gpio_get_value(pdata->id_gpio) == 0) {
		FTS_INFO("The id_gpio valus is 0.");
		rc = fts_get_dt_coords(dev, "focaltech,display-coords-old", pdata);
		if (rc)
			FTS_ERROR("Unable to get display-coords-old");
	} else {
		FTS_INFO("The id_gpio valus is 1.");
	}
	#endif

	return rc;
}

/*****************************************************************************
*  Name: fts_ts_probe
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fts_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct fts_ts_platform_data *pdata;
	struct fts_ts_data *data;
	struct input_dev *input_dev;
	int err;

	FTS_FUNC_ENTER();
	/* 1. Get Platform data */
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
		                     sizeof(struct fts_ts_platform_data),
		                     GFP_KERNEL);
		if (!pdata) {
			FTS_ERROR("[MEMORY]Failed to allocate memory");
			FTS_FUNC_EXIT();
			return -ENOMEM;
		}
		err = fts_parse_dt(&client->dev, pdata);
		if (err) {
			FTS_ERROR("[DTS]DT parsing failed");
		}
		err = fts_ts_compability(&client->dev, pdata);
		if (err) {
			FTS_ERROR("[DTS]fts_ts_compability failed");
		}
	} else {
		pdata = client->dev.platform_data;
	}

	if (!pdata) {
		FTS_ERROR("Invalid pdata");
		FTS_FUNC_EXIT();
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		FTS_ERROR("I2C not supported");
		FTS_FUNC_EXIT();
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(struct fts_ts_data), GFP_KERNEL);
	if (!data) {
		FTS_ERROR("[MEMORY]Failed to allocate memory");
		FTS_FUNC_EXIT();
		return -ENOMEM;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		FTS_ERROR("[INPUT]Failed to allocate input device");
		FTS_FUNC_EXIT();
		return -ENOMEM;
	}



	data->input_dev = input_dev;
	data->client = client;
	data->pdata = pdata;

	fts_wq_data = data;
	fts_i2c_client = client;
	fts_input_dev = input_dev;

	spin_lock_init(&fts_wq_data->irq_lock);
	mutex_init(&fts_wq_data->report_mutex);

#ifdef CONFIG_FT5XX_TGESTURE_FUNCTION
#if FTS_GESTURE_EN
	//fts_Gesture_init(input_dev);
	//init_para(720,1280,0,0,0);
	ft5xx_key_dev = input_allocate_device();
	if (!ft5xx_key_dev) {
		dev_err(&client->dev,"[syna]SY_key_dev: fail!\n");
	}
	__set_bit(EV_KEY,  ft5xx_key_dev->evbit);
	__set_bit(KEY_FT5XX_SENSOR,  ft5xx_key_dev->keybit);
	__set_bit(KEY_POWER,  ft5xx_key_dev->keybit);
	ft5xx_key_dev->id.bustype = BUS_HOST;
	ft5xx_key_dev->name = "TPFT5XX_GESTURE";
	if(input_register_device(ft5xx_key_dev)) {
		dev_err(&client->dev,"[ft5xx]ft5xx_key_dev register : fail!\n");
	} else {
		dev_err(&client->dev,"[ft5xx]ft5xx_key_dev register : success!!\n");
	}
#endif
#endif

	fts_input_dev_init(client, data, input_dev, pdata);

	fts_ctpm_get_upgrade_array();

	err = fts_gpio_configure(data);
	if (err < 0) {
		FTS_ERROR("[GPIO]Failed to configure the gpios");
		goto free_gpio;
	}

#if FTS_POWER_SOURCE_CUST_EN
	fts_power_source_init(data);
	fts_power_source_ctrl(data, 1);
#endif

	fts_reset_proc(200);
	err = fts_wait_tp_to_valid(client);
	if (err < 0) {
		FTS_ERROR("Failed to communicate with tp controller, exit probe");
		goto free_gpio;
	}

	err = request_threaded_irq(client->irq, NULL, fts_ts_interrupt,
	                           pdata->irq_gpio_flags | IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
	                           client->dev.driver->name, data);
	if (err) {
		FTS_ERROR("Request irq failed!");
		goto free_gpio;
	}

	fts_irq_disable();

#if FTS_PSENSOR_EN
	if ( fts_sensor_init(data) != 0) {
		FTS_ERROR("fts_sensor_init failed!");
		FTS_FUNC_EXIT();
		return 0;
	}
#endif

#if FTS_APK_NODE_EN
	fts_create_apk_debug_channel(client);
#endif

#if FTS_SYSFS_NODE_EN
	fts_create_sysfs(client);
#endif

#if FTS_POINT_REPORT_CHECK_EN
	fts_point_report_check_init();
#endif

	fts_ex_mode_init(client);

#ifdef CONFIG_FT5XX_TGESTURE_FUNCTION
#if FTS_GESTURE_EN
	fts_gesture_init(input_dev, client);
#endif
#endif

#if FTS_ESDCHECK_EN
	fts_esdcheck_init();
#endif

	fts_irq_enable();

#if FTS_AUTO_UPGRADE_EN
	fts_ctpm_upgrade_init();
#endif

#if FTS_TEST_EN
	fts_test_init(client);
#endif

#if defined(CONFIG_FB)

#ifdef CONFIG_TINNO_DRV_OPTIMIZE
	INIT_WORK(&data->fb_notify_work, fb_notify_resume_work);
#endif

	data->fb_notif.notifier_call = fb_notifier_callback;
	err = fb_register_client(&data->fb_notif);
	if (err)
		FTS_ERROR("[FB]Unable to register fb_notifier: %d", err);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + FTS_SUSPEND_LEVEL;
	data->early_suspend.suspend = fts_ts_early_suspend;
	data->early_suspend.resume = fts_ts_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

	FTS_FUNC_EXIT();
	return 0;

free_gpio:
	if (gpio_is_valid(pdata->reset_gpio))
		gpio_free(pdata->reset_gpio);
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->irq_gpio);
	return err;

}

/*****************************************************************************
*  Name: fts_ts_remove
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fts_ts_remove(struct i2c_client *client)
{
	struct fts_ts_data *data = i2c_get_clientdata(client);

	FTS_FUNC_ENTER();
	cancel_work_sync(&data->touch_event_work);

#if FTS_PSENSOR_EN
	fts_sensor_remove(data);
#endif

#if FTS_POINT_REPORT_CHECK_EN
	fts_point_report_check_exit();
#endif

#if FTS_APK_NODE_EN
	fts_release_apk_debug_channel();
#endif

#if FTS_SYSFS_NODE_EN
	fts_remove_sysfs(client);
#endif

	fts_ex_mode_exit(client);

#if FTS_AUTO_UPGRADE_EN
	cancel_work_sync(&fw_update_work);
#endif

#if defined(CONFIG_FB)
	if (fb_unregister_client(&data->fb_notif))
		FTS_ERROR("Error occurred while unregistering fb_notifier.");
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&data->early_suspend);
#endif
	free_irq(client->irq, data);

	if (gpio_is_valid(data->pdata->reset_gpio))
		gpio_free(data->pdata->reset_gpio);

	if (gpio_is_valid(data->pdata->irq_gpio))
		gpio_free(data->pdata->irq_gpio);

	input_unregister_device(data->input_dev);

#if FTS_TEST_EN
	fts_test_exit(client);
#endif

#if FTS_ESDCHECK_EN
	fts_esdcheck_exit();
#endif

	FTS_FUNC_EXIT();
	return 0;
}

/*****************************************************************************
*  Name: fts_ts_suspend
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fts_ts_suspend(struct device *dev)
{
	struct fts_ts_data *data = dev_get_drvdata(dev);
	int retval = 0;

	FTS_FUNC_ENTER();
	if (data->suspended) {
		FTS_INFO("Already in suspend state");
		FTS_FUNC_EXIT();
		return -1;
	}

#if FTS_ESDCHECK_EN
	fts_esdcheck_suspend();
#endif

#ifdef CONFIG_FT5XX_TGESTURE_FUNCTION
#if FTS_GESTURE_EN
	if (bEnTGesture) {
		retval = fts_gesture_suspend(data->client);
		if (retval == 0) {
			/* Enter into gesture mode(suspend) */
			retval = enable_irq_wake(fts_wq_data->client->irq);
			if (retval)
				FTS_ERROR("%s: set_irq_wake failed", __func__);
			data->suspended = true;
			FTS_FUNC_EXIT();
			return 0;
		}
	}
#endif
#endif

#if FTS_PSENSOR_EN
	if ( fts_sensor_suspend(data) != 0 ) {
		enable_irq_wake(data->client->irq);
		data->suspended = true;
		return 0;
	}
#endif

	fts_irq_disable();

	/* TP enter sleep mode */
	retval = fts_i2c_write_reg(data->client, FTS_REG_POWER_MODE, FTS_REG_POWER_MODE_SLEEP_VALUE);
	if (retval < 0) {
		FTS_ERROR("Set TP to sleep mode fail, ret=%d!", retval);
	}
	data->suspended = true;

	FTS_FUNC_EXIT();

	return 0;
}

/*****************************************************************************
*  Name: fts_ts_resume
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int fts_ts_resume(struct device *dev)
{
	struct fts_ts_data *data = dev_get_drvdata(dev);

	FTS_FUNC_ENTER();
	if (!data->suspended) {
		FTS_DEBUG("Already in awake state");
		FTS_FUNC_EXIT();
		return -1;
	}

	fts_release_all_finger();

#if (!FTS_CHIP_IDC)
	fts_reset_proc(200);
#endif

	fts_tp_state_recovery(data->client);

#if FTS_ESDCHECK_EN
	fts_esdcheck_resume();
#endif

#ifdef CONFIG_FT5XX_TGESTURE_FUNCTION
#if FTS_GESTURE_EN
	if (bEnTGesture) {
		if (fts_gesture_resume(data->client) == 0) {
			int err;
			err = disable_irq_wake(data->client->irq);
			if (err)
				FTS_ERROR("%s: disable_irq_wake failed",__func__);
			data->suspended = false;
			FTS_FUNC_EXIT();
			return 0;
		}
	}
#endif
#endif

#if FTS_PSENSOR_EN
	if ( fts_sensor_resume(data) != 0 ) {
		disable_irq_wake(data->client->irq);
		data->suspended = false;
		FTS_FUNC_EXIT();
		return 0;
	}
#endif

	data->suspended = false;

	fts_irq_enable();

	FTS_FUNC_EXIT();
	return 0;
}

/*****************************************************************************
* I2C Driver
*****************************************************************************/
static const struct i2c_device_id fts_ts_id[] = {
	{FTS_DRIVER_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, fts_ts_id);

static struct of_device_id fts_match_table[] = {
	{ .compatible = "focaltech,5446", },
	{ },
};

static struct i2c_driver fts_ts_driver = {
	.probe = fts_ts_probe,
	.remove = fts_ts_remove,
	.driver = {
		.name = FTS_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = fts_match_table,
	},
	.id_table = fts_ts_id,
};

/*****************************************************************************
*  Name: fts_ts_init
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static int __init fts_ts_init(void)
{
	int ret = 0;

	FTS_FUNC_ENTER();
	ret = i2c_add_driver(&fts_ts_driver);
	if ( ret != 0 ) {
		FTS_ERROR("Focaltech touch screen driver init failed!");
	}
	FTS_FUNC_EXIT();
	return ret;
}

/*****************************************************************************
*  Name: fts_ts_exit
*  Brief:
*  Input:
*  Output:
*  Return:
*****************************************************************************/
static void __exit fts_ts_exit(void)
{
	i2c_del_driver(&fts_ts_driver);
}

//module_init(fts_ts_init);
late_initcall(fts_ts_init);
module_exit(fts_ts_exit);

MODULE_AUTHOR("FocalTech Driver Team");
MODULE_DESCRIPTION("FocalTech Touchscreen Driver");
MODULE_LICENSE("GPL v2");
