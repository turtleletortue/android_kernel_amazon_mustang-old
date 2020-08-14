#ifndef __AMZ_MT_SPI_PCM_H__
#define __AMZ_MT_SPI_PCM_H__

#include "dough.h"

#define AMZN_MT_SPI_PCM "amzn-mt-spi-pcm"

#define FIRMWARE_MAX_BYTES     (10*4096)
#define FPGA_FIRMWARE_REV       30
#define FPGA_FIRMWARE_NAME      CONFIG_EXTRA_FIRMWARE

#define SPI_SPEED_HZ            50000000 /* Maximum speed 50 MHz */
#define SPI_READ_WAIT_MIN_USEC  6000
#define SPI_READ_WAIT_MAX_USEC  7000
#define FPGA_DELAY_MS           2
#define PINCTRL_DELAY_MS        2
#define MARGIN_USEC             500

#define SAMPLING_RATE           16000

#define SPI_HEADER              1
#define SPI_HEADER_DISABLE      0

#if defined CONFIG_SND_SOC_8_MICS
#define SPI_N_CHANNELS          9
#elif defined CONFIG_SND_SOC_4_MICS
#define SPI_N_CHANNELS          6
#else
#define SPI_N_CHANNELS          4
#endif

#define SPI_BYTES_PER_CHANNEL   3
#define SPI_BYTES_PER_FRAME     (SPI_N_CHANNELS * SPI_BYTES_PER_CHANNEL)
/* = 27 for 9 channels, = 18 for 6 channels */
#define SPI_BYTES_PER_PERIOD    (SPI_BYTES_PER_FRAME *\
				(DOUGH_AUDIO_FRAME_BUF+SPI_HEADER))
/* = 6912 for 9 channels, = 4608 for 6 channels */
#define SPI_N_PERIODS_MIN       1
#define SPI_N_PERIODS_MAX       10
#define SPI_PERIOD_BYTES_MIN    (SPI_BYTES_PER_PERIOD * SPI_N_PERIODS_MIN)
#define SPI_PERIOD_BYTES_MAX    (SPI_BYTES_PER_PERIOD * SPI_N_PERIODS_MAX)
#define SPI_BUFFER_BYTES_MAX    SPI_PERIOD_BYTES_MAX
#define SPI_DMA_BYTES_MAX       (SPI_PERIOD_BYTES_MAX * 2)

#define MAX_SCHEDULED_WORK_Q    3
#define MAX_FLUSHED_CYCLES      10

#define SPI_READ_WAIT_MIN_48K_USEC  1500
#define SPI_READ_WAIT_MAX_48K_USEC  2000

#define SPI_READ_WAIT_MIN_96K_USEC  2000
#define SPI_READ_WAIT_MAX_96K_USEC  2500

#endif
