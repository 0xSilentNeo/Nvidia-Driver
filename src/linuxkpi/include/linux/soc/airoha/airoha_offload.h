/* SPDX-License-Identifier: MIT */
/* Airoha NPU offload only exists on Airoha SoCs. */
#ifndef _LKPI_LINUX_SOC_AIROHA_AIROHA_OFFLOAD_H_
#define _LKPI_LINUX_SOC_AIROHA_AIROHA_OFFLOAD_H_
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/device.h>
struct airoha_npu { int dummy; };
struct airoha_ppe_dev {
	struct {
		int (*setup_tc_block_cb)(struct airoha_ppe_dev *dev, void *type_data);
		void (*check_skb)(struct airoha_ppe_dev *dev, struct sk_buff *skb, u16 hash, bool rx_wlan);
	} ops;
	void *priv;
};
enum { PPE_CPU_REASON_HIT_UNBIND_RATE_REACHED = 0x0f };
struct airoha_npu_rx_dma_desc { u32 dummy; };
struct airoha_npu_tx_dma_desc { u32 dummy; };
enum airoha_npu_wlan_set_cmd { AIROHA_NPU_WLAN_SET_CMD_DUMMY };
enum airoha_npu_wlan_get_cmd { AIROHA_NPU_WLAN_GET_CMD_DUMMY };
static inline struct airoha_npu *airoha_npu_get(struct device *dev, dma_addr_t *stats) { (void)dev; (void)stats; return NULL; }
static inline void airoha_npu_put(struct airoha_npu *npu) { (void)npu; }
static inline int airoha_npu_wlan_init_reserved_memory(struct airoha_npu *npu) { (void)npu; return -ENODEV; }
static inline int airoha_npu_wlan_send_msg(struct airoha_npu *npu, int ifindex, enum airoha_npu_wlan_set_cmd cmd,
    void *data, int size, gfp_t gfp) { (void)npu; (void)ifindex; (void)cmd; (void)data; (void)size; (void)gfp; return -ENODEV; }
static inline int airoha_npu_wlan_get_msg(struct airoha_npu *npu, int ifindex, enum airoha_npu_wlan_get_cmd cmd,
    void *data, int size, gfp_t gfp) { (void)npu; (void)ifindex; (void)cmd; (void)data; (void)size; (void)gfp; return -ENODEV; }
static inline u32 airoha_npu_wlan_get_queue_addr(struct airoha_npu *npu, int qid, bool xmit) { (void)npu; (void)qid; (void)xmit; return 0; }
static inline void airoha_npu_wlan_set_irq_status(struct airoha_npu *npu, u32 val) { (void)npu; (void)val; }
static inline u32 airoha_npu_wlan_get_irq_status(struct airoha_npu *npu, int q) { (void)npu; (void)q; return 0; }
static inline void airoha_npu_wlan_enable_irq(struct airoha_npu *npu, int q) { (void)npu; (void)q; }
static inline void airoha_npu_wlan_disable_irq(struct airoha_npu *npu, int q) { (void)npu; (void)q; }
static inline struct airoha_ppe_dev *airoha_ppe_get_dev(struct device *dev) { (void)dev; return NULL; }
static inline void airoha_ppe_put_dev(struct airoha_ppe_dev *dev) { (void)dev; }
#endif
