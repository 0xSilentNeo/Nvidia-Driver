/* SPDX-License-Identifier: MIT */
/*
 * net_device and NAPI.
 *
 * mt76 creates a dummy net_device purely to host its NAPI instances
 * (alloc_netdev_dummy) and relies on napi_schedule/napi_complete to run
 * its RX polling.  NAPI here is a dedicated poll thread per device
 * (src/linuxkpi/src/napi.c): napi_schedule() marks the instance and wakes
 * the thread, which calls ->poll() with the configured budget until it
 * reports completion.
 */
#ifndef _LKPI_LINUX_NETDEVICE_H_
#define _LKPI_LINUX_NETDEVICE_H_
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/netdev_features.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/if.h>

#define IFNAMSIZ	16
#define MAX_ADDR_LEN	32
#define NAPI_POLL_WEIGHT 64
#define NETDEV_ALIGN	32

struct net_device;
struct wireless_dev;
struct inet6_dev;
struct net_device_path;
struct net_device_path_ctx;

struct napi_struct {
	struct list_head poll_list;
	unsigned long state;
	int weight;
	int (*poll)(struct napi_struct *, int);
	struct net_device *dev;
	struct list_head rx_list;	/* skbs batched by napi_gro_receive */
	int rx_count;
	struct list_head dev_list;
	unsigned int napi_id;
};
enum {
	NAPI_STATE_SCHED,	/* poll pending or running */
	NAPI_STATE_DISABLE,
	NAPI_STATE_NPSVC,
	NAPI_STATE_LISTED,
	NAPI_STATE_SCHED_THREADED,
};

struct net_device_stats {
	unsigned long rx_packets, tx_packets, rx_bytes, tx_bytes;
	unsigned long rx_errors, tx_errors, rx_dropped, tx_dropped;
	unsigned long multicast, collisions;
};
struct rtnl_link_stats64 {
	u64 rx_packets, tx_packets, rx_bytes, tx_bytes;
	u64 rx_errors, tx_errors, rx_dropped, tx_dropped;
	u64 multicast, collisions;
};
struct netdev_hw_addr {
	struct list_head list;
	unsigned char addr[MAX_ADDR_LEN];
	unsigned char type;
};
struct netdev_hw_addr_list {
	struct list_head list;
	int count;
};
#define netdev_hw_addr_list_count(l) ((l)->count)
#define netdev_hw_addr_list_empty(l) ((l)->count == 0)
#define netdev_hw_addr_list_for_each(ha, l) list_for_each_entry(ha, &(l)->list, list)
#define netdev_mc_count(dev) netdev_hw_addr_list_count(&(dev)->mc)
#define netdev_for_each_mc_addr(ha, dev) netdev_hw_addr_list_for_each(ha, &(dev)->mc)

struct net_device_ops;
struct ethtool_ops;
struct header_ops;

struct net_device {
	char name[IFNAMSIZ];
	unsigned long state;
	struct list_head napi_list;
	unsigned int flags;
	unsigned int priv_flags;
	unsigned int mtu, min_mtu, max_mtu;
	unsigned short type;
	unsigned short hard_header_len;
	unsigned short needed_headroom, needed_tailroom;
	unsigned char addr_len;
	unsigned char dev_addr[MAX_ADDR_LEN] __aligned(2);
	unsigned char perm_addr[MAX_ADDR_LEN];
	unsigned char broadcast[MAX_ADDR_LEN];
	struct netdev_hw_addr_list uc, mc;
	netdev_features_t features, hw_features, wanted_features, vlan_features;
	const struct net_device_ops *netdev_ops;
	const struct ethtool_ops *ethtool_ops;
	const struct header_ops *header_ops;
	struct wireless_dev *ieee80211_ptr;
	struct inet6_dev *ip6_ptr;
	struct device dev;
	void *priv_destructor_ctx;
	void (*priv_destructor)(struct net_device *);
	int ifindex;
	unsigned int num_tx_queues, real_num_tx_queues;
	unsigned int num_rx_queues, real_num_rx_queues;
	struct net_device_stats stats;
	int threaded;
	bool reg_state_registered;
	void *lkpi_napi_thread;		/* owned by napi.c */
	/* private area follows, NETDEV_ALIGN aligned */
};
struct net_device_ops {
	int (*ndo_open)(struct net_device *dev);
	int (*ndo_stop)(struct net_device *dev);
	int (*ndo_start_xmit)(struct sk_buff *skb, struct net_device *dev);
	int (*ndo_set_mac_address)(struct net_device *dev, void *addr);
	int (*ndo_change_mtu)(struct net_device *dev, int new_mtu);
	void (*ndo_set_rx_mode)(struct net_device *dev);
	void (*ndo_get_stats64)(struct net_device *dev, struct rtnl_link_stats64 *s);
};

