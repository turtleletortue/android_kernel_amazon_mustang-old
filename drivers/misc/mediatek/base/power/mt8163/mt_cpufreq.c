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

/**
 * @file    mt_cpufreq.c
 * @brief   Driver for CPU DVFS
 *
 */

#define __MT_CPUFREQ_C__

/*=============================================================*/
/* Include files                                               */
/*=============================================================*/

/* system includes */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/cpu.h>
#ifdef CONFIG_AMAZON_THERMAL
#include <linux/cpu_cooling.h>
#endif
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#else
#include <linux/notifier.h>
#include <linux/fb.h>
#endif	/* L318_Need_Related_File */
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regulator/consumer.h>
#endif

/* project includes */
#include "mach/mt_freqhopping.h"
#include "mt_hotplug_strategy.h"
#include "mach/mt_thermal.h"
#include "mt_static_power.h"

#ifndef __KERNEL__
#include "freqhop_sw.h"
#include "mt_spm.h"
#else
#include "mt_spm.h"
#endif

/* local includes */
#include "mt_cpufreq.h"

#ifdef CONFIG_OF
static void __iomem *cpufreq_apmixed_base;
#define APMIXED_BASE     (cpufreq_apmixed_base)
#define ARMCA7PLL_CON1          (APMIXED_BASE + 0x214)

static void __iomem *infracfg_ao_base;
#define INFRACFG_AO_BASE     (infracfg_ao_base)
#define TOP_CKMUXSEL            (INFRACFG_AO_BASE + 0x00)
#define TOP_CKDIV1              (INFRACFG_AO_BASE + 0x08)

static void __iomem *clk_cksys_base;
#define CLK_CKSYS_BASE     (clk_cksys_base)
#define CLK_CFG_0               (CLK_CKSYS_BASE + 0x040)
#define CLK_MISC_CFG_0          (CLK_CKSYS_BASE + 0x104)

struct regulator *reg_ext_vproc;	/* sym827 */
#endif

int	regulator_ext_vproc_voltage;

/*=============================================================*/
/* Macro definition                                            */
/*=============================================================*/

/*
 * CONFIG
 */
#define CONFIG_CPU_DVFS_SHOWLOG 1
/* #define CONFIG_CPU_DVFS_BRINGUP 1 */         /* for bring up */
/* #define CONFIG_CPU_DVFS_FFTT_TEST 1 */       /* FF TT SS volt test */
/* #define CONFIG_CPU_DVFS_DOWNGRADE_FREQ 1 */  /* downgrade freq */
#define CONFIG_CPU_DVFS_POWER_THROTTLING  1     /* power throttling features */
#ifdef CONFIG_MTK_RAM_CONSOLE
#define CONFIG_CPU_DVFS_AEE_RR_REC 1            /* AEE SRAM debugging */
#endif

#define MAX(a, b) ((a) >= (b) ? (a) : (b))
#define MIN(a, b) ((a) >= (b) ? (b) : (a))

/* used @ set_cur_volt_extBuck() */
/* #define MIN_DIFF_VSRAM_PROC        1000  */   /* 10mv * 100 */
/* #define NORMAL_DIFF_VRSAM_VPROC    10000 */   /* 100mv * 100 */
/* #define MAX_DIFF_VSRAM_VPROC       20000 */   /* 200mv * 100 */
/* #define MIN_VSRAM_VOLT             93125 */   /* 931.25mv * 100 */
/* #define MAX_VSRAM_VOLT             115000*/   /* 1150mv * 100 */
#define MAX_VPROC_VOLT             1150000  /* 1150mv * 1000 */

 /* PMIC/PLL settle time (us), should not be changed */
#define PMIC_CMD_DELAY_TIME     5
#define MIN_PMIC_SETTLE_TIME    25
#define PMIC_VOLT_UP_SETTLE_TIME(old_volt, new_volt)   \
	(((((new_volt) - (old_volt)) + 1250 - 1) / 1250) + PMIC_CMD_DELAY_TIME)
#define PMIC_VOLT_DOWN_SETTLE_TIME(old_volt, new_volt)  \
	(((((old_volt) - (new_volt)) * 2)  / 625) + PMIC_CMD_DELAY_TIME)
#define PLL_SETTLE_TIME         (20)

/* RAMP DOWN TIMES to postpone frequency degrade */
#define RAMP_DOWN_TIMES         (2)
/* if cross 1105MHz when DFS, don't used FHCTL */
#define CPUFREQ_BOUNDARY_FOR_FHCTL   (CPU_DVFS_FREQ2)

#define DEFAULT_VOLT_VSRAM      (1050000)
#define DEFAULT_VOLT_VPROC      (1000000)
#define DEFAULT_VOLT_VGPU       (1000000)
#define DEFAULT_VOLT_VCORE      (1000000)
#define DEFAULT_VOLT_VLTE       (1000000)

/* for DVFS OPP table */

#define CPU_DVFS_SB_FREQ0 (1500000) /* KHz */
#define CPU_DVFS_SB_FREQ1 (1350000) /* KHz */
#define CPU_DVFS_SB_FREQ2 (1200000) /* KHz */
#define CPU_DVFS_SB_FREQ3 (1050000) /* KHz */
#define CPU_DVFS_SB_FREQ4 (871000) /* KHz */
#define CPU_DVFS_SB_FREQ5 (741000) /* KHz */
#define CPU_DVFS_SB_FREQ6 (624000) /* KHz */
#define CPU_DVFS_SB_FREQ7 (600000) /* KHz */

#define CPU_DVFS_FREQ0 (1300000) /* KHz */
#define CPU_DVFS_FREQ1 (1216000) /* KHz */
#define CPU_DVFS_FREQ2 (1133000) /* KHz */
#define CPU_DVFS_FREQ3 (1050000) /* KHz */
#define CPU_DVFS_FREQ4 (871000) /* KHz */
#define CPU_DVFS_FREQ5 (741000) /* KHz */
#define CPU_DVFS_FREQ6 (624000) /* KHz */
#define CPU_DVFS_FREQ7 (600000) /* KHz */

#define CPUFREQ_LAST_FREQ_LEVEL    (CPU_DVFS_FREQ7)

#ifdef CONFIG_CPU_DVFS_POWER_THROTTLING
#define PWR_THRO_MODE_LBAT_936MHZ	BIT(0)
#define PWR_THRO_MODE_BAT_PER_936MHZ	BIT(1)
#define PWR_THRO_MODE_BAT_OC_1170MHZ	BIT(2)
#define PWR_THRO_MODE_BAT_OC_1287MHZ	BIT(3)
#define PWR_THRO_MODE_BAT_OC_1417MHZ	BIT(4)
#endif

/*
 * LOG and Test
 */
#ifndef __KERNEL__ /* for CTP */
#define USING_XLOG
#else
/* #define USING_XLOG */
#endif

#define HEX_FMT "0x%08x"
#undef TAG

#define TAG     "[Power/cpufreq] "

#define cpufreq_err(fmt, args...)       \
	pr_err(TAG"[ERROR]"fmt, ##args)
#define cpufreq_warn(fmt, args...)      \
	pr_warn(TAG"[WARNING]"fmt, ##args)
#define cpufreq_info(fmt, args...)      \
	pr_warn(TAG""fmt, ##args)
#define cpufreq_dbg(fmt, args...)       \
	do {                                \
		if (func_lv_mask)           \
			cpufreq_info(fmt, ##args);     \
	} while (0)
#define cpufreq_ver(fmt, args...)       \
	do {                                \
		if (func_lv_mask)           \
			pr_debug(TAG""fmt, ##args);    \
	} while (0)

#define FUNC_LV_MODULE      BIT(0)  /* module, platform driver interface */
#define FUNC_LV_CPUFREQ     BIT(1)  /* cpufreq driver interface          */
#define FUNC_LV_API         BIT(2)  /* mt_cpufreq driver global function */
#define FUNC_LV_LOCAL       BIT(3)  /* mt_cpufreq driver local function  */
#define FUNC_LV_HELP        BIT(4)  /* mt_cpufreq driver help function   */

/* static unsigned int func_lv_mask = (FUNC_LV_MODULE | FUNC_LV_CPUFREQ
 * | FUNC_LV_API | FUNC_LV_LOCAL | FUNC_LV_HELP);
 */
static unsigned int func_lv_mask;
static unsigned int do_dvfs_stress_test;
static unsigned int is_fix_freq_in_ES = 1;

#ifdef CONFIG_CPU_DVFS_SHOWLOG
#define FUNC_ENTER(lv)          \
	do { if ((lv) & func_lv_mask) \
		cpufreq_dbg(">> %s()\n", __func__); } while (0)
#define FUNC_EXIT(lv)           \
	do { if ((lv) & func_lv_mask) \
		cpufreq_dbg("<< %s():%d\n", __func__, __LINE__); } while (0)
#else
#define FUNC_ENTER(lv)
#define FUNC_EXIT(lv)
#endif /* CONFIG_CPU_DVFS_SHOWLOG */

/*
 * BIT Operation
 */
#define _BIT_(_bit_)                    (unsigned int)(1 << (_bit_))
#define _BITS_(_bits_, _val_)           \
	((((unsigned int) -1 >> (31 - ((1) ? _bits_))) \
	& ~((1U << ((0) ? _bits_)) - 1)) & ((_val_)<<((0) ? _bits_)))
#define _BITMASK_(_bits_)             \
	(((unsigned int) -1 >> (31 - ((1) ? _bits_))) \
	& ~((1U << ((0) ? _bits_)) - 1))
#define _GET_BITS_VAL_(_bits_, _val_) \
	(((_val_) & (_BITMASK_(_bits_))) >> ((0) ? _bits_))

/*
 * REG ACCESS
 */
/* #define cpufreq_read(addr)				   DRV_Reg32(addr) */
#define cpufreq_read(addr)                  __raw_readl(addr)
#define cpufreq_write(addr, val)            mt_reg_sync_writel(val, addr)
#define cpufreq_write_mask(addr, mask, val) \
	cpufreq_write(addr, (cpufreq_read(addr) \
	& ~(_BITMASK_(mask))) | _BITS_(mask, val))


/*=============================================================*/
/* Local type definition                                       */
/*=============================================================*/


/*=============================================================*/
/* Local variable definition                                   */
/*=============================================================*/


/*=============================================================*/
/* Local function definition                                   */
/*=============================================================*/


/*=============================================================*/
/* Gobal function definition                                   */
/*=============================================================*/

/*
 * LOCK
 */
static DEFINE_MUTEX(cpufreq_mutex);
bool is_in_cpufreq;
#define cpufreq_lock(flags) \
do { \
	/* to fix compile warning */  \
	flags = (unsigned long)&flags; \
	mutex_lock(&cpufreq_mutex); \
	is_in_cpufreq = 1;\
} while (0)

#define cpufreq_unlock(flags) \
do { \
	/* to fix compile warning */  \
	flags = (unsigned long)&flags; \
	is_in_cpufreq = 0;\
	mutex_unlock(&cpufreq_mutex); \
} while (0)

/*
 * EFUSE
 */
#define CPUFREQ_EFUSE_INDEX     (3)
#define FUNC_CODE_EFUSE_INDEX	(28)

#define	EFUSE_BIT0	(1 << 0)
#define	EFUSE_BIT12	(1 << 12)

#define CPU_LEVEL_0             (0x0)
#define CPU_LEVEL_1             (0x1)

#define CPU_LV_TO_OPP_IDX(lv)   ((lv)) /* cpu_level to opp_idx */
unsigned int AllowTurboMode;

#ifdef __KERNEL__
static unsigned int _mt_cpufreq_get_cpu_level(void)
{
	unsigned int lv = 0;

	unsigned int cpu_speed_bounding
	= get_devinfo_with_index(FUNC_CODE_EFUSE_INDEX);

	cpufreq_info("No DT, get CPU frequency bounding from efuse = %x\n"
		     , cpu_speed_bounding);

	lv = CPU_LEVEL_1; /* 1.3G */
	if (cpu_speed_bounding & EFUSE_BIT12) {
		if (cpu_speed_bounding & EFUSE_BIT0)
			lv = CPU_LEVEL_1; /* 1.3G */
		else
			lv = CPU_LEVEL_0; /* 1.5G */
		cpufreq_info("No DT, get CPU frequency bounding ");
		cpufreq_info("lv = %d from efuse = %x\n"
			, lv, cpu_speed_bounding);
	} else {
		cpufreq_err("No suitable DVFS table, ");
		cpufreq_err("set to default CPU level! efuse=0x%x\n"
			, cpu_speed_bounding);
	}
	cpufreq_info("current CPU efuse is %d, AllowTurboMode=%d\n"
		, cpu_speed_bounding, AllowTurboMode);

	return lv;
}
#else
static unsigned int _mt_cpufreq_get_cpu_level(void)
{
	return CPU_LEVEL_1;
}
#endif


/*
 * AEE (SRAM debug)
 */
#ifdef CONFIG_CPU_DVFS_AEE_RR_REC
enum cpu_dvfs_state {
	CPU_DVFS_LITTLE_IS_DOING_DVFS = 0,
	CPU_DVFS_LITTLE_IS_TURBO,
};

static void _mt_cpufreq_aee_init(void)
{
	aee_rr_rec_cpu_dvfs_vproc_big(0xFF);
	aee_rr_rec_cpu_dvfs_vproc_little(0xFF);
	aee_rr_rec_cpu_dvfs_oppidx(0xFF);
	aee_rr_rec_cpu_dvfs_status(0xFF);
}
#endif


/*
 * PMIC_WRAP
 */
/* TODO: defined @ pmic head file??? */
#define VOLT_TO_PMIC_VAL(volt)  (((volt) - 700000 + 6250 - 1) / 6250)
#define PMIC_VAL_TO_VOLT(pmic)  (((pmic) * 6250) + 700000)

/* (((((volt) - 300) + 9) / 10) & 0x7F) */
#define VOLT_TO_EXTBUCK_VAL(volt)   VOLT_TO_PMIC_VAL(volt)
/* (300 + ((val) & 0x7F) * 10) */
#define EXTBUCK_VAL_TO_VOLT(val)    PMIC_VAL_TO_VOLT(val)

void __iomem *pwrap_base;
#define PWRAP_BASE_ADDR     ((unsigned long)pwrap_base)

#define PMIC_WRAP_DVFS_ADR0     (PWRAP_BASE_ADDR + 0x0E8)
#define PMIC_WRAP_DVFS_WDATA0   (PWRAP_BASE_ADDR + 0x0EC)
#define PMIC_WRAP_DVFS_ADR1     (PWRAP_BASE_ADDR + 0x0F0)
#define PMIC_WRAP_DVFS_WDATA1   (PWRAP_BASE_ADDR + 0x0F4)
#define PMIC_WRAP_DVFS_ADR2     (PWRAP_BASE_ADDR + 0x0F8)
#define PMIC_WRAP_DVFS_WDATA2   (PWRAP_BASE_ADDR + 0x0FC)
#define PMIC_WRAP_DVFS_ADR3     (PWRAP_BASE_ADDR + 0x100)
#define PMIC_WRAP_DVFS_WDATA3   (PWRAP_BASE_ADDR + 0x104)
#define PMIC_WRAP_DVFS_ADR4     (PWRAP_BASE_ADDR + 0x108)
#define PMIC_WRAP_DVFS_WDATA4   (PWRAP_BASE_ADDR + 0x10C)
#define PMIC_WRAP_DVFS_ADR5     (PWRAP_BASE_ADDR + 0x110)
#define PMIC_WRAP_DVFS_WDATA5   (PWRAP_BASE_ADDR + 0x114)
#define PMIC_WRAP_DVFS_ADR6     (PWRAP_BASE_ADDR + 0x118)
#define PMIC_WRAP_DVFS_WDATA6   (PWRAP_BASE_ADDR + 0x11C)
#define PMIC_WRAP_DVFS_ADR7     (PWRAP_BASE_ADDR + 0x120)
#define PMIC_WRAP_DVFS_WDATA7   (PWRAP_BASE_ADDR + 0x124)

/* PMIC ADDR */ /* TODO: include other head file */
#define PMIC_ADDR_VPROC_VOSEL_ON     0x04BA  /* [6:0]                    */
#define PMIC_ADDR_VPROC_VOSEL        0x04B8  /* [6:0]                    */
#define PMIC_ADDR_VPROC_VOSEL_CTRL   0x04B0  /* [1]                      */
#define PMIC_ADDR_VPROC_EN           0x04B4  /* [0] (shared with others) */
#define PMIC_ADDR_VSRAM_VOSEL_ON     0x0506  /* [6:0]                    */
#define PMIC_ADDR_VSRAM_VOSEL        0x0A58  /* [15:9]                   */
#define PMIC_ADDR_VSRAM_VOSEL_CTRL   0x04FC  /* [1]                      */
#define PMIC_ADDR_VSRAM_EN           0x0A34  /* [1] (shared with others) */
#define PMIC_ADDR_VSRAM_FAST_TRSN_EN 0x0A62  /* [8]                      */
#define PMIC_ADDR_VGPU_VOSEL_ON      0x0618  /* [6:0]                    */
#define PMIC_ADDR_VCORE_VOSEL_ON     0x0220  /* [6:0]                    */
#define PMIC_ADDR_VLTE_VOSEL_ON      0x063E  /* [6:0]                    */

#define NR_PMIC_WRAP_CMD 8 /* num of pmic wrap cmd (fixed value) */

struct pmic_wrap_cmd {
	unsigned long cmd_addr;
	unsigned long cmd_wdata;
};

struct pmic_wrap_setting {
	enum pmic_wrap_phase_id phase;
	struct pmic_wrap_cmd addr[NR_PMIC_WRAP_CMD];
	struct {
	struct {
		unsigned long cmd_addr;
		unsigned long cmd_wdata;
	} _[NR_PMIC_WRAP_CMD];
	const int nr_idx;
	} set[NR_PMIC_WRAP_PHASE];
};

static struct pmic_wrap_setting pw = {
	.phase = NR_PMIC_WRAP_PHASE, /* invalid setting for init */
	.addr = { {0, 0} },
	.set[PMIC_WRAP_PHASE_NORMAL] = {
		._[IDX_NM_VSRAM]
		= { PMIC_ADDR_VSRAM_VOSEL_ON
		, VOLT_TO_PMIC_VAL(DEFAULT_VOLT_VSRAM), },
		._[IDX_NM_VPROC]
		= { PMIC_ADDR_VPROC_VOSEL_ON
		, VOLT_TO_PMIC_VAL(DEFAULT_VOLT_VPROC), },
		._[IDX_NM_VGPU]
		= { PMIC_ADDR_VGPU_VOSEL_ON
		,  VOLT_TO_PMIC_VAL(DEFAULT_VOLT_VGPU),  },
		._[IDX_NM_VCORE]
		= { PMIC_ADDR_VCORE_VOSEL_ON
		, VOLT_TO_PMIC_VAL(DEFAULT_VOLT_VCORE), },
		._[IDX_NM_VLTE]
		= { PMIC_ADDR_VLTE_VOSEL_ON
		,  VOLT_TO_PMIC_VAL(DEFAULT_VOLT_VLTE),  },
		.nr_idx = NR_IDX_NM,
	},

	.set[PMIC_WRAP_PHASE_SUSPEND] = {
		._[IDX_SP_VPROC_PWR_ON]
		= { PMIC_ADDR_VPROC_EN
		, _BITS_(0  : 0,	1),	  },
		._[IDX_SP_VPROC_SHUTDOWN]
		= { PMIC_ADDR_VPROC_EN
		, _BITS_(0  : 0,	0),	  },
		._[IDX_SP_VSRAM_PWR_ON]
		= { PMIC_ADDR_VSRAM_EN
		, _BITS_(1  : 1,	1),	  },
		._[IDX_SP_VSRAM_SHUTDOWN]
		= { PMIC_ADDR_VSRAM_EN
		, _BITS_(1  : 1,	0),	  },
		._[IDX_SP_VCORE_NORMAL]
		= { PMIC_ADDR_VCORE_VOSEL_ON
		, VOLT_TO_PMIC_VAL(1150000),},
		._[IDX_SP_VCORE_SLEEP]
		= { PMIC_ADDR_VCORE_VOSEL_ON
		, VOLT_TO_PMIC_VAL(1050000),},
		._[IDX_SP_VPROC_NORMAL]
		= { PMIC_ADDR_VPROC_VOSEL_CTRL
		, _BITS_(1  : 1,	1)		  },
		._[IDX_SP_VPROC_SLEEP]
		= { PMIC_ADDR_VPROC_VOSEL_CTRL
		, _BITS_(1  : 1,	0)		  },
		.nr_idx = NR_IDX_SP,
	},

	.set[PMIC_WRAP_PHASE_DEEPIDLE] = {
		._[IDX_DI_VPROC_NORMAL]
		= { PMIC_ADDR_VPROC_VOSEL_CTRL
		, _BITS_(1  : 1,  1),	   },
		._[IDX_DI_VPROC_SLEEP]
		= { PMIC_ADDR_VPROC_VOSEL_CTRL
		, _BITS_(1  : 1,  0),	   },
		._[IDX_DI_VSRAM_NORMAL]
		= { PMIC_ADDR_VSRAM_VOSEL_CTRL
		, _BITS_(1  : 1,  1),	   },
		._[IDX_DI_VSRAM_SLEEP]
		= { PMIC_ADDR_VSRAM_VOSEL_CTRL
		, _BITS_(1  : 1,  0),	   },
		._[IDX_DI_VCORE_NORMAL]
		= { PMIC_ADDR_VCORE_VOSEL_ON
		, VOLT_TO_PMIC_VAL(1150000),},
		._[IDX_DI_VCORE_SLEEP]
		= { PMIC_ADDR_VCORE_VOSEL_ON
		, VOLT_TO_PMIC_VAL(1050000),},
		._[IDX_DI_VSRAM_FAST_TRSN_DIS]
		= { PMIC_ADDR_VSRAM_FAST_TRSN_EN
		, _BITS_(8  : 8,  1),	   },
		._[IDX_DI_VSRAM_FAST_TRSN_EN]
		= { PMIC_ADDR_VSRAM_FAST_TRSN_EN
		, _BITS_(8  : 8,  0),	   },
		.nr_idx = NR_IDX_DI,
	},

	.set[PMIC_WRAP_PHASE_SODI] = {
		._[IDX_DI_VPROC_NORMAL]
		= { PMIC_ADDR_VPROC_VOSEL_CTRL
		, _BITS_(1  : 1,  1),	   },
		._[IDX_DI_VPROC_SLEEP]
		= { PMIC_ADDR_VPROC_VOSEL_CTRL
		, _BITS_(1  : 1,  0),	   },
		._[IDX_DI_VSRAM_NORMAL]
		= { PMIC_ADDR_VSRAM_VOSEL_CTRL
		, _BITS_(1  : 1,  1),	   },
		._[IDX_DI_VSRAM_SLEEP]
		= { PMIC_ADDR_VSRAM_VOSEL_CTRL
		, _BITS_(1  : 1,  0),	   },
		._[IDX_DI_VCORE_NORMAL]
		= { PMIC_ADDR_VCORE_VOSEL_ON
		, VOLT_TO_PMIC_VAL(1150000),},
		._[IDX_DI_VCORE_SLEEP]
		= { PMIC_ADDR_VCORE_VOSEL_ON
		, VOLT_TO_PMIC_VAL(1050000),},
		._[IDX_DI_VSRAM_FAST_TRSN_DIS]
		= { PMIC_ADDR_VSRAM_FAST_TRSN_EN
		, _BITS_(8  : 8,  1),	   },
		._[IDX_DI_VSRAM_FAST_TRSN_EN]
		= { PMIC_ADDR_VSRAM_FAST_TRSN_EN
		, _BITS_(8  : 8,  0),	   },
		.nr_idx = NR_IDX_DI,
	},
};

static DEFINE_SPINLOCK(pmic_wrap_lock);
#define pmic_wrap_lock(flags) spin_lock_irqsave(&pmic_wrap_lock, flags)
#define pmic_wrap_unlock(flags) spin_unlock_irqrestore(&pmic_wrap_lock, flags)

static int _spm_dvfs_ctrl_volt(u32 value)
{
	#define MAX_RETRY_COUNT (100)

	u32 ap_dvfs_con;
	int retry = 0;

	FUNC_ENTER(FUNC_LV_HELP);

	spm_write(SPM_POWERON_CONFIG_SET
		  , (SPM_PROJECT_CODE << 16) | (1U << 0));

	ap_dvfs_con = spm_read(SPM_AP_DVFS_CON_SET);
	spm_write(SPM_AP_DVFS_CON_SET, (ap_dvfs_con & ~(0x7)) | value);
	udelay(5);

	while ((spm_read(SPM_AP_DVFS_CON_SET) & (0x1 << 31)) == 0) {
		if (retry >= MAX_RETRY_COUNT) {
			cpufreq_err("FAIL: no response from PMIC wrapper\n");
			return -1;
		}

		retry++;

		udelay(5);
	}

	FUNC_EXIT(FUNC_LV_HELP);

	return 0;
}

/* Turbo Dram/GPU B */
#define GPU800MHz_DRAM1866MHz_MASK	0x80000000
int	GPU800MHz_DRAM1866MHz_Flag;
/* Turbo Dram/GPU E */

void _mt_cpufreq_pmic_table_init(void)
{
	struct pmic_wrap_cmd pwrap_cmd_default[NR_PMIC_WRAP_CMD] = {
	{ PMIC_WRAP_DVFS_ADR0, PMIC_WRAP_DVFS_WDATA0, },
	{ PMIC_WRAP_DVFS_ADR1, PMIC_WRAP_DVFS_WDATA1, },
	{ PMIC_WRAP_DVFS_ADR2, PMIC_WRAP_DVFS_WDATA2, },
	{ PMIC_WRAP_DVFS_ADR3, PMIC_WRAP_DVFS_WDATA3, },
	{ PMIC_WRAP_DVFS_ADR4, PMIC_WRAP_DVFS_WDATA4, },
	{ PMIC_WRAP_DVFS_ADR5, PMIC_WRAP_DVFS_WDATA5, },
	{ PMIC_WRAP_DVFS_ADR6, PMIC_WRAP_DVFS_WDATA6, },
	{ PMIC_WRAP_DVFS_ADR7, PMIC_WRAP_DVFS_WDATA7, },
	};

	FUNC_ENTER(FUNC_LV_HELP);

	memcpy(pw.addr, pwrap_cmd_default, sizeof(pwrap_cmd_default));

	/* Turbo Dram/GPU B */
	GPU800MHz_DRAM1866MHz_Flag
		= get_devinfo_with_index(CPUFREQ_EFUSE_INDEX)
		  & GPU800MHz_DRAM1866MHz_MASK;
	cpufreq_info("[GPU/DRAM][Turbo], GPU800MHz_DRAM1866MHz_Flag=%d\n"
		     , GPU800MHz_DRAM1866MHz_Flag);
	if (GPU800MHz_DRAM1866MHz_Flag) {
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_DEEPIDLE, 0, 112500);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_DEEPIDLE, 1, 112500);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_DEEPIDLE, 2, 108750);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_DEEPIDLE, 3, 105000);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_DEEPIDLE, 4, 101250);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_DEEPIDLE, 5,  97500);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_DEEPIDLE, 6,  93750);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_DEEPIDLE, 7,  90000);

		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_SUSPEND, 0, 112500);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_SUSPEND, 1, 112500);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_SUSPEND, 2, 108750);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_SUSPEND, 3, 105000);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_SUSPEND, 4, 101250);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_SUSPEND, 5,  97500);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_SUSPEND, 6,  93750);
		mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_SUSPEND, 7,  90000);
	}
	/* Turbo Dram/GPU E */

	FUNC_EXIT(FUNC_LV_HELP);
}

