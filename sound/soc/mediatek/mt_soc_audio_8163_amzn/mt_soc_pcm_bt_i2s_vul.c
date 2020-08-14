/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*****************************************************************************
 *                     C O M P I L E R   F L A G S
 *****************************************************************************/

/*****************************************************************************
 *                E X T E R N A L   R E F E R E N C E S
 *****************************************************************************/

#include <linux/dma-mapping.h>
#include "AudDrv_Common.h"
#include "AudDrv_Def.h"
#include "AudDrv_Afe.h"
#include "AudDrv_Ana.h"
#include "AudDrv_Clk.h"
#include "AudDrv_Kernel.h"
#include "mt_soc_afe_control.h"
#include "mt_soc_pcm_common.h"

/* information about */
static struct AFE_MEM_CONTROL_T *pcm_i2s0_vul_Control_context;
static struct snd_dma_buffer *I2S0_Capture_dma_buf;

static DEFINE_SPINLOCK(auddrv_VULInCtl_lock);

/*
 *    function implementation
 */
static void StartAudioI2S0VULHardware(struct snd_pcm_substream *substream);
static void StopAudioI2S0VULHardware(struct snd_pcm_substream *substream);
static int mtk_pcm_i2s0_vul_probe(struct platform_device *pdev);
static int mtk_pcm_i2s0_vul_pcm_close(struct snd_pcm_substream *substream);
static int mtk_asoc_pcm_i2s0_vul_pcm_new(struct snd_soc_pcm_runtime *rtd);
static int mtk_afe_pcm_i2s0_vul_probe(struct snd_soc_platform *platform);

static struct snd_pcm_hardware mtk_capture_hardware = {

	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_RESUME | SNDRV_PCM_INFO_MMAP_VALID),
	.formats = SND_SOC_ADV_MT_FMTS,
	.rates = SOC_HIGH_USE_RATE,
	.rate_min = SOC_HIGH_USE_RATE_MIN,
	.rate_max = SOC_HIGH_USE_RATE_MAX,
	.channels_min = SOC_NORMAL_USE_CHANNELS_MIN,
	.channels_max = SOC_NORMAL_USE_CHANNELS_MAX,
	.buffer_bytes_max = UL1_MAX_BUFFER_SIZE,
	.period_bytes_max = UL1_MAX_BUFFER_SIZE,
	.periods_min = UL1_MIN_PERIOD_SIZE,
	.periods_max = UL1_MAX_PERIOD_SIZE,
	.fifo_size = 0,
};

static void StopAudioI2S0VULHardware(struct snd_pcm_substream *substream)
{
	pr_debug("%s\n", __func__);

	Disable_2nd_I2S_4pin();

	SetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_VUL, false);

	/* here to set interrupt */
	SetIrqEnable(Soc_Aud_IRQ_MCU_MODE_IRQ2_MCU_MODE, false);

	SetConnection(Soc_Aud_InterCon_DisConnect,
		      Soc_Aud_InterConnectionInput_I00,
		      Soc_Aud_InterConnectionOutput_O09);
	SetConnection(Soc_Aud_InterCon_DisConnect,
		      Soc_Aud_InterConnectionInput_I01,
		      Soc_Aud_InterConnectionOutput_O10);

	EnableAfe(false);
}

