// SPDX-License-Identifier: MIT
/*
 * net_device and NAPI.
 *
 * mt76 only needs net_device as a home for its NAPI instances and as the
 * device an RX skb points at.  Registration is bookkeeping.  Frames that
 * reach netif_receive_skb() are handed to the hook installed by the
 * 802.11 glue (lkpi_set_netif_rx_hook) or dropped.
 *
 * NAPI is one poll thread per net_device ("lkpi-napi"), created on the
 * first netif_napi_add().  napi_schedule() sets NAPI_STATE_SCHED, queues
 * the instance on the thread's poll list and wakes the thread; the thread
 * calls ->poll(napi, weight) and, like Linux, keeps the instance queued
 * while the poll exhausts its budget, dropping it once the poll calls
 * napi_complete_done().  A schedule that arrives while the instance is
 * already running is remembered in a MISSED bit and replayed by
 * napi_complete_done(), so no interrupt is lost between poll rounds.
 * napi_disable() owns NAPI_STATE_SCHED for as long as the instance is
 * disabled, which is what makes napi_schedule_prep() fail meanwhile; a
 * freshly added instance starts in that state until napi_enable().
 */
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <lkpi/runtime.h>
#include <lkpi/os.h>

/* Private state bit; the public ones live in linux/netdevice.h. */
#define LKPI_NAPI_STATE_MISSED	8

#define LKPI_NAPI_WAIT_NS	(1000ULL * 1000 * 1000)
#define LKPI_GRO_BATCH		64

struct lkpi_napi_thread {
	struct net_device *dev;
	spinlock_t lock;
	struct list_head poll_list;	/* scheduled napi_struct.poll_list */
	lkpi_os_thread_t thread;
	bool stop;
	bool exited;
	struct list_head global;	/* lkpi_napi_threads */
};

static DEFINE_SPINLOCK(lkpi_napi_lock);		/* threads list, napi_list */
static LIST_HEAD(lkpi_napi_threads);
static lkpi_netif_rx_hook_t lkpi_netif_rx_hook;
static int lkpi_netdev_ifindex;

/* ---- net_device ----------------------------------------------------- */

/*
 * The private area must be NETDEV_ALIGN aligned, so the struct itself is
 * placed on a NETDEV_ALIGN boundary inside a slightly larger allocation;
 * the pointer to the allocation sits just in front of the struct.
 */
static struct net_device *lkpi_netdev_alloc(int sizeof_priv)
{
	size_t size = ALIGN(sizeof(struct net_device), NETDEV_ALIGN);
	struct net_device *dev;
	u8 *base;

	if (sizeof_priv > 0)
		size += (size_t)sizeof_priv;
	base = kzalloc(size + NETDEV_ALIGN + sizeof(void *), GFP_KERNEL);
	if (!base)
		return NULL;
	dev = PTR_ALIGN((struct net_device *)(base + sizeof(void *)), NETDEV_ALIGN);
	((void **)dev)[-1] = base;
	return dev;
}

static void lkpi_netdev_core_init(struct net_device *dev)
{
	INIT_LIST_HEAD(&dev->napi_list);
	INIT_LIST_HEAD(&dev->uc.list);
	INIT_LIST_HEAD(&dev->mc.list);
	dev->uc.count = 0;
	dev->mc.count = 0;
	dev->lkpi_napi_thread = NULL;
	dev->reg_state_registered = false;
}

struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name, unsigned char name_assign_type,
    void (*setup)(struct net_device *), unsigned int txqs, unsigned int rxqs)
{
	struct net_device *dev;

	if (WARN_ON_ONCE(txqs < 1 || rxqs < 1))
		return NULL;

	dev = lkpi_netdev_alloc(sizeof_priv);
	if (!dev)
		return NULL;

	lkpi_netdev_core_init(dev);
	dev->num_tx_queues = txqs;
	dev->real_num_tx_queues = txqs;
	dev->num_rx_queues = rxqs;
	dev->real_num_rx_queues = rxqs;
	dev->ifindex = __atomic_add_fetch(&lkpi_netdev_ifindex, 1, __ATOMIC_SEQ_CST);
	if (name) {
		if (strchr(name, '%'))
			snprintf(dev->name, sizeof(dev->name), name, dev->ifindex);
		else
			strscpy(dev->name, name, sizeof(dev->name));
	}
	if (setup)
		setup(dev);
	return dev;
}