void mt_cpufreq_set_pmic_phase(enum pmic_wrap_phase_id phase)
{
	int i;
	unsigned long flags;

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(phase >= NR_PMIC_WRAP_PHASE);

	if (pw.addr[0].cmd_addr == 0) {
		cpufreq_warn("pmic table not initialized\n");
		_mt_cpufreq_pmic_table_init();
	}

	pmic_wrap_lock(flags);

	pw.phase = phase;

	for (i = 0; i < pw.set[phase].nr_idx; i++) {
		cpufreq_write(pw.addr[i].cmd_addr
			, pw.set[phase]._[i].cmd_addr);
		cpufreq_write(pw.addr[i].cmd_wdata
			, pw.set[phase]._[i].cmd_wdata);
	}

	pmic_wrap_unlock(flags);

	FUNC_EXIT(FUNC_LV_API);
}
EXPORT_SYMBOL(mt_cpufreq_set_pmic_phase);

/* just set wdata value */
void mt_cpufreq_set_pmic_cmd(enum pmic_wrap_phase_id phase
	, int idx, unsigned int cmd_wdata)
{
	unsigned long flags;

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(phase >= NR_PMIC_WRAP_PHASE);
	WARN_ON(idx >= pw.set[phase].nr_idx);

	/* Turbo Dram/GPU B */
	if (GPU800MHz_DRAM1866MHz_Flag) {
		if ((phase == PMIC_WRAP_PHASE_NORMAL)
			&& (idx == IDX_NM_VCORE)) {
			if (PMIC_VAL_TO_VOLT(cmd_wdata) > 1050000)
				cmd_wdata = VOLT_TO_PMIC_VAL(1050000);
		}
	}

	pmic_wrap_lock(flags);

	pw.set[phase]._[idx].cmd_wdata = cmd_wdata;

	if (pw.phase == phase)
		cpufreq_write(pw.addr[idx].cmd_wdata, cmd_wdata);

	pmic_wrap_unlock(flags);

	FUNC_EXIT(FUNC_LV_API);
}
EXPORT_SYMBOL(mt_cpufreq_set_pmic_cmd);

void mt_cpufreq_apply_pmic_cmd(int idx) /* kick spm */
{
	unsigned long flags;

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(idx >= pw.set[pw.phase].nr_idx);

	/* cpufreq_dbg("@%s: idx = %d\n", __func__, idx); */

	pmic_wrap_lock(flags);

	_spm_dvfs_ctrl_volt(idx);

	pmic_wrap_unlock(flags);

	FUNC_EXIT(FUNC_LV_API);
}
EXPORT_SYMBOL(mt_cpufreq_apply_pmic_cmd);

/* cpu voltage sampler */
static cpuVoltsampler_func g_pCpuVoltSampler;

void mt_cpufreq_setvolt_registerCB(cpuVoltsampler_func pCB)
{
	g_pCpuVoltSampler = pCB;
}
EXPORT_SYMBOL(mt_cpufreq_setvolt_registerCB);

/* empty function */
void mt_vcore_dvfs_disable_by_sdio(unsigned int type, bool disabled) {}
void mt_vcore_dvfs_volt_set_by_sdio(unsigned int volt) {}
unsigned int mt_vcore_dvfs_volt_get_by_sdio(void){return 0; }

/*
 * mt_cpufreq driver
 */
 #define OP(khz, volt) {            \
	.cpufreq_khz = khz,             \
	.cpufreq_volt = volt,           \
	.cpufreq_volt_org = volt,       \
}

#define for_each_cpu_dvfs(i, p)    \
	for (i = 0, p = cpu_dvfs; i < NR_MT_CPU_DVFS; i++, p = &cpu_dvfs[i])
#define cpu_dvfs_is(p, id)                 (p == &cpu_dvfs[id])
#define cpu_dvfs_is_available(p)      (p->opp_tbl)
#define cpu_dvfs_get_name(p)         (p->name)

#define cpu_dvfs_get_cur_freq(p)         \
	(p->opp_tbl[p->idx_opp_tbl].cpufreq_khz)
#define cpu_dvfs_get_freq_by_idx(p, idx) \
	(p->opp_tbl[idx].cpufreq_khz)
#define cpu_dvfs_get_max_freq(p)         \
	(p->opp_tbl[0].cpufreq_khz)
#define cpu_dvfs_get_normal_max_freq(p)  \
	(p->opp_tbl[p->idx_normal_max_opp].cpufreq_khz)
#define cpu_dvfs_get_min_freq(p)         \
	(p->opp_tbl[p->nr_opp_tbl - 1].cpufreq_khz)

#define cpu_dvfs_get_cur_volt(p)         \
	(p->opp_tbl[p->idx_opp_tbl].cpufreq_volt)
#define cpu_dvfs_get_volt_by_idx(p, idx) \
	(p->opp_tbl[idx].cpufreq_volt)

struct mt_cpu_freq_info {
	const unsigned int cpufreq_khz;
	unsigned int cpufreq_volt;  /* mv * 1000 */
	const unsigned int cpufreq_volt_org;    /* mv * 1000 */
};

struct mt_cpu_power_info {
	unsigned int cpufreq_khz;
	unsigned int cpufreq_ncpu;
	unsigned int cpufreq_power;
};

struct mt_cpu_dvfs {
	const char *name;
	unsigned int cpu_id;	                /* for cpufreq */
	unsigned int cpu_level;
	struct mt_cpu_dvfs_ops *ops;

	/* opp (freq) table */
	struct mt_cpu_freq_info *opp_tbl;/* OPP table */
	int nr_opp_tbl;                  /* size for OPP table */
	int idx_opp_tbl;                 /* current OPP idx */
	int idx_normal_max_opp;          /* idx for normal max OPP */
	int idx_opp_tbl_for_late_resume; /* keep the setting for late resume */

	/* freq table for cpufreq */
	struct cpufreq_frequency_table *freq_tbl_for_cpufreq;

	/* power table */
	struct mt_cpu_power_info *power_tbl;
	unsigned int nr_power_tbl;

	/* enable/disable DVFS function */
	int dvfs_disable_count;
	bool dvfs_disable_by_ptpod;
	bool dvfs_disable_by_suspend;
	bool dvfs_disable_by_early_suspend;
	bool dvfs_disable_by_procfs;

	/* limit for thermal */
	unsigned int limited_max_ncpu;
	unsigned int limited_max_freq;
	unsigned int idx_opp_tbl_for_thermal_thro;
	unsigned int thermal_protect_limited_power;

	/* limit for HEVC (via. sysfs) */
	unsigned int limited_freq_by_hevc;

	/* limit max freq from user */
	unsigned int limited_max_freq_by_user;

	/* for ramp down */
	int ramp_down_count;
	int ramp_down_count_const;

#ifdef CONFIG_CPU_DVFS_DOWNGRADE_FREQ
	/* param for micro throttling */
	bool downgrade_freq_for_ptpod;
#endif

	int over_max_cpu;
	int ptpod_temperature_limit_1;
	int ptpod_temperature_limit_2;
	int ptpod_temperature_time_1;
	int ptpod_temperature_time_2;

	int pre_online_cpu;
	unsigned int pre_freq;
	unsigned int downgrade_freq;

	unsigned int downgrade_freq_counter;
	unsigned int downgrade_freq_counter_return;

	unsigned int downgrade_freq_counter_limit;
	unsigned int downgrade_freq_counter_return_limit;

	/* turbo mode */
	unsigned int turbo_mode;

	/* power throttling */
#ifdef CONFIG_CPU_DVFS_POWER_THROTTLING
	/* keep the setting for power throttling */
	int idx_opp_tbl_for_pwr_thro;
	/* idx for power throttle max OPP */
	int idx_pwr_thro_max_opp;
	unsigned int pwr_thro_mode;
#endif
#ifdef CONFIG_AMAZON_THERMAL
	struct thermal_cooling_device *cdev;
#endif
};

struct mt_cpu_dvfs_ops {
	/* for thermal */
	/* set power limit by thermal */
	void (*protect)(struct mt_cpu_dvfs *p, unsigned int limited_power);
	/* return temperature         */ /* TODO: necessary??? */
	unsigned int (*get_temp)(struct mt_cpu_dvfs *p);
	int (*setup_power_table)(struct mt_cpu_dvfs *p);

	/* for freq change (PLL/MUX) */
	/* return (physical) freq (KHz) */
	unsigned int (*get_cur_phy_freq)(struct mt_cpu_dvfs *p);
	void (*set_cur_freq)(struct mt_cpu_dvfs *p, unsigned int cur_khz
		, unsigned int target_khz); /* set freq  */

	/* for volt change (PMICWRAP/extBuck) */
	/* return volt (mV * 1000) */
	unsigned int (*get_cur_volt)(struct mt_cpu_dvfs *p);
	/* set volt (mv * 1000), return 0 (success), -1 (fail) */
	int (*set_cur_volt)(struct mt_cpu_dvfs *p, unsigned int volt);
};


/* for thermal */
static int setup_power_table(struct mt_cpu_dvfs *p);

/* for freq change (PLL/MUX) */
static unsigned int get_cur_phy_freq(struct mt_cpu_dvfs *p);
static void set_cur_freq(struct mt_cpu_dvfs *p, unsigned int cur_khz
	, unsigned int target_khz);

/* for volt change (PMICWRAP/extBuck) */
static unsigned int get_cur_volt_extbuck(struct mt_cpu_dvfs *p);
/* volt: mv * 100 */
static int set_cur_volt_extbuck(struct mt_cpu_dvfs *p, unsigned int volt);

/* for limited_max_ncpu,
 * it will be modified at driver initialization stage if needed
 */
static unsigned int max_cpu_num = 8;

static struct mt_cpu_dvfs_ops dvfs_ops_extbuck = {
	.setup_power_table = setup_power_table,

	.get_cur_phy_freq = get_cur_phy_freq,
	.set_cur_freq = set_cur_freq,

	.get_cur_volt = get_cur_volt_extbuck,
	.set_cur_volt = set_cur_volt_extbuck,
};


