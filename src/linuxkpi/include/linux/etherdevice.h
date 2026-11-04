/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_ETHERDEVICE_H_
#define _LKPI_LINUX_ETHERDEVICE_H_
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/random.h>
#include <linux/string.h>
#include <asm/unaligned.h>

struct net_device;
struct sk_buff;

static inline bool is_zero_ether_addr(const u8 *addr)
{
	return (*(const u32 *)addr | *(const u16 *)(addr + 4)) == 0;
}
static inline bool is_multicast_ether_addr(const u8 *addr) { return 0x01 & addr[0]; }
static inline bool is_local_ether_addr(const u8 *addr) { return 0x02 & addr[0]; }
static inline bool is_broadcast_ether_addr(const u8 *addr)
{
	return (*(const u16 *)(addr + 0) & *(const u16 *)(addr + 2) & *(const u16 *)(addr + 4)) == 0xffff;
}
static inline bool is_unicast_ether_addr(const u8 *addr) { return !is_multicast_ether_addr(addr); }
static inline bool is_valid_ether_addr(const u8 *addr) { return !is_multicast_ether_addr(addr) && !is_zero_ether_addr(addr); }
static inline bool is_link_local_ether_addr(const u8 *addr)
{
	static const u8 pfx[] = { 0x01, 0x80, 0xc2 };
	return memcmp(addr, pfx, 3) == 0 && (addr[3] | addr[4] | (addr[5] & 0xf0)) == 0;
}
static inline void eth_random_addr(u8 *addr)
{
	get_random_bytes(addr, ETH_ALEN);
	addr[0] &= 0xfe;
	addr[0] |= 0x02;
}
static inline void eth_broadcast_addr(u8 *addr) { memset(addr, 0xff, ETH_ALEN); }
static inline void eth_zero_addr(u8 *addr) { memset(addr, 0x00, ETH_ALEN); }
static inline void ether_addr_copy(u8 *dst, const u8 *src) { memcpy(dst, src, ETH_ALEN); }
static inline bool ether_addr_equal(const u8 *a, const u8 *b) { return memcmp(a, b, ETH_ALEN) == 0; }
static inline bool ether_addr_equal_64bits(const u8 *a, const u8 *b) { return ether_addr_equal(a, b); }
static inline bool ether_addr_equal_unaligned(const u8 *a, const u8 *b) { return ether_addr_equal(a, b); }
static inline bool ether_addr_equal_masked(const u8 *a, const u8 *b, const u8 *mask)
{
	int i;
	for (i = 0; i < ETH_ALEN; i++)
		if ((a[i] ^ b[i]) & mask[i])
			return false;
	return true;
}
static inline u64 ether_addr_to_u64(const u8 *addr)
{
	u64 u = 0;
	int i;
	for (i = 0; i < ETH_ALEN; i++)
		u = u << 8 | addr[i];
	return u;
}
static inline void u64_to_ether_addr(u64 u, u8 *addr)
{
	int i;
	for (i = ETH_ALEN - 1; i >= 0; i--) {
		addr[i] = u & 0xff;
		u = u >> 8;
	}
}
static inline void eth_addr_dec(u8 *addr) { u64_to_ether_addr(ether_addr_to_u64(addr) - 1, addr); }
static inline void eth_addr_inc(u8 *addr) { u64_to_ether_addr(ether_addr_to_u64(addr) + 1, addr); }
static inline unsigned long compare_ether_header(const void *a, const void *b)
{
	return memcmp(a, b, ETH_HLEN) != 0;
}
static inline bool eth_proto_is_802_3(__be16 proto) { return be16_to_cpu(proto) >= ETH_P_802_3_MIN; }
__be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev);
int eth_header_parse_protocol_lkpi(const struct sk_buff *skb);
void eth_hw_addr_set(struct net_device *dev, const u8 *addr);
void eth_hw_addr_random(struct net_device *dev);
#define eth_hw_addr_inherit(d, s) do { } while (0)
#define eth_mac_addr(d, p) (0)
#define eth_validate_addr(d) (0)
struct net_device *alloc_etherdev_mqs(int sizeof_priv, unsigned int txqs, unsigned int rxqs);
#define alloc_etherdev(p) alloc_etherdev_mqs(p, 1, 1)
#endif