void init_dummy_netdev(struct net_device *dev)
{
	memset(dev, 0, sizeof(*dev));
	lkpi_netdev_core_init(dev);
	/* running, so NAPI accepts the instance */
	set_bit(0, &dev->state);
	strscpy(dev->name, "dummy", sizeof(dev->name));
}

struct net_device *alloc_netdev_dummy(int sizeof_priv)
{
	struct net_device *dev = lkpi_netdev_alloc(sizeof_priv);

	if (!dev)
		return NULL;
	lkpi_netdev_core_init(dev);
	set_bit(0, &dev->state);
	dev->num_tx_queues = dev->real_num_tx_queues = 1;
	dev->num_rx_queues = dev->real_num_rx_queues = 1;
	dev->ifindex = __atomic_add_fetch(&lkpi_netdev_ifindex, 1, __ATOMIC_SEQ_CST);
	snprintf(dev->name, sizeof(dev->name), "dummy%d", dev->ifindex);
	return dev;
}

static void lkpi_napi_thread_stop(struct lkpi_napi_thread *t);

void free_netdev(struct net_device *dev)
{
	struct napi_struct *napi, *tmp;

	if (!dev)
		return;

	if (dev->lkpi_napi_thread)
		lkpi_napi_thread_stop(dev->lkpi_napi_thread);
	list_for_each_entry_safe(napi, tmp, &dev->napi_list, dev_list)
		netif_napi_del(napi);
	if (dev->priv_destructor)
		dev->priv_destructor(dev);
	kfree(((void **)dev)[-1]);
}

int register_netdevice(struct net_device *dev)
{
	if (!dev)
		return -EINVAL;
	dev->reg_state_registered = true;
	return 0;
}

int register_netdev(struct net_device *dev)
{
	return register_netdevice(dev);
}

void unregister_netdevice(struct net_device *dev)
{
	if (dev)
		dev->reg_state_registered = false;
}

void unregister_netdev(struct net_device *dev)
{
	unregister_netdevice(dev);
}

int dev_set_mtu(struct net_device *dev, int mtu)
{
	if (!dev || mtu < 0)
		return -EINVAL;
	if (dev->min_mtu && mtu < (int)dev->min_mtu)
		return -EINVAL;
	if (dev->max_mtu && mtu > (int)dev->max_mtu)
		return -EINVAL;
	if (dev->netdev_ops && dev->netdev_ops->ndo_change_mtu)
		return dev->netdev_ops->ndo_change_mtu(dev, mtu);
	dev->mtu = (unsigned int)mtu;
	return 0;
}

/* ---- ethernet --------------------------------------------------------- */

__be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	const struct ethhdr *eth;

	skb->dev = dev;
	skb_reset_mac_header(skb);
	eth = (const struct ethhdr *)skb->data;
	skb_pull_inline(skb, ETH_HLEN);

	if (dev && !ether_addr_equal(eth->h_dest, dev->dev_addr)) {
		if (is_multicast_ether_addr(eth->h_dest))
			skb->pkt_type = is_broadcast_ether_addr(eth->h_dest) ?
			    PACKET_BROADCAST : PACKET_MULTICAST;
		else
			skb->pkt_type = PACKET_OTHERHOST;
	}

	if (eth_proto_is_802_3(eth->h_proto))
		return eth->h_proto;
	if (skb->len >= 2 && *(const unsigned short *)skb->data == 0xFFFF)
		return cpu_to_be16(ETH_P_802_3);
	return cpu_to_be16(ETH_P_802_2);
}

int eth_header_parse_protocol_lkpi(const struct sk_buff *skb)
{
	const struct ethhdr *eth = eth_hdr_lkpi(skb);

	return be16_to_cpu(eth->h_proto);
}

void eth_hw_addr_set(struct net_device *dev, const u8 *addr)
{
	ether_addr_copy(dev->dev_addr, addr);
}

void eth_hw_addr_random(struct net_device *dev)
{
	u8 addr[ETH_ALEN];

	eth_random_addr(addr);
	eth_hw_addr_set(dev, addr);
	ether_addr_copy(dev->perm_addr, addr);
}