static struct mt_cpu_dvfs cpu_dvfs[] = {
	[MT_CPU_DVFS_LITTLE]    = {
	.name                           = __stringify(MT_CPU_DVFS_LITTLE),
	.cpu_id                         = MT_CPU_DVFS_LITTLE,
	.cpu_level                    = CPU_LEVEL_1,  /* 1.7GHz */
	.ops                            = &dvfs_ops_extbuck,

	/* TODO: check the following settings */
	.over_max_cpu                   = 8, /* 4 */
	.ptpod_temperature_limit_1      = 110000,
	.ptpod_temperature_limit_2      = 120000,
	.ptpod_temperature_time_1       = 1,
	.ptpod_temperature_time_2       = 4,
	.pre_online_cpu                 = 0,
	.pre_freq                       = 0,
	.downgrade_freq                 = 0,
	.downgrade_freq_counter         = 0,
	.downgrade_freq_counter_return  = 0,
	.downgrade_freq_counter_limit   = 0,
	.downgrade_freq_counter_return_limit = 0,

	.ramp_down_count_const		= RAMP_DOWN_TIMES,

	.turbo_mode			= 0,
#ifdef CONFIG_CPU_DVFS_POWER_THROTTLING
	.idx_opp_tbl_for_pwr_thro	= -1,
	.idx_pwr_thro_max_opp = 0,
#endif
	},
};


static struct mt_cpu_dvfs *id_to_cpu_dvfs(enum mt_cpu_dvfs_id id)
{
	return (id < NR_MT_CPU_DVFS) ? &cpu_dvfs[id] : NULL;
}


/* DVFS OPP table */
/* Notice: Each table MUST has 8 element to avoid ptpod error */

#define NR_MAX_OPP_TBL  8
#define NR_MAX_CPU      8

/* CPU LEVEL 0, SB, 1.5GHz segment */
static struct mt_cpu_freq_info opp_tbl_e1_0[] = {
	OP(CPU_DVFS_SB_FREQ0, 1300000),
	OP(CPU_DVFS_SB_FREQ1, 1250000),
	OP(CPU_DVFS_SB_FREQ2, 1200000),
	OP(CPU_DVFS_SB_FREQ3, 1150000),
	OP(CPU_DVFS_SB_FREQ4, 1150000),
	OP(CPU_DVFS_SB_FREQ5, 1150000),
	OP(CPU_DVFS_SB_FREQ6, 1150000),
	OP(CPU_DVFS_SB_FREQ7, 1150000),
};

/* CPU LEVEL 1, FY, 1.3GHz segment */
static struct mt_cpu_freq_info opp_tbl_e1_1[] = {
	OP(CPU_DVFS_FREQ0, 1300000),
	OP(CPU_DVFS_FREQ1, 1250000),
	OP(CPU_DVFS_FREQ2, 1200000),
	OP(CPU_DVFS_FREQ3, 1150000),
	OP(CPU_DVFS_FREQ4, 1150000),
	OP(CPU_DVFS_FREQ5, 1150000),
	OP(CPU_DVFS_FREQ6, 1150000),
	OP(CPU_DVFS_FREQ7, 1150000),
};

struct opp_tbl_info {
	struct mt_cpu_freq_info *const opp_tbl;
	const int size;
};

static struct opp_tbl_info opp_tbls[] = {
	[CPU_LV_TO_OPP_IDX(CPU_LEVEL_0)]
		= { opp_tbl_e1_0, ARRAY_SIZE(opp_tbl_e1_0)},
	[CPU_LV_TO_OPP_IDX(CPU_LEVEL_1)]
	= { opp_tbl_e1_1, ARRAY_SIZE(opp_tbl_e1_1)},
};

/* for freq change (PLL/MUX) */
#define PLL_FREQ_STEP		(13000)		/* KHz */

/* #define PLL_MAX_FREQ		(1989000)	*//* KHz */
#define PLL_MIN_FREQ		(130000)	/* KHz */
#define PLL_DIV1_FREQ		(1001000)	/* KHz */
#define PLL_DIV2_FREQ		(520000)	/* KHz */
#define PLL_DIV4_FREQ		(260000)	/* KHz */
#define PLL_DIV8_FREQ		(PLL_MIN_FREQ)	/* KHz */

#define DDS_DIV1_FREQ		(0x0009A000)	/* 1001MHz */
#define DDS_DIV2_FREQ		(0x010A0000)	/* 520MHz  */
#define DDS_DIV4_FREQ		(0x020A0000)	/* 260MHz  */
#define DDS_DIV8_FREQ		(0x030A0000)	/* 130MHz  */

/* for turbo mode */
#define TURBO_MODE_BOUNDARY_CPU_NUM	2

/* idx sort by temp from low to high */
enum turbo_mode {
	TURBO_MODE_2,
	TURBO_MODE_1,
	TURBO_MODE_NONE,

	NR_TURBO_MODE,
};

/* idx sort by temp from low to high */
struct turbo_mode_cfg {
	int temp;       /* degree x 1000 */
	int freq_delta; /* percentage    */
	int volt_delta; /* mv * 1000       */
} turbo_mode_cfg[] = {
	[TURBO_MODE_2] = {
	.temp = 65000,
	.freq_delta = 10,
	.volt_delta = 40000,
	},
	[TURBO_MODE_1] = {
	.temp = 85000,
	.freq_delta = 5,
	.volt_delta = 20000,
	},
	[TURBO_MODE_NONE] = {
	.temp = 125000,
	.freq_delta = 0,
	.volt_delta = 0,
	},
};

#define TURBO_MODE_FREQ(mode, freq) \
	(((freq * (100 + turbo_mode_cfg[mode].freq_delta)) \
	/ PLL_FREQ_STEP) / 100 * PLL_FREQ_STEP)
#define TURBO_MODE_VOLT(mode, volt) (volt + turbo_mode_cfg[mode].volt_delta)

static unsigned int num_online_cpus_delta;
static bool is_in_turbo_mode;

static enum turbo_mode get_turbo_mode(struct mt_cpu_dvfs *p
	, unsigned int target_khz)
{
	enum turbo_mode mode = TURBO_MODE_NONE;
	#ifdef CONFIG_THERMAL
	int temp = tscpu_get_temp_by_bank(THERMAL_BANK0);    /* bank0 for CPU */
	#else
	int temp = 0;
	#endif
	unsigned int online_cpus = num_online_cpus() + num_online_cpus_delta;
	int i;

	if (p->turbo_mode
		&& target_khz == cpu_dvfs_get_freq_by_idx(p, 0)
		&& online_cpus <= TURBO_MODE_BOUNDARY_CPU_NUM) {
		for (i = 0; i < NR_TURBO_MODE; i++) {
			if (temp < turbo_mode_cfg[i].temp) {
				mode = i;
				break;
			}
		}
	}

	cpufreq_ver("%s(), mode = %d, temp = %d, target_khz = %d ",
		__func__,
		mode,
		temp,
		target_khz);
	cpufreq_ver("(%d), num_online_cpus = %d\n",
		TURBO_MODE_FREQ(mode, target_khz),
		online_cpus);

	return mode;
}

/* for PTP-OD */
static int _set_cur_volt_locked(struct mt_cpu_dvfs *p, unsigned int volt)
{
	int ret = -1;

	FUNC_ENTER(FUNC_LV_HELP);

	WARN_ON(p == NULL);

	if (!cpu_dvfs_is_available(p)) {
		FUNC_EXIT(FUNC_LV_HELP);
		return 0;
	}

	/* set volt */
	ret = p->ops->set_cur_volt(p, volt);

	FUNC_EXIT(FUNC_LV_HELP);

	return ret;
}

static int _restore_default_volt(struct mt_cpu_dvfs *p)
{
	unsigned long flags;
	int i;
	int ret = -1;

	FUNC_ENTER(FUNC_LV_HELP);

	WARN_ON(p == NULL);

	if (!cpu_dvfs_is_available(p)) {
		FUNC_EXIT(FUNC_LV_HELP);
		return 0;
	}
	cpufreq_lock(flags);

	/* restore to default volt */
	for (i = 0; i < p->nr_opp_tbl; i++)
		p->opp_tbl[i].cpufreq_volt = p->opp_tbl[i].cpufreq_volt_org;

	/* set volt */
	ret = _set_cur_volt_locked(p,
		   TURBO_MODE_VOLT(get_turbo_mode(p, cpu_dvfs_get_cur_freq(p)),
		   cpu_dvfs_get_cur_volt(p)));

	cpufreq_unlock(flags);

	FUNC_EXIT(FUNC_LV_HELP);

	return ret;
}

unsigned int mt_cpufreq_get_freq_by_idx(enum mt_cpu_dvfs_id id, int idx)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(p == NULL);

	if (!cpu_dvfs_is_available(p)) {
		FUNC_EXIT(FUNC_LV_API);
		return 0;
	}

	WARN_ON(idx >= p->nr_opp_tbl);

	FUNC_EXIT(FUNC_LV_API);

	return cpu_dvfs_get_freq_by_idx(p, idx);
}
EXPORT_SYMBOL(mt_cpufreq_get_freq_by_idx);

int mt_cpufreq_update_volt(enum mt_cpu_dvfs_id id
	, unsigned int *volt_tbl, int nr_volt_tbl)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);
	unsigned long flags;
	int i;
	int ret = -1;

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(p == NULL);

	if (!cpu_dvfs_is_available(p)) {
		FUNC_EXIT(FUNC_LV_API);
		return 0;
	}

	WARN_ON(nr_volt_tbl > p->nr_opp_tbl);

	cpufreq_lock(flags);

	/* update volt table */
	for (i = 0; i < nr_volt_tbl; i++)
		p->opp_tbl[i].cpufreq_volt = PMIC_VAL_TO_VOLT(volt_tbl[i]);

	/* set volt */
	ret = _set_cur_volt_locked(p,
		TURBO_MODE_VOLT(get_turbo_mode(p, cpu_dvfs_get_cur_freq(p)),
		cpu_dvfs_get_cur_volt(p)));

	cpufreq_unlock(flags);

	FUNC_EXIT(FUNC_LV_API);

	return ret;
}
EXPORT_SYMBOL(mt_cpufreq_update_volt);

void mt_cpufreq_restore_default_volt(enum mt_cpu_dvfs_id id)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(p == NULL);

	if (!cpu_dvfs_is_available(p)) {
		FUNC_EXIT(FUNC_LV_API);
		return;
	}

	/* Disable turbo mode since PTPOD is disabled */
	if (p->turbo_mode) {
		cpufreq_info("@%s: Turbo mode disabled!\n", __func__);
		p->turbo_mode = 0;
	}

	_restore_default_volt(p);

	FUNC_EXIT(FUNC_LV_API);
}
EXPORT_SYMBOL(mt_cpufreq_restore_default_volt);

static unsigned int _cpu_freq_calc(unsigned int con1, unsigned int ckdiv1)
{
	unsigned int freq = 0;


	con1 &= _BITMASK_(26 : 0);

	if (con1 >= DDS_DIV8_FREQ) {
		freq = DDS_DIV8_FREQ;
		freq = PLL_DIV8_FREQ
			+ (((con1 - freq) / 0x2000) * PLL_FREQ_STEP / 8);
	} else if (con1 >= DDS_DIV4_FREQ) {
		freq = DDS_DIV4_FREQ;
		freq = PLL_DIV4_FREQ
			+ (((con1 - freq) / 0x2000) * PLL_FREQ_STEP / 4);
	} else if (con1 >= DDS_DIV2_FREQ) {
		freq = DDS_DIV2_FREQ;
		freq = PLL_DIV2_FREQ
			+ (((con1 - freq) / 0x2000) * PLL_FREQ_STEP / 2);
	} else if (con1 >= DDS_DIV1_FREQ) {
		freq = DDS_DIV1_FREQ;
		freq = PLL_DIV1_FREQ
			+ (((con1 - freq) / 0x2000) * PLL_FREQ_STEP);
	} else {
		freq = DDS_DIV1_FREQ;
		freq = PLL_DIV1_FREQ
			- (((freq - con1) / 0x2000) * PLL_FREQ_STEP);
	}

	FUNC_ENTER(FUNC_LV_HELP);

	switch (ckdiv1) {
	case 9:
		freq = freq * 3 / 4;
		break;

	case 10:
		freq = freq * 2 / 4;
		break;

	case 11:
		freq = freq * 1 / 4;
		break;

	case 17:
		freq = freq * 4 / 5;
		break;

	case 18:
		freq = freq * 3 / 5;
		break;

	case 19:
		freq = freq * 2 / 5;
		break;

	case 20:
		freq = freq * 1 / 5;
		break;

	case 25:
		freq = freq * 5 / 6;
		break;

	case 26:
		freq = freq * 4 / 6;
		break;

	case 27:
		freq = freq * 3 / 6;
		break;

	case 28:
		freq = freq * 2 / 6;
		break;

	case 29:
		freq = freq * 1 / 6;
		break;

	case 8:
	case 16:
	case 24:
	default:
		break;
	}

	FUNC_EXIT(FUNC_LV_HELP);

	return freq; /* TODO: adjust by ptp level??? */
}

static unsigned int get_cur_phy_freq(struct mt_cpu_dvfs *p)
{
	unsigned int con1;
	unsigned int ckdiv1;
	unsigned int cur_khz;

	FUNC_ENTER(FUNC_LV_LOCAL);

	WARN_ON(p == NULL);

	con1 = cpufreq_read(ARMCA7PLL_CON1);
	ckdiv1 = cpufreq_read(TOP_CKDIV1);
	ckdiv1 = _GET_BITS_VAL_(4 : 0, ckdiv1);

	cur_khz = _cpu_freq_calc(con1, ckdiv1);

	cpufreq_ver("@%s: cur_khz = %d, con1 = 0x%x, ckdiv1_val = 0x%x\n"
		    , __func__, cur_khz, con1, ckdiv1);

	FUNC_EXIT(FUNC_LV_LOCAL);

	return cur_khz;
}

static unsigned int _mt_cpufreq_get_cur_phy_freq(enum mt_cpu_dvfs_id id)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	FUNC_ENTER(FUNC_LV_LOCAL);

	WARN_ON(p == NULL);

	FUNC_EXIT(FUNC_LV_LOCAL);

	return p->ops->get_cur_phy_freq(p);
}

static unsigned int _cpu_dds_calc(unsigned int khz)
{
	unsigned int dds = 0;

	FUNC_ENTER(FUNC_LV_HELP);

	if (khz >= PLL_DIV1_FREQ)
		dds = DDS_DIV1_FREQ
		+ ((khz - PLL_DIV1_FREQ) / PLL_FREQ_STEP) * 0x2000;
	else if (khz >= PLL_DIV2_FREQ)
		dds = DDS_DIV2_FREQ
		+ ((khz - PLL_DIV2_FREQ) * 2 / PLL_FREQ_STEP) * 0x2000;
	else if (khz >= PLL_DIV4_FREQ)
		dds = DDS_DIV4_FREQ
		+ ((khz - PLL_DIV4_FREQ) * 4 / PLL_FREQ_STEP) * 0x2000;
	else if (khz >= PLL_DIV8_FREQ)
		dds = DDS_DIV8_FREQ
		+ ((khz - PLL_DIV8_FREQ) * 8 / PLL_FREQ_STEP) * 0x2000;
	else
		WARN_ON(1);

	FUNC_EXIT(FUNC_LV_HELP);

	return dds;
}

static void _cpu_clock_switch(struct mt_cpu_dvfs *p, enum top_ckmuxsel sel)
{
	FUNC_ENTER(FUNC_LV_HELP);

	switch (sel) {
	case TOP_CKMUXSEL_CLKSQ:
	case TOP_CKMUXSEL_ARMPLL:
		cpufreq_write_mask(TOP_CKMUXSEL, 1 : 0, sel);
		/* disable gating cell (clear clk_misc_cfg_0[5:4]) */
		cpufreq_write_mask(CLK_MISC_CFG_0, 5 : 4, 0x0);
		break;
	case TOP_CKMUXSEL_MAINPLL:
	case TOP_CKMUXSEL_UNIVPLL:
		/* enable gating cell (set clk_misc_cfg_0[5:4]) */
		cpufreq_write_mask(CLK_MISC_CFG_0, 5 : 4, 0x3);
		udelay(3);
		cpufreq_write_mask(TOP_CKMUXSEL, 1 : 0, sel);
		break;
	default:
		WARN_ON(1);
		break;
	}

	FUNC_EXIT(FUNC_LV_HELP);
}

static enum top_ckmuxsel _get_cpu_clock_switch(struct mt_cpu_dvfs *p)
{
	unsigned int val = cpufreq_read(TOP_CKMUXSEL);
	unsigned int mask = _BITMASK_(1 : 0);

	FUNC_ENTER(FUNC_LV_HELP);

	val &= mask;                    /* _BITMASK_(1 : 0) */

	FUNC_EXIT(FUNC_LV_HELP);

	return val;
}

int mt_cpufreq_clock_switch(enum mt_cpu_dvfs_id id, enum top_ckmuxsel sel)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	if (!p)
		return -1;

	_cpu_clock_switch(p, sel);

	return 0;
}

enum top_ckmuxsel mt_cpufreq_get_clock_switch(enum mt_cpu_dvfs_id id)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	if (!p)
		return -1;

	return _get_cpu_clock_switch(p);
}

/*
 * CPU freq scaling
 *
 * above 1209MHz: use freq hopping
 * below 1209MHz: set CLKDIV1
 * if cross 1209MHz, migrate to 1209MHz first.
 *
 */
static void set_cur_freq(struct mt_cpu_dvfs *p
	, unsigned int cur_khz, unsigned int target_khz)
{
	unsigned int dds;
	unsigned int is_fhctl_used;
	unsigned int ckdiv1_val = _GET_BITS_VAL_(4 : 0
						 , cpufreq_read(TOP_CKDIV1));
	unsigned int ckdiv1_mask = _BITMASK_(4 : 0);
	unsigned int sel = 0;
	unsigned int cur_volt = 0;
	unsigned int mainpll_volt_idx = 0;

	#define IS_CLKDIV_USED(clkdiv)  \
		(((clkdiv < 8) || ((clkdiv % 8) == 0)) ? 0 : 1)

	FUNC_ENTER(FUNC_LV_LOCAL);

	if (cur_khz == target_khz)
		return;

	if (((cur_khz < CPUFREQ_BOUNDARY_FOR_FHCTL)
		&& (target_khz > CPUFREQ_BOUNDARY_FOR_FHCTL))
		|| ((target_khz < CPUFREQ_BOUNDARY_FOR_FHCTL)
		&& (cur_khz > CPUFREQ_BOUNDARY_FOR_FHCTL))) {
		set_cur_freq(p, cur_khz, CPUFREQ_BOUNDARY_FOR_FHCTL);
		cur_khz = CPUFREQ_BOUNDARY_FOR_FHCTL;
	}

	is_fhctl_used = ((target_khz >= CPUFREQ_BOUNDARY_FOR_FHCTL)
		&& (cur_khz >= CPUFREQ_BOUNDARY_FOR_FHCTL)) ? 1 : 0;

	cpufreq_ver("@%s():%d, cur_khz = %d, target_khz = %d, ",
		__func__,
		__LINE__,
		cur_khz,
		target_khz);
	cpufreq_ver("is_fhctl_used = %d\n", is_fhctl_used);

	if (!is_fhctl_used) {
		/* target_khz > CPUFREQ_BOUNDARY_FOR_FHCTL == 1105MHz */
		if (target_khz > 936000) { /* 936MHz is ok */
			dds = _cpu_dds_calc(target_khz);
			sel = 8;	/* 4/4 */
		} else {
			dds = _cpu_dds_calc(target_khz * 2);
			sel = 10;	/* 2/4 */
		}

		cur_volt = p->ops->get_cur_volt(p);
		switch (p->cpu_level) {
		case CPU_LEVEL_0:
			mainpll_volt_idx = 2; /*CPU_DVFS_SB_FREQ2*/
			break;
		case CPU_LEVEL_1:
			mainpll_volt_idx = 2; /*CPU_DVFS_FREQ2*/
			break;
		default:
			mainpll_volt_idx = 1;
			break;
		}
		if (cur_volt < cpu_dvfs_get_volt_by_idx(p, mainpll_volt_idx))
			p->ops->set_cur_volt(p
			, cpu_dvfs_get_volt_by_idx(p, mainpll_volt_idx));
		else
			cur_volt = 0;

		/* set ARMPLL and CLKDIV */
		_cpu_clock_switch(p, TOP_CKMUXSEL_MAINPLL);
		cpufreq_write(ARMCA7PLL_CON1, dds | _BIT_(31)); /* CHG */
		udelay(PLL_SETTLE_TIME);
		cpufreq_write(TOP_CKDIV1, (ckdiv1_val & ~ckdiv1_mask) | sel);
		_cpu_clock_switch(p, TOP_CKMUXSEL_ARMPLL);

		/* restore Vproc */
		if (cur_volt)
			p->ops->set_cur_volt(p, cur_volt);
	} else {
		dds = _cpu_dds_calc(target_khz);
		WARN_ON(dds & _BITMASK_(26 : 24)); /* should not use posdiv */

#if !defined(__KERNEL__) && defined(MTKDRV_FREQHOP)
		fhdrv_dvt_dvfs_enable(ARMCA7PLL_ID, dds);
#else  /* __KERNEL__ */
		mt_dfs_armpll(FH_ARMCA7_PLLID, dds);
#endif /* ! __KERNEL__ */
	}

	FUNC_EXIT(FUNC_LV_LOCAL);
}