static void StartAudioI2S0VULHardware(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	pr_debug("%s\n", __func__);

	/* here to set interrupt */
	SetIrqMcuCounter(Soc_Aud_IRQ_MCU_MODE_IRQ2_MCU_MODE,
			 runtime->period_size >> 1);
	SetIrqMcuSampleRate(Soc_Aud_IRQ_MCU_MODE_IRQ2_MCU_MODE, runtime->rate);
	SetIrqEnable(Soc_Aud_IRQ_MCU_MODE_IRQ2_MCU_MODE, true);

	SetSampleRate(Soc_Aud_Digital_Block_MEM_VUL, runtime->rate);
	SetChannels(Soc_Aud_Digital_Block_MEM_VUL, runtime->channels);
	SetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_VUL, true);

	SetConnection(Soc_Aud_InterCon_Connection,
		      Soc_Aud_InterConnectionInput_I00,
		      Soc_Aud_InterConnectionOutput_O09);
	SetConnection(Soc_Aud_InterCon_Connection,
		      Soc_Aud_InterConnectionInput_I01,
		      Soc_Aud_InterConnectionOutput_O10);

	SetoutputConnectionFormat(OUTPUT_DATA_FORMAT_16BIT,
				  Soc_Aud_InterConnectionInput_I00);
	SetoutputConnectionFormat(OUTPUT_DATA_FORMAT_16BIT,
				  Soc_Aud_InterConnectionInput_I01);

	if (GetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_IN_2) == false ||
	    GetMemoryPathEnable(Soc_Aud_Digital_Block_I2S_OUT_2) == false) {
		Enable_2nd_I2S_4pin(runtime->rate);
	}

	EnableAfe(true);
}

static int mtk_pcm_i2s0_vul_alsa_stop(struct snd_pcm_substream *substream)
{
	pr_debug("%s\n", __func__);

	StopAudioI2S0VULHardware(substream);
	RemoveMemifSubStream(Soc_Aud_Digital_Block_MEM_VUL, substream);

	return 0;
}

static snd_pcm_uframes_t mtk_is20_vul_pcm_pointer(struct snd_pcm_substream
						  *substream)
{
	kal_uint32 Frameidx = 0;
	struct AFE_BLOCK_T *VUL_Block = &(pcm_i2s0_vul_Control_context->rBlock);

	PRINTK_AUD_UL1("%s, Vul_Block->u4WriteIdx = 0x%x\n", __func__
		       VUL_Block->u4WriteIdx);

	if (GetMemoryPathEnable(Soc_Aud_Digital_Block_MEM_VUL) == true) {
		/* get total bytes to copysinewavetohdmi */
		Frameidx =
		    bytes_to_frames(substream->runtime, VUL_Block->u4WriteIdx);
		return Frameidx;
	}
	return 0;
}

static void SetVULBuffer(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct AFE_BLOCK_T *pblock = &pcm_i2s0_vul_Control_context->rBlock;

	pr_debug("%s\n", __func__);

	pblock->pucPhysBufAddr = runtime->dma_addr;
	pblock->pucVirtBufAddr = runtime->dma_area;
	pblock->u4BufferSize = runtime->dma_bytes;
	pblock->u4SampleNumMask = 0x001f;	/* 32 byte align */
	pblock->u4WriteIdx = 0;
	pblock->u4DMAReadIdx = 0;
	pblock->u4DataRemained = 0;
	pblock->u4fsyncflag = false;
	pblock->uResetFlag = true;

	pr_debug
	    ("u4BufferSize = %d pucVirtBufAddr = %p pucPhysBufAddr = 0x%x\n",
	     pblock->u4BufferSize, pblock->pucVirtBufAddr,
	     pblock->pucPhysBufAddr);

	/* set dram address top hardware */
	Afe_Set_Reg(AFE_VUL_BASE, pblock->pucPhysBufAddr, 0xffffffff);
	Afe_Set_Reg(AFE_VUL_END,
		    pblock->pucPhysBufAddr + (pblock->u4BufferSize - 1),
		    0xffffffff);
}

static int mtk_i2s0_vul_pcm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dma_buffer *dma_buf = &substream->dma_buffer;
	int ret = 0;

	pr_debug("%s\n", __func__);

	dma_buf->dev.type = SNDRV_DMA_TYPE_DEV;
	dma_buf->dev.dev = substream->pcm->card->dev;
	dma_buf->private_data = NULL;

	if (I2S0_Capture_dma_buf->area) {
		pr_debug
		    ("mtk_i2s0_vul_pcm_hw_params I2S0_Capture_dma_buf->area\n");
		runtime->dma_bytes = params_buffer_bytes(hw_params);
		runtime->dma_area = I2S0_Capture_dma_buf->area;
		runtime->dma_addr = I2S0_Capture_dma_buf->addr;
	} else {
		pr_debug
		    ("mtk_i2s0_vul_pcm_hw_params snd_pcm_lib_malloc_pages\n");
		ret =
		    snd_pcm_lib_malloc_pages(substream,
					     params_buffer_bytes(hw_params));
		if (ret < 0)
			return ret;

	}
	pr_debug("%s dma_bytes = %zu, dma_area = %p, dma_addr = 0x%x\n",
		 __func__, runtime->dma_bytes,
		 runtime->dma_area,
		 (uint32) runtime->dma_addr);

	SetVULBuffer(substream, hw_params);
	return ret;
}

