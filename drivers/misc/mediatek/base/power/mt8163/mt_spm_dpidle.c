/*
 * Copyright (C) 2015 MediaTek Inc.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/of_fdt.h>

/* #include <mach/irqs.h> */
#include <mt-plat/mtk_cirq.h>
#include <mach/wd_api.h>
/* #include <mach/mt_gpt.h> */
#ifdef CONFIG_MD32_SUPPORT
#include <mach/md32_helper.h>
#endif
#include "mt_spm_internal.h"
#include "mt_spm_idle.h"
#include "mt_cpuidle.h"
#include "mt_cpufreq.h"

/**************************************
 * only for internal debug
 **************************************
 */
#define  CONFIG_MTK_LDVT

#ifdef CONFIG_MTK_LDVT
#define SPM_PWAKE_EN            0
#define SPM_BYPASS_SYSPWREQ     1
#else
#define SPM_PWAKE_EN            1
#define SPM_BYPASS_SYSPWREQ     0
#endif

#define WAKE_SRC_FOR_DPIDLE \
		(WAKE_SRC_MD32_WDT | WAKE_SRC_KP | WAKE_SRC_GPT | \
		WAKE_SRC_CONN2AP | WAKE_SRC_EINT | WAKE_SRC_CONN_WDT | \
		WAKE_SRC_MD32_SPM | WAKE_SRC_USB_CD | WAKE_SRC_USB_PDN | \
		WAKE_SRC_AFE | WAKE_SRC_SYSPWREQ)

#define WAKE_SRC_FOR_MD32  0

#define I2C_CHANNEL 1

#define spm_is_wakesrc_invalid(wakesrc)     (!!((u32)(wakesrc) & 0xc0003803))

/* (CA7MCUCFG_BASE + 0x1C) //0x1020011c */
#define CA70_BUS_CONFIG          0xF020002C
/* (CA7MCUCFG_BASE + 0x1C) //0x1020011c */
#define CA71_BUS_CONFIG          0xF020022C

#ifdef CONFIG_OF
#define MCUCFG_BASE          spm_mcucfg
#else
#define MCUCFG_BASE          (0xF0200000)	/* 0x1020_0000 */
#endif
#define MP0_AXI_CONFIG          (MCUCFG_BASE + 0x2C)
#define MP1_AXI_CONFIG          (MCUCFG_BASE + 0x22C)
#define ACINACTM                (1<<4)


/**********************************************************
 * PCM code for deep idle
 **********************************************************/