static unsigned int get_cur_volt_extbuck(struct mt_cpu_dvfs *p)
{
	unsigned int ret_volt = 0;
	unsigned int isenabled = 0;

	FUNC_ENTER(FUNC_LV_LOCAL);

	isenabled = regulator_is_enabled(reg_ext_vproc);
	if (isenabled)
		ret_volt = regulator_get_voltage(reg_ext_vproc);
	else
		ret_volt = 0;

	FUNC_EXIT(FUNC_LV_LOCAL);

	return ret_volt;
}

unsigned int mt_cpufreq_get_cur_volt(enum mt_cpu_dvfs_id id)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);
	unsigned int volt_10_uV = 0;

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(p == NULL);
	WARN_ON(p->ops == NULL);

	FUNC_EXIT(FUNC_LV_API);

	volt_10_uV = p->ops->get_cur_volt(p) / 10; /* mv * 100 == 10 uV */
	return volt_10_uV;
}
EXPORT_SYMBOL(mt_cpufreq_get_cur_volt);

#define SYM827_PRECISION	12500	/* uv */
/* volt: vproc (mv*1000) */
static int set_cur_volt_extbuck(struct mt_cpu_dvfs *p, unsigned int volt)
{
	unsigned int cur_vproc = get_cur_volt_extbuck(p);
	bool is_leaving_turbo_mode = false;
	int ret = 0;

	FUNC_ENTER(FUNC_LV_LOCAL);

#ifdef CONFIG_CPU_DVFS_AEE_RR_REC
	aee_rr_rec_cpu_dvfs_vproc_little(VOLT_TO_EXTBUCK_VAL(volt));
#endif

	if (is_in_turbo_mode
		&& cur_vproc > cpu_dvfs_get_volt_by_idx(p, 0)
		&& volt <= cpu_dvfs_get_volt_by_idx(p, 0))
		is_leaving_turbo_mode = true;

	/* mt8163 external buck */
	ret = regulator_set_voltage(reg_ext_vproc
				    , volt, volt + SYM827_PRECISION - 1);
	if (ret) {
		cpufreq_err("%s[%d], CPUFREQ, set reg_ext_vproc FAIL\n"
			    , __func__, __LINE__);
		regulator_ext_vproc_voltage = 0;
		return ret;
	}
	regulator_ext_vproc_voltage = volt / 1000;

	if (g_pCpuVoltSampler != NULL)
		g_pCpuVoltSampler(MT_CPU_DVFS_LITTLE, volt / 100); /* mv */

	cpufreq_ver("@%s():%d, cur_vproc = %d\n"
		    , __func__, __LINE__, cur_vproc);

	if (is_leaving_turbo_mode) {
		is_in_turbo_mode = false;
#ifdef CONFIG_CPU_DVFS_AEE_RR_REC
		aee_rr_rec_cpu_dvfs_status(aee_rr_curr_cpu_dvfs_status()
					   & ~(1 << CPU_DVFS_LITTLE_IS_TURBO));
#endif
	}

	FUNC_EXIT(FUNC_LV_LOCAL);

	return ret;
}

/* cpufreq set (freq & volt) */

static unsigned int _search_available_volt(struct mt_cpu_dvfs *p
	, unsigned int target_khz)
{
	int i;

	FUNC_ENTER(FUNC_LV_HELP);

	WARN_ON(p == NULL);

	/* search available voltage */
	for (i = p->nr_opp_tbl - 1; i >= 0; i--) {
		if (target_khz <= cpu_dvfs_get_freq_by_idx(p, i))
			break;
	}

	WARN_ON(i < 0); /* i.e. target_khz > p->opp_tbl[0].cpufreq_khz */

	FUNC_EXIT(FUNC_LV_HELP);

	return cpu_dvfs_get_volt_by_idx(p, i); /* mv * 1000 */
}

static int _cpufreq_set_locked(struct mt_cpu_dvfs *p, unsigned int cur_khz
	, unsigned int target_khz, struct cpufreq_policy *policy)
{
	unsigned int volt; /* mv * 1000 */
	int ret = 0;
#ifdef CONFIG_CPU_FREQ
	struct cpufreq_freqs freqs;
	unsigned int target_khz_orig = target_khz;
#endif

	enum turbo_mode mode = get_turbo_mode(p, target_khz);

	FUNC_ENTER(FUNC_LV_HELP);

	volt = _search_available_volt(p, target_khz);

	if (cur_khz != TURBO_MODE_FREQ(mode, target_khz))
		cpufreq_ver("@%s(), target_khz = %d (%d), volt = %d (%d), ",
			__func__,
			target_khz,
			TURBO_MODE_FREQ(mode, target_khz),
			volt,
			TURBO_MODE_VOLT(mode, volt));
		cpufreq_ver("num_online_cpus = %d, cur_khz = %d\n",
			num_online_cpus(),
			cur_khz);

	volt = TURBO_MODE_VOLT(mode, volt);
	target_khz = TURBO_MODE_FREQ(mode, target_khz);

	if (cur_khz == target_khz)
		goto out;

	/* set volt (UP) */
	if (cur_khz < target_khz) {
		ret = p->ops->set_cur_volt(p, volt);

	if (ret) /* set volt fail */
		goto out;
	}

#ifdef CONFIG_CPU_FREQ
	freqs.old = cur_khz;
	/* new freq without turbo */
	freqs.new = target_khz_orig;
	/* fix notify transition hang issue for Linux-3.18 */
	if (policy) {
		freqs.cpu = policy->cpu;
		cpufreq_freq_transition_begin(policy, &freqs);
	}
#endif

/* set freq (UP/DOWN) */
	if (cur_khz != target_khz)
		p->ops->set_cur_freq(p, cur_khz, target_khz);

#ifdef CONFIG_CPU_FREQ
	/* fix notify transition hang issue for Linux-3.18 */
	if (policy)
		cpufreq_freq_transition_end(policy, &freqs, 0);
#endif

	/* set volt (DOWN) */
	if (cur_khz > target_khz) {
		ret = p->ops->set_cur_volt(p, volt);

	if (ret) /* set volt fail */
		goto out;
	}

	cpufreq_dbg("@%s(): Vproc = %dmv, freq = %d KHz\n",
		__func__,
		(p->ops->get_cur_volt(p)) / 1000,
		p->ops->get_cur_phy_freq(p)
		);

	/* trigger exception if freq/volt not correct during stress */
	if (do_dvfs_stress_test) {
		WARN_ON(p->ops->get_cur_volt(p) < volt);
		WARN_ON(p->ops->get_cur_phy_freq(p) != target_khz);
	}

	FUNC_EXIT(FUNC_LV_HELP);
out:
	return ret;
}

static unsigned int _calc_new_opp_idx(struct mt_cpu_dvfs *p, int new_opp_idx);

static void _mt_cpufreq_set(enum mt_cpu_dvfs_id id, int new_opp_idx)
{
	unsigned long flags;
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);
	unsigned int cur_freq;
	unsigned int target_freq;
#ifdef CONFIG_CPU_FREQ
	struct cpufreq_policy *policy;
#endif

	FUNC_ENTER(FUNC_LV_LOCAL);

	WARN_ON(p == NULL);
	WARN_ON(new_opp_idx >= p->nr_opp_tbl);

#ifdef CONFIG_CPU_FREQ
	policy = cpufreq_cpu_get(p->cpu_id);
#endif

	cpufreq_lock(flags);

	/* get current idx here to avoid idx synchronization issue */
	if (new_opp_idx == -1)
		new_opp_idx = p->idx_opp_tbl;

	if (do_dvfs_stress_test)
		new_opp_idx = jiffies & 0x7; /* 0~7 */
	else {
#if defined(CONFIG_CPU_DVFS_BRINGUP)
		new_opp_idx = id_to_cpu_dvfs(id)->idx_normal_max_opp;
#else
	new_opp_idx = _calc_new_opp_idx(id_to_cpu_dvfs(id), new_opp_idx);
#endif
	}

	cur_freq = p->ops->get_cur_phy_freq(p);
	target_freq = cpu_dvfs_get_freq_by_idx(p, new_opp_idx);
#ifdef CONFIG_CPU_FREQ
	_cpufreq_set_locked(p, cur_freq, target_freq, policy);
#else
	_cpufreq_set_locked(p, cur_freq, target_freq, NULL);
#endif
	p->idx_opp_tbl = new_opp_idx;

	cpufreq_unlock(flags);

#ifdef CONFIG_CPU_FREQ
	if (policy)
		cpufreq_cpu_put(policy);
#endif

	FUNC_EXIT(FUNC_LV_LOCAL);
}

static int turbo_mode_cpu_callback(struct notifier_block *nfb,
	unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	unsigned int online_cpus = num_online_cpus();
	struct device *dev;
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(0);

	cpufreq_ver("@%s():%d, cpu = %d, action = %lu, oppidx = %d, ",
				__func__,
				__LINE__,
				cpu,
				action,
				p->idx_opp_tbl);
	cpufreq_ver("num_online_cpus = %d, num_online_cpus_delta = %d\n",
				online_cpus,
				num_online_cpus_delta);

	dev = get_cpu_device(cpu);

	if (dev) {
		if (online_cpus == TURBO_MODE_BOUNDARY_CPU_NUM) {
			switch (action) {
			case CPU_UP_PREPARE:
			case CPU_UP_PREPARE_FROZEN:
				num_online_cpus_delta = 1;
			case CPU_DEAD:
			case CPU_DEAD_FROZEN:
				_mt_cpufreq_set(MT_CPU_DVFS_LITTLE, -1);
				break;
			}
		} else {
			switch (action) {
			case CPU_ONLINE:    /* CPU UP done */
			case CPU_ONLINE_FROZEN:
			case CPU_UP_CANCELED:   /* CPU UP failed */
			case CPU_UP_CANCELED_FROZEN:
				num_online_cpus_delta = 0;
				break;
		}
	}

	cpufreq_ver("@%s():%d, cpu = %d, action = %lu, oppidx = %d, ",
				__func__,
				__LINE__,
				cpu,
				action,
				p->idx_opp_tbl);
	cpufreq_ver("num_online_cpus = %d, num_online_cpus_delta = %d\n",
				online_cpus,
				num_online_cpus_delta);
	}

	return NOTIFY_OK;
}

static struct notifier_block __refdata turbo_mode_cpu_notifier = {
	.notifier_call = turbo_mode_cpu_callback,
};

static void _set_no_limited(struct mt_cpu_dvfs *p)
{
	FUNC_ENTER(FUNC_LV_HELP);

	WARN_ON(p == NULL);

	p->limited_max_freq = cpu_dvfs_get_max_freq(p);
	p->limited_max_ncpu = max_cpu_num;

	FUNC_EXIT(FUNC_LV_HELP);
}

#ifdef CONFIG_CPU_DVFS_DOWNGRADE_FREQ
static void _downgrade_freq_check(enum mt_cpu_dvfs_id id)
{
	struct cpufreq_policy *policy;
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);
	int temp = 0;

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(p == NULL);

	/* if not CPU_LEVEL0 */
	if (p->cpu_level != CPU_LEVEL_0)
		goto out;

	/* get temp */
#ifdef CONFIG_THERMAL
	temp = tscpu_get_temp_by_bank(THERMAL_BANK0);    /* bank0 for CPU */
#endif


	if (temp < 0 || 125000 < temp)
		goto out;

	{
	static enum turbo_mode pre_mode = TURBO_MODE_NONE;
	enum turbo_mode cur_mode = get_turbo_mode(p, cpu_dvfs_get_cur_freq(p));

		if (pre_mode != cur_mode) {
			_mt_cpufreq_set(MT_CPU_DVFS_LITTLE, -1);
			cpufreq_ver("@%s():%d, oppidx = %d, ",
				__func__,
				__LINE__,
				p->idx_opp_tbl);
			cpufreq_ver("num_online_cpus = %d, pre_mode = %d, ",
				num_online_cpus(),
				pre_mode);
			cpufreq_ver("cur_mode = %d\n",
				cur_mode);
			pre_mode = cur_mode;
		}
	}

	if (temp <= p->ptpod_temperature_limit_1) {
		p->downgrade_freq_for_ptpod  = false;
		goto out;
	} else if ((temp > p->ptpod_temperature_limit_1)
	&& (temp < p->ptpod_temperature_limit_2)) {
		p->downgrade_freq_counter_return_limit
			= p->downgrade_freq_counter_limit
			* p->ptpod_temperature_time_1;
	} else {
		p->downgrade_freq_counter_return_limit
			= p->downgrade_freq_counter_limit
			* p->ptpod_temperature_time_2;
	}

	if (p->downgrade_freq_for_ptpod == false) {
		if ((num_online_cpus() == p->pre_online_cpu)
			&& (cpu_dvfs_get_cur_freq(p) == p->pre_freq)) {
			if ((num_online_cpus() >= p->over_max_cpu)
				&& (p->idx_opp_tbl == 0)) {
				p->downgrade_freq_counter++;

				if (p->downgrade_freq_counter
					>= p->downgrade_freq_counter_limit) {
					p->downgrade_freq
					= cpu_dvfs_get_freq_by_idx(p, 1);

					p->downgrade_freq_for_ptpod = true;
					p->downgrade_freq_counter = 0;

					cpufreq_info("freq limit, ");
					cpufreq_info("downgrade_");
					cpufreq_info("freq_for_ptpod ");
					cpufreq_info("= %d\n"
						, p->downgrade_freq_for_ptpod);

					policy = cpufreq_cpu_get(p->cpu_id);

					if (!policy)
						goto out;

					cpufreq_driver_target(policy
						, p->downgrade_freq
						, CPUFREQ_RELATION_L);

					cpufreq_cpu_put(policy);
				}
			} else
			p->downgrade_freq_counter = 0;
		} else {
			p->pre_online_cpu = num_online_cpus();
			p->pre_freq = cpu_dvfs_get_cur_freq(p);

			p->downgrade_freq_counter = 0;
		}
	} else {
		p->downgrade_freq_counter_return++;

		if (p->downgrade_freq_counter_return
			>= p->downgrade_freq_counter_return_limit) {
			p->downgrade_freq_for_ptpod  = false;
			p->downgrade_freq_counter_return = 0;
		}
	}

out:
	FUNC_EXIT(FUNC_LV_API);
}

static void _init_downgrade(struct mt_cpu_dvfs *p, unsigned int cpu_level)
{
	FUNC_ENTER(FUNC_LV_HELP);

	switch (cpu_level) {
	case CPU_LEVEL_0:
	case CPU_LEVEL_1:
	default:
		p->downgrade_freq_counter_limit = 10;
		p->ptpod_temperature_time_1     = 2;
		p->ptpod_temperature_time_2     = 8;
		break;
	}

#ifdef __KERNEL__
	/* install callback */
	cpufreq_freq_check = _downgrade_freq_check;
#endif

	FUNC_EXIT(FUNC_LV_HELP);
}
#endif

static int _sync_opp_tbl_idx(struct mt_cpu_dvfs *p)
{
	int ret = -1;
	unsigned int freq;
	int i;

	FUNC_ENTER(FUNC_LV_HELP);

	WARN_ON(p == NULL);
	WARN_ON(p->opp_tbl == NULL);
	WARN_ON(p->ops == NULL);

	freq = p->ops->get_cur_phy_freq(p);

	for (i = p->nr_opp_tbl - 1; i >= 0; i--) {
		if (freq <= cpu_dvfs_get_freq_by_idx(p, i)) {
			p->idx_opp_tbl = i;
			break;
		}

	}

	if (i >= 0) {
		cpufreq_info("%s freq = %d\n", cpu_dvfs_get_name(p)
			     , cpu_dvfs_get_cur_freq(p));

	/* TODO: apply correct voltage??? */

	ret = 0;
	} else
	cpufreq_warn("%s can't find freq = %d\n", cpu_dvfs_get_name(p), freq);

	FUNC_EXIT(FUNC_LV_HELP);

	return ret;
}

static void _mt_cpufreq_sync_opp_tbl_idx(void)
{
	struct mt_cpu_dvfs *p;
	int i;

	FUNC_ENTER(FUNC_LV_LOCAL);

	for_each_cpu_dvfs(i, p) {
		if (cpu_dvfs_is_available(p))
			_sync_opp_tbl_idx(p);
	}

	FUNC_EXIT(FUNC_LV_LOCAL);
}

static enum mt_cpu_dvfs_id _get_cpu_dvfs_id(unsigned int cpu_id)
{
	/* Little core only for K2 */
	return MT_CPU_DVFS_LITTLE;
}

int mt_cpufreq_state_set(int enabled)
{
	int ret = 0;

	FUNC_ENTER(FUNC_LV_API);

	FUNC_EXIT(FUNC_LV_API);

	return ret;
}
EXPORT_SYMBOL(mt_cpufreq_state_set);