static int mtk_fm_i2s_capture_pcm_hw_free(struct snd_pcm_substream *substream)
{
	pr_debug("%s\n", __func__);

	if (I2S0_Capture_dma_buf->area)
		return 0;
	return snd_pcm_lib_free_pages(substream);
}

static struct snd_pcm_hw_constraint_list
pcm_i2s0_vul_constraints_sample_rates = {

	.count = ARRAY_SIZE(soc_normal_supported_sample_rates),
	.list = soc_normal_supported_sample_rates,
};

static int mtk_pcm_i2s0_vul_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	pr_debug("%s\n", __func__);

	pcm_i2s0_vul_Control_context =
	    Get_Mem_ControlT(Soc_Aud_Digital_Block_MEM_VUL);
	runtime->hw = mtk_capture_hardware;
	memcpy((void *)(&(runtime->hw)), (void *)&mtk_capture_hardware,
	       sizeof(struct snd_pcm_hardware));

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		pr_debug("%s SNDRV_PCM_STREAM_CAPTURE OK\n", __func__);
	else
		return -1;

	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
		&pcm_i2s0_vul_constraints_sample_rates);

	if (ret < 0) {
		pr_warn("snd_pcm_hw_constraint_list failed\n");
		return ret;
	}
	ret =
	    snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	if (ret < 0) {
		pr_warn("snd_pcm_hw_constraint_integer failed\n");
		return ret;
	}

	pr_debug
	    ("mtk_pcm_i2s0_vul_pcm_open, runtime->rate = %d, channels = %d\n",
	     runtime->rate, runtime->channels);


	/* here open audio clocks */
	AudDrv_ANA_Clk_On();
	AudDrv_Clk_On();
	AudDrv_I2S_Clk_On();
	AudDrv_Emi_Clk_On();

	pr_debug("%s return\n", __func__);
	return 0;
}

static int mtk_pcm_i2s0_vul_pcm_close(struct snd_pcm_substream *substream)
{
	AudDrv_Emi_Clk_Off();
	AudDrv_I2S_Clk_Off();
	AudDrv_Clk_Off();
	AudDrv_ANA_Clk_Off();
	return 0;
}

static int mtk_pcm_i2s0_vul_alsa_start(struct snd_pcm_substream *substream)
{
	pr_debug("mtk_pcm_i2s0_vul_alsa_start\n");
	SetMemifSubStream(Soc_Aud_Digital_Block_MEM_VUL, substream);
	StartAudioI2S0VULHardware(substream);
	return 0;
}

static int mtk_capture_fm_i2s_pcm_trigger(struct snd_pcm_substream *substream,
					  int cmd)
{
	pr_err("mtk_capture_fm_i2s_pcm_trigger cmd = %d\n", cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		return mtk_pcm_i2s0_vul_alsa_start(substream);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return mtk_pcm_i2s0_vul_alsa_stop(substream);
	}
	return -EINVAL;
}