static const u32 dpidle_binary[] = {
	0x81f48407, 0x80328400, 0x80318400, 0xe8208000, 0x10006354, 0xffff1fff,
	0xe8208000, 0x10001108, 0x00000000, 0x1b80001f, 0x20000034, 0xe8208000,
	0x10006b04, 0x00000000, 0xc2802860, 0x1290041f, 0x1b00001f, 0x7ffcf7ff,
	0xf0000000, 0x17c07c1f, 0x1b00001f, 0x3ffce7ff, 0x1b80001f, 0x20000004,
	0xd820038c, 0x17c07c1f, 0xd0000620, 0x17c07c1f, 0xe8208000, 0x10001108,
	0x00000002, 0x1b80001f, 0x20000034, 0xe8208000, 0x10006354, 0xffffffff,
	0xe8208000, 0x10006b04, 0x00000001, 0xc2802860, 0x1290841f, 0xa0118400,
	0xa0128400, 0xe8208000, 0x10006b04, 0x00000004, 0x1b00001f, 0x3ffcefff,
	0xa1d48407, 0xf0000000, 0x17c07c1f, 0x81489801, 0xd80007c5, 0x17c07c1f,
	0x81419801, 0xd80007c5, 0x17c07c1f, 0x1a00001f, 0x10006604, 0xe2200004,
	0xc0c02960, 0x10807c1f, 0x81411801, 0xd8000925, 0x17c07c1f, 0x1b80001f,
	0x20000208, 0x18c0001f, 0x10006240, 0xe0e00016, 0xe0e0001e, 0xe0e0000e,
	0xe0e0000f, 0x81481801, 0xd8200c65, 0x17c07c1f, 0x18c0001f, 0x10004828,
	0x1910001f, 0x10004828, 0x89000004, 0x3fffffff, 0xe0c00004, 0x18c0001f,
	0x100041dc, 0x1910001f, 0x100041dc, 0x89000004, 0x3fffffff, 0xe0c00004,
	0x18c0001f, 0x1000f63c, 0x1910001f, 0x1000f63c, 0x89000004, 0xfffffff9,
	0xe0c00004, 0xc2802860, 0x1294841f, 0x803e0400, 0x1b80001f, 0x20000a50,
	0x803e8400, 0x803f0400, 0x803f8400, 0x1b80001f, 0x20000208, 0x80380400,
	0x1b80001f, 0x20000300, 0x803b0400, 0x1b80001f, 0x20000300, 0x803d0400,
	0x1b80001f, 0x20000300, 0x80340400, 0x80310400, 0xe8208000, 0x10000044,
	0x00000100, 0xe8208000, 0x10000004, 0x00000002, 0x1b80001f, 0x20000068,
	0x1b80001f, 0x2000000a, 0x18c0001f, 0x10006240, 0xe0e0000d, 0x81411801,
	0xd8001205, 0x17c07c1f, 0x18c0001f, 0x100040f4, 0x1910001f, 0x100040f4,
	0xa11c8404, 0xe0c00004, 0x1b80001f, 0x2000000a, 0x813c8404, 0xe0c00004,
	0x1b80001f, 0x20000100, 0x81fa0407, 0x81f08407, 0xe8208000, 0x10006354,
	0xfff01b47, 0xa1d80407, 0xa1dc0407, 0xa1de8407, 0xa1df0407, 0xc2802860,
	0x1291041f, 0x1b00001f, 0xbffce7ff, 0xf0000000, 0x17c07c1f, 0x1b80001f,
	0x20000fdf, 0x1a50001f, 0x10006608, 0x80c9a401, 0x810aa401, 0x10918c1f,
	0xa0939002, 0x80ca2401, 0x810ba401, 0xa09c0c02, 0xa0979002, 0x8080080d,
	0xd8201742, 0x17c07c1f, 0x1b00001f, 0x3ffce7ff, 0x1b80001f, 0x20000004,
	0xd8001d8c, 0x17c07c1f, 0x1b00001f, 0xbffce7ff, 0xd0001d80, 0x17c07c1f,
	0x81f80407, 0x81fc0407, 0x81fe8407, 0x81ff0407, 0x1880001f, 0x10006320,
	0xc0c02100, 0xe080000f, 0xd8001603, 0x17c07c1f, 0xe080001f, 0xa1da0407,
	0xe8208000, 0x10000048, 0x00000100, 0xe8208000, 0x10000004, 0x00000002,
	0x1b80001f, 0x20000068, 0xa0110400, 0xa0140400, 0xa01b0400, 0xa01d0400,
	0xa0180400, 0xa01e0400, 0x1b80001f, 0x20000104, 0x81411801, 0xd8001ba5,
	0x17c07c1f, 0x18c0001f, 0x10006240, 0xc0c02040, 0x17c07c1f, 0x81489801,
	0xd8001d05, 0x17c07c1f, 0x81419801, 0xd8001d05, 0x17c07c1f, 0x1a00001f,
	0x10006604, 0xe2200005, 0xc0c02960, 0x10807c1f, 0xc2802860, 0x1291841f,
	0x1b00001f, 0x7ffcf7ff, 0xf0000000, 0x17c07c1f, 0x1900001f, 0x10006830,
	0xe1000003, 0xf0000000, 0x17c07c1f, 0xe0f07f16, 0x1380201f, 0xe0f07f1e,
	0x1380201f, 0xe0f07f0e, 0x1b80001f, 0x20000104, 0xe0f07f0c, 0xe0f07f0d,
	0xe0f07e0d, 0xe0f07c0d, 0xe0f0780d, 0xe0f0700d, 0xf0000000, 0x17c07c1f,
	0xe0f07f0d, 0xe0f07f0f, 0xe0f07f1e, 0xe0f07f12, 0xf0000000, 0x17c07c1f,
	0xa1d08407, 0x1b80001f, 0x20000080, 0x80eab401, 0x1a00001f, 0x10006814,
	0xe2000003, 0xf0000000, 0x17c07c1f, 0x81429801, 0xd80023e5, 0x17c07c1f,
	0x18c0001f, 0x65930005, 0x1900001f, 0x10006830, 0xe1000003, 0xe8208000,
	0x10006834, 0x00000000, 0xe8208000, 0x10006834, 0x00000001, 0xf0000000,
	0x17c07c1f, 0xa1d10407, 0x1b80001f, 0x20000020, 0xf0000000, 0x17c07c1f,
	0xa1d00407, 0x1b80001f, 0x20000100, 0x80ea3401, 0x1a00001f, 0x10006814,
	0xe2000003, 0xf0000000, 0x17c07c1f, 0xe0e0000f, 0xe0e0000e, 0xe0e0001e,
	0xe0e00012, 0xf0000000, 0x17c07c1f, 0xd80027ea, 0x17c07c1f, 0xe0e00016,
	0xe0e0001e, 0x1380201f, 0xe0e0001f, 0xe0e0001d, 0xe0e0000d, 0xd0002820,
	0x17c07c1f, 0xe0e03301, 0xe0e03101, 0xf0000000, 0x17c07c1f, 0x18c0001f,
	0x10006b6c, 0x1910001f, 0x10006b6c, 0xa1002804, 0xe0c00004, 0xf0000000,
	0x17c07c1f, 0x18d0001f, 0x10006604, 0x10cf8c1f, 0xd8202963, 0x17c07c1f,
	0xf0000000, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x1840001f, 0x00000001, 0x1990001f, 0x10006b08,
	0xe8208000, 0x10006b6c, 0x00000000, 0x1b00001f, 0x2ffce7ff, 0x1b80001f,
	0x500f0000, 0xe8208000, 0x10006354, 0xfff01b47, 0xc0c02420, 0x81401801,
	0xd8004765, 0x17c07c1f, 0x81f60407, 0x18c0001f, 0x10006200, 0xc0c061c0,
	0x12807c1f, 0xe8208000, 0x1000625c, 0x00000001, 0x1890001f, 0x1000625c,
	0x81040801, 0xd8204344, 0x17c07c1f, 0xc0c061c0, 0x1280041f, 0x18c0001f,
	0x10006208, 0xc0c061c0, 0x12807c1f, 0x1b80001f, 0x20000003, 0xe8208000,
	0x10006248, 0x00000000, 0x1890001f, 0x10006248, 0x81040801, 0xd8004544,
	0x17c07c1f, 0xc0c061c0, 0x1280041f, 0x18c0001f, 0x10006290, 0xe0e0004f,
	0xc0c061c0, 0x1280041f, 0xe8208000, 0x10006404, 0x00003101, 0xc2802860,
	0x1292041f, 0x1b00001f, 0x2ffce7ff, 0x1b80001f, 0x30000004, 0x8880000c,
	0x2ffce7ff, 0xd8005bc2, 0x17c07c1f, 0xe8208000, 0x10006294, 0x0003ffff,
	0x18c0001f, 0x10006294, 0xe0e03fff, 0xe0e003ff, 0x81449801, 0xd8004b65,
	0x17c07c1f, 0x1a00001f, 0x10006604, 0x81491801, 0xd8004b05, 0x17c07c1f,
	0xc2802860, 0x1294041f, 0xc0c06680, 0x12807c1f, 0xd0004b60, 0x17c07c1f,
	0xe2200003, 0xc0c02960, 0x17c07c1f, 0x81489801, 0xd8204e45, 0x17c07c1f,
	0x81419801, 0xd8004e45, 0x17c07c1f, 0x1a00001f, 0x10006604, 0xe2200000,
	0xc0c02960, 0x17c07c1f, 0xc0c06de0, 0x17c07c1f, 0xe2200001, 0xc0c02960,
	0x17c07c1f, 0xc0c06de0, 0x17c07c1f, 0xe2200005, 0xc0c02960, 0x17c07c1f,
	0xc0c06de0, 0x17c07c1f, 0xc0c065c0, 0x17c07c1f, 0xa1d38407, 0xa1d98407,
	0xa0108400, 0xa0120400, 0xa0148400, 0xa0150400, 0xa0158400, 0xa01b8400,
	0xa01c0400, 0xa01c8400, 0xa0188400, 0xa0190400, 0xa0198400, 0xe8208000,
	0x10006310, 0x0b1600f8, 0x1b00001f, 0xbffce7ff, 0x1b80001f, 0x90100000,
	0x1240301f, 0x80c28001, 0xc8c00003, 0x17c07c1f, 0x80c10001, 0xc8c00663,
	0x17c07c1f, 0x1b00001f, 0x2ffce7ff, 0x18c0001f, 0x10006294, 0xe0e007fe,
	0xe0e00ffc, 0xe0e01ff8, 0xe0e03ff0, 0xe0e03fe0, 0xe0e03fc0, 0x1b80001f,
	0x20000020, 0xe8208000, 0x10006294, 0x0003ffc0, 0xe8208000, 0x10006294,
	0x0003fc00, 0x80388400, 0x80390400, 0x80398400, 0x1b80001f, 0x20000300,
	0x803b8400, 0x803c0400, 0x803c8400, 0x1b80001f, 0x20000300, 0x80348400,
	0x80350400, 0x80358400, 0x1b80001f, 0x20000104, 0x80308400, 0x80320400,
	0x81f38407, 0x81f98407, 0x81f90407, 0x81f40407, 0x81489801, 0xd82059a5,
	0x17c07c1f, 0x81419801, 0xd80059a5, 0x17c07c1f, 0x1a00001f, 0x10006604,
	0xe2200001, 0xc0c02960, 0x17c07c1f, 0xc0c06de0, 0x17c07c1f, 0xe2200000,
	0xc0c02960, 0x17c07c1f, 0xc0c06de0, 0x17c07c1f, 0xe2200004, 0xc0c02960,
	0x17c07c1f, 0xc0c06de0, 0x17c07c1f, 0x81449801, 0xd8005bc5, 0x17c07c1f,
	0x1a00001f, 0x10006604, 0x81491801, 0xd8005b25, 0x17c07c1f, 0xc0c06680,
	0x1280041f, 0xd0005b80, 0x17c07c1f, 0xe2200002, 0xc0c02960, 0x17c07c1f,
	0x1b80001f, 0x200016a8, 0x81401801, 0xd8006125, 0x17c07c1f, 0xe8208000,
	0x10006404, 0x00000101, 0x18c0001f, 0x10006290, 0x1212841f, 0xc0c06340,
	0x12807c1f, 0xc0c06340, 0x1280041f, 0x18c0001f, 0x10006208, 0x1212841f,
	0xc0c06340, 0x12807c1f, 0xe8208000, 0x10006248, 0x00000001, 0x1890001f,
	0x10006248, 0x81040801, 0xd8205e64, 0x17c07c1f, 0xc0c06340, 0x1280041f,
	0x18c0001f, 0x10006200, 0x1212841f, 0xc0c06340, 0x12807c1f, 0xe8208000,
	0x1000625c, 0x00000000, 0x1890001f, 0x1000625c, 0x81040801, 0xd8006044,
	0x17c07c1f, 0xc0c06340, 0x1280041f, 0x19c0001f, 0x61415820, 0x1ac0001f,
	0x55aa55aa, 0xf0000000, 0xd800626a, 0x17c07c1f, 0xe2e0004f, 0xe2e0006f,
	0xe2e0002f, 0xd820630a, 0x17c07c1f, 0xe2e0002e, 0xe2e0003e, 0xe2e00032,
	0xf0000000, 0x17c07c1f, 0xd800644a, 0x17c07c1f, 0xe2e00036, 0x17c07c1f,
	0x17c07c1f, 0xe2e0003e, 0x1380201f, 0xe2e0003c, 0xd820658a, 0x17c07c1f,
	0x1b80001f, 0x20000018, 0xe2e0007c, 0x1b80001f, 0x20000003, 0xe2e0005c,
	0xe2e0004c, 0xe2e0004d, 0xf0000000, 0x17c07c1f, 0xa1d40407, 0x1391841f,
	0xa1d90407, 0x1392841f, 0xf0000000, 0x17c07c1f, 0xe8208000, 0x10059850,
	0x00000001, 0xe8208000, 0x10059840, 0x00000003, 0xe8208000, 0x10059854,
	0x00000000, 0xe8208000, 0x10059838, 0x00000001, 0xe8208000, 0x1005980c,
	0x0000001f, 0xe8208000, 0x10059814, 0x00000002, 0xe8208000, 0x10059820,
	0x00000011, 0xe8208000, 0x10059848, 0x00000103, 0xe8208000, 0x10059810,
	0x0000002a, 0xe8208000, 0x10059804, 0x000000c0, 0x1a00001f, 0x10059800,
	0xd8206cca, 0x17c07c1f, 0xe2200001, 0xe22000ac, 0xe8208000, 0x10059824,
	0x00000001, 0x814c1801, 0xd8006c45, 0x17c07c1f, 0x1b80001f, 0x20000524,
	0xd0006c80, 0x17c07c1f, 0x1b80001f, 0x200066cb, 0xd0006da0, 0x17c07c1f,
	0xe2200001, 0xe2200094, 0xe8208000, 0x10059824, 0x00000001, 0x1b80001f,
	0x20000524, 0xf0000000, 0x17c07c1f, 0x1880001f, 0x0000001d, 0x814a1801,
	0xd80070a5, 0x17c07c1f, 0x81499801, 0xd80071a5, 0x17c07c1f, 0x814a9801,
	0xd80072a5, 0x17c07c1f, 0x814b9801, 0xd80073a5, 0x17c07c1f, 0x18d0001f,
	0x40000000, 0x18d0001f, 0x40000000, 0xd8006fa2, 0x00a00402, 0xd00074a0,
	0x17c07c1f, 0x18d0001f, 0x40000000, 0x18d0001f, 0x80000000, 0xd80070a2,
	0x00a00402, 0xd00074a0, 0x17c07c1f, 0x18d0001f, 0x40000000, 0x18d0001f,
	0x60000000, 0xd80071a2, 0x00a00402, 0xd00074a0, 0x17c07c1f, 0x18d0001f,
	0x40000000, 0x18d0001f, 0xc0000000, 0xd80072a2, 0x00a00402, 0xd00074a0,
	0x17c07c1f, 0x18d0001f, 0x40000000, 0x18d0001f, 0xa0000000, 0xd80073a2,
	0x00a00402, 0xd00074a0, 0x17c07c1f, 0xf0000000, 0x17c07c1f
};