/* Power Table */
static void _power_calculation(struct mt_cpu_dvfs *p, int oppidx, int ncpu)
{
	/* mt8163-TBD Define power */
	#define CA53_8CORE_REF_POWER	2286	/* mW  */
	#define CA53_6CORE_REF_POWER	1736	/* mW  */
	#define CA53_4CORE_REF_POWER	1159	/* mW  */
	#define CA53_REF_FREQ	1690000 /* KHz */
	#define CA53_REF_VOLT	1000000	/* mV * 1000 */

	int p_dynamic = 0, p_leakage = 0, ref_freq, ref_volt;
	int possible_cpu = max_cpu_num;

	FUNC_ENTER(FUNC_LV_HELP);

	ref_freq  = CA53_REF_FREQ;
	ref_volt  = CA53_REF_VOLT;

	switch (possible_cpu) {
	case 4:
		p_dynamic = CA53_4CORE_REF_POWER;
		break;
	case 6:
		p_dynamic = CA53_6CORE_REF_POWER;
		break;
	case 8:
	default:
		p_dynamic = CA53_8CORE_REF_POWER;
		break;
	}

	/* TODO: should not use a hardcode value for leakage power */
	p_leakage = mt_spower_get_leakage(MT_SPOWER_CA7
		, p->opp_tbl[oppidx].cpufreq_volt / 1000, 65);

	p_dynamic = p_dynamic *
	(p->opp_tbl[oppidx].cpufreq_khz / 1000) / (ref_freq / 1000) *
	p->opp_tbl[oppidx].cpufreq_volt / ref_volt *
	p->opp_tbl[oppidx].cpufreq_volt / ref_volt +
	p_leakage;

	p->power_tbl[NR_MAX_OPP_TBL
		* (possible_cpu - 1 - ncpu) + oppidx].cpufreq_ncpu
		= ncpu + 1;
	p->power_tbl[NR_MAX_OPP_TBL
		* (possible_cpu - 1 - ncpu) + oppidx].cpufreq_khz
		= p->opp_tbl[oppidx].cpufreq_khz;
	p->power_tbl[NR_MAX_OPP_TBL
		* (possible_cpu - 1 - ncpu) + oppidx].cpufreq_power
		= p_dynamic * (ncpu + 1) / possible_cpu;

	FUNC_EXIT(FUNC_LV_HELP);
}

static int setup_power_table(struct mt_cpu_dvfs *p)
{
	unsigned int pwr_eff_tbl[NR_MAX_OPP_TBL][NR_MAX_CPU];
	unsigned int pwr_eff_num = 0;
	int possible_cpu = max_cpu_num; /*num_possible_cpus();*/
	int i, j;
	int ret = 0;

	FUNC_ENTER(FUNC_LV_LOCAL);

	WARN_ON(p == NULL);

	if (p->power_tbl)
		goto out;

	/* allocate power table */
	memset((void *)pwr_eff_tbl, 0, sizeof(pwr_eff_tbl));
	p->power_tbl = kzalloc(p->nr_opp_tbl * possible_cpu
			       * sizeof(struct mt_cpu_power_info), GFP_KERNEL);

	if (p->power_tbl == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	p->nr_power_tbl = p->nr_opp_tbl * (possible_cpu - pwr_eff_num);

	/* calc power and fill in power table */
	for (i = 0; i < p->nr_opp_tbl; i++) {
		for (j = 0; j < possible_cpu; j++) {
			if (pwr_eff_tbl[i][j] == 0)
				_power_calculation(p, i, j);
		}
	}

	/* sort power table */
	for (i = p->nr_opp_tbl * possible_cpu; i > 0; i--) {
		for (j = 1; j < i; j++) {
			if (p->power_tbl[j - 1].cpufreq_power
				< p->power_tbl[j].cpufreq_power) {
				struct mt_cpu_power_info tmp;

				tmp.cpufreq_khz
					= p->power_tbl[j - 1].cpufreq_khz;
				tmp.cpufreq_ncpu
					= p->power_tbl[j - 1].cpufreq_ncpu;
				tmp.cpufreq_power
					= p->power_tbl[j - 1].cpufreq_power;

				p->power_tbl[j - 1].cpufreq_khz
					= p->power_tbl[j].cpufreq_khz;
				p->power_tbl[j - 1].cpufreq_ncpu
					= p->power_tbl[j].cpufreq_ncpu;
				p->power_tbl[j - 1].cpufreq_power
					= p->power_tbl[j].cpufreq_power;

				p->power_tbl[j].cpufreq_khz
					= tmp.cpufreq_khz;
				p->power_tbl[j].cpufreq_ncpu
					= tmp.cpufreq_ncpu;
				p->power_tbl[j].cpufreq_power
					= tmp.cpufreq_power;
			}
		}
	}

	/* dump power table */
	for (i = 0; i < p->nr_opp_tbl * possible_cpu; i++) {
		cpufreq_info("[%d] = { .cpufreq_khz = %d,\t",
		i,
		p->power_tbl[i].cpufreq_khz);
		cpufreq_info(".cpufreq_ncpu = %d,\t.cpufreq_power = %d }\n",
		p->power_tbl[i].cpufreq_ncpu,
		p->power_tbl[i].cpufreq_power);
	}

out:
	FUNC_EXIT(FUNC_LV_LOCAL);

	return ret;
}

static int _mt_cpufreq_setup_freqs_table(struct cpufreq_policy *policy
, struct mt_cpu_freq_info *freqs, int num)
{
	struct mt_cpu_dvfs *p;
	struct cpufreq_frequency_table *table = NULL;
	int i, ret = 0;

	FUNC_ENTER(FUNC_LV_LOCAL);

	WARN_ON(policy == NULL);
	WARN_ON(freqs == NULL);

	p = id_to_cpu_dvfs(_get_cpu_dvfs_id(policy->cpu));

	if (p->freq_tbl_for_cpufreq == NULL) {
		table = kzalloc((num + 1) * sizeof(*table), GFP_KERNEL);

		if (table == NULL) {
			ret = -ENOMEM;
			goto out;
		}

		for (i = 0; i < num; i++) {
			table[i].driver_data = i;
			table[i].frequency = freqs[i].cpufreq_khz;
		}

		table[num].driver_data = i;
		table[num].frequency = CPUFREQ_TABLE_END;

		p->opp_tbl = freqs;
		p->nr_opp_tbl = num;
		p->freq_tbl_for_cpufreq = table;
	}

#ifdef CONFIG_CPU_FREQ
	ret = cpufreq_frequency_table_cpuinfo(policy, p->freq_tbl_for_cpufreq);

	if (!ret)
		policy->freq_table = table;
#endif

	if (p->power_tbl == NULL)
		p->ops->setup_power_table(p);

out:
	FUNC_EXIT(FUNC_LV_LOCAL);

	return 0;
}

void mt_cpufreq_enable_by_ptpod(enum mt_cpu_dvfs_id id)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(p == NULL);

	p->dvfs_disable_by_ptpod = false;

	/* pmic auto mode: the variance of voltage
	 * is wide but saves more power.
	 */
	regulator_set_mode(reg_ext_vproc, REGULATOR_MODE_NORMAL);
	if (regulator_get_mode(reg_ext_vproc) != REGULATOR_MODE_NORMAL) {
		cpufreq_err("Vproc should be REGULATOR_MODE_NORMAL(%d), "
			    , REGULATOR_MODE_NORMAL);
		cpufreq_err("but mode = %d\n"
			    , regulator_get_mode(reg_ext_vproc));
	}


	if (!cpu_dvfs_is_available(p)) {
		FUNC_EXIT(FUNC_LV_API);
		return;
	}

	_mt_cpufreq_set(id, p->idx_opp_tbl_for_late_resume);

	FUNC_EXIT(FUNC_LV_API);
}
EXPORT_SYMBOL(mt_cpufreq_enable_by_ptpod);

unsigned int mt_cpufreq_disable_by_ptpod(enum mt_cpu_dvfs_id id)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	FUNC_ENTER(FUNC_LV_API);

	WARN_ON(p == NULL);

	p->dvfs_disable_by_ptpod = true;

	/* pmic PWM mode: the variance of voltage
	 * is narrow but consumes more power.
	 */
	regulator_set_mode(reg_ext_vproc, REGULATOR_MODE_FAST);
	if (regulator_get_mode(reg_ext_vproc) != REGULATOR_MODE_FAST) {
		cpufreq_err("Vproc should be REGULATOR_MODE_FAST(%d), "
			    , REGULATOR_MODE_FAST);
		cpufreq_err("but mode = %d\n"
			    , regulator_get_mode(reg_ext_vproc));
	}

	if (!cpu_dvfs_is_available(p)) {
		FUNC_EXIT(FUNC_LV_API);
		return 0;
	}

	p->idx_opp_tbl_for_late_resume = p->idx_opp_tbl;
	_mt_cpufreq_set(id, p->idx_normal_max_opp);
	_set_cur_volt_locked(p, 1150000); /* from YT, from HPT team */

	FUNC_EXIT(FUNC_LV_API);

	return cpu_dvfs_get_cur_freq(p);
}
EXPORT_SYMBOL(mt_cpufreq_disable_by_ptpod);

void mt_cpufreq_thermal_protect(unsigned int limited_power)
{
	FUNC_ENTER(FUNC_LV_API);

	cpufreq_info("%s(): limited_power = %d\n", __func__, limited_power);

#ifdef CONFIG_CPU_FREQ
	{
	struct cpufreq_policy *policy;
	struct mt_cpu_dvfs *p;
	int possible_cpu;
	int ncpu;
	int found = 0;
	unsigned long flag;
	int i;

	policy = cpufreq_cpu_get(0);
	if (policy == NULL)
		goto no_policy;

	p = id_to_cpu_dvfs(_get_cpu_dvfs_id(policy->cpu));

	WARN_ON(p == NULL);

	cpufreq_lock(flag);

	/* save current oppidx */
	if (!p->thermal_protect_limited_power)
		p->idx_opp_tbl_for_thermal_thro = p->idx_opp_tbl;

	p->thermal_protect_limited_power = limited_power;
	possible_cpu = max_cpu_num;

	/* no limited */
	if (limited_power == 0) {
		p->limited_max_ncpu = possible_cpu;
		p->limited_max_freq = cpu_dvfs_get_max_freq(p);
		/* restore oppidx */
		p->idx_opp_tbl = p->idx_opp_tbl_for_thermal_thro;
	} else {
		for (ncpu = possible_cpu; ncpu > 0; ncpu--) {
			for (i = 0; i < p->nr_opp_tbl * possible_cpu; i++) {
				/* p->power_tbl[i].cpufreq_ncpu == ncpu && */
				if (p->power_tbl[i].cpufreq_power
				    <= limited_power) {
					p->limited_max_ncpu
					= p->power_tbl[i].cpufreq_ncpu;
					p->limited_max_freq
					= p->power_tbl[i].cpufreq_khz;
					found = 1;
					ncpu = 0; /* for break outer loop */
					break;
				}
			}
		}

		/* not found and use lowest power limit */
		if (!found) {
			p->limited_max_ncpu
			= p->power_tbl[p->nr_power_tbl - 1].cpufreq_ncpu;
			p->limited_max_freq
			= p->power_tbl[p->nr_power_tbl - 1].cpufreq_khz;
		}
	}

	cpufreq_dbg("found = %d, ", found);
	cpufreq_dbg("limited_max_freq = %d, limited_max_ncpu = %d\n"
		    , p->limited_max_freq, p->limited_max_ncpu);

	cpufreq_unlock(flag);

	hps_set_cpu_num_limit(LIMIT_THERMAL, p->limited_max_ncpu, 0);
	/* correct opp idx will be calcualted in _thermal_limited_verify() */
	_mt_cpufreq_set(MT_CPU_DVFS_LITTLE, -1);
	cpufreq_cpu_put(policy);
	}
no_policy:
#endif

	FUNC_EXIT(FUNC_LV_API);
}
EXPORT_SYMBOL(mt_cpufreq_thermal_protect);

/* for ramp down */
void mt_cpufreq_set_ramp_down_count_const(enum mt_cpu_dvfs_id id, int count)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	WARN_ON(p == NULL);

	p->ramp_down_count_const = count;
}
EXPORT_SYMBOL(mt_cpufreq_set_ramp_down_count_const);

#ifdef CONFIG_CPU_DVFS_RAMP_DOWN
/* TODO: inline @ mt_cpufreq_target() */
static int _keep_max_freq(struct mt_cpu_dvfs *p, unsigned int freq_old
	, unsigned int freq_new)
{
	int ret = 0;

	FUNC_ENTER(FUNC_LV_HELP);

	if (p->ramp_down_count_const > RAMP_DOWN_TIMES)
		p->ramp_down_count_const--;
	else
		p->ramp_down_count_const = RAMP_DOWN_TIMES;

	if (freq_new < freq_old
	    && p->ramp_down_count < p->ramp_down_count_const) {
		ret = 1;
		p->ramp_down_count++;
	} else
		p->ramp_down_count = 0;

	FUNC_ENTER(FUNC_LV_HELP);

	return ret;
}
#endif

static int _search_available_freq_idx(struct mt_cpu_dvfs *p
	, unsigned int target_khz
	, unsigned int relation) /* return -1 (not found) */
{
	int new_opp_idx = -1;
	int i;

	FUNC_ENTER(FUNC_LV_HELP);

	if (relation == CPUFREQ_RELATION_L) {
		for (i = (signed int)(p->nr_opp_tbl - 1); i >= 0; i--) {
			if (cpu_dvfs_get_freq_by_idx(p, i) >= target_khz) {
				new_opp_idx = i;
				break;
			}
		}
	} else { /* CPUFREQ_RELATION_H */
		for (i = 0; i < (signed int)p->nr_opp_tbl; i++) {
			if (cpu_dvfs_get_freq_by_idx(p, i) <= target_khz) {
				new_opp_idx = i;
				break;
			}
		}
	}

	FUNC_EXIT(FUNC_LV_HELP);

	return new_opp_idx;
}

static int _thermal_limited_verify(struct mt_cpu_dvfs *p, int new_opp_idx)
{
	unsigned int target_khz = cpu_dvfs_get_freq_by_idx(p, new_opp_idx);
	int possible_cpu = 0;
	unsigned int online_cpu = 0;
	int found = 0;
	int i;

	FUNC_ENTER(FUNC_LV_HELP);

	possible_cpu = max_cpu_num;
	online_cpu = num_online_cpus();

	/* no limited */
	if (p->thermal_protect_limited_power == 0)
		return new_opp_idx;

	for (i = 0; i < p->nr_opp_tbl * possible_cpu; i++) {
		if (p->power_tbl[i].cpufreq_ncpu == p->limited_max_ncpu
			&& p->power_tbl[i].cpufreq_khz  == p->limited_max_freq)
			break;
	}

	cpufreq_dbg("%s(): idx = %d", __func__, i);
	cpufreq_dbg(", limited_max_ncpu = %d, limited_max_freq = %d\n"
		    , p->limited_max_ncpu, p->limited_max_freq);

	for (; i < p->nr_opp_tbl * possible_cpu; i++) {
		if (p->power_tbl[i].cpufreq_ncpu == online_cpu) {
			if (target_khz >= p->power_tbl[i].cpufreq_khz) {
				found = 1;
				break;
			}
		}
	}

	if (found) {
		target_khz = p->power_tbl[i].cpufreq_khz;
		cpufreq_dbg("%s(): freq found, idx = %d, ", __func__, i);
		cpufreq_dbg("target_khz = %d, online_cpu = %d\n"
			    , target_khz, online_cpu);
	} else {
		target_khz = p->limited_max_freq;
		cpufreq_dbg("%s(): freq not found, ", __func__);
		cpufreq_dbg("set to limited_max_freq = %d\n", target_khz);
	}

	/* TODO: refine this function for idx searching */
	i = _search_available_freq_idx(p, target_khz, CPUFREQ_RELATION_H);

	FUNC_EXIT(FUNC_LV_HELP);

	return i;
}

static unsigned int _calc_new_opp_idx(struct mt_cpu_dvfs *p, int new_opp_idx)
{
	int idx;

	FUNC_ENTER(FUNC_LV_HELP);

	WARN_ON(p == NULL);

	/* for ramp down */
#ifdef CONFIG_CPU_DVFS_RAMP_DOWN
	if (_keep_max_freq(p, cpu_dvfs_get_cur_freq(p)
			   , cpu_dvfs_get_freq_by_idx(p, new_opp_idx))) {
		cpufreq_dbg("%s(): ramp down, idx = %d, freq_old = %d",
			__func__,
			new_opp_idx,
			cpu_dvfs_get_cur_freq(p),
			);
		cpufreq_dbg(", freq_new = %d\n",
			cpu_dvfs_get_freq_by_idx(p, new_opp_idx));
		new_opp_idx = p->idx_opp_tbl;
	}
#endif

	/* HEVC */
	if (p->limited_freq_by_hevc) {
		idx = _search_available_freq_idx(p, p->limited_freq_by_hevc
						 , CPUFREQ_RELATION_L);

		if (idx != -1) {
			new_opp_idx = idx;
			cpufreq_dbg("%s(): hevc limited freq, idx = %d\n"
				    , __func__, new_opp_idx);
		}
	}

#ifdef CONFIG_CPU_DVFS_DOWNGRADE_FREQ
	if (p->downgrade_freq_for_ptpod == true) {
		if (cpu_dvfs_get_freq_by_idx(p, new_opp_idx)
		    > p->downgrade_freq) {
			idx = _search_available_freq_idx(p,
							 p->downgrade_freq,
							 CPUFREQ_RELATION_H);

			if (idx != -1) {
				new_opp_idx = idx;
				cpufreq_dbg("%s(): downgrade freq, idx = %d\n"
					    , __func__, new_opp_idx);
			}
		}
	}
#endif /* CONFIG_CPU_DVFS_DOWNGRADE_FREQ */

	/* for early suspend */
	if (p->dvfs_disable_by_early_suspend) {
		if (is_fix_freq_in_ES)
			new_opp_idx = p->idx_normal_max_opp;
		else {
			if (new_opp_idx > p->idx_normal_max_opp)
				new_opp_idx = p->idx_normal_max_opp;
		}
		cpufreq_dbg("%s(): for early suspend, idx = %d\n"
			    , __func__, new_opp_idx);
	}

	/* for suspend */
	if (p->dvfs_disable_by_suspend)
		new_opp_idx = p->idx_normal_max_opp;

	/* for power throttling */
#ifdef CONFIG_CPU_DVFS_POWER_THROTTLING
	if (p->pwr_thro_mode && new_opp_idx < p->idx_pwr_thro_max_opp) {
		new_opp_idx = p->idx_pwr_thro_max_opp;
		cpufreq_dbg("%s(): for power throttling = %d\n"
			    , __func__, new_opp_idx);
	}
#endif

	/* limit max freq by user */
	if (p->limited_max_freq_by_user) {
		idx = _search_available_freq_idx(p, p->limited_max_freq_by_user
						 , CPUFREQ_RELATION_H);

		if (idx != -1 && new_opp_idx < idx) {
			new_opp_idx = idx;
			cpufreq_dbg("%s(): limited max freq by user"
				    , __func__);
			cpufreq_dbg(", idx = %d\n", new_opp_idx);
		}
	}

        /* search thermal limited freq */
        idx = _thermal_limited_verify(p, new_opp_idx);
        if (idx != -1 && idx != new_opp_idx) {
                if (cpu_dvfs_get_freq_by_idx(p, idx) < cpu_dvfs_get_freq_by_idx(p, new_opp_idx)) {
                        new_opp_idx = idx;
                        cpufreq_dbg("%s(): thermal limited freq, idx = %d\n"
                                    , __func__, new_opp_idx);
                }
        }

	/* for ptpod init */
	if (p->dvfs_disable_by_ptpod) {
		/* at least CPU_DVFS_FREQ6 will make sure VBoot >= 1V */
		idx = _search_available_freq_idx(p, CPU_DVFS_FREQ6
						 , CPUFREQ_RELATION_L);
		if (idx != -1) {
			new_opp_idx = idx;
			cpufreq_info("%s(): for ptpod init", __func__);
			cpufreq_info(", idx = %d\n", new_opp_idx);
		}
	}

#ifdef CONFIG_CPU_DVFS_AEE_RR_REC
	aee_rr_rec_cpu_dvfs_oppidx((aee_rr_curr_cpu_dvfs_oppidx() & 0xF0)
				   | new_opp_idx);
#endif

	FUNC_EXIT(FUNC_LV_HELP);

	return new_opp_idx;
}

