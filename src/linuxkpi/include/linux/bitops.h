/* SPDX-License-Identifier: MIT */
/*
 * Bit operations.  The atomic variants use compiler builtins so they are
 * correct both on the host test build and inside the kernel; XNU does not
 * export per-bit atomics anyway.
 */
#ifndef _LKPI_LINUX_BITOPS_H_
#define _LKPI_LINUX_BITOPS_H_
#include <linux/types.h>
#include <linux/bits.h>
#include <linux/compiler.h>

#define BITS_PER_TYPE(t) (sizeof(t) * BITS_PER_BYTE)
#define min_lkpi(a, b) ((a) < (b) ? (a) : (b))

/* ---- non-atomic ---- */
static inline void __set_bit(unsigned long nr, volatile unsigned long *addr)
{
	addr[BIT_WORD(nr)] |= BIT_MASK(nr);
}
static inline void __clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	addr[BIT_WORD(nr)] &= ~BIT_MASK(nr);
}
static inline void __change_bit(unsigned long nr, volatile unsigned long *addr)
{
	addr[BIT_WORD(nr)] ^= BIT_MASK(nr);
}
static inline bool __test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr), old = addr[BIT_WORD(nr)];
	addr[BIT_WORD(nr)] = old | mask;
	return (old & mask) != 0;
}
static inline bool __test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr), old = addr[BIT_WORD(nr)];
	addr[BIT_WORD(nr)] = old & ~mask;
	return (old & mask) != 0;
}
static inline bool test_bit(unsigned long nr, const volatile unsigned long *addr)
{
	return (__atomic_load_n(&addr[BIT_WORD(nr)], __ATOMIC_RELAXED) & BIT_MASK(nr)) != 0;
}
#define test_bit_acquire(nr, addr) test_bit(nr, addr)

/* ---- atomic ---- */
static inline void set_bit(unsigned long nr, volatile unsigned long *addr)
{
	__atomic_fetch_or(&addr[BIT_WORD(nr)], BIT_MASK(nr), __ATOMIC_SEQ_CST);
}
static inline void clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	__atomic_fetch_and(&addr[BIT_WORD(nr)], ~BIT_MASK(nr), __ATOMIC_SEQ_CST);
}
#define clear_bit_unlock(nr, addr) clear_bit(nr, addr)
#define __clear_bit_unlock(nr, addr) clear_bit(nr, addr)
static inline void change_bit(unsigned long nr, volatile unsigned long *addr)
{
	__atomic_fetch_xor(&addr[BIT_WORD(nr)], BIT_MASK(nr), __ATOMIC_SEQ_CST);
}
static inline bool test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	return (__atomic_fetch_or(&addr[BIT_WORD(nr)], BIT_MASK(nr), __ATOMIC_SEQ_CST) & BIT_MASK(nr)) != 0;
}
#define test_and_set_bit_lock(nr, addr) test_and_set_bit(nr, addr)
static inline bool test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	return (__atomic_fetch_and(&addr[BIT_WORD(nr)], ~BIT_MASK(nr), __ATOMIC_SEQ_CST) & BIT_MASK(nr)) != 0;
}
static inline bool test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	return (__atomic_fetch_xor(&addr[BIT_WORD(nr)], BIT_MASK(nr), __ATOMIC_SEQ_CST) & BIT_MASK(nr)) != 0;
}

/* ---- find / count ---- */
static inline unsigned int hweight8(unsigned int w) { return __builtin_popcount(w & 0xff); }
static inline unsigned int hweight16(unsigned int w) { return __builtin_popcount(w & 0xffff); }
static inline unsigned int hweight32(unsigned int w) { return __builtin_popcount(w); }
static inline unsigned int hweight64(u64 w) { return __builtin_popcountll(w); }
static inline unsigned long hweight_long(unsigned long w) { return __builtin_popcountl(w); }