static inline void *netdev_priv(const struct net_device *dev)
{
	return (u8 *)dev + ALIGN(sizeof(struct net_device), NETDEV_ALIGN);
}
#define SET_NETDEV_DEV(net, pdev) ((net)->dev.parent = (pdev))
#define SET_NETDEV_DEVTYPE(net, type) do { } while (0)
static inline const char *netdev_name(const struct net_device *dev) { return dev->name; }
#define netdev_printk(level, dev, fmt, ...) printk(level "%s: " fmt, netdev_name(dev), ##__VA_ARGS__)
#define netdev_err(dev, fmt, ...)	netdev_printk(KERN_ERR, dev, fmt, ##__VA_ARGS__)
#define netdev_warn(dev, fmt, ...)	netdev_printk(KERN_WARNING, dev, fmt, ##__VA_ARGS__)
#define netdev_info(dev, fmt, ...)	netdev_printk(KERN_INFO, dev, fmt, ##__VA_ARGS__)
#define netdev_notice(dev, fmt, ...)	netdev_printk(KERN_NOTICE, dev, fmt, ##__VA_ARGS__)
#define netdev_dbg(dev, fmt, ...)	do { } while (0)
#define netdev_WARN(dev, fmt, ...)	WARN(1, fmt, ##__VA_ARGS__)
#define netdev_err_once(dev, fmt, ...)	netdev_err(dev, fmt, ##__VA_ARGS__)
#define netdev_warn_once(dev, fmt, ...)	netdev_warn(dev, fmt, ##__VA_ARGS__)
#define netdev_info_once(dev, fmt, ...)	netdev_info(dev, fmt, ##__VA_ARGS__)
#define net_ratelimit() (1)
#define netdev_tx_t int
#define NETDEV_TX_OK 0
#define NETDEV_TX_BUSY 0x10
#define NET_RX_SUCCESS 0
#define NET_RX_DROP 1

struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name, unsigned char name_assign_type,
    void (*setup)(struct net_device *), unsigned int txqs, unsigned int rxqs);
#define alloc_netdev(sizeof_priv, name, name_assign_type, setup) \
	alloc_netdev_mqs(sizeof_priv, name, name_assign_type, setup, 1, 1)
struct net_device *alloc_netdev_dummy(int sizeof_priv);
void free_netdev(struct net_device *dev);
void init_dummy_netdev(struct net_device *dev);
int register_netdevice(struct net_device *dev);
int register_netdev(struct net_device *dev);
void unregister_netdevice(struct net_device *dev);
void unregister_netdev(struct net_device *dev);
#define NET_NAME_UNKNOWN 0
#define NET_NAME_ENUM 1
#define NET_NAME_USER 3
static inline void ether_setup(struct net_device *dev)
{
	dev->hard_header_len = ETH_HLEN;
	dev->mtu = ETH_DATA_LEN;
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = ETH_DATA_LEN;
	dev->addr_len = ETH_ALEN;
	dev->type = 1;
	memset(dev->broadcast, 0xff, ETH_ALEN);
}
static inline void dev_hold(struct net_device *dev) { (void)dev; }
static inline void dev_put(struct net_device *dev) { (void)dev; }
#define netdev_hold(d, t, g) do { } while (0)
#define netdev_put(d, t) do { } while (0)
#define netif_running(dev) (test_bit(0, &(dev)->state))
#define netif_carrier_ok(dev) (!test_bit(2, &(dev)->state))
static inline void netif_carrier_on(struct net_device *dev) { clear_bit(2, &dev->state); }
static inline void netif_carrier_off(struct net_device *dev) { set_bit(2, &dev->state); }
static inline bool netif_device_present(struct net_device *dev) { return !test_bit(3, &dev->state); }
static inline void netif_device_detach(struct net_device *dev) { set_bit(3, &dev->state); }
static inline void netif_device_attach(struct net_device *dev) { clear_bit(3, &dev->state); }
#define netif_tx_start_all_queues(dev) do { } while (0)
#define netif_tx_stop_all_queues(dev) do { } while (0)
#define netif_tx_wake_all_queues(dev) do { } while (0)
#define netif_tx_disable(dev) do { } while (0)
#define netif_wake_queue(dev) do { } while (0)
#define netif_stop_queue(dev) do { } while (0)
#define netif_start_queue(dev) do { } while (0)
#define netif_queue_stopped(dev) (0)
#define netif_set_real_num_tx_queues(dev, n) (0)
#define netif_set_real_num_rx_queues(dev, n) (0)
#define dev_net(dev) ((struct net *)NULL)
#define dev_addr_set(dev, addr) ether_addr_copy((dev)->dev_addr, addr)
#define dev_addr_mod(dev, off, addr, len) memcpy((dev)->dev_addr + (off), addr, len)
#define rtnl_lock() do { } while (0)
#define rtnl_unlock() do { } while (0)
#define rtnl_is_locked() (1)
#define ASSERT_RTNL() do { } while (0)
#define dev_valid_name(n) (1)
#define dev_get_by_index(net, idx) ((struct net_device *)NULL)
#define dev_set_threaded(dev, on) (0)
int netif_rx(struct sk_buff *skb);
#define netif_rx_ni(skb) netif_rx(skb)
int netif_receive_skb(struct sk_buff *skb);
void netif_receive_skb_list(struct list_head *head);
int dev_queue_xmit(struct sk_buff *skb);
int dev_set_mtu(struct net_device *dev, int mtu);