#define EMI_FREQ_CHECK  0
unsigned int gEMI_DFS_enable = 1;

/*
 * cpufreq driver
 */
static int _mt_cpufreq_verify(struct cpufreq_policy *policy)
{
	struct mt_cpu_dvfs *p;
	int ret = 0; /* cpufreq_frequency_table_verify() always return 0 */

	FUNC_ENTER(FUNC_LV_MODULE);

	p = id_to_cpu_dvfs(_get_cpu_dvfs_id(policy->cpu));

	WARN_ON(p == NULL);

#ifdef CONFIG_CPU_FREQ
	ret = cpufreq_frequency_table_verify(policy, p->freq_tbl_for_cpufreq);
#endif

	FUNC_EXIT(FUNC_LV_MODULE);

	return ret;
}

static int _mt_cpufreq_target(struct cpufreq_policy *policy
	, unsigned int target_freq, unsigned int relation)
{
	/* unsigned int cpu;   */
	/* struct cpufreq_freqs freqs;   */
	unsigned int new_opp_idx;

	enum mt_cpu_dvfs_id id = _get_cpu_dvfs_id(policy->cpu);

	/* unsigned long flags;    */
	int ret = 0; /* -EINVAL; */

	FUNC_ENTER(FUNC_LV_MODULE);

	if (policy->cpu >= max_cpu_num
		|| (id_to_cpu_dvfs(id)
		&& id_to_cpu_dvfs(id)->dvfs_disable_by_procfs))
		return -EINVAL;

	new_opp_idx = cpufreq_frequency_table_target(policy, target_freq
						     , relation);
	if (new_opp_idx < 0)
		return -new_opp_idx;

	/* freqs.old = policy->cur;   */
	/* XXX: move to _cpufreq_set_locked() */
	/* freqs.new = mt_cpufreq_max_frequency_by_DVS(id, new_opp_idx); */
	/* freqs.cpu = policy->cpu;   */

	/* XXX: move to _cpufreq_set_locked() */
	/* for_each_online_cpu(cpu) { // TODO: big LITTLE issue (id mapping) */
	/* freqs.cpu = cpu;    */
	/* XXX: move to _cpufreq_set_locked() */
	/* cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE); */
	/* }    */

	/* cpufreq_lock(flags);   */

#ifdef CONFIG_CPU_DVFS_AEE_RR_REC
	aee_rr_rec_cpu_dvfs_status(aee_rr_curr_cpu_dvfs_status()
				   | (1 << CPU_DVFS_LITTLE_IS_DOING_DVFS));
#endif

	_mt_cpufreq_set(id, new_opp_idx);

#ifdef CONFIG_CPU_DVFS_AEE_RR_REC
	aee_rr_rec_cpu_dvfs_status(aee_rr_curr_cpu_dvfs_status()
				   & ~(1 << CPU_DVFS_LITTLE_IS_DOING_DVFS));
#endif

	/* cpufreq_unlock(flags);   */

	/* XXX: move to _cpufreq_set_locked() */
	/* for_each_online_cpu(cpu) { */
	/* freqs.cpu = cpu;   */
	/* XXX: move to _cpufreq_set_locked() */
	/* cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE); */
	/* }   */

	FUNC_EXIT(FUNC_LV_MODULE);

	return ret;
}

static int _mt_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret = -EINVAL;
	unsigned int opp_idx;

	FUNC_ENTER(FUNC_LV_MODULE);

	max_cpu_num = num_possible_cpus();

	if (policy->cpu >= max_cpu_num)
		return -EINVAL;

	cpufreq_info("@%s: max_cpu_num: %d\n", __func__, max_cpu_num);

	policy->shared_type = CPUFREQ_SHARED_TYPE_ANY;
	cpumask_setall(policy->cpus);

	/*******************************************************
	 * 1 us, assumed, will be overwrited by min_sampling_rate
	 ********************************************************/
	policy->cpuinfo.transition_latency = 1000;

	/*********************************************
	 * set default policy and cpuinfo, unit : Khz
	 **********************************************/
	{
#define DORMANT_MODE_VOLT   80000

	enum mt_cpu_dvfs_id id = _get_cpu_dvfs_id(policy->cpu);
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);
	unsigned int lv = _mt_cpufreq_get_cpu_level();
	struct opp_tbl_info *opp_tbl_info = &opp_tbls[CPU_LV_TO_OPP_IDX(lv)];

	WARN_ON(p == NULL);
	WARN_ON(!(lv == CPU_LEVEL_0 || lv == CPU_LEVEL_1));

	p->cpu_level = lv;

	ret = _mt_cpufreq_setup_freqs_table(policy,
		opp_tbl_info->opp_tbl,
		opp_tbl_info->size
		);

	policy->cpuinfo.max_freq = cpu_dvfs_get_max_freq(id_to_cpu_dvfs(id));
	policy->cpuinfo.min_freq = cpu_dvfs_get_min_freq(id_to_cpu_dvfs(id));

	policy->cur = _mt_cpufreq_get_cur_phy_freq(id);
	opp_idx = cpufreq_frequency_table_target(policy, policy->cur
						 , CPUFREQ_RELATION_L);
	policy->cur = cpu_dvfs_get_freq_by_idx(p, opp_idx);
	policy->max = cpu_dvfs_get_max_freq(id_to_cpu_dvfs(id));
	policy->min = cpu_dvfs_get_min_freq(id_to_cpu_dvfs(id));

	/* sync p->idx_opp_tbl first before _restore_default_volt() */
	if (_sync_opp_tbl_idx(p) >= 0)
		p->idx_normal_max_opp = p->idx_opp_tbl;

	/* restore default volt, sync opp idx, set default limit */
	_restore_default_volt(p);

	_set_no_limited(p);
#ifdef CONFIG_CPU_DVFS_DOWNGRADE_FREQ
	_init_downgrade(p, _mt_cpufreq_get_cpu_level());
#endif
#ifdef CONFIG_CPU_DVFS_POWER_THROTTLING
#ifdef PMIC_6325
	register_battery_percent_notify(&bat_per_protection_powerlimit
					, BATTERY_PERCENT_PRIO_CPU_L);
	register_battery_oc_notify(&bat_oc_protection_powerlimit
				   , BATTERY_OC_PRIO_CPU_L);
	register_low_battery_notify(&Lbat_protection_powerlimit
				    , LOW_BATTERY_PRIO_CPU_L);
#endif
#endif
	}

	if (ret)
		cpufreq_err("failed to setup frequency table\n");

	FUNC_EXIT(FUNC_LV_MODULE);

	return ret;
}

static unsigned int _mt_cpufreq_get(unsigned int cpu)
{
	struct mt_cpu_dvfs *p;

	FUNC_ENTER(FUNC_LV_MODULE);

	p = id_to_cpu_dvfs(_get_cpu_dvfs_id(cpu));

	WARN_ON(p == NULL);

	FUNC_EXIT(FUNC_LV_MODULE);

	return cpu_dvfs_get_cur_freq(p);
}

#define FIX_FREQ_WHEN_SCREEN_OFF	1
#if FIX_FREQ_WHEN_SCREEN_OFF
static bool _allow_dpidle_ctrl_vproc;
#else
/* return _allow_dpidle_ctrl_vproc; */
#define VPROC_THRESHOLD_TO_DEEPIDLE	990
#endif
bool mt_cpufreq_earlysuspend_status_get(void)
{
#if FIX_FREQ_WHEN_SCREEN_OFF
	return _allow_dpidle_ctrl_vproc;
#else
	int	ret = 0;

	if (regulator_ext_vproc_voltage
	    && (regulator_ext_vproc_voltage < VPROC_THRESHOLD_TO_DEEPIDLE))
		ret = 1;

	return ret;
#endif
}
EXPORT_SYMBOL(mt_cpufreq_earlysuspend_status_get);

static void _mt_cpufreq_lcm_status_switch(int onoff)
{
	struct mt_cpu_dvfs *p;
	int i;
	#ifdef CONFIG_CPU_FREQ
	struct cpufreq_policy *policy;
	#endif

	cpufreq_info("@%s: LCM is %s\n", __func__, (onoff) ? "on" : "off");

	/* onoff = 0: LCM OFF */
	/* others: LCM ON */
	if (onoff) {
		_allow_dpidle_ctrl_vproc = false;

		for_each_cpu_dvfs(i, p) {
			if (!cpu_dvfs_is_available(p))
				continue;

			p->dvfs_disable_by_early_suspend = false;


#ifdef CONFIG_CPU_FREQ
				policy = cpufreq_cpu_get(p->cpu_id);

				if (policy) {
					cpufreq_driver_target(
						policy,
						cpu_dvfs_get_freq_by_idx(
						p,
						p->idx_opp_tbl_for_late_resume
						),
						CPUFREQ_RELATION_L
					);
					cpufreq_cpu_put(policy);
				}
#endif
		}
	} else {
		for_each_cpu_dvfs(i, p) {
			if (!cpu_dvfs_is_available(p))
				continue;

			p->dvfs_disable_by_early_suspend = true;

			p->idx_opp_tbl_for_late_resume = p->idx_opp_tbl;

#ifdef CONFIG_CPU_FREQ
				policy = cpufreq_cpu_get(p->cpu_id);

				if (policy) {
					cpufreq_driver_target(
						policy,
						cpu_dvfs_get_normal_max_freq(p)
						, CPUFREQ_RELATION_L);
					cpufreq_cpu_put(policy);
				}
#endif
		}
		_allow_dpidle_ctrl_vproc = true;
	}
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void _mt_cpufreq_early_suspend(struct early_suspend *h)
{
	FUNC_ENTER(FUNC_LV_MODULE);

	_mt_cpufreq_lcm_status_switch(0);

	FUNC_EXIT(FUNC_LV_MODULE);
}

static void _mt_cpufreq_late_resume(struct early_suspend *h)
{
	FUNC_ENTER(FUNC_LV_MODULE);

	_mt_cpufreq_lcm_status_switch(1);

	FUNC_EXIT(FUNC_LV_MODULE);
}

static struct early_suspend _mt_cpufreq_early_suspend_handler = {
	.level    = EARLY_SUSPEND_LEVEL_DISABLE_FB + 200,
	.suspend  = _mt_cpufreq_early_suspend,
	.resume   = _mt_cpufreq_late_resume,
};
#else
static int _mt_cpufreq_fb_notifier_callback(struct notifier_block *self
	, unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int blank;

	FUNC_ENTER(FUNC_LV_MODULE);

	/* skip if it's not a blank event */
	if (event != FB_EVENT_BLANK)
		return 0;

	if (evdata == NULL)
		return 0;
	if (evdata->data == NULL)
		return 0;

	blank = *(int *)evdata->data;

	cpufreq_ver("@%s: blank = %d, event = %lu\n", __func__, blank, event);

	switch (blank) {
	/* LCM ON */
	case FB_BLANK_UNBLANK:
		_mt_cpufreq_lcm_status_switch(1);
		break;
	/* LCM OFF */
	case FB_BLANK_POWERDOWN:
		_mt_cpufreq_lcm_status_switch(0);
		break;
	default:
		break;
	}

	FUNC_EXIT(FUNC_LV_MODULE);

	return 0;
}

static struct notifier_block _mt_cpufreq_fb_notifier = {
	.notifier_call = _mt_cpufreq_fb_notifier_callback,
};

#endif /* CONFIG_HAS_EARLYSUSPEND */

#ifdef CONFIG_CPU_FREQ
#ifdef CONFIG_AMAZON_THERMAL
static void _mt_cpufreq_ready(struct cpufreq_policy *policy)
{
        struct mt_cpu_dvfs *info = id_to_cpu_dvfs(_get_cpu_dvfs_id(policy->cpu));
	struct device *cpu_dev = get_cpu_device(policy->cpu);
	struct device_node *np = of_node_get(cpu_dev->of_node);

        if (WARN_ON(!np))
                return;

        if (of_find_property(np, "#cooling-cells", NULL)) {

                info->cdev = of_cpufreq_power_cooling_register(np,
                                                policy->related_cpus,
                                                0, NULL);

                if (IS_ERR(info->cdev)) {
                        dev_err(cpu_dev,
                                "running cpufreq without cooling device: %ld\n",
                                PTR_ERR(info->cdev));

                        info->cdev = NULL;
                }
        }

        of_node_put(np);

}

static int mtk_cpufreq_exit(struct cpufreq_policy *policy)
{
        struct mt_cpu_dvfs *info = id_to_cpu_dvfs(_get_cpu_dvfs_id(policy->cpu));
	struct device *cpu_dev = get_cpu_device(policy->cpu);

	cpufreq_cooling_unregister(info->cdev);
	dev_pm_opp_free_cpufreq_table(cpu_dev, &policy->freq_table);

	return 0;
}
#endif
static struct freq_attr *_mt_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver _mt_cpufreq_driver = {
	.flags = CPUFREQ_ASYNC_NOTIFICATION,
	.verify = _mt_cpufreq_verify,
	.target = _mt_cpufreq_target,
	.init   = _mt_cpufreq_init,
#ifdef CONFIG_AMAZON_THERMAL
	.exit = mtk_cpufreq_exit,
	.ready  = _mt_cpufreq_ready,
#endif
	.get    = _mt_cpufreq_get,
	.name   = "mt-cpufreq",
	.attr   = _mt_cpufreq_attr,
};
#endif

/*
 * Platform driver
 */
static int _mt_cpufreq_suspend(struct device *dev)
{
	/* struct cpufreq_policy *policy; */
	struct mt_cpu_dvfs *p;
	int i;

	FUNC_ENTER(FUNC_LV_MODULE);

	/* mt_cpufreq_set_pmic_phase(PMIC_WRAP_PHASE_SUSPEND); */

	for_each_cpu_dvfs(i, p) {
		if (!cpu_dvfs_is_available(p))
			continue;

		p->dvfs_disable_by_suspend = true;

		/* XXX: useless, decided @ _calc_new_opp_idx() */
		_mt_cpufreq_set(MT_CPU_DVFS_LITTLE, p->idx_normal_max_opp);
	}

	FUNC_EXIT(FUNC_LV_MODULE);

	return 0;
}

static int _mt_cpufreq_resume(struct device *dev)
{
	struct mt_cpu_dvfs *p;
	int i;

	FUNC_ENTER(FUNC_LV_MODULE);

	/* mt_cpufreq_set_pmic_phase(PMIC_WRAP_PHASE_NORMAL); */

	for_each_cpu_dvfs(i, p) {
		if (!cpu_dvfs_is_available(p))
			continue;

		p->dvfs_disable_by_suspend = false;
	}

	FUNC_EXIT(FUNC_LV_MODULE);

	return 0;
}

/* for IPO-H HW(freq) / SW(opp_tbl_idx) */ /* TODO: DON'T CARE??? */
static int _mt_cpufreq_pm_restore_early(struct device *dev)
{
	FUNC_ENTER(FUNC_LV_MODULE);

	_mt_cpufreq_sync_opp_tbl_idx();

	FUNC_EXIT(FUNC_LV_MODULE);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mtcpufreq_of_match[] = {
	{.compatible = "mediatek,mt8163-cpufreq",},
	{},
};

MODULE_DEVICE_TABLE(of, mtcpufreq_of_match);

void __iomem *mtcpufreq_base;

#endif

static int _mt_cpufreq_pdrv_probe(struct platform_device *pdev)
{
	int	ret;

	FUNC_ENTER(FUNC_LV_MODULE);

	/* TODO: check extBuck init with James */

	if (pw.addr[0].cmd_addr == 0)
		_mt_cpufreq_pmic_table_init();

#ifdef CONFIG_OF
	reg_ext_vproc = devm_regulator_get(&pdev->dev, "reg-ext-vproc");
	if (IS_ERR(reg_ext_vproc)) {
		ret = PTR_ERR(reg_ext_vproc);
		dev_err(&pdev->dev, "Failed to request reg-ext-vproc: %d\n"
			, ret);
		return ret;
	}
	ret = regulator_enable(reg_ext_vproc);

#endif


	/* init static power table */
	mt_spower_init();

#ifdef CONFIG_CPU_DVFS_AEE_RR_REC
	_mt_cpufreq_aee_init();
#endif

	/* register early suspend */
#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&_mt_cpufreq_early_suspend_handler);
#else
	if (fb_register_client(&_mt_cpufreq_fb_notifier)) {
		cpufreq_err("@%s: register FB client failed!\n", __func__);
		return 0;
	}
#endif

	/* init PMIC_WRAP & volt */
	mt_cpufreq_set_pmic_phase(PMIC_WRAP_PHASE_NORMAL);

#ifdef CONFIG_CPU_FREQ
	cpufreq_register_driver(&_mt_cpufreq_driver);
#endif

	register_hotcpu_notifier(&turbo_mode_cpu_notifier);

	FUNC_EXIT(FUNC_LV_MODULE);

	return 0;
}

static int _mt_cpufreq_pdrv_remove(struct platform_device *pdev)
{
	FUNC_ENTER(FUNC_LV_MODULE);

	unregister_hotcpu_notifier(&turbo_mode_cpu_notifier);
#ifdef CONFIG_CPU_FREQ
	cpufreq_unregister_driver(&_mt_cpufreq_driver);
#endif

	FUNC_EXIT(FUNC_LV_MODULE);

	return 0;
}

static const struct dev_pm_ops _mt_cpufreq_pm_ops = {
	.suspend	= _mt_cpufreq_suspend,
	.resume		= _mt_cpufreq_resume,
	.restore_early	= _mt_cpufreq_pm_restore_early,
	.freeze		= _mt_cpufreq_suspend,
	.thaw		= _mt_cpufreq_resume,
	.restore	= _mt_cpufreq_resume,
};

static struct platform_driver _mt_cpufreq_pdrv = {
	.probe      = _mt_cpufreq_pdrv_probe,
	.remove     = _mt_cpufreq_pdrv_remove,
	.driver     = {
	.name   = "mt-cpufreq",
#ifdef CONFIG_OF
	.of_match_table = of_match_ptr(mtcpufreq_of_match),
#endif
	.pm     = &_mt_cpufreq_pm_ops,
	.owner  = THIS_MODULE,
	},
};

#ifndef __KERNEL__
/*
 * For CTP
 */
int mt_cpufreq_pdrv_probe(void)
{
	static struct cpufreq_policy policy;

	_mt_cpufreq_pdrv_probe(NULL);

	policy.cpu = cpu_dvfs[MT_CPU_DVFS_LITTLE].cpu_id;
	_mt_cpufreq_init(&policy);

	return 0;
}

int mt_cpufreq_set_opp_volt(enum mt_cpu_dvfs_id id, int idx)
{
	int ret = 0;
	static struct opp_tbl_info *info;
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	info = &opp_tbls[CPU_LV_TO_OPP_IDX(p->cpu_level)];
	if (idx >= info->size)
		return -1;

	return _set_cur_volt_locked(p, info->opp_tbl[idx].cpufreq_volt);
}

int mt_cpufreq_set_freq(enum mt_cpu_dvfs_id id, int idx)
{
	unsigned int cur_freq;
	unsigned int target_freq;
	int ret;
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	cur_freq = p->ops->get_cur_phy_freq(p);
	target_freq = cpu_dvfs_get_freq_by_idx(p, idx);

	ret = _cpufreq_set_locked(p, cur_freq, target_freq);

	if (ret < 0)
		return ret;

	return target_freq;
}

#include "dvfs.h"

static unsigned int _mt_get_cpu_freq(void)
{
	unsigned int output = 0, i = 0;
	unsigned int temp, clk26cali_0, clk_dbg_cfg;
	unsigned int clk_misc_cfg_0, clk26cali_1;

	clk26cali_0 = DRV_Reg32(CLK26CALI_0);

	clk_dbg_cfg = DRV_Reg32(CLK_DBG_CFG);
	/* sel abist_cksw and enable freq meter sel abist */
	DRV_WriteReg32(CLK_DBG_CFG, 2<<16);

	clk_misc_cfg_0 = DRV_Reg32(CLK_MISC_CFG_0);
	/* select divider */
	DRV_WriteReg32(CLK_MISC_CFG_0
		       , (clk_misc_cfg_0 & 0x0000FFFF) | (0x07 << 16));

	clk26cali_1 = DRV_Reg32(CLK26CALI_1);
	DRV_WriteReg32(CLK26CALI_1, 0x00ff0000); /*  */

	/* temp = DRV_Reg32(CLK26CALI_0); */
	DRV_WriteReg32(CLK26CALI_0, 0x1000);
	DRV_WriteReg32(CLK26CALI_0, 0x1010);

	/* wait frequency meter finish */
	while (DRV_Reg32(CLK26CALI_0) & 0x10) {
		mdelay(10);
		i++;
		if (i > 10)
			break;
	}

	temp = DRV_Reg32(CLK26CALI_1) & 0xFFFF;

	output = (((temp * 26000)) / 256) * 8; /* Khz */

	DRV_WriteReg32(CLK_DBG_CFG, clk_dbg_cfg);
	DRV_WriteReg32(CLK_MISC_CFG_0, clk_misc_cfg_0);
	DRV_WriteReg32(CLK26CALI_0, clk26cali_0);
	DRV_WriteReg32(CLK26CALI_1, clk26cali_1);

	cpufreq_dbg("CLK26CALI_1 = 0x%x, CPU freq = %d KHz\n", temp, output);

	if (i > 10) {
		cpufreq_dbg("meter not finished!\n");
		return 0;
	} else
		return output;
}

unsigned int dvfs_get_cpu_freq(enum mt_cpu_dvfs_id id)
{
	/* return _mt_cpufreq_get_cur_phy_freq(id); */
	return _mt_get_cpu_freq();
}

void dvfs_set_cpu_freq_FH(enum mt_cpu_dvfs_id id, int freq)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);
	int idx;

	if (!p) {
		cpufreq_err("%s(%d, %d), id is wrong\n", __func__, id, freq);
		return;
	}

	idx = _search_available_freq_idx(p, freq, CPUFREQ_RELATION_H);

	if (idx == -1) {
		cpufreq_err("%s(%d, %d), freq is wrong\n", __func__, id, freq);
		return;
	}

	mt_cpufreq_set_freq(id, idx);
}

