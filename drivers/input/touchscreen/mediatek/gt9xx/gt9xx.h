/*
 * Goodix GT9xx touchscreen driver
 *
 * Copyright  (C)  2018 Goodix. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Version: 2.4
 * Release Date: 2014/11/28
 */

#ifndef _GOODIX_GT9XX_H_
#define _GOODIX_GT9XX_H_

#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#endif
#ifdef CONFIG_FB
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <mt-plat/mtk_boot_common.h>

/* **************PART1:ON/OFF define**************** */
#define GTP_CUSTOM_CFG        0
#define GTP_CHANGE_X2Y        0	   /* swap x y */
#define GTP_DRIVER_SEND_CFG   1	   /* driver send config */
#define GTP_HAVE_TOUCH_KEY    0
#define GTP_POWER_CTRL_SLEEP  0    /* power off when suspend */
#define GTP_ICS_SLOT_REPORT   1    /*  slot protocol */

#define GTP_AUTO_UPDATE       1    /* auto update fw by .bin file as default */
/*
 * auto update fw by request fw from userspace,
 * function together with GTP_AUTO_UPDATE
 */
#define GTP_REQUEST_FW        1
/* auto update config by .cfg file, function together with GTP_AUTO_UPDATE */
#define GTP_AUTO_UPDATE_CFG   0

#define GTP_COMPATIBLE_MODE   0    /*  compatible with GT9XXF */

#define GTP_CREATE_WR_NODE    1
#define GTP_ESD_PROTECT       1 /* esd protection with a cycle of 2 seconds */

#define GTP_WITH_PEN          0
 /* active pen has buttons, function together with GTP_WITH_PEN */
#define GTP_PEN_HAVE_BUTTON   0

#define GTP_GESTURE_WAKEUP    0    /*  gesture wakeup */

#define GTP_DEBUG_ON          0
#define GTP_DEBUG_ARRAY_ON    0
#define GTP_DEBUG_FUNC_ON     0

#ifdef CONFIG_GTP9XX_SLEEP_MODE
#define GTP_SUSPEND_PWROFF    0
#else
#define GTP_SUSPEND_PWROFF    1
#endif

#if GTP_COMPATIBLE_MODE
enum CHIP_TYPE_T {
	CHIP_TYPE_GT9  = 0,
	CHIP_TYPE_GT9F = 1,
};
#endif

struct goodix_ts_data {
	spinlock_t irq_lock;
	struct i2c_client *client;
	struct input_dev  *input_dev;
	struct hrtimer timer;
	struct work_struct  work;
	s32 irq_is_disable;
	s32 use_irq;
	u16 abs_x_max;
	u16 abs_y_max;
	u8  max_touch_num;
	u8  int_trigger_type;
	u8  green_wake_mode;
	u8  enter_update;
	u8  gtp_is_suspend;
	u8  gtp_rawdiff_mode;
	int  gtp_cfg_len;
	u8  fw_error;
	u8  pnl_init_error;

#if   defined(CONFIG_FB)
	struct notifier_block notifier;
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif

#if GTP_WITH_PEN
	struct input_dev *pen_dev;
#endif

#if GTP_ESD_PROTECT
	spinlock_t esd_lock;
	u8  esd_running;
	s32 clk_tick_cnt;
#endif
#if GTP_COMPATIBLE_MODE
	u16 bak_ref_len;
	s32 ref_chk_fs_times;
	s32 clk_chk_fs_times;
	enum CHIP_TYPE_T chip_type;
	u8 rqst_processing;
	u8 is_950;
#endif

};

extern u16 show_len;
extern u16 total_len;
extern int gtp_rst_gpio;
extern int gtp_int_gpio;

/*
 **************** PART2:TODO define *****************
 * STEP_1(REQUIRED): Define Configuration Information Group(s)
 * Sensor_ID Map:
 * sensor_opt1 sensor_opt2 Sensor_ID
   GND         GND          0
   VDDIO      GND          1
   NC           GND          2
   GND         NC/300K    3
   VDDIO      NC/300K    4
   NC           NC/300K    5

 * TODO: define your own default or for Sensor_ID == 0 config here.
 * The predefined one is just a sample config,
 * which is not suitable for your tp in most cases.
 */