static int mtk_pcm_i2s0_vul_pcm_copy(struct snd_pcm_substream *substream,
				     int channel, snd_pcm_uframes_t pos,
				     void __user *dst, snd_pcm_uframes_t count)
{
	struct AFE_MEM_CONTROL_T *pVUL_MEM_ConTrol = NULL;
	struct AFE_BLOCK_T *Vul_Block = NULL;
	char *Read_Data_Ptr = (char *)dst;
	ssize_t DMA_Read_Ptr = 0, read_size = 0, read_count = 0;
	unsigned long flags;

	/* get total bytes to copy */
	count = Align64ByteSize(frames_to_bytes(substream->runtime, count));

	PRINTK_AUD_UL1("%s, pos = %lu, count = %lu\n", __func__, pos, count);

	/* check which memif nned to be write */
	pVUL_MEM_ConTrol = pcm_i2s0_vul_Control_context;
	Vul_Block = &(pVUL_MEM_ConTrol->rBlock);

	if (pVUL_MEM_ConTrol == NULL) {
		pr_err("cannot find MEM control !!!!!!!\n");
		return 0;
	}

	if (Vul_Block->u4BufferSize <= 0) {
		return 0;
	}

	if (NULL == Vul_Block->pucVirtBufAddr) {
		pr_err("CheckNullPointer pucVirtBufAddr Check Null\n");
		return 0;
	}

	spin_lock_irqsave(&auddrv_VULInCtl_lock, flags);
	if (Vul_Block->u4DataRemained > Vul_Block->u4BufferSize) {
		pr_debug
		    ("AudDrv_MEMIF_Read u4DataRemained=%x > u4BufferSize=%x\n",
		     Vul_Block->u4DataRemained, Vul_Block->u4BufferSize);
		Vul_Block->u4DataRemained = 0;
		Vul_Block->u4DMAReadIdx = Vul_Block->u4WriteIdx;
	}

	if (count > Vul_Block->u4DataRemained)
		read_size = Vul_Block->u4DataRemained;
	else
		read_size = count;

	DMA_Read_Ptr = Vul_Block->u4DMAReadIdx;
	spin_unlock_irqrestore(&auddrv_VULInCtl_lock, flags);

	PRINTK_AUD_UL1
	    ("%s finish0, read_count:%x, read_size:%x, u4DataRemained:%x, ",
	     __func__, read_count, read_size, Vul_Block->u4DataRemained);
	PRINTK_AUD_UL1("u4DMAReadIdx:%x, u4WriteIdx:%x\n",
		Vul_Block->u4DMAReadIdx, Vul_Block->u4WriteIdx)

	if (DMA_Read_Ptr + read_size < Vul_Block->u4BufferSize) {
		if (DMA_Read_Ptr != Vul_Block->u4DMAReadIdx) {
			PRINTK_AUD_UL1
			    ("%s 1, read_size:%zu, DataRemained:%x, ",
			     __func__, read_size, Vul_Block->u4DataRemained);
			PRINTK_AUD_UL1("MA_Read_Ptr:%zu, DMAReadIdx:%x\n",
				DMA_Read_Ptr, Vul_Block->u4DMAReadIdx)
		}

		if (copy_to_user((void __user *)Read_Data_Ptr,
				 (Vul_Block->pucVirtBufAddr + DMA_Read_Ptr),
				 read_size)) {
			pr_err
			    ("%s Fail 1 copy to user, ",
			     __func__);
			pr_err("Read_Data_Ptr:%p, pucVirtBufAddr:%p,",
				Read_Data_Ptr, Vul_Block->pucVirtBufAddr);
			pr_err
			    (" u4DMAReadIdx:0x%x, DMA_Read_Ptr:0x%zu,",
			     Vul_Block->u4DMAReadIdx, DMA_Read_Ptr);
			pr_err(" read_size:%zu\n",  read_size);
			return -1;
		}

		read_count += read_size;
		spin_lock(&auddrv_VULInCtl_lock);
		Vul_Block->u4DataRemained -= read_size;
		Vul_Block->u4DMAReadIdx += read_size;
		Vul_Block->u4DMAReadIdx %= Vul_Block->u4BufferSize;
		DMA_Read_Ptr = Vul_Block->u4DMAReadIdx;
		spin_unlock(&auddrv_VULInCtl_lock);

		Read_Data_Ptr += read_size;
		count -= read_size;

		PRINTK_AUD_UL1
		    ("%s finish1, copy size:%x, u4DMAReadIdx:%x, ",
		     __func__, read_size, Vul_Block->u4DMAReadIdx);
		PRINTK_AUD_UL1("u4WriteIdx:%x, u4DataRemained:%x\n",
			Vul_Block->u4WriteIdx, Vul_Block->u4DataRemained);
	} else {
		uint32 size_1 = Vul_Block->u4BufferSize - DMA_Read_Ptr;
		uint32 size_2 = read_size - size_1;

		if (DMA_Read_Ptr != Vul_Block->u4DMAReadIdx) {
			PRINTK_AUD_UL1
			    ("%s 2, read_size1:%x, DataRemained:%x, ",
			     __func__, size_1, Vul_Block->u4DataRemained);
			PRINTK_AUD_UL1("DMA_Read_Ptr:%zu, DMAReadIdx:%x\n",
				DMA_Read_Ptr, Vul_Block->u4DMAReadIdx);
		}

		if (copy_to_user((void __user *)Read_Data_Ptr,
				 (Vul_Block->pucVirtBufAddr + DMA_Read_Ptr),
				 size_1)) {
			pr_err
			    ("%s Fail 2 copy to user, Read_Data_Ptr:%p, ",
			     __func__, Read_Data_Ptr);
			pr_err("pucVirtBufAddr:%p,", Vul_Block->pucVirtBufAddr);
			pr_err
			    (" u4DMAReadIdx:0x%x, DMA_Read_Ptr:0x%zu, ",
			     Vul_Block->u4DMAReadIdx, DMA_Read_Ptr);
			pr_err("read_size:%zu\n", read_size);
			return -1;
		}

		read_count += size_1;
		spin_lock(&auddrv_VULInCtl_lock);
		Vul_Block->u4DataRemained -= size_1;
		Vul_Block->u4DMAReadIdx += size_1;
		Vul_Block->u4DMAReadIdx %= Vul_Block->u4BufferSize;
		DMA_Read_Ptr = Vul_Block->u4DMAReadIdx;
		spin_unlock(&auddrv_VULInCtl_lock);

		PRINTK_AUD_UL1
		    ("%s finish2, copy size_1:%x, u4DMAReadIdx:%x, ",
		     __func__, size_1, Vul_Block->u4DMAReadIdx);
		PRINTK_AUD_UL1("u4WriteIdx:%x, u4DataRemained:%x\n",
			Vul_Block->u4WriteIdx, Vul_Block->u4DataRemained);
		if (DMA_Read_Ptr != Vul_Block->u4DMAReadIdx) {
			PRINTK_AUD_UL1
			    ("%s 3, read_size2:%x, DataRemained:%x, ",
			     __func__, size_2, Vul_Block->u4DataRemained);
			PRINTK_AUD_UL1("DMA_Read_Ptr:%zu, DMAReadIdx:%x\n",
				DMA_Read_Ptr, Vul_Block->u4DMAReadIdx);
		}

		if (copy_to_user((void __user *)(Read_Data_Ptr + size_1),
				 (Vul_Block->pucVirtBufAddr + DMA_Read_Ptr),
				 size_2)) {
			pr_err
			    ("%s Fail 3 copy to user, Read_Data_Ptr:%p, ",
			     __func__, Read_Data_Ptr);
			pr_err("pucVirtBufAddr:%p,", Vul_Block->pucVirtBufAddr);
			pr_err
			    (" u4DMAReadIdx:0x%x, DMA_Read_Ptr:0x%zu,",
			     Vul_Block->u4DMAReadIdx, DMA_Read_Ptr);
			pr_err(" read_size:%zu\n", read_size);
			return -1;
		}

		read_count += size_2;
		spin_lock(&auddrv_VULInCtl_lock);
		Vul_Block->u4DataRemained -= size_2;
		Vul_Block->u4DMAReadIdx += size_2;
		DMA_Read_Ptr = Vul_Block->u4DMAReadIdx;
		spin_unlock(&auddrv_VULInCtl_lock);

		count -= read_size;
		Read_Data_Ptr += read_size;

		PRINTK_AUD_UL1
		    ("%s finish3, copy size_2:%x, u4DMAReadIdx:%x,",
		     __func__, size_2, Vul_Block->u4DMAReadIdx);
		PRINTK_AUD_UL1(" u4WriteIdx:%x, u4DataRemained:%x\n",
			Vul_Block->u4WriteIdx, Vul_Block->u4DataRemained);
	}

	return read_count;
}