unsigned int cpu_frequency_output_slt(enum mt_cpu_dvfs_id id)
{
	return (id == MT_CPU_DVFS_LITTLE) ? _mt_get_cpu_freq() : 0;
}

void dvfs_set_cpu_volt(enum mt_cpu_dvfs_id id, int volt)  /* volt: mv * 1000 */
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(id);

	cpufreq_dbg("%s(%d, %d)\n", __func__, id, volt);

	if (!p) {
		cpufreq_err("%s(%d, %d), id is wrong\n", __func__, id, volt);
		return;
	}

	if (_set_cur_volt_locked(p, volt))
		cpufreq_err("%s(%d, %d), set volt fail\n", __func__, id, volt);

	cpufreq_dbg("%s(%d, %d) Vproc = %d\n",
	__func__,
	id,
	volt,
	p->ops->get_cur_volt(p));
}

void dvfs_set_gpu_volt(int pmic_val)
{
	cpufreq_dbg("%s(%d)\n", __func__, pmic_val);
	mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_NORMAL, IDX_NM_VGPU, pmic_val);
	mt_cpufreq_apply_pmic_cmd(IDX_NM_VGPU);
}

/* NOTE: This is ONLY for PTPOD SLT. Should not adjust VCORE in other cases. */
void dvfs_set_vcore_ao_volt(int pmic_val)
{
	cpufreq_dbg("%s(%d)\n", __func__, pmic_val);
	mt_cpufreq_set_pmic_cmd(PMIC_WRAP_PHASE_NORMAL
				, IDX_NM_VCORE, pmic_val);
	mt_cpufreq_apply_pmic_cmd(IDX_NM_VCORE);
}

/* static unsigned int little_freq_backup; */
static unsigned int vcpu_backup;
static unsigned int vgpu_backup;
static unsigned int vcore_ao_backup;
/* static unsigned int vcore_pdn_backup; */

void dvfs_disable_by_ptpod(void)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(MT_CPU_DVFS_LITTLE);

	cpufreq_dbg("%s()\n", __func__);

	vcpu_backup = cpu_dvfs_get_cur_volt(p);
	pmic_read_interface(PMIC_ADDR_VGPU_VOSEL_ON, &vgpu_backup, 0x7F, 0);
	pmic_read_interface(PMIC_ADDR_VCORE_VOSEL_ON
			    , &vcore_ao_backup, 0x7F, 0);

	dvfs_set_cpu_volt(MT_CPU_DVFS_LITTLE, 1000000);    /* 1V */
	dvfs_set_gpu_volt(VOLT_TO_PMIC_VAL(1000000));      /* 1V */
	dvfs_set_vcore_ao_volt(VOLT_TO_PMIC_VAL(1000000)); /* 1V */
	/* dvfs_set_vcore_pdn_volt(0x30); */
}

void dvfs_enable_by_ptpod(void)
{
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(MT_CPU_DVFS_LITTLE);

	cpufreq_dbg("%s()\n", __func__);

	dvfs_set_cpu_volt(MT_CPU_DVFS_LITTLE, vcpu_backup);
	dvfs_set_gpu_volt(vgpu_backup);
	dvfs_set_vcore_ao_volt(vcore_ao_backup);
	/* dvfs_set_vcore_pdn_volt(vcore_pdn_backup); */
}
#endif /* ! __KERNEL__ */

#ifdef CONFIG_PROC_FS
/*
 * PROC
 */

static char *_copy_from_user_for_proc(const char __user *buffer, size_t count)
{
	char *buf = (char *)__get_free_page(GFP_USER);

	if (!buf)
		return NULL;

	if (count >= PAGE_SIZE)
		goto out;

	if (copy_from_user(buf, buffer, count))
		goto out;

	buf[count] = '\0';

	return buf;

out:
	free_page((unsigned long)buf);

	return NULL;
}

/* cpufreq_debug */
static int cpufreq_debug_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "cpufreq debug (log level) = %d\n", func_lv_mask);

	return 0;
}

static ssize_t cpufreq_debug_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	unsigned int dbg_lv;

	char *buf = _copy_from_user_for_proc(buffer, count);

	if (!buf)
		return -EINVAL;

	if (!kstrtouint(buf, 10, &dbg_lv))
		func_lv_mask = dbg_lv;
	else {
		cpufreq_err("echo dbg_lv (dec) >");
		cpufreq_err(" /proc/cpufreq/cpufreq_debug\n");
	}

	free_page((unsigned long)buf);
	return count;
}

/* cpufreq_downgrade_freq_info */
static int cpufreq_downgrade_freq_info_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	seq_printf(m, "downgrade_freq_counter_limit = %d\n"
		"ptpod_temperature_limit_1 = %d\n"
		"ptpod_temperature_limit_2 = %d\n"
		"ptpod_temperature_time_1 = %d\n"
		"ptpod_temperature_time_2 = %d\n"
		"downgrade_freq_counter_return_limit 1 = %d\n"
		"downgrade_freq_counter_return_limit 2 = %d\n"
		"over_max_cpu = %d\n",
		p->downgrade_freq_counter_limit,
		p->ptpod_temperature_limit_1,
		p->ptpod_temperature_limit_2,
		p->ptpod_temperature_time_1,
		p->ptpod_temperature_time_2,
		p->ptpod_temperature_limit_1 * p->ptpod_temperature_time_1,
		p->ptpod_temperature_limit_2 * p->ptpod_temperature_time_2,
		p->over_max_cpu
		);

	return 0;
}

/* cpufreq_downgrade_freq_counter_limit */
static int cpufreq_downgrade_freq_counter_limit_proc_show(struct seq_file *m
	, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	seq_printf(m, "%d\n", p->downgrade_freq_counter_limit);

	return 0;
}

static ssize_t cpufreq_downgrade_freq_counter_limit_proc_write(
	struct file *file, const char __user *buffer
	, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	int downgrade_freq_counter_limit;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if (!kstrtoint(buf, 10, &downgrade_freq_counter_limit))
		p->downgrade_freq_counter_limit = downgrade_freq_counter_limit;
	else {
		cpufreq_err("echo downgrade_freq_counter_limit (dec)");
		cpufreq_err(" > /proc/cpufreq/");
		cpufreq_err("cpufreq_downgrade_freq_counter_limit\n");
	}

	free_page((unsigned long)buf);
	return count;
}

/* cpufreq_downgrade_freq_counter_return_limit */
static int cpufreq_downgrade_freq_counter_return_limit_proc_show(
	struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	seq_printf(m, "%d\n", p->downgrade_freq_counter_return_limit);

	return 0;
}

static ssize_t cpufreq_downgrade_freq_counter_return_limit_proc_write(
	struct file *file, const char __user *buffer
	, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	int downgrade_freq_counter_return_limit;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if (!kstrtoint(buf, 10, &downgrade_freq_counter_return_limit))
		p->downgrade_freq_counter_return_limit
		= downgrade_freq_counter_return_limit;
	else {
		cpufreq_err("echo downgrade_freq_counter_return_limit (dec)");
		cpufreq_err(" > /proc/cpufreq/");
		cpufreq_err("cpufreq_downgrade_freq_counter_return_limit\n");
	}

	free_page((unsigned long)buf);
	return count;
}

/* cpufreq_fftt_test */
#include <linux/sched_clock.h>

static unsigned long _delay_us;
static unsigned long _delay_us_buf;

static int cpufreq_fftt_test_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", _delay_us);

	if (_delay_us < _delay_us_buf)
		cpufreq_err("@%s(), %lu < %lu, loops_per_jiffy = %lu\n"
		, __func__, _delay_us, _delay_us_buf, loops_per_jiffy);

	return 0;
}

static ssize_t cpufreq_fftt_test_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	char *buf = _copy_from_user_for_proc(buffer, count);

	if (!buf)
		return -EINVAL;

	if (!kstrtoul(buf, 10, &_delay_us_buf)) {
		unsigned long start;

		start = (unsigned long)sched_clock();
		udelay(_delay_us_buf);
		_delay_us = ((unsigned long)sched_clock() - start) / 1000;

		cpufreq_ver("@%s(%lu)\n", __func__, _delay_us_buf);
		cpufreq_ver(", _delay_us = %lu", _delay_us);
		cpufreq_ver(", loops_per_jiffy = %lu\n", loops_per_jiffy);
	}

	free_page((unsigned long)buf);

	return count;
}

static int cpufreq_stress_test_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", do_dvfs_stress_test);

	return 0;
}

static ssize_t cpufreq_stress_test_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	unsigned int do_stress;

	char *buf = _copy_from_user_for_proc(buffer, count);

	if (!buf)
		return -EINVAL;

	if (!kstrtouint(buf, 10, &do_stress))
		do_dvfs_stress_test = do_stress;
	else
		cpufreq_err("echo 0/1 > /proc/cpufreq/cpufreq_stress_test\n");

	free_page((unsigned long)buf);
	return count;
}

static int cpufreq_fix_freq_in_es_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", is_fix_freq_in_ES);

	return 0;
}

static ssize_t cpufreq_fix_freq_in_es_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	unsigned int fix_freq_in_ES;

	char *buf = _copy_from_user_for_proc(buffer, count);

	if (!buf)
		return -EINVAL;

	if (!kstrtouint(buf, 10, &fix_freq_in_ES))
		is_fix_freq_in_ES = fix_freq_in_ES;
	else {
		cpufreq_err("echo 0/1 > ");
		cpufreq_err("/proc/cpufreq/cpufreq_fix_freq_in_es\n");
	}

	free_page((unsigned long)buf);
	return count;
}

static int cpufreq_emi_dfs_proc_show(struct seq_file *m, void *v)
{
	return 0;
}

static ssize_t cpufreq_emi_dfs_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	char *buf = _copy_from_user_for_proc(buffer, count);
	unsigned int tmp;

	if (!buf)
		return -EINVAL;

	if (!kstrtouint(buf, 10, &tmp)) {
		if (tmp == 0 || tmp == 1)
			gEMI_DFS_enable = tmp;
		else {
			cpufreq_err("echo [0|1] > ");
			cpufreq_err("/proc/cpufreq/cpufreq_emi_dfs\n");
		}
	} else
		cpufreq_err("echo [0|1] > /proc/cpufreq/cpufreq_emi_dfs\n");

	free_page((unsigned long)buf);

	return count;
}

/* cpufreq_limited_by_hevc */
static int cpufreq_limited_by_hevc_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	seq_printf(m, "%d\n", p->limited_freq_by_hevc);

	return 0;
}

static ssize_t cpufreq_limited_by_hevc_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	int limited_freq_by_hevc;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if (!kstrtoint(buf, 10, &limited_freq_by_hevc)) {
		p->limited_freq_by_hevc = limited_freq_by_hevc;
		if (cpu_dvfs_is_available(p)
		    && (p->limited_freq_by_hevc > cpu_dvfs_get_cur_freq(p))) {
#ifdef CONFIG_CPU_FREQ
			struct cpufreq_policy *policy
				= cpufreq_cpu_get(p->cpu_id);

			if (policy) {
				cpufreq_driver_target(policy
						      , p->limited_freq_by_hevc
						      , CPUFREQ_RELATION_L);
				cpufreq_cpu_put(policy);
			}
#endif
		}
	} else {
		cpufreq_err("echo limited_freq_by_hevc (dec) >");
		cpufreq_err(" /proc/cpufreq/cpufreq_limited_by_hevc\n");
	}

	free_page((unsigned long)buf);
	return count;
}

static int cpufreq_limited_max_freq_by_user_proc_show(struct seq_file *m
	, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	seq_printf(m, "%d\n", p->limited_max_freq_by_user);

	return 0;
}

static ssize_t cpufreq_limited_max_freq_by_user_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	int limited_max_freq;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if (!kstrtoint(buf, 10, &limited_max_freq)) {
		p->limited_max_freq_by_user = limited_max_freq;

		if (cpu_dvfs_is_available(p)
		    && (p->limited_max_freq_by_user)
		    && (p->limited_max_freq_by_user
			< cpu_dvfs_get_cur_freq(p))) {
			struct cpufreq_policy *policy
				= cpufreq_cpu_get(p->cpu_id);

			if (policy) {
				cpufreq_driver_target(policy
					, p->limited_max_freq_by_user
					, CPUFREQ_RELATION_H);
				cpufreq_cpu_put(policy);
			}
		}
	} else {
		cpufreq_err("echo limited_max_freq (dec) > ");
		cpufreq_err("/proc/cpufreq/%s/", p->name);
		cpufreq_err("cpufreq_limited_max_freq_by_user\n");
	}

	free_page((unsigned long)buf);
	return count;
}


/* cpufreq_limited_power */
static int cpufreq_limited_power_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p;
	int i;

	for_each_cpu_dvfs(i, p) {
		seq_printf(m, "[%s/%d] %d\n"
		"limited_max_freq = %d\n"
		"limited_max_ncpu = %d\n",
		p->name, i, p->thermal_protect_limited_power,
		p->limited_max_freq,
		p->limited_max_ncpu
		);
	}

	return 0;
}

static ssize_t cpufreq_limited_power_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	int limited_power;

	char *buf = _copy_from_user_for_proc(buffer, count);

	if (!buf)
		return -EINVAL;

	if (!kstrtoint(buf, 10, &limited_power))
		mt_cpufreq_thermal_protect(limited_power);
	else {
		cpufreq_err("echo limited_power (dec) > ");
		cpufreq_err("/proc/cpufreq/cpufreq_limited_power\n");
	}

	free_page((unsigned long)buf);
	return count;
}

/* cpufreq_over_max_cpu */
static int cpufreq_over_max_cpu_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	seq_printf(m, "%d\n", p->over_max_cpu);

	return 0;
}

static ssize_t cpufreq_over_max_cpu_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	int over_max_cpu;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if (!kstrtoint(buf, 10, &over_max_cpu))
		p->over_max_cpu = over_max_cpu;
	else {
		cpufreq_err("echo over_max_cpu (dec) > ");
		cpufreq_err("/proc/cpufreq/cpufreq_over_max_cpu\n");
	}

	free_page((unsigned long)buf);
	return count;
}

/* cpufreq_power_dump */
static int cpufreq_power_dump_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p;
	int i, j;

	for_each_cpu_dvfs(i, p) {
		seq_printf(m, "[%s/%d]\n", p->name, i);

		for (j = 0; j < p->nr_power_tbl; j++) {
			seq_printf(m, "[%d] = { .cpufreq_khz = %d,\t",
				j, p->power_tbl[j].cpufreq_khz);
			seq_printf(m, ".cpufreq_ncpu = %d,\t",
				p->power_tbl[j].cpufreq_ncpu);
			seq_printf(m, ".cpufreq_power = %d, },\n",
				p->power_tbl[j].cpufreq_power);
		}
	}

	return 0;
}

/* cpufreq_ptpod_freq_volt */
static int cpufreq_ptpod_freq_volt_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;
	int j;

	for (j = 0; j < p->nr_opp_tbl; j++) {
		seq_printf(m,
		"[%d] = { .cpufreq_khz = %d,\t.cpufreq_volt = %d,",
		j,
		p->opp_tbl[j].cpufreq_khz,
		p->opp_tbl[j].cpufreq_volt);
		seq_printf(m, "\t.cpufreq_volt_org = %d, },\n",
		p->opp_tbl[j].cpufreq_volt_org);
	}

	return 0;
}