static struct pcm_desc dpidle_pcm = {
	.version = "pcm_deepidle_v00.04_mt8163_20171222_v9",
	.base = dpidle_binary,
	.size = 935,
	.sess = 2,
	.replace = 0,
	.vec0 = EVENT_VEC(11, 1, 0, 0),	/* FUNC_26M_WAKEUP */
	.vec1 = EVENT_VEC(12, 1, 0, 20),	/* FUNC_26M_SLEEP */
	.vec2 = EVENT_VEC(30, 1, 0, 51),	/* FUNC_APSRC_WAKEUP */
	.vec3 = EVENT_VEC(31, 1, 0, 161),	/* FUNC_APSRC_SLEEP */
};

static struct pwr_ctrl dpidle_ctrl = {
	.wake_src = WAKE_SRC_FOR_DPIDLE,
	.wake_src_md32 = WAKE_SRC_FOR_MD32,
	.r0_ctrl_en = 1,
	.r7_ctrl_en = 1,
	.infra_dcm_lock = 1,
	.wfi_op = WFI_OP_AND,
	.ca15_wfi0_en = 1,
	.ca15_wfi1_en = 1,
	.ca15_wfi2_en = 1,
	.ca15_wfi3_en = 1,
	.ca7_wfi0_en = 1,
	.ca7_wfi1_en = 1,
	.ca7_wfi2_en = 1,
	.ca7_wfi3_en = 1,
	.disp_req_mask = 1,
	.mfg_req_mask = 1,
	.syspwreq_mask = 1,
};