static struct snd_pcm_ops mtk_pcm_i2s0_vul_ops = {

	.open = mtk_pcm_i2s0_vul_pcm_open,
	.close = mtk_pcm_i2s0_vul_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = mtk_i2s0_vul_pcm_hw_params,
	.hw_free = mtk_fm_i2s_capture_pcm_hw_free,
	.trigger = mtk_capture_fm_i2s_pcm_trigger,
	.pointer = mtk_is20_vul_pcm_pointer,
	.copy = mtk_pcm_i2s0_vul_pcm_copy,
};

static struct snd_soc_platform_driver mtk_soc_platform = {

	.ops = &mtk_pcm_i2s0_vul_ops,
	.pcm_new = mtk_asoc_pcm_i2s0_vul_pcm_new,
	.probe = mtk_afe_pcm_i2s0_vul_probe,
};

static int mtk_pcm_i2s0_vul_probe(struct platform_device *pdev)
{
	pr_err("mtk_pcm_i2s0_vul_probe\n");

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(64);

	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	if (pdev->dev.of_node)
		dev_set_name(&pdev->dev, "%s", MT_SOC_VOIP_BT_IN);

	pr_err("%s: dev name %s\n", __func__, dev_name(&pdev->dev));
	return snd_soc_register_platform(&pdev->dev, &mtk_soc_platform);
}