struct net_device *alloc_etherdev_mqs(int sizeof_priv, unsigned int txqs, unsigned int rxqs)
{
	return alloc_netdev_mqs(sizeof_priv, "eth%d", NET_NAME_ENUM, ether_setup, txqs, rxqs);
}

/* ---- receive / transmit --------------------------------------------- */

void lkpi_set_netif_rx_hook(lkpi_netif_rx_hook_t hook)
{
	__atomic_store_n(&lkpi_netif_rx_hook, hook, __ATOMIC_RELEASE);
}

int netif_receive_skb(struct sk_buff *skb)
{
	lkpi_netif_rx_hook_t hook;

	if (!skb)
		return NET_RX_DROP;
	skb->next = NULL;
	skb->prev = NULL;
	hook = __atomic_load_n(&lkpi_netif_rx_hook, __ATOMIC_ACQUIRE);
	if (hook) {
		hook(skb);
		return NET_RX_SUCCESS;
	}
	kfree_skb(skb);
	return NET_RX_DROP;
}

int netif_rx(struct sk_buff *skb)
{
	return netif_receive_skb(skb);
}

void netif_receive_skb_list(struct list_head *head)
{
	struct sk_buff *skb, *tmp;

	list_for_each_entry_safe(skb, tmp, head, list) {
		list_del(&skb->list);
		netif_receive_skb(skb);
	}
}

int dev_queue_xmit(struct sk_buff *skb)
{
	kfree_skb(skb);
	return 0;
}

/* ---- NAPI poll thread ------------------------------------------------- */

static void lkpi_napi_flush_rx_list(struct napi_struct *napi)
{
	if (list_empty(&napi->rx_list))
		return;
	netif_receive_skb_list(&napi->rx_list);
	napi->rx_count = 0;
}

static void lkpi_napi_thread_fn(void *arg)
{
	struct lkpi_napi_thread *t = arg;
	lkpi_os_thread_t self = lkpi_os_thread_self();
	unsigned long flags;

	for (;;) {
		struct napi_struct *napi;
		int weight, work;

		spin_lock_irqsave(&t->lock, flags);
		if (t->stop) {
			spin_unlock_irqrestore(&t->lock, flags);
			break;
		}
		napi = list_first_entry_or_null(&t->poll_list, struct napi_struct, poll_list);
		if (!napi) {
			spin_unlock_irqrestore(&t->lock, flags);
			lkpi_os_thread_wait(self, LKPI_NAPI_WAIT_NS);
			continue;
		}
		list_del_init(&napi->poll_list);
		spin_unlock_irqrestore(&t->lock, flags);

		weight = napi->weight;
		work = napi->poll(napi, weight);
		WARN_ONCE(work > weight, "napi poll %p returned %d, exceeding its budget %d\n",
		    napi->poll, work, weight);
		if (work < weight)
			continue;	/* the poll called napi_complete_done() */

		if (napi_disable_pending(napi)) {
			napi_complete_done(napi, work);
			continue;
		}
		if (!test_bit(NAPI_STATE_SCHED, &napi->state))
			continue;

		/* Budget exhausted: keep polling, but give others a turn. */
		spin_lock_irqsave(&t->lock, flags);
		if (!t->stop && list_empty(&napi->poll_list))
			list_add_tail(&napi->poll_list, &t->poll_list);
		spin_unlock_irqrestore(&t->lock, flags);
		lkpi_os_thread_yield();
	}

	__atomic_store_n(&t->exited, true, __ATOMIC_RELEASE);
}

/* Called from netif_napi_add(); callers add instances to one device sequentially. */
static struct lkpi_napi_thread *lkpi_napi_thread_get(struct net_device *dev)
{
	struct lkpi_napi_thread *t = dev->lkpi_napi_thread;
	unsigned long flags;

	if (t)
		return t;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return NULL;
	t->dev = dev;
	spin_lock_init(&t->lock);
	INIT_LIST_HEAD(&t->poll_list);
	INIT_LIST_HEAD(&t->global);
	dev->lkpi_napi_thread = t;

	spin_lock_irqsave(&lkpi_napi_lock, flags);
	list_add_tail(&t->global, &lkpi_napi_threads);
	spin_unlock_irqrestore(&lkpi_napi_lock, flags);

