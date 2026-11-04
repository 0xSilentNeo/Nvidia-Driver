/* SPDX-License-Identifier: MIT */
/* MediaTek WED (wireless ethernet dispatch) only exists on MT798x SoCs. */
#ifndef _LKPI_LINUX_SOC_MEDIATEK_MTK_WED_H_
#define _LKPI_LINUX_SOC_MEDIATEK_MTK_WED_H_
#include <linux/types.h>
struct mtk_wed_device {
	int dummy;
};
struct mtk_wed_ops;
#define mtk_wed_device_active(dev)			(false)
#define mtk_wed_device_attach(dev)			(-ENODEV)
#define mtk_wed_device_detach(dev)			do { } while (0)
#define mtk_wed_device_start(dev, mask)			do { } while (0)
#define mtk_wed_device_start_hw_rro(dev, mask, reset)	do { } while (0)
#define mtk_wed_device_tx_ring_setup(dev, i, regs, reset) (-ENODEV)
#define mtk_wed_device_txfree_ring_setup(dev, regs)	(-ENODEV)
#define mtk_wed_device_rx_ring_setup(dev, i, regs, reset) (-ENODEV)
#define mtk_wed_device_rro_rx_ring_setup(dev, i, regs)	(-ENODEV)
#define mtk_wed_device_msdu_pg_rx_ring_setup(dev, i, regs) (-ENODEV)
#define mtk_wed_device_ind_rx_ring_setup(dev, regs)	(-ENODEV)
#define mtk_wed_device_reg_read(dev, reg)		(0)
#define mtk_wed_device_reg_write(dev, reg, val)		do { } while (0)
#define mtk_wed_device_irq_get(dev, mask)		(0)
#define mtk_wed_device_irq_set_mask(dev, mask)		do { } while (0)
#define mtk_wed_device_update_msg(dev, id, msg, len)	(-ENODEV)
#define mtk_wed_device_dma_reset(dev)			do { } while (0)
#define mtk_wed_device_setup_tc(dev, netdev, type, data) (-EOPNOTSUPP)
#define mtk_wed_device_stop(dev)			do { } while (0)
#define mtk_wed_device_ppe_check(dev, skb, reason, hash) do { } while (0)
#define mtk_wed_get_rx_capa(dev)			(false)
#define mtk_wed_is_amsdu_supported(dev)			(false)
#endif
