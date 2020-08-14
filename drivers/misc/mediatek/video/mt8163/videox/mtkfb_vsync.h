/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __MTKFB_VSYNC_H__
#define __MTKFB_VSYNC_H__

#define MTKFB_VSYNC_DEVNAME "mtkfb_vsync"

#define MTKFB_VSYNC_IOCTL_MAGIC 'V'
extern void mtkfb_waitVsync(void);
extern void mtkfb_disable_non_fb_layer(void);
#if defined(CONFIG_MTK_HDMI_SUPPORT)
extern void hdmi_waitVsync(void);
#endif

enum vsync_src {
	MTKFB_VSYNC_SOURCE_LCM = 0,
	MTKFB_VSYNC_SOURCE_HDMI = 1,
	MTKFB_VSYNC_SOURCE_EPD = 2,
};

#define MTKFB_VSYNC_IOCTL _IOW(MTKFB_VSYNC_IOCTL_MAGIC, 1, enum vsync_src)

#if defined(CONFIG_ARCH_MT6735) || defined(CONFIG_ARCH_MT6735M) ||             \
	defined(CONFIG_ARCH_MT6753)
void mtkfb_vsync_log_enable(int enable);
#endif

#endif /* MTKFB_VSYNC_H */