static inline unsigned long __ffs(unsigned long word) { return __builtin_ctzl(word); }
static inline unsigned long __fls(unsigned long word) { return BITS_PER_LONG - 1 - __builtin_clzl(word); }
static inline unsigned long ffz(unsigned long word) { return __builtin_ctzl(~word); }
#define ffs(x) __builtin_ffs(x)
static inline int fls(unsigned int x) { return x ? 32 - __builtin_clz(x) : 0; }
static inline int fls64(u64 x) { return x ? 64 - __builtin_clzll(x) : 0; }
static inline unsigned long __ffs64(u64 word) { return __builtin_ctzll(word); }
static inline int fls_long(unsigned long l) { return l ? BITS_PER_LONG - __builtin_clzl(l) : 0; }
static inline unsigned int fns(unsigned long word, unsigned int n)
{
	while (word && n--)
		word &= word - 1;
	return word ? __ffs(word) : BITS_PER_LONG;
}

static inline unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
    unsigned long offset)
{
	while (offset < size) {
		unsigned long w = addr[BIT_WORD(offset)] >> (offset % BITS_PER_LONG);
		if (w)
			return min_lkpi(offset + __ffs(w), size);
		offset = (offset + BITS_PER_LONG) & ~(BITS_PER_LONG - 1);
	}
	return size;
}
static inline unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
    unsigned long offset)
{
	while (offset < size) {
		unsigned long w = ~addr[BIT_WORD(offset)] >> (offset % BITS_PER_LONG);
		if (w)
			return min_lkpi(offset + __ffs(w), size);
		offset = (offset + BITS_PER_LONG) & ~(BITS_PER_LONG - 1);
	}
	return size;
}
#define find_first_bit(addr, size) find_next_bit(addr, size, 0)
#define find_first_zero_bit(addr, size) find_next_zero_bit(addr, size, 0)
static inline unsigned long find_last_bit(const unsigned long *addr, unsigned long size)
{
	unsigned long i = size;
	while (i-- > 0)
		if (test_bit(i, addr))
			return i;
	return size;
}
#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size)); \
	     (bit) < (size); \
	     (bit) = find_next_bit((addr), (size), (bit) + 1))
#define for_each_set_bit_from(bit, addr, size) \
	for ((bit) = find_next_bit((addr), (size), (bit)); \
	     (bit) < (size); \
	     (bit) = find_next_bit((addr), (size), (bit) + 1))
#define for_each_clear_bit(bit, addr, size) \
	for ((bit) = find_first_zero_bit((addr), (size)); \
	     (bit) < (size); \
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

/* ---- rotate / sign ---- */
static inline u32 rol32(u32 word, unsigned int shift) { return (word << (shift & 31)) | (word >> ((-shift) & 31)); }
static inline u32 ror32(u32 word, unsigned int shift) { return (word >> (shift & 31)) | (word << ((-shift) & 31)); }
static inline u16 rol16(u16 word, unsigned int shift) { return (word << (shift & 15)) | (word >> ((-shift) & 15)); }
static inline u16 ror16(u16 word, unsigned int shift) { return (word >> (shift & 15)) | (word << ((-shift) & 15)); }
static inline u8 rol8(u8 word, unsigned int shift) { return (word << (shift & 7)) | (word >> ((-shift) & 7)); }
static inline u8 ror8(u8 word, unsigned int shift) { return (word >> (shift & 7)) | (word << ((-shift) & 7)); }
static inline s32 sign_extend32(u32 value, int index)
{
	u8 shift = 31 - index;
	return (s32)(value << shift) >> shift;
}
static inline s64 sign_extend64(u64 value, int index)
{
	u8 shift = 63 - index;
	return (s64)(value << shift) >> shift;
}
static inline unsigned int get_count_order(unsigned int count)
{
	return count <= 1 ? 0 : fls(count - 1);
}
static inline unsigned long get_count_order_long(unsigned long l)
{
	return l <= 1 ? 0 : fls_long(l - 1);
}

#endif /* _LKPI_LINUX_BITOPS_H_ */
