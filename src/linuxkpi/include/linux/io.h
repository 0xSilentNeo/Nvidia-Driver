/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_IO_H_
#define _LKPI_LINUX_IO_H_
#include <linux/types.h>
#include <asm/byteorder.h>
#include <lkpi/os.h>

static inline u8 readb(const volatile void __iomem *a) { return lkpi_os_mmio_read8(a); }
static inline u16 readw(const volatile void __iomem *a) { return lkpi_os_mmio_read16(a); }
static inline u32 readl(const volatile void __iomem *a) { return lkpi_os_mmio_read32(a); }
static inline u64 readq(const volatile void __iomem *a)
{
	return (u64)lkpi_os_mmio_read32((const volatile u8 *)a + 4) << 32 | lkpi_os_mmio_read32(a);
}
static inline void writeb(u8 v, volatile void __iomem *a) { lkpi_os_mmio_write8(a, v); }
static inline void writew(u16 v, volatile void __iomem *a) { lkpi_os_mmio_write16(a, v); }
static inline void writel(u32 v, volatile void __iomem *a) { lkpi_os_mmio_write32(a, v); }
static inline void writeq(u64 v, volatile void __iomem *a)
{
	lkpi_os_mmio_write32(a, (u32)v);
	lkpi_os_mmio_write32((volatile u8 *)a + 4, (u32)(v >> 32));
}
#define readb_relaxed readb
#define readw_relaxed readw
#define readl_relaxed readl
#define readq_relaxed readq
#define writeb_relaxed writeb
#define writew_relaxed writew
#define writel_relaxed writel
#define writeq_relaxed writeq
#define __raw_readb readb
#define __raw_readw readw
#define __raw_readl readl
#define __raw_readq readq
#define __raw_writeb writeb
#define __raw_writew writew
#define __raw_writel writel
#define __raw_writeq writeq
#define ioread8(a) readb(a)
#define ioread16(a) readw(a)
#define ioread32(a) readl(a)
#define ioread16be(a) be16_to_cpu(readw(a))
#define ioread32be(a) be32_to_cpu(readl(a))
#define iowrite8(v, a) writeb(v, a)
#define iowrite16(v, a) writew(v, a)
#define iowrite32(v, a) writel(v, a)
#define iowrite16be(v, a) writew(cpu_to_be16(v), a)
#define iowrite32be(v, a) writel(cpu_to_be32(v), a)
#define mmiowb() lkpi_os_mmio_barrier()

static inline void memcpy_fromio(void *to, const volatile void __iomem *from, size_t count)
{
	const volatile u8 *s = (const volatile u8 *)from;
	u8 *d = (u8 *)to;
	while (count >= 4) {
		u32 v = readl(s);
		__builtin_memcpy(d, &v, 4);
		s += 4; d += 4; count -= 4;
	}
	while (count--)
		*d++ = readb(s++);
}
static inline void memcpy_toio(volatile void __iomem *to, const void *from, size_t count)
{
	volatile u8 *d = (volatile u8 *)to;
	const u8 *s = (const u8 *)from;
	while (count >= 4) {
		u32 v;
		__builtin_memcpy(&v, s, 4);
		writel(v, d);
		s += 4; d += 4; count -= 4;
	}
	while (count--)
		writeb(*s++, d++);
}
static inline void memset_io(volatile void __iomem *to, int c, size_t count)
{
	volatile u8 *d = (volatile u8 *)to;
	while (count--)
		writeb((u8)c, d++);
}
#define ioremap(phys, size) ((void __iomem *)0)
#define ioremap_wc(phys, size) ((void __iomem *)0)
#define iounmap(addr) do { } while (0)
#define IOMEM_ERR_PTR(err) ((void __iomem *)ERR_PTR(err))
#endif
