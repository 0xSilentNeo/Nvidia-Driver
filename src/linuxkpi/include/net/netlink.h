/* SPDX-License-Identifier: MIT */
/*
 * Netlink attribute helpers.  There is no netlink on macOS; mt76 only needs
 * the types for its (disabled) testmode interface and for the cfg80211
 * vendor-command prototypes.
 */
#ifndef _LKPI_NET_NETLINK_H_
#define _LKPI_NET_NETLINK_H_
#include <linux/types.h>
#include <linux/skbuff.h>

struct nlattr {
	u16 nla_len;
	u16 nla_type;
};
#define NLA_ALIGNTO		4
#define NLA_ALIGN(len)		(((len) + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1))
#define NLA_HDRLEN		((int)NLA_ALIGN(sizeof(struct nlattr)))
#define NLA_F_NESTED		(1 << 15)
#define NLA_TYPE_MASK		~(NLA_F_NESTED | (1 << 14))

enum {
	NLA_UNSPEC, NLA_U8, NLA_U16, NLA_U32, NLA_U64, NLA_STRING, NLA_FLAG,
	NLA_MSECS, NLA_NESTED, NLA_NESTED_ARRAY, NLA_NUL_STRING, NLA_BINARY,
	NLA_S8, NLA_S16, NLA_S32, NLA_S64, NLA_BITFIELD32, NLA_REJECT, NLA_BE16, NLA_BE32,
};
struct nla_policy {
	u8 type;
	u8 validation_type;
	u16 len;
	union {
		u16 strict_start_type;
		const struct nla_policy *nested_policy;
		s16 min, max;
	};
};
#define NLA_POLICY_NESTED(policy) { .type = NLA_NESTED, .nested_policy = policy }
#define NLA_POLICY_NESTED_ARRAY(policy) { .type = NLA_NESTED_ARRAY, .nested_policy = policy }
#define NLA_POLICY_EXACT_LEN(_len) { .type = NLA_BINARY, .len = _len }
#define NLA_POLICY_MAX(tp, _max) { .type = tp, .max = _max }
#define NLA_POLICY_RANGE(tp, _min, _max) { .type = tp, .min = _min, .max = _max }
#define NLA_POLICY_MIN_LEN(_len) { .type = NLA_BINARY, .len = _len }

struct netlink_callback {
	struct sk_buff *skb;
	long args[6];
};
struct netlink_ext_ack {
	const char *_msg;
};
struct genl_info {
	struct nlattr **attrs;
	struct netlink_ext_ack *extack;
};

static inline int nla_type(const struct nlattr *nla) { return nla->nla_type & NLA_TYPE_MASK; }
static inline void *nla_data(const struct nlattr *nla) { return (char *)nla + NLA_HDRLEN; }
static inline int nla_len(const struct nlattr *nla) { return nla->nla_len - NLA_HDRLEN; }
static inline u8 nla_get_u8(const struct nlattr *nla) { return *(u8 *)nla_data(nla); }
static inline u16 nla_get_u16(const struct nlattr *nla) { return *(u16 *)nla_data(nla); }
static inline u32 nla_get_u32(const struct nlattr *nla) { return *(u32 *)nla_data(nla); }
static inline u64 nla_get_u64(const struct nlattr *nla) { u64 v; memcpy(&v, nla_data(nla), 8); return v; }
static inline s8 nla_get_s8(const struct nlattr *nla) { return *(s8 *)nla_data(nla); }
static inline s32 nla_get_s32(const struct nlattr *nla) { return *(s32 *)nla_data(nla); }
static inline int nla_ok(const struct nlattr *nla, int remaining)
{
	return remaining >= (int)sizeof(*nla) && nla->nla_len >= sizeof(*nla) && nla->nla_len <= remaining;
}
static inline struct nlattr *nla_next(const struct nlattr *nla, int *remaining)
{
	unsigned int totlen = NLA_ALIGN(nla->nla_len);
	*remaining -= totlen;
	return (struct nlattr *)((char *)nla + totlen);
}
#define nla_for_each_attr(pos, head, len, rem) \
	for (pos = head, rem = len; nla_ok(pos, rem); pos = nla_next(pos, &(rem)))
#define nla_for_each_nested(pos, nla, rem) \
	nla_for_each_attr(pos, (struct nlattr *)nla_data(nla), nla_len(nla), rem)
/* Message construction is unsupported: every put fails with -EMSGSIZE. */
static inline int nla_put(struct sk_buff *skb, int attrtype, int attrlen, const void *data) { (void)skb; (void)attrtype; (void)attrlen; (void)data; return -EMSGSIZE; }
#define nla_put_u8(skb, t, v) nla_put(skb, t, 1, NULL)
#define nla_put_u16(skb, t, v) nla_put(skb, t, 2, NULL)
#define nla_put_u32(skb, t, v) nla_put(skb, t, 4, NULL)
#define nla_put_u64_64bit(skb, t, v, p) nla_put(skb, t, 8, NULL)
#define nla_put_s8(skb, t, v) nla_put(skb, t, 1, NULL)
#define nla_put_s32(skb, t, v) nla_put(skb, t, 4, NULL)
#define nla_put_flag(skb, t) nla_put(skb, t, 0, NULL)
#define nla_put_string(skb, t, s) nla_put(skb, t, 0, NULL)
static inline struct nlattr *nla_nest_start(struct sk_buff *skb, int attrtype) { (void)skb; (void)attrtype; return NULL; }
#define nla_nest_start_noflag(skb, t) nla_nest_start(skb, t)
static inline int nla_nest_end(struct sk_buff *skb, struct nlattr *start) { (void)skb; (void)start; return 0; }
static inline void nla_nest_cancel(struct sk_buff *skb, struct nlattr *start) { (void)skb; (void)start; }
static inline int nla_parse_nested(struct nlattr *tb[], int maxtype, const struct nlattr *nla,
    const struct nla_policy *policy, struct netlink_ext_ack *extack)
{
	(void)tb; (void)maxtype; (void)nla; (void)policy; (void)extack;
	return -EINVAL;
}
#define nla_parse_nested_deprecated nla_parse_nested
static inline int nla_total_size(int payload) { return NLA_ALIGN(NLA_HDRLEN + payload); }
#endif /* _LKPI_NET_NETLINK_H_ */