struct spm_lp_scen __spm_dpidle = {
	.pcmdesc = &dpidle_pcm,
	.pwrctrl = &dpidle_ctrl,
};

int __attribute__ ((weak)) request_uart_to_sleep(void)
{
	return 0;
}

int __attribute__ ((weak)) request_uart_to_wakeup(void)
{
	return 0;
}

void __attribute__ ((weak)) mt_cirq_clone_gic(void)
{
}

void __attribute__ ((weak)) mt_cirq_enable(void)
{
}

void __attribute__ ((weak)) mt_cirq_flush(void)
{
}

void __attribute__ ((weak)) mt_cirq_disable(void)
{
}

u32 __attribute__ ((weak)) spm_get_sleep_wakesrc(void)
{
	return 0;
}

int __attribute__ ((weak)) mt_cpu_dormant(unsigned long flags)
{
	return 0;
}

static void spm_trigger_wfi_for_dpidle(struct pwr_ctrl *pwrctrl)
{
	if (is_cpu_pdn(pwrctrl->pcm_flags)) {
		mt_cpu_dormant(CPU_DEEPIDLE_MODE);
	} else {
		spm_write(MP0_AXI_CONFIG, spm_read(MP0_AXI_CONFIG) | ACINACTM);
		wfi_with_sync();
		spm_write(MP0_AXI_CONFIG, spm_read(MP0_AXI_CONFIG) & ~ACINACTM);
	}
}