/* ---- NAPI ---- */
void netif_napi_add_weight(struct net_device *dev, struct napi_struct *napi,
    int (*poll)(struct napi_struct *, int), int weight);
static inline void netif_napi_add(struct net_device *dev, struct napi_struct *napi,
    int (*poll)(struct napi_struct *, int))
{
	netif_napi_add_weight(dev, napi, poll, NAPI_POLL_WEIGHT);
}
static inline void netif_napi_add_tx_weight(struct net_device *dev, struct napi_struct *napi,
    int (*poll)(struct napi_struct *, int), int weight)
{
	netif_napi_add_weight(dev, napi, poll, weight);
}
static inline void netif_napi_add_tx(struct net_device *dev, struct napi_struct *napi,
    int (*poll)(struct napi_struct *, int))
{
	netif_napi_add_weight(dev, napi, poll, NAPI_POLL_WEIGHT);
}
#define netif_napi_add_config(dev, napi, poll, idx) netif_napi_add(dev, napi, poll)
void netif_napi_del(struct napi_struct *napi);
#define __netif_napi_del(n) netif_napi_del(n)
void napi_enable(struct napi_struct *napi);
void napi_disable(struct napi_struct *napi);
bool napi_schedule_prep(struct napi_struct *napi);
void __napi_schedule(struct napi_struct *napi);
#define __napi_schedule_irqoff(n) __napi_schedule(n)
static inline bool napi_schedule(struct napi_struct *napi)
{
	if (napi_schedule_prep(napi)) {
		__napi_schedule(napi);
		return true;
	}
	return false;
}
#define napi_schedule_irqoff(n) napi_schedule(n)
bool napi_complete_done(struct napi_struct *napi, int work_done);
static inline bool napi_complete(struct napi_struct *napi) { return napi_complete_done(napi, 0); }
void napi_synchronize(const struct napi_struct *napi);
static inline bool napi_disable_pending(struct napi_struct *napi) { return test_bit(NAPI_STATE_DISABLE, &napi->state); }
static inline bool napi_is_scheduled(struct napi_struct *napi) { return test_bit(NAPI_STATE_SCHED, &napi->state); }
int napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb);
#define napi_gro_flush(napi, flush_old) do { } while (0)
#define napi_get_frags(napi) (NULL)
#define skb_mark_napi_id(skb, napi) do { } while (0)
#define napi_hash_add(n) do { } while (0)
#define napi_hash_del(n) do { } while (0)
#define netif_threaded_enable(dev) do { } while (0)
#define netif_napi_set_irq(napi, irq) do { } while (0)

/* The 802.11 stack receives frames from mac80211 glue through this hook. */
typedef void (*lkpi_netif_rx_hook_t)(struct sk_buff *skb);
void lkpi_set_netif_rx_hook(lkpi_netif_rx_hook_t hook);

enum tc_setup_type { TC_SETUP_QDISC_MQPRIO, TC_SETUP_BLOCK, TC_SETUP_FT };
#endif /* _LKPI_LINUX_NETDEVICE_H_ */