	t->thread = lkpi_os_thread_create(lkpi_napi_thread_fn, t, "lkpi-napi");
	if (!t->thread) {
		pr_err("%s: cannot create NAPI poll thread\n", netdev_name(dev));
		spin_lock_irqsave(&lkpi_napi_lock, flags);
		list_del_init(&t->global);
		spin_unlock_irqrestore(&lkpi_napi_lock, flags);
		dev->lkpi_napi_thread = NULL;
		spin_lock_destroy(&t->lock);
		kfree(t);
		return NULL;
	}
	return t;
}

static void lkpi_napi_thread_stop(struct lkpi_napi_thread *t)
{
	unsigned long flags;

	spin_lock_irqsave(&t->lock, flags);
	t->stop = true;
	spin_unlock_irqrestore(&t->lock, flags);
	lkpi_os_thread_wake(t->thread);
	while (!__atomic_load_n(&t->exited, __ATOMIC_ACQUIRE))
		msleep(1);
	lkpi_os_thread_release(t->thread);

	spin_lock_irqsave(&lkpi_napi_lock, flags);
	list_del_init(&t->global);
	spin_unlock_irqrestore(&lkpi_napi_lock, flags);
	if (t->dev)
		t->dev->lkpi_napi_thread = NULL;
	spin_lock_destroy(&t->lock);
	kfree(t);
}

/* ---- NAPI API --------------------------------------------------------- */

void netif_napi_add_weight(struct net_device *dev, struct napi_struct *napi,
    int (*poll)(struct napi_struct *, int), int weight)
{
	unsigned long flags;

	if (WARN_ON_ONCE(!dev || !napi || !poll))
		return;
	if (weight <= 0 || weight > NAPI_POLL_WEIGHT) {
		pr_warn("%s: napi weight %d out of range, using %d\n", netdev_name(dev), weight,
		    NAPI_POLL_WEIGHT);
		weight = NAPI_POLL_WEIGHT;
	}

	INIT_LIST_HEAD(&napi->poll_list);
	INIT_LIST_HEAD(&napi->rx_list);
	INIT_LIST_HEAD(&napi->dev_list);
	napi->rx_count = 0;
	napi->state = 0;
	napi->weight = weight;
	napi->poll = poll;
	napi->dev = dev;
	napi->napi_id = 0;
	/* disabled until napi_enable() */
	set_bit(NAPI_STATE_SCHED, &napi->state);
	set_bit(NAPI_STATE_NPSVC, &napi->state);

	if (!lkpi_napi_thread_get(dev))
		pr_err("%s: NAPI instance %p will never be polled\n", netdev_name(dev), napi);

	spin_lock_irqsave(&lkpi_napi_lock, flags);
	list_add_tail(&napi->dev_list, &dev->napi_list);
	spin_unlock_irqrestore(&lkpi_napi_lock, flags);
}

void netif_napi_del(struct napi_struct *napi)
{
	struct lkpi_napi_thread *t;
	struct sk_buff *skb, *tmp;
	unsigned long flags;

	if (!napi || !napi->dev)
		return;

	t = napi->dev->lkpi_napi_thread;
	if (t) {
		spin_lock_irqsave(&t->lock, flags);
		if (!list_empty(&napi->poll_list))
			list_del_init(&napi->poll_list);
		spin_unlock_irqrestore(&t->lock, flags);
	}

	spin_lock_irqsave(&lkpi_napi_lock, flags);
	if (!list_empty(&napi->dev_list))
		list_del_init(&napi->dev_list);
	spin_unlock_irqrestore(&lkpi_napi_lock, flags);

	list_for_each_entry_safe(skb, tmp, &napi->rx_list, list) {
		list_del(&skb->list);
		kfree_skb(skb);
	}
	napi->rx_count = 0;
}

bool napi_schedule_prep(struct napi_struct *napi)
{
	unsigned long val = READ_ONCE(napi->state), new;

	do {
		if (unlikely(val & BIT(NAPI_STATE_DISABLE)))
			return false;
		new = val | BIT(NAPI_STATE_SCHED);
		if (val & BIT(NAPI_STATE_SCHED))
			new |= BIT(LKPI_NAPI_STATE_MISSED);
	} while (!try_cmpxchg(&napi->state, &val, new));

	return !(val & BIT(NAPI_STATE_SCHED));
}