/*
 * wakesrc: WAKE_SRC_XXX
 * enable : enable or disable @wakesrc
 * replace: if true, will replace the default setting
 */
int spm_set_dpidle_wakesrc(u32 wakesrc, bool enable, bool replace)
{
	unsigned long flags;

	if (spm_is_wakesrc_invalid(wakesrc))
		return -EINVAL;

	spin_lock_irqsave(&__spm_lock, flags);
	if (enable) {
		if (replace)
			__spm_dpidle.pwrctrl->wake_src = wakesrc;
		else
			__spm_dpidle.pwrctrl->wake_src |= wakesrc;
	} else {
		if (replace)
			__spm_dpidle.pwrctrl->wake_src = 0;
		else
			__spm_dpidle.pwrctrl->wake_src &= ~wakesrc;
	}
	spin_unlock_irqrestore(&__spm_lock, flags);

	return 0;
}

enum mempll_type {
	MEMP26MHZ = 0,
	MEMPLL3PLL = 1,
	MEMPLL1PLL = 2,
};

bool spm_set_dpidle_pcm_init_flag(void)
{
	return true;
}

static bool spm_set_dpidle_pcm_flag(struct pwr_ctrl *pwrctrl)
{
	if (pwrctrl->pcm_flags_cust == 0) {
		/* MEMPLL3PLL */
		pwrctrl->pcm_flags &=
			~(SPM_MEMPLL_1PLL_3PLL_SEL | SPM_VCORE_DVS_POSITION);
	}
	return true;
}