static int mtk_asoc_pcm_i2s0_vul_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	pr_debug("mtk_asoc_pcm_i2s0_vul_pcm_new\n");
	return 0;
}

static int mtk_afe_pcm_i2s0_vul_probe(struct snd_soc_platform *platform)
{
	pr_debug("mtk_afe_pcm_i2s0_vul_probe\n");
	AudDrv_Allocate_mem_Buffer(platform->dev, Soc_Aud_Digital_Block_MEM_VUL,
				   UL1_MAX_BUFFER_SIZE);
	I2S0_Capture_dma_buf = Get_Mem_Buffer(Soc_Aud_Digital_Block_MEM_VUL);

	return 0;
}

static int mtk_pcm_i2s0_vul_remove(struct platform_device *pdev)
{
	pr_debug("%s\n", __func__);
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mt_soc_pcm_pcm_i2s0_vul_of_ids[] = {

	{.compatible = "mediatek,mt8163-soc-pcm-bt-dai",},
	{}
};
#endif

static struct platform_driver mtk_pcm_i2s0_vul_capture_driver = {

	.driver = {
		   .name = MT_SOC_VOIP_BT_IN,
		   .owner = THIS_MODULE,
#ifdef CONFIG_OF
		   .of_match_table = mt_soc_pcm_pcm_i2s0_vul_of_ids,
#endif
		   },
	.probe = mtk_pcm_i2s0_vul_probe,
	.remove = mtk_pcm_i2s0_vul_remove,
};

#ifndef CONFIG_OF
static struct platform_device *soc_fm_i2s_capture_dev;
#endif

static int __init mtk_soc_pcm_i2s0_vul_platform_init(void)
{
	int ret = 0;

	pr_debug("%s\n", __func__);
#ifndef CONFIG_OF
	soc_fm_i2s_capture_dev = platform_device_alloc(MT_SOC_VOIP_BT_IN, -1);

	if (!soc_fm_i2s_capture_dev)
		return -ENOMEM;

	ret = platform_device_add(soc_fm_i2s_capture_dev);
	if (ret != 0) {
		platform_device_put(soc_fm_i2s_capture_dev);
		return ret;
	}
#endif
	ret = platform_driver_register(&mtk_pcm_i2s0_vul_capture_driver);
	return ret;
}

static void __exit mtk_soc_pcm_i2s0_vul_platform_exit(void)
{
	pr_debug("%s\n", __func__);
	platform_driver_unregister(&mtk_pcm_i2s0_vul_capture_driver);
}

module_init(mtk_soc_pcm_i2s0_vul_platform_init);
module_exit(mtk_soc_pcm_i2s0_vul_platform_exit);

MODULE_DESCRIPTION("AFE PCM module platform driver");
MODULE_LICENSE("GPL");
