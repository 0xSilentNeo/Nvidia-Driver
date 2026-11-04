/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_HEX_H_
#define _LKPI_LINUX_HEX_H_
#include <linux/types.h>
extern const char hex_asc[];
extern const char hex_asc_upper[];
#define hex_asc_lo(x)	hex_asc[((x) & 0x0f)]
#define hex_asc_hi(x)	hex_asc[((x) & 0xf0) >> 4]
static inline char *hex_byte_pack(char *buf, u8 byte)
{
	*buf++ = hex_asc_hi(byte);
	*buf++ = hex_asc_lo(byte);
	return buf;
}
static inline int hex_to_bin(unsigned char ch)
{
	if (ch >= '0' && ch <= '9') return ch - '0';
	ch |= 0x20;
	if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	return -1;
}
static inline int hex2bin(u8 *dst, const char *src, size_t count)
{
	while (count--) {
		int hi = hex_to_bin(*src++), lo = hex_to_bin(*src++);
		if (hi < 0 || lo < 0)
			return -1;
		*dst++ = (hi << 4) | lo;
	}
	return 0;
}
static inline char *bin2hex(char *dst, const void *src, size_t count)
{
	const unsigned char *_src = (const unsigned char *)src;
	while (count--)
		dst = hex_byte_pack(dst, *_src++);
	return dst;
}
#endif