int spm_go_to_dpidle(u32 spm_flags, u32 spm_data)
{
	struct wake_status wakesta;
	unsigned long flags;
	struct mtk_irq_mask mask;
	struct irq_desc *desc = irq_to_desc(spm_irq_0);
	int wr = WR_NONE;
	struct pcm_desc *pcmdesc = __spm_dpidle.pcmdesc;
	struct pwr_ctrl *pwrctrl = __spm_dpidle.pwrctrl;

	set_pwrctrl_pcm_flags(pwrctrl, spm_flags);
	if (spm_set_dpidle_pcm_flag(pwrctrl) == false)
		return WR_UNKNOWN;

	/* set PMIC WRAP table for deepidle power control */
	mt_cpufreq_set_pmic_phase(PMIC_WRAP_PHASE_DEEPIDLE);

	spm_dpidle_before_wfi();

	spin_lock_irqsave(&__spm_lock, flags);
	mt_irq_mask_all(&mask);
	if (desc)
		unmask_irq(desc);
	mt_cirq_clone_gic();
	mt_cirq_enable();

	if (mtk8250_request_to_sleep()) {
		wr = WR_UART_BUSY;
		goto RESTORE_IRQ;
	}

	__spm_reset_and_init_pcm(pcmdesc);

	__spm_kick_im_to_fetch(pcmdesc);

	__spm_init_pcm_register();

	__spm_init_event_vector(pcmdesc);

	__spm_set_power_control(pwrctrl);

	__spm_set_wakeup_event(pwrctrl);

	__spm_kick_pcm_to_run(pwrctrl);

#ifdef CONFIG_MD32_SUPPORT
	write_md32_cfgreg((read_md32_cfgreg(0x2C) & ~0xFFFF) | 0xcafe, 0x2C);
#endif
	spm_trigger_wfi_for_dpidle(pwrctrl);

#ifdef CONFIG_MD32_SUPPORT
	write_md32_cfgreg((read_md32_cfgreg(0x2C) & ~0xFFFF) | 0xbeef, 0x2C);
#endif
	__spm_get_wakeup_status(&wakesta);

	__spm_clean_after_wakeup();

	mtk8250_request_to_wakeup();

	if (pwrctrl->enable_log)
		wr = __spm_output_wake_reason(&wakesta, pcmdesc, false);

RESTORE_IRQ:
	mt_cirq_flush();
	mt_cirq_disable();
	mt_irq_mask_restore(&mask);

	spin_unlock_irqrestore(&__spm_lock, flags);

	spm_dpidle_after_wfi();

	/* set PMIC WRAP table for normal power control */
	mt_cpufreq_set_pmic_phase(PMIC_WRAP_PHASE_NORMAL);

	return wr;
}