#define CTP_CFG_GROUP0 {\
	0x4B, 0x20, 0x03, 0x00, 0x05, 0x0A, 0x35, 0x00, 0x02, 0x08, 0x2D,\
	0x0F, 0x5A, 0x32, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x13, 0x21,\
	0x00, 0x17, 0x18, 0x1A, 0x14, 0x93, 0x33, 0xCC, 0x58, 0x5A, 0xEB,\
	0x17, 0x00, 0x00, 0x00, 0x22, 0x03, 0x1D, 0x00, 0x00, 0x00, 0x00,\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x64, 0x81, 0xE2,\
	0x01, 0x14, 0x0A, 0x00, 0x04, 0x7A, 0x51, 0x00, 0x7D, 0x55, 0x00,\
	0x81, 0x59, 0x00, 0x85, 0x5D, 0x00, 0x8A, 0x61, 0x00, 0x8A, 0x00,\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00,\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,\
	0x00, 0x00, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10, 0x0F,\
	0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,\
	0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,\
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,\
	0x0C, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x15, 0x16, 0x17, 0x18,\
	0x19, 0x1B, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,\
	0x26, 0x27, 0x28, 0x29, 0xFF, 0xFF, 0xFF, 0xFF, 0x32, 0x01\
}

/* TODO: define your config for Sensor_ID == 1 here, if needed */
#define CTP_CFG_GROUP1 {\
}

/* TODO: define your config for Sensor_ID == 2 here, if needed */
#define CTP_CFG_GROUP2 {\
	0x48, 0xD0, 0x02, 0x00, 0x05, 0x05, 0x75, 0x01, 0x01, 0x0F, 0x24,\
	0x0F, 0x64, 0x3C, 0x03, 0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,\
	0x00, 0x16, 0x19, 0x1C, 0x14, 0x8C, 0x0E, 0x0E, 0x24, 0x00, 0x31,\
	0x0D, 0x00, 0x00, 0x00, 0x83, 0x33, 0x1D, 0x00, 0x41, 0x00, 0x00,\
	0x3C, 0x0A, 0x14, 0x08, 0x0A, 0x00, 0x2B, 0x1C, 0x3C, 0x94, 0xD5,\
	0x03, 0x08, 0x00, 0x00, 0x04, 0x93, 0x1E, 0x00, 0x82, 0x23, 0x00,\
	0x74, 0x29, 0x00, 0x69, 0x2F, 0x00, 0x5F, 0x37, 0x00, 0x5F, 0x20,\
	0x40, 0x60, 0x00, 0xF0, 0x40, 0x30, 0x55, 0x50, 0x27, 0x00, 0x00,\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x19, 0x00, 0x00,\
	0x50, 0x50, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12,\
	0x14, 0x16, 0x18, 0x1A, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1D,\
	0x1E, 0x1F, 0x20, 0x21, 0x22, 0x24, 0x26, 0x28, 0x29, 0x2A, 0x1C,\
	0x18, 0x16, 0x14, 0x13, 0x12, 0x10, 0x0F, 0x0C, 0x0A, 0x08, 0x06,\
	0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x01\
}

/* TODO: define your config for Sensor_ID == 3 here, if needed */
#define CTP_CFG_GROUP3 {\
}
/* TODO: define your config for Sensor_ID == 4 here, if needed */
#define CTP_CFG_GROUP4 {\
}

/* TODO: define your config for Sensor_ID == 5 here, if needed */
#define CTP_CFG_GROUP5 {\
}

/* STEP_2(REQUIRED): Customize your I/O ports & I/O operations */
#define GTP_RST_PORT    103/* S5PV210_GPJ3(6) */
#define GTP_INT_PORT    109/* S5PV210_GPH1(3) */

/* Copied from drivers/gpio/gpiolib.h */
struct gpio_desc {
	struct gpio_chip	*chip;
	unsigned long		flags;
	/* flag symbols are bit numbers */
#define FLAG_REQUESTED	0
#define FLAG_IS_OUT	1
#define FLAG_EXPORT	2	/* protected by sysfs_lock */
#define FLAG_SYSFS	3	/* exported via /sys/class/gpio/control */
#define FLAG_TRIG_FALL	4	/* trigger on falling edge */
#define FLAG_TRIG_RISE	5	/* trigger on rising edge */
#define FLAG_ACTIVE_LOW	6	/* value has active low */
#define FLAG_OPEN_DRAIN	7	/* Gpio is open drain type */
#define FLAG_OPEN_SOURCE 8	/* Gpio is open source type */
#define FLAG_USED_AS_IRQ 9	/* GPIO is connected to an IRQ */

#define ID_SHIFT	16	/* add new flags before this one */

#define GPIO_FLAGS_MASK		((1 << ID_SHIFT) - 1)
#define GPIO_TRIGGER_MASK	(BIT(FLAG_TRIG_FALL) | BIT(FLAG_TRIG_RISE))

	const char		*label;
};


