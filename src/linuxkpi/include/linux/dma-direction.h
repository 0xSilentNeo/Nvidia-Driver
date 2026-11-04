/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_DMA_DIRECTION_H_
#define _LKPI_LINUX_DMA_DIRECTION_H_
enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};
#endif