/*
 * cpu_pdn:
 *    true  = CPU dormant
 *    false = CPU standby
 * pwrlevel:
 *    0 = AXI is off
 *    1 = AXI is 26M
 * pwake_time:
 *    >= 0  = specific wakeup period
 */
int spm_go_to_sleep_dpidle(u32 spm_flags, u32 spm_data)
{
	u32 sec = 0;
#if 0
	int wd_ret;
#endif
	struct wake_status wakesta;
	unsigned long flags;
#if 0
	struct wd_api *wd_api;
#endif
	struct mtk_irq_mask mask;
	struct irq_desc *desc = irq_to_desc(spm_irq_0);
	static int last_wr = WR_NONE;
	struct pcm_desc *pcmdesc = __spm_dpidle.pcmdesc;
	struct pwr_ctrl *pwrctrl = __spm_dpidle.pwrctrl;

	set_pwrctrl_pcm_flags(pwrctrl, spm_flags);

#if SPM_PWAKE_EN
	sec = spm_get_wake_period(-1/* FIXME */, last_wr);
#endif
	pwrctrl->timer_val = sec * 32768;

	pwrctrl->wake_src = spm_get_sleep_wakesrc();

#if 0
	wd_ret = get_wd_api(&wd_api);
	if (!wd_ret)
		wd_api->wd_suspend_notify();
#endif
	spin_lock_irqsave(&__spm_lock, flags);
	mt_irq_mask_all(&mask);
	if (desc)
		unmask_irq(desc);
	mt_cirq_clone_gic();
	mt_cirq_enable();

	/* set PMIC WRAP table for deepidle power control */
	mt_cpufreq_set_pmic_phase(PMIC_WRAP_PHASE_DEEPIDLE);

	spm_crit2("sleep_deepidle, sec = %u, wakesrc = 0x%x [%u]\n",
		  sec, pwrctrl->wake_src, is_cpu_pdn(pwrctrl->pcm_flags));

	__spm_reset_and_init_pcm(pcmdesc);

	__spm_kick_im_to_fetch(pcmdesc);

	if (mtk8250_request_to_sleep()) {
		last_wr = WR_UART_BUSY;
		goto RESTORE_IRQ;
	}

	__spm_init_pcm_register();

	__spm_init_event_vector(pcmdesc);

	__spm_set_power_control(pwrctrl);

	__spm_set_wakeup_event(pwrctrl);

	__spm_kick_pcm_to_run(pwrctrl);

	spm_trigger_wfi_for_dpidle(pwrctrl);

	__spm_get_wakeup_status(&wakesta);

	__spm_clean_after_wakeup();

	mtk8250_request_to_wakeup();

	last_wr = __spm_output_wake_reason(&wakesta, pcmdesc, true);

RESTORE_IRQ:
	/* set PMIC WRAP table for normal power control */
	mt_cpufreq_set_pmic_phase(PMIC_WRAP_PHASE_NORMAL);
	mt_cirq_flush();
	mt_cirq_disable();
	mt_irq_mask_restore(&mask);

	spin_unlock_irqrestore(&__spm_lock, flags);

#if 0
	if (!wd_ret)
		wd_api->wd_resume_notify();
#endif
	return last_wr;
}

MODULE_DESCRIPTION("SPM-DPIdle Driver v0.1");
