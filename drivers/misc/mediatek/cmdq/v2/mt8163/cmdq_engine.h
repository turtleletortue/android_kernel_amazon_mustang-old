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

#ifndef __CMDQ_ENGINE_H__
#define __CMDQ_ENGINE_H__

enum CMDQ_ENG_ENUM {
	/* ISP */
	CMDQ_ENG_ISP_IMGI = 0,
	CMDQ_ENG_ISP_IMGO,	/* 1 */
	CMDQ_ENG_ISP_IMG2O,	/* 2 */

	/* MDP */
	CMDQ_ENG_MDP_CAMIN,	/* 3 */
	CMDQ_ENG_MDP_RDMA0,	/* 4 */
	CMDQ_ENG_MDP_RSZ0,	/* 5 */
	CMDQ_ENG_MDP_RSZ1,	/* 6 */
	CMDQ_ENG_MDP_TDSHP0,	/* 7 */
	CMDQ_ENG_MDP_WROT0,	/* 8 */
	CMDQ_ENG_MDP_WDMA,	/* 9 */

	/* JPEG & VENC */
	CMDQ_ENG_JPEG_ENC,	/* 10 */
	CMDQ_ENG_VIDEO_ENC,	/* 11 */
	CMDQ_ENG_JPEG_DEC,	/* 12 */
	CMDQ_ENG_JPEG_REMDC,	/* 13 */

	/* DISP */
	CMDQ_ENG_DISP_UFOE,	/* 14 */
	CMDQ_ENG_DISP_AAL,	/* 15 */
	CMDQ_ENG_DISP_COLOR0,	/* 16 */
	CMDQ_ENG_DISP_COLOR1,	/* 17 */
	CMDQ_ENG_DISP_RDMA0,	/* 18 */
	CMDQ_ENG_DISP_RDMA1,	/* 19 */
	CMDQ_ENG_DISP_RDMA2,	/* 20 */
	CMDQ_ENG_DISP_WDMA0,	/* 21 */
	CMDQ_ENG_DISP_WDMA1,	/* 22 */
	CMDQ_ENG_DISP_OVL0,	/* 23 */
	CMDQ_ENG_DISP_OVL1,	/* 24 */
	CMDQ_ENG_DISP_GAMMA,	/* 25 */
	CMDQ_ENG_DISP_MERGE,	/* 26 */
	CMDQ_ENG_DISP_SPLIT0,	/* 27 */
	CMDQ_ENG_DISP_SPLIT1,	/* 28 */
	CMDQ_ENG_DISP_DSI0_VDO,	/* 29 */
	CMDQ_ENG_DISP_DSI1_VDO,	/* 30 */
	CMDQ_ENG_DISP_DSI0_CMD,	/* 31 */
	CMDQ_ENG_DISP_DSI1_CMD,	/* 32 */
	CMDQ_ENG_DISP_DSI0,	/* 33 */
	CMDQ_ENG_DISP_DSI1,	/* 34 */
	CMDQ_ENG_DISP_DPI,	/* 35 */

	/* temp: CMDQ internal usage */
	CMDQ_ENG_CMDQ,
	CMDQ_ENG_DISP_MUTEX,
	CMDQ_ENG_MMSYS_CONFIG,

	/* Dummy Engine */
	CMDQ_ENG_MDP_RDMA1,
	CMDQ_ENG_MDP_RSZ2,
	CMDQ_ENG_MDP_TDSHP1,
	CMDQ_ENG_MDP_COLOR0,
	CMDQ_ENG_MDP_MOUT0,
	CMDQ_ENG_MDP_MOUT1,
	CMDQ_ENG_MDP_WROT1,

	CMDQ_ENG_DISP_OVL2,
	CMDQ_ENG_DISP_2L_OVL0,
	CMDQ_ENG_DISP_2L_OVL1,
	CMDQ_ENG_DISP_2L_OVL2,
	CMDQ_ENG_DPE,


	CMDQ_MAX_ENGINE_COUNT	/* ALWAYS keep at the end */
};

#endif				/* __CMDQ_ENGINE_H__ */
