/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef __KPD_H__
#define __KPD_H__

#include <linux/ioctl.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/atomic.h>
#include <hal_kpd.h>
#include <linux/kernel.h>
#include <linux/delay.h>

struct keypad_dts_data {
	u32 kpd_key_debounce;
	u32 kpd_sw_pwrkey;
	u32 kpd_hw_pwrkey;
	u32 kpd_sw_rstkey;
	u32 kpd_hw_rstkey;
	u32 kpd_use_extend_type;
	u32 kpd_hw_map_num;
	u32 kpd_hw_init_map[KPD_NUM_KEYS];
	u32 kpd_hw_dl_key1;
	u32 kpd_hw_dl_key2;
	u32 kpd_hw_dl_key3;
	u32 kpd_hw_recovery_key;
	u32 kpd_hw_factory_key;
};

extern struct keypad_dts_data kpd_dts_data;
extern int kpd_klog_en;

#define KPD_DEBUG	1
#define SET_KPD_KCOL		_IO('k', 29)
#define KPD_SAY		"kpd: "

#if KPD_DEBUG
#define kpd_print(fmt, arg...)	do { \
	if (kpd_klog_en) \
		pr_debug(KPD_SAY fmt, ##arg); \
	} while (0)
#define kpd_info(fmt, arg...)	do { \
	if (kpd_klog_en) \
		pr_info(KPD_SAY fmt, ##arg); \
	} while (0)
#define kpd_notice(fmt, arg...)	 \
			pr_notice(KPD_SAY fmt, ##arg)

#else
#define kpd_print(fmt, arg...)	do {} while (0)
#define kpd_info(fmt, arg...)	do {} while (0)
#endif

#ifdef CONFIG_KPD_PWRKEY_USE_PMIC
void kpd_pwrkey_pmic_handler(unsigned long pressed);
#endif
#ifdef CONFIG_KPD_VOLUME_KEY_SWAP
u32 kpd_get_linux_key_code(u32 keycode, bool pressed);
#endif
#endif				/* __KPD_H__ */