static inline void gtp_gpio_direction_output(unsigned int pin, int level)
{
	struct gpio_desc *desc = gpio_to_desc(pin);

	if (test_bit(FLAG_USED_AS_IRQ, &desc->flags)) {
		clear_bit(FLAG_USED_AS_IRQ, &desc->flags);
		gpio_direction_output(pin, level);
		set_bit(FLAG_USED_AS_IRQ, &desc->flags);
	} else
		gpio_direction_output(pin, level);
}

#define GTP_GPIO_AS_INPUT(pin)	gpio_direction_input(pin)

#define GTP_GPIO_AS_INT(pin)	GTP_GPIO_AS_INPUT(pin)

#define GTP_GPIO_GET_VALUE(pin)         gpio_get_value(pin)
#define GTP_GPIO_OUTPUT(pin, level)      gtp_gpio_direction_output(pin, level)
#define GTP_GPIO_REQUEST(pin, label)    gpio_request(pin, label)
#define GTP_GPIO_FREE(pin)              gpio_free(pin)
#define GTP_IRQ_TAB                     {IRQ_TYPE_EDGE_RISING,\
	IRQ_TYPE_EDGE_FALLING,\
	IRQ_TYPE_LEVEL_LOW,\
	IRQ_TYPE_LEVEL_HIGH}

/*  STEP_3(optional): Specify your special config info if needed */
#if GTP_CUSTOM_CFG
#define GTP_MAX_HEIGHT   800
#define GTP_MAX_WIDTH    480
#define GTP_INT_TRIGGER  0            /*  0: Rising 1: Falling */
#else
#define GTP_MAX_HEIGHT   4096
#define GTP_MAX_WIDTH    4096
#define GTP_INT_TRIGGER  1
#endif
#define GTP_MAX_TOUCH         10

#ifdef CONFIG_TOUCHSCREEN_GT967
#undef GTP_MAX_HEIGHT
#undef GTP_MAX_WIDTH
#undef GTP_INT_TRIGGER

#define GTP_MAX_HEIGHT   1024
#define GTP_MAX_WIDTH    600
#define GTP_INT_TRIGGER  1
#endif

/* STEP_4(optional): If keys are available and */
/* reported as keys, config your key info here */
#if GTP_HAVE_TOUCH_KEY
#define GTP_KEY_TAB  {KEY_MENU, KEY_HOME, KEY_BACK}
#endif

/* *************PART3:OTHER define******************** */
#define GTP_DRIVER_VERSION          "V2.4<2014/11/28>"
#define GTP_I2C_NAME                "Goodix-TS"
#define GT91XX_CONFIG_PROC_FILE     "gt9xx_config"
#define GT91XX_FW_VERSION_PROC_FILE     "tp_fw_version"
#define GTP_POLL_TIME         10
#define GTP_ADDR_LENGTH       2
#define GTP_CONFIG_MIN_LENGTH 186
#define GTP_CONFIG_MAX_LENGTH 240
#define FAIL                  0
#define SUCCESS               1
#define SWITCH_OFF            0
#define SWITCH_ON             1

/* ***************** For GT9XXF Start ***************** */
#define GTP_REG_BAK_REF                 0x99D0
#define GTP_REG_MAIN_CLK                0x8020
#define GTP_REG_CHIP_TYPE               0x8000
#define GTP_REG_HAVE_KEY                0x804E
#define GTP_REG_MATRIX_DRVNUM           0x8069
#define GTP_REG_MATRIX_SENNUM           0x806A

#define GTP_FL_FW_BURN              0x00
#define GTP_FL_ESD_RECOVERY         0x01
#define GTP_FL_READ_REPAIR          0x02

#define GTP_BAK_REF_SEND                0
#define GTP_BAK_REF_STORE               1
#define CFG_LOC_DRVA_NUM                29
#define CFG_LOC_DRVB_NUM                30
#define CFG_LOC_SENS_NUM                31

#define GTP_CHK_FW_MAX                  40
#define GTP_CHK_FS_MNT_MAX              300
#define GTP_BAK_REF_PATH                "/data/gtp_ref.bin"
#define GTP_MAIN_CLK_PATH               "/data/gtp_clk.bin"
#define GTP_RQST_CONFIG                 0x01
#define GTP_RQST_BAK_REF                0x02
#define GTP_RQST_RESET                  0x03
#define GTP_RQST_MAIN_CLOCK             0x04
#define GTP_RQST_RESPONDED              0x00
#define GTP_RQST_IDLE                   0xFF

/* ******************* For GT9XXF End ********************* */
/*  Registers define */
#define GTP_READ_COOR_ADDR    0x814E
#define GTP_REG_SLEEP         0x8040
#define GTP_REG_SENSOR_ID     0x814A
#define GTP_REG_CONFIG_DATA   0x8047
#define GTP_REG_VERSION       0x8140