void __napi_schedule(struct napi_struct *napi)
{
	struct lkpi_napi_thread *t = napi->dev ? napi->dev->lkpi_napi_thread : NULL;
	unsigned long flags;

	if (WARN_ON_ONCE(!t)) {
		clear_bit(NAPI_STATE_SCHED, &napi->state);
		return;
	}

	spin_lock_irqsave(&t->lock, flags);
	if (list_empty(&napi->poll_list))
		list_add_tail(&napi->poll_list, &t->poll_list);
	spin_unlock_irqrestore(&t->lock, flags);
	if (t->thread)
		lkpi_os_thread_wake(t->thread);
}

bool napi_complete_done(struct napi_struct *napi, int work_done)
{
	unsigned long val, new;

	lkpi_napi_flush_rx_list(napi);

	val = READ_ONCE(napi->state);
	do {
		WARN_ON_ONCE(!(val & BIT(NAPI_STATE_SCHED)));
		new = val & ~(BIT(NAPI_STATE_SCHED) | BIT(LKPI_NAPI_STATE_MISSED) |
		    BIT(NAPI_STATE_SCHED_THREADED));
		/* A schedule arrived while polling: stay scheduled. */
		if (val & BIT(LKPI_NAPI_STATE_MISSED))
			new |= BIT(NAPI_STATE_SCHED);
	} while (!try_cmpxchg(&napi->state, &val, new));

	if (unlikely(val & BIT(LKPI_NAPI_STATE_MISSED))) {
		__napi_schedule(napi);
		return false;
	}
	return true;
}

void napi_disable(struct napi_struct *napi)
{
	unsigned long val, new;

	set_bit(NAPI_STATE_DISABLE, &napi->state);

	val = READ_ONCE(napi->state);
	do {
		while (val & (BIT(NAPI_STATE_SCHED) | BIT(NAPI_STATE_NPSVC))) {
			msleep(1);
			val = READ_ONCE(napi->state);
		}
		new = val | BIT(NAPI_STATE_SCHED) | BIT(NAPI_STATE_NPSVC);
	} while (!try_cmpxchg(&napi->state, &val, new));

	/* SCHED stays ours while disabled; a late schedule cannot pass. */
	clear_bit(NAPI_STATE_DISABLE, &napi->state);
}

void napi_enable(struct napi_struct *napi)
{
	unsigned long val = READ_ONCE(napi->state), new;

	WARN_ON_ONCE(!(val & BIT(NAPI_STATE_SCHED)));
	do {
		new = val & ~(BIT(NAPI_STATE_SCHED) | BIT(NAPI_STATE_NPSVC) |
		    BIT(NAPI_STATE_DISABLE) | BIT(LKPI_NAPI_STATE_MISSED));
	} while (!try_cmpxchg(&napi->state, &val, new));
}

void napi_synchronize(const struct napi_struct *napi)
{
	while (test_bit(NAPI_STATE_SCHED, &napi->state))
		msleep(1);
}

int napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	if (!skb)
		return NET_RX_DROP;
	if (!napi) {
		netif_receive_skb(skb);
		return NET_RX_SUCCESS;
	}

	list_add_tail(&skb->list, &napi->rx_list);
	if (++napi->rx_count >= LKPI_GRO_BATCH)
		lkpi_napi_flush_rx_list(napi);
	return NET_RX_SUCCESS;
}

/* ---- runtime hooks ---------------------------------------------------- */

int lkpi_napi_init(void)
{
	spin_lock_init(&lkpi_napi_lock);
	INIT_LIST_HEAD(&lkpi_napi_threads);
	return 0;
}

void lkpi_napi_fini(void)
{
	unsigned long flags;

	for (;;) {
		struct lkpi_napi_thread *t;

		spin_lock_irqsave(&lkpi_napi_lock, flags);
		t = list_first_entry_or_null(&lkpi_napi_threads, struct lkpi_napi_thread, global);
		spin_unlock_irqrestore(&lkpi_napi_lock, flags);
		if (!t)
			break;
		pr_warn("lkpi: NAPI poll thread for %s still alive at shutdown\n",
		    t->dev ? netdev_name(t->dev) : "?");
		lkpi_napi_thread_stop(t);
	}
	spin_lock_destroy(&lkpi_napi_lock);
}