/* cpufreq_ptpod_temperature_limit */
static int cpufreq_ptpod_temperature_limit_proc_show(struct seq_file *m
	, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	seq_printf(m, "ptpod_temperature_limit_1 = %d\n"
		"ptpod_temperature_limit_2 = %d\n",
		p->ptpod_temperature_limit_1,
		p->ptpod_temperature_limit_2
		);

	return 0;
}

static ssize_t cpufreq_ptpod_temperature_limit_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	int ptpod_temperature_limit_1;
	int ptpod_temperature_limit_2;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if (sscanf(buf, "%d %d", &ptpod_temperature_limit_1
		   , &ptpod_temperature_limit_2) == 2) {
		p->ptpod_temperature_limit_1 = ptpod_temperature_limit_1;
		p->ptpod_temperature_limit_2 = ptpod_temperature_limit_2;
	} else {
		cpufreq_err("echo ptpod_temperature_limit_1 (dec) ");
		cpufreq_err("ptpod_temperature_limit_2 (dec)");
		cpufreq_err(" > /proc/cpufreq/\n");
		cpufreq_err("cpufreq_ptpod_temperature_limit\n");
	}

	free_page((unsigned long)buf);
	return count;
}

/* cpufreq_ptpod_temperature_time */
static int cpufreq_ptpod_temperature_time_proc_show(struct seq_file *m
	, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	seq_printf(m, "ptpod_temperature_time_1 = %d\n"
		"ptpod_temperature_time_2 = %d\n",
		p->ptpod_temperature_time_1,
		p->ptpod_temperature_time_2
		);

	return 0;
}

static ssize_t cpufreq_ptpod_temperature_time_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	int ptpod_temperature_time_1;
	int ptpod_temperature_time_2;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if (sscanf(buf, "%d %d", &ptpod_temperature_time_1
		   , &ptpod_temperature_time_2) == 2) {
		p->ptpod_temperature_time_1 = ptpod_temperature_time_1;
		p->ptpod_temperature_time_2 = ptpod_temperature_time_2;
	} else {
		cpufreq_err("echo ptpod_temperature_time_1 ");
		cpufreq_err("(dec) ptpod_temperature_time_2 (dec)");
		cpufreq_err(" > /proc/cpufreq/");
		cpufreq_err("cpufreq_ptpod_temperature_time\n");
	}

	free_page((unsigned long)buf);
	return count;
}

/* cpufreq_ptpod_test */
static int cpufreq_ptpod_test_proc_show(struct seq_file *m, void *v)
{
	return 0;
}

static ssize_t cpufreq_ptpod_test_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	return count;
}

/* cpufreq_state */
static int cpufreq_state_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p;
	int i;

	for_each_cpu_dvfs(i, p) {
		seq_printf(m, "[%s/%d]\n"
			"dvfs_disable_by_procfs = %d\n"
			"limited_freq_by_hevc = %d KHz\n"
#ifdef CONFIG_CPU_DVFS_DOWNGRADE_FREQ
			"downgrade_freq_for_ptpod = %d\n"
#endif
			"thermal_protect_limited_power = %d mW\n"
			"dvfs_disable_by_early_suspend = %d\n"
			"dvfs_disable_by_suspend = %d\n"
#ifdef CONFIG_CPU_DVFS_POWER_THROTTLING
			"pwr_thro_mode = %d\n"
#endif
			"limited_max_freq_by_user = %d KHz\n"
			"dvfs_disable_by_ptpod = %d\n",
			p->name, i,
			p->dvfs_disable_by_procfs,
			p->limited_freq_by_hevc,
#ifdef CONFIG_CPU_DVFS_DOWNGRADE_FREQ
			p->downgrade_freq_for_ptpod,
#endif
			p->thermal_protect_limited_power,
			p->dvfs_disable_by_early_suspend,
			p->dvfs_disable_by_suspend,
#ifdef CONFIG_CPU_DVFS_POWER_THROTTLING
			p->pwr_thro_mode,
#endif
			p->limited_max_freq_by_user,
			p->dvfs_disable_by_ptpod
			);
	}

	return 0;
}

static ssize_t cpufreq_state_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	char *buf = _copy_from_user_for_proc(buffer, count);
	int enable;

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if (!kstrtoint(buf, 10, &enable)) {
		if (enable == 0)
			p->dvfs_disable_by_procfs = true;
		else
			p->dvfs_disable_by_procfs = false;
	} else
		cpufreq_err("echo 1/0 > /proc/cpufreq/cpufreq_state\n");

	free_page((unsigned long)buf);
	return count;
}

/* cpufreq_oppidx */
static int cpufreq_oppidx_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;
	int j;

	seq_printf(m, "[%s/%d]\n"
		"cpufreq_oppidx = %d\n",
		p->name, p->cpu_id,
		p->idx_opp_tbl
		);

	for (j = 0; j < p->nr_opp_tbl; j++) {
		seq_printf(m, "\tOP(%d, %d),\n",
		cpu_dvfs_get_freq_by_idx(p, j),
		cpu_dvfs_get_volt_by_idx(p, j)
		);
	}

	return 0;
}

static ssize_t cpufreq_oppidx_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	int oppidx;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	WARN_ON(p == NULL);

	if (!kstrtoint(buf, 10, &oppidx)
		&& 0 <= oppidx && oppidx < p->nr_opp_tbl) {
		p->dvfs_disable_by_procfs = true;
		_mt_cpufreq_set(MT_CPU_DVFS_LITTLE, oppidx);
	} else {
		p->dvfs_disable_by_procfs = false;
		cpufreq_err("echo oppidx > /proc/cpufreq/cpufreq_oppidx ");
		cpufreq_err("(0 <= %d < %d)\n", oppidx, p->nr_opp_tbl);
	}

	free_page((unsigned long)buf);

	return count;
}

/* cpufreq_freq */
static int cpufreq_freq_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	seq_printf(m, "%d KHz\n", p->ops->get_cur_phy_freq(p));

	return 0;
}

static ssize_t cpufreq_freq_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	unsigned long flags;
	struct mt_cpu_dvfs *p;
	unsigned int cur_freq;
	int freq, i, found;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	WARN_ON(p == NULL);

	if (!kstrtoint(buf, 10, &freq)) {
		if (freq < CPUFREQ_LAST_FREQ_LEVEL) {
			if (freq != 0) {
				cpufreq_err("frequency should higher than ");
				cpufreq_err("%dKHz!\n"
					    , CPUFREQ_LAST_FREQ_LEVEL);
			}
			p->dvfs_disable_by_procfs = false;
			goto end;
		} else {
			for (i = 0; i < p->nr_opp_tbl; i++) {
				if (freq == p->opp_tbl[i].cpufreq_khz) {
					found = 1;
					break;
				}
			}

			if (found == 1) {
				p->dvfs_disable_by_procfs = true;
				cpufreq_lock(flags);
				cur_freq = p->ops->get_cur_phy_freq(p);
				if (freq != cur_freq)
					p->ops->set_cur_freq(p, cur_freq, freq);
				cpufreq_unlock(flags);
			} else {
				p->dvfs_disable_by_procfs = false;
				cpufreq_err("frequency %dKHz! ", freq);
				cpufreq_err("is not found in CPU opp table\n");
			}
		}
	} else {
		p->dvfs_disable_by_procfs = false;
		cpufreq_err("echo khz > /proc/cpufreq/cpufreq_freq\n");
	}

end:
	free_page((unsigned long)buf);

	return count;
}

/* cpufreq_volt */
static int cpufreq_volt_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;

	#ifdef CONFIG_OF
	seq_printf(m, "%d mv\n", p->ops->get_cur_volt(p) / 1000);  /* mv */
	#else
	if (cpu_dvfs_is_extbuck_valid())
		seq_printf(m, "Vproc: %d mv\n"
			   , p->ops->get_cur_volt(p) / 1000);
	else
		seq_printf(m, "%d mv\n", p->ops->get_cur_volt(p) / 1000);
	#endif

	return 0;
}

static ssize_t cpufreq_volt_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	unsigned long flags;
	struct mt_cpu_dvfs *p;
	int mv;

	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if (!kstrtoint(buf, 10, &mv)) {
		p->dvfs_disable_by_procfs = true;
		cpufreq_lock(flags);
		_set_cur_volt_locked(p, mv * 1000);
		cpufreq_unlock(flags);
	} else {
		p->dvfs_disable_by_procfs = false;
		cpufreq_err("echo mv > /proc/cpufreq/cpufreq_volt\n");
	}

	free_page((unsigned long)buf);

	return count;
}

/* cpufreq_turbo_mode */
static int cpufreq_turbo_mode_proc_show(struct seq_file *m, void *v)
{
	struct mt_cpu_dvfs *p = (struct mt_cpu_dvfs *)m->private;
	int i;

	seq_printf(m, "turbo_mode = %d\n", p->turbo_mode);

	for (i = 0; i < NR_TURBO_MODE; i++) {
		seq_printf(m, "[%d] = { .temp = %d,",
		i,
		turbo_mode_cfg[i].temp);
		seq_printf(m, " .freq_delta = %d, .volt_delta = %d }\n",
		turbo_mode_cfg[i].freq_delta,
		turbo_mode_cfg[i].volt_delta);
	}

	return 0;
}

static ssize_t cpufreq_turbo_mode_proc_write(struct file *file
	, const char __user *buffer, size_t count, loff_t *pos)
{
	struct mt_cpu_dvfs *p;
	unsigned int turbo_mode;
	int temp;
	int freq_delta;
	int volt_delta;
	char *buf = _copy_from_user_for_proc(buffer, count);

	p = (struct mt_cpu_dvfs *)PDE_DATA(file_inode(file));

	if (!buf)
		return -EINVAL;

	if ((sscanf(buf, "%d %d %d %d", &turbo_mode, &temp, &freq_delta
		    , &volt_delta) == 4)
		&& turbo_mode < NR_TURBO_MODE) {
		turbo_mode_cfg[turbo_mode].temp = temp;
		turbo_mode_cfg[turbo_mode].freq_delta = freq_delta;
		turbo_mode_cfg[turbo_mode].volt_delta = volt_delta;
	} else if (!kstrtouint(buf, 10, &turbo_mode))
		p->turbo_mode = turbo_mode;
	else {
		cpufreq_err("echo 0/1 > /proc/cpufreq/cpufreq_turbo_mode\n");
		cpufreq_err("echo idx temp freq_delta volt_delta >");
		cpufreq_err("    /proc/cpufreq/cpufreq_turbo_mode\n");
	}

	free_page((unsigned long)buf);

	return count;
}

#define PROC_FOPS_RW(name)						\
static int name ## _proc_open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, name ## _proc_show, PDE_DATA(inode));	\
}									\
static const struct file_operations name ## _proc_fops = {		\
	.owner          = THIS_MODULE,					\
	.open           = name ## _proc_open,				\
	.read           = seq_read,					\
	.llseek         = seq_lseek,					\
	.release        = single_release,				\
	.write          = name ## _proc_write,				\
}

#define PROC_FOPS_RO(name)						\
static int name ## _proc_open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, name ## _proc_show, PDE_DATA(inode));	\
}									\
static const struct file_operations name ## _proc_fops = {		\
	.owner          = THIS_MODULE,					\
	.open           = name ## _proc_open,				\
	.read           = seq_read,					\
	.llseek         = seq_lseek,					\
	.release        = single_release,				\
}

#define PROC_ENTRY(name)	{__stringify(name), &name ## _proc_fops}

PROC_FOPS_RW(cpufreq_debug);
PROC_FOPS_RW(cpufreq_fftt_test);
PROC_FOPS_RW(cpufreq_stress_test);
PROC_FOPS_RW(cpufreq_fix_freq_in_es);
PROC_FOPS_RW(cpufreq_limited_power);
PROC_FOPS_RO(cpufreq_power_dump);
PROC_FOPS_RW(cpufreq_ptpod_test);
PROC_FOPS_RW(cpufreq_state);
PROC_FOPS_RW(cpufreq_emi_dfs);

PROC_FOPS_RO(cpufreq_downgrade_freq_info);
PROC_FOPS_RW(cpufreq_downgrade_freq_counter_limit);
PROC_FOPS_RW(cpufreq_downgrade_freq_counter_return_limit);
PROC_FOPS_RW(cpufreq_limited_by_hevc);
PROC_FOPS_RW(cpufreq_limited_max_freq_by_user);
PROC_FOPS_RW(cpufreq_over_max_cpu);
PROC_FOPS_RO(cpufreq_ptpod_freq_volt);
PROC_FOPS_RW(cpufreq_ptpod_temperature_limit);
PROC_FOPS_RW(cpufreq_ptpod_temperature_time);
PROC_FOPS_RW(cpufreq_oppidx);
PROC_FOPS_RW(cpufreq_freq);
PROC_FOPS_RW(cpufreq_volt);
PROC_FOPS_RW(cpufreq_turbo_mode);

static int _create_procfs(void)
{
	struct proc_dir_entry *dir = NULL;
	/* struct proc_dir_entry *cpu_dir = NULL; */
	struct mt_cpu_dvfs *p = id_to_cpu_dvfs(0);
	int i; /* , j; */

	struct pentry {
	const char *name;
	const struct file_operations *fops;
	};

	const struct pentry entries[] = {
	PROC_ENTRY(cpufreq_debug),
	PROC_ENTRY(cpufreq_fftt_test),
	PROC_ENTRY(cpufreq_stress_test),
	PROC_ENTRY(cpufreq_fix_freq_in_es),
	PROC_ENTRY(cpufreq_limited_power),
	PROC_ENTRY(cpufreq_power_dump),
	PROC_ENTRY(cpufreq_ptpod_test),
	PROC_ENTRY(cpufreq_emi_dfs),
	};

	const struct pentry cpu_entries[] = {
	PROC_ENTRY(cpufreq_downgrade_freq_info),
	PROC_ENTRY(cpufreq_downgrade_freq_counter_limit),
	PROC_ENTRY(cpufreq_downgrade_freq_counter_return_limit),
	PROC_ENTRY(cpufreq_limited_by_hevc),
	PROC_ENTRY(cpufreq_limited_max_freq_by_user),
	PROC_ENTRY(cpufreq_over_max_cpu),
	PROC_ENTRY(cpufreq_ptpod_freq_volt),
	PROC_ENTRY(cpufreq_ptpod_temperature_limit),
	PROC_ENTRY(cpufreq_ptpod_temperature_time),
	PROC_ENTRY(cpufreq_state),
	PROC_ENTRY(cpufreq_oppidx),
	PROC_ENTRY(cpufreq_freq),
	PROC_ENTRY(cpufreq_volt),
	PROC_ENTRY(cpufreq_turbo_mode),
	};

	dir = proc_mkdir("cpufreq", NULL);

	if (!dir) {
		cpufreq_err("fail to create /proc/cpufreq @ %s()\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		if (!proc_create(entries[i].name
				 , 0664
				 , dir, entries[i].fops))
			cpufreq_err("%s(), create /proc/cpufreq/%s failed\n"
				    , __func__, entries[i].name);
	}

	for (i = 0; i < ARRAY_SIZE(cpu_entries); i++) {
		if (!proc_create_data(cpu_entries[i].name
				      , 0664
				      , dir, cpu_entries[i].fops, p))
			cpufreq_err("%s(), create /proc/cpufreq/%s failed\n"
				    , __func__, cpu_entries[i].name);
	}

	return 0;
}
#endif /* CONFIG_PROC_FS */

/*
 * Module driver
 */
static int __init _mt_cpufreq_pdrv_init(void)
{
	int ret = 0;

	struct device_node *apmix_node = NULL;
	struct device_node *infra_ao_node = NULL;
	struct device_node *topckgen_node = NULL;
	struct device_node *pwrap_node = NULL;

	FUNC_ENTER(FUNC_LV_MODULE);

	apmix_node = of_find_compatible_node(NULL, NULL
					     , "mediatek,mt8163-apmixedsys");
	if (apmix_node) {
		/* Setup IO addresses */
		cpufreq_apmixed_base = of_iomap(apmix_node, 0);
		if (!cpufreq_apmixed_base) {
			cpufreq_err("cpufreq_apmixed_base = 0x%lx\n"
				    , (unsigned long)cpufreq_apmixed_base);
			return 0;
		}
		cpufreq_err("cpufreq_apmixed_base = 0x%lx\n"
			    , (unsigned long)cpufreq_apmixed_base);
	}

	infra_ao_node = of_find_compatible_node(NULL, NULL
						, "mediatek,mt8163-infracfg");
	if (infra_ao_node) {
		/* Setup IO addresses */
		infracfg_ao_base = of_iomap(infra_ao_node, 0);
		if (!infracfg_ao_base) {
			cpufreq_err("infracfg_ao_base = 0x%lx\n"
				    , (unsigned long)infracfg_ao_base);
			return 0;
		}
		cpufreq_err("infracfg_ao_base = 0x%lx\n"
			    , (unsigned long)infracfg_ao_base);
	}

	topckgen_node = of_find_compatible_node(NULL, NULL
						, "mediatek,mt8163-topckgen");
	if (topckgen_node) {
		/* Setup IO addresses */
		clk_cksys_base = of_iomap(topckgen_node, 0);
		if (!clk_cksys_base) {
			cpufreq_err("infracfg_ao_base = 0x%lx\n"
				    , (unsigned long)clk_cksys_base);
			return 0;
		}
		cpufreq_err("infracfg_ao_base = 0x%lx\n"
			    , (unsigned long)clk_cksys_base);
	}

	pwrap_node = of_find_compatible_node(NULL, NULL
					     , "mediatek,mt8163-pwrap");
	if (pwrap_node) {
		/* Setup IO addresses */
		pwrap_base = of_iomap(pwrap_node, 0);
		if (!pwrap_base) {
			cpufreq_err("pwrap_base = 0x%lx\n"
				    , (unsigned long)pwrap_base);
			return 0;
		}
		cpufreq_err("pwrap_base = 0x%lx\n", (unsigned long)pwrap_base);
	}

#ifdef CONFIG_PROC_FS
	/* init proc */
	if (_create_procfs())
		goto out;
#endif /* CONFIG_PROC_FS */

	ret = platform_driver_register(&_mt_cpufreq_pdrv);

	if (ret)
		cpufreq_err("fail to register cpufreq driver @ %s()\n"
			    , __func__);

out:
	FUNC_EXIT(FUNC_LV_MODULE);

	return ret;
}

static void __exit _mt_cpufreq_pdrv_exit(void)
{
	FUNC_ENTER(FUNC_LV_MODULE);

	platform_driver_unregister(&_mt_cpufreq_pdrv);

	FUNC_EXIT(FUNC_LV_MODULE);
}

late_initcall(_mt_cpufreq_pdrv_init);
module_exit(_mt_cpufreq_pdrv_exit);

MODULE_DESCRIPTION("MediaTek CPU DVFS Driver v0.3");
MODULE_LICENSE("GPL");