#define RESOLUTION_LOC        3
#define TRIGGER_LOC           8

#define CFG_GROUP_LEN(p_cfg_grp)  (ARRAY_SIZR(p_cfg_grp) / sizeof(p_cfg_grp[0]))
/*  Log define */
#define GTP_INFO(fmt, arg...)           pr_info("<<-GTP-INFO->> "fmt"\n", ##arg)
#define GTP_ERROR(fmt, arg...)          pr_err("<<-GTP-ERROR->> "fmt"\n", ##arg)
#define GTP_DEBUG(fmt, arg...)          do {\
	if (GTP_DEBUG_ON)\
	pr_debug("<<-GTP-DEBUG->> [%d]"fmt"\n", __LINE__, ##arg);\
} while (0)
#define GTP_DEBUG_ARRAY(array, num)    do {\
	s32 i;\
	u8 *a = array;\
	if (GTP_DEBUG_ARRAY_ON) {\
		GTP_DEBUG("<<-GTP-DEBUG-ARRAY->>\n");\
		for (i = 0; i < (num); i++) {\
			GTP_DEBUG("%02x   ", (a)[i]);\
			if ((i + 1) % 10 == 0) {\
				GTP_DEBUG("\n");\
			} \
		} \
		GTP_DEBUG("\n");\
	} \
} while (0)
#define GTP_DEBUG_FUNC()               do {\
	if (GTP_DEBUG_FUNC_ON)\
	GTP_DEBUG("<<-GTP-FUNC->> Func:%s@Line:%d\n", __func__, __LINE__);\
} while (0)
#define GTP_SWAP(x, y)                 do {\
	typeof(x) z = x;\
	x = y;\
	y = z;\
} while (0)

/* ******************End of Part III******************* */

#if GTP_ESD_PROTECT
extern void gtp_esd_switch(struct i2c_client *client, s32 on);
#endif

#if GTP_COMPATIBLE_MODE
s32 gup_fw_download_proc(void *dir, u8 dwn_mode);
#endif

ssize_t goodix_tool_read(struct file *file,
	char __user *page, size_t size, loff_t *ppos);
ssize_t goodix_tool_write(struct file *filp,
	const char __user *buff, size_t len, loff_t *off);

extern void gtp_irq_disable(struct goodix_ts_data *ts);
extern void gtp_irq_enable(struct goodix_ts_data *ts);

extern u8 config[GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH];
extern void gtp_reset_guitar(struct i2c_client *client, s32 ms);
extern s32  gtp_send_cfg(struct i2c_client *client);
extern s32 gtp_read_version(struct i2c_client *client, u16 *version);
extern struct i2c_client *i2c_connect_client;
extern void gtp_irq_enable(struct goodix_ts_data *ts);
extern void gtp_irq_disable(struct goodix_ts_data *ts);
extern s32 gtp_i2c_read_dbl_check(struct i2c_client *client,
	u16 addr, u8 *rxbuf, int len);

#if GTP_COMPATIBLE_MODE

s32 gtp_fw_startup(struct i2c_client *client);

#endif

#if GTP_ESD_PROTECT
void gtp_esd_switch(struct i2c_client *client,
	s32 on);
#endif

#if GTP_COMPATIBLE_MODE
extern s32 i2c_read_bytes(struct i2c_client *client,
	u16 addr, u8 *buf, s32 len);
extern s32 i2c_write_bytes(struct i2c_client *client,
	u16 addr, u8 *buf, s32 len);
extern s32 gup_clk_calibration(void);
extern s32 gup_fw_download_proc(void *dir, u8 dwn_mode);
extern u8 gup_check_fs_mounted(char *path_name);

void gtp_recovery_reset(struct i2c_client *client);
#endif

#if GTP_CREATE_WR_NODE
extern s32 init_wr_node(struct i2c_client *client);
extern void uninit_wr_node(void);
#endif

#if GTP_AUTO_UPDATE
extern u8 gup_init_update_proc(struct goodix_ts_data *ts);
#endif

void gtp_reset_guitar(struct i2c_client *client, s32 ms);
s32 gtp_send_cfg(struct i2c_client *client);
void gtp_int_sync(s32 ms);

#define UPDATE_FUNCTIONS

#ifdef UPDATE_FUNCTIONS
extern s32 gup_enter_update_mode(struct i2c_client *client);
extern void gup_leave_update_mode(void);
extern s32 gup_update_proc(void *dir);
#endif

#ifdef CONFIG_OF
#define GTP_CONFIG_OF
int gtp_parse_dt_cfg(struct device *dev, u8 *cfg, int *cfg_len, u8 sid);
#endif
#endif /* _GOODIX_GT9XX_H_ */
