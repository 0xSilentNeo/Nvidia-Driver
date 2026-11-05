/* SPDX-License-Identifier: MIT */
/*
 * sys/endian.h - byte order helpers.  Little-endian hosts only (x86-64,
 * arm64); lkpi/bsd.h already provides the hto*/*toh macros as constant
 * expressions, this adds FreeBSD's bswap and the enc/dec family.
 */
#ifndef _BSDKPI_SYS_ENDIAN_H_
#define _BSDKPI_SYS_ENDIAN_H_
#include <sys/cdefs.h>
#include <sys/types.h>
#include <lkpi/bsd.h>

#ifndef _LITTLE_ENDIAN
#define _LITTLE_ENDIAN	1234
#endif
#ifndef _BIG_ENDIAN
#define _BIG_ENDIAN	4321
#endif
#ifndef _PDP_ENDIAN
#define _PDP_ENDIAN	3412
#endif
#ifndef _BYTE_ORDER
#define _BYTE_ORDER	_LITTLE_ENDIAN
#endif
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN	_LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN	_BIG_ENDIAN
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER	_BYTE_ORDER
#endif
#ifndef _QUAD_HIGHWORD
#define _QUAD_HIGHWORD	1
#define _QUAD_LOWWORD	0
#endif

#ifndef bswap16
#define bswap16(x)	__builtin_bswap16((uint16_t)(x))
#define bswap32(x)	__builtin_bswap32((uint32_t)(x))
#define bswap64(x)	__builtin_bswap64((uint64_t)(x))
#endif

static __inline uint16_t
be16dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return ((uint16_t)((p[0] << 8) | p[1]));
}

static __inline uint32_t
be32dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | p[3]);
}

static __inline uint64_t
be64dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return (((uint64_t)be32dec(p) << 32) | be32dec(p + 4));
}

static __inline uint16_t
le16dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return ((uint16_t)((p[1] << 8) | p[0]));
}

static __inline uint32_t
le32dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return (((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
	    ((uint32_t)p[1] << 8) | p[0]);
}

static __inline uint64_t
le64dec(const void *pp)
{
	const uint8_t *p = (const uint8_t *)pp;

	return (((uint64_t)le32dec(p + 4) << 32) | le32dec(p));
}

static __inline void
be16enc(void *pp, uint16_t u)
{
	uint8_t *p = (uint8_t *)pp;

	p[0] = (u >> 8) & 0xff;
	p[1] = u & 0xff;
}

static __inline void
be32enc(void *pp, uint32_t u)
{
	uint8_t *p = (uint8_t *)pp;

	p[0] = (u >> 24) & 0xff;
	p[1] = (u >> 16) & 0xff;
	p[2] = (u >> 8) & 0xff;
	p[3] = u & 0xff;
}

static __inline void
be64enc(void *pp, uint64_t u)
{
	uint8_t *p = (uint8_t *)pp;

	be32enc(p, (uint32_t)(u >> 32));
	be32enc(p + 4, (uint32_t)(u & 0xffffffffU));
}

static __inline void
le16enc(void *pp, uint16_t u)
{
	uint8_t *p = (uint8_t *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
}

static __inline void
le32enc(void *pp, uint32_t u)
{
	uint8_t *p = (uint8_t *)pp;

	p[0] = u & 0xff;
	p[1] = (u >> 8) & 0xff;
	p[2] = (u >> 16) & 0xff;
	p[3] = (u >> 24) & 0xff;
}

static __inline void
le64enc(void *pp, uint64_t u)
{
	uint8_t *p = (uint8_t *)pp;

	le32enc(p, (uint32_t)(u & 0xffffffffU));
	le32enc(p + 4, (uint32_t)(u >> 32));
}

#endif /* _BSDKPI_SYS_ENDIAN_H_ */
