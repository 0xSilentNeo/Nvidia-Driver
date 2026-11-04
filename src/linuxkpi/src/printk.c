// SPDX-License-Identifier: MIT
/*
 * printk, dev_printk, WARN/BUG, hex dumps, kasprintf and the kstrto*
 * family.  All output funnels into lkpi_os_vlog(); the KERN_SOH level
 * prefix is parsed off the format string and mapped to an LKPI_LOG_* level.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/limits.h>
#include <linux/printk.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/bug.h>
#include <linux/hex.h>
#include <linux/kstrtox.h>
#include <linux/sprintf.h>
#include <lkpi/os.h>

const char hex_asc[] = "0123456789abcdef";
const char hex_asc_upper[] = "0123456789ABCDEF";

/* ---- level handling -------------------------------------------------- */

static int lkpi_level_to_os(int linux_level)
{
	switch (linux_level) {
	case 0:			/* KERN_EMERG */
	case 1:			/* KERN_ALERT */
		return LKPI_LOG_EMERG;
	case 2:			/* KERN_CRIT */
	case 3:			/* KERN_ERR */
		return LKPI_LOG_ERR;
	case 4:			/* KERN_WARNING */
		return LKPI_LOG_WARN;
	case 5:			/* KERN_NOTICE */
		return LKPI_LOG_NOTICE;
	case 7:			/* KERN_DEBUG */
		return LKPI_LOG_DEBUG;
	case 6:			/* KERN_INFO */
	default:
		return LKPI_LOG_INFO;
	}
}

/*
 * Strip every leading "\001X" sequence from @fmt.  Returns the OS level;
 * KERN_CONT carries no level of its own and is logged as INFO (we cannot
 * join lines across lkpi_os_vlog() calls).
 */
static int lkpi_parse_level(const char **fmt)
{
	int level = 6;
	const char *f = *fmt;

	while (f[0] == KERN_SOH[0] && f[1] != '\0') {
		if (f[1] >= '0' && f[1] <= '7')
			level = f[1] - '0';
		else if (f[1] != 'c')
			break;
		f += 2;
	}
	*fmt = f;
	return lkpi_level_to_os(level);
}

static int lkpi_vlog_len(int level, const char *fmt, va_list ap)
{
	va_list ap2;
	int len;

	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	lkpi_os_vlog(level, fmt, ap);
	return len < 0 ? 0 : len;
}

int vprintk(const char *fmt, va_list ap)
{
	int level = lkpi_parse_level(&fmt);

	return lkpi_vlog_len(level, fmt, ap);
}

int printk(const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vprintk(fmt, ap);
	va_end(ap);
	return ret;
}

int _printk(const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vprintk(fmt, ap);
	va_end(ap);
	return ret;
}

/* ---- dev_printk ------------------------------------------------------ */

/*
 * Build "<name>: <fmt>" so the arguments are formatted by the backend in one
 * go.  '%' in the device name is doubled so it cannot be taken as a
 * conversion.
 */
static void lkpi_dev_vprintk(int os_level, const struct device *dev, const char *fmt, va_list ap)
{
	char buf[192];
	const char *name = dev ? dev_name(dev) : NULL;
	size_t i = 0;

	if (!name || !*name)
		name = "(null device)";
	while (*name && i < sizeof(buf) - 4) {
		if (*name == '%')
			buf[i++] = '%';
		buf[i++] = *name++;
	}
	buf[i++] = ':';
	buf[i++] = ' ';
	buf[i] = '\0';
	while (fmt[0] == KERN_SOH[0] && fmt[1] != '\0')
		fmt += 2;
	if (strlen(fmt) + i < sizeof(buf)) {
		memcpy(buf + i, fmt, strlen(fmt) + 1);
		lkpi_os_vlog(os_level, buf, ap);
	} else {
		/* format too long for the stack buffer: lose the prefix, keep the text */
		lkpi_os_vlog(os_level, fmt, ap);
	}
}

void dev_printk_emit(int level, const struct device *dev, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	lkpi_dev_vprintk(lkpi_level_to_os(level), dev, fmt, ap);
	va_end(ap);
}

int dev_err_probe(const struct device *dev, int err, const char *fmt, ...)
{
	char msg[160];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	if (err == -EPROBE_DEFER)
		dev_info(dev, "deferred probe: %s", msg);
	else
		dev_err(dev, "error %d: %s", err, msg);
	return err;
}

/* ---- WARN / BUG ------------------------------------------------------ */

void lkpi_warn(const char *file, int line, const char *fmt, ...)
{
	char msg[160];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	lkpi_os_log(LKPI_LOG_WARN, "WARNING: %s:%d %s\n", file, line, msg);
	lkpi_os_dump_stack();
}

void lkpi_bug(const char *file, int line)
{
	lkpi_os_panic("BUG at %s:%d", file, line);
}

/* ---- hex dumps -------------------------------------------------------- */

int hex_dump_to_buffer(const void *buf, size_t len, int rowsize, int groupsize,
    char *linebuf, size_t linebuflen, bool ascii)
{
	const u8 *ptr = buf;
	int ngroups;
	int j, lx = 0;
	int ascii_column;
	int ret;

	if (rowsize != 16 && rowsize != 32)
		rowsize = 16;
	if (len > (size_t)rowsize)
		len = rowsize;
	if (groupsize != 1 && groupsize != 2 && groupsize != 4 && groupsize != 8)
		groupsize = 1;
	if ((len % groupsize) != 0)
		groupsize = 1;

	ngroups = (int)len / groupsize;
	ascii_column = rowsize * 2 + rowsize / groupsize + 1;

	if (!linebuflen)
		goto overflow1;
	if (!len)
		goto nil;

	if (groupsize == 8) {
		for (j = 0; j < ngroups; j++) {
			u64 v;

			memcpy(&v, ptr + j * 8, sizeof(v));
			ret = snprintf(linebuf + lx, linebuflen - lx, "%s%16.16llx",
				       j ? " " : "", (unsigned long long)v);
			if (ret < 0 || (size_t)ret >= linebuflen - lx)
				goto overflow1;
			lx += ret;
		}
	} else if (groupsize == 4) {
		for (j = 0; j < ngroups; j++) {
			u32 v;

			memcpy(&v, ptr + j * 4, sizeof(v));
			ret = snprintf(linebuf + lx, linebuflen - lx, "%s%8.8x", j ? " " : "", v);
			if (ret < 0 || (size_t)ret >= linebuflen - lx)
				goto overflow1;
			lx += ret;
		}
	} else if (groupsize == 2) {
		for (j = 0; j < ngroups; j++) {
			u16 v;

			memcpy(&v, ptr + j * 2, sizeof(v));
			ret = snprintf(linebuf + lx, linebuflen - lx, "%s%4.4x", j ? " " : "", v);
			if (ret < 0 || (size_t)ret >= linebuflen - lx)
				goto overflow1;
			lx += ret;
		}
	} else {
		for (j = 0; j < (int)len; j++) {
			u8 ch;

			if (linebuflen < (size_t)lx + 2)
				goto overflow2;
			ch = ptr[j];
			linebuf[lx++] = hex_asc_hi(ch);
			linebuf[lx++] = hex_asc_lo(ch);
			if (j < (int)len - 1) {
				if (linebuflen < (size_t)lx + 2)
					goto overflow2;
				linebuf[lx++] = ' ';
			}
		}
	}
	if (!ascii)
		goto nil;

	while (lx < ascii_column) {
		if (linebuflen < (size_t)lx + 2)
			goto overflow2;
		linebuf[lx++] = ' ';
	}
	for (j = 0; j < (int)len; j++) {
		u8 ch;

		if (linebuflen < (size_t)lx + 2)
			goto overflow2;
		ch = ptr[j];
		linebuf[lx++] = (isascii(ch) && isprint(ch)) ? (char)ch : '.';
	}
nil:
	linebuf[lx] = '\0';
	return lx;
overflow2:
	linebuf[lx++] = '\0';
overflow1:
	return ascii ? ascii_column + (int)len : (groupsize * 2 + 1) * ngroups - 1;
}

void print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
    int rowsize, int groupsize, const void *buf, size_t len, bool ascii)
{
	const u8 *ptr = buf;
	char linebuf[32 * 3 + 2 + 32 + 1];
	size_t i, linelen, remaining = len;
	int os_level;

	if (!level)
		level = KERN_DEBUG;
	os_level = lkpi_parse_level(&level);
	if (rowsize != 16 && rowsize != 32)
		rowsize = 16;
	if (!prefix_str)
		prefix_str = "";

	for (i = 0; i < len; i += rowsize) {
		linelen = remaining < (size_t)rowsize ? remaining : (size_t)rowsize;
		remaining -= linelen;
		hex_dump_to_buffer(ptr + i, linelen, rowsize, groupsize, linebuf,
				   sizeof(linebuf), ascii);
		switch (prefix_type) {
		case DUMP_PREFIX_ADDRESS:
			lkpi_os_log(os_level, "%s%p: %s\n", prefix_str, (const void *)(ptr + i), linebuf);
			break;
		case DUMP_PREFIX_OFFSET:
			lkpi_os_log(os_level, "%s%.8zx: %s\n", prefix_str, i, linebuf);
			break;
		default:
			lkpi_os_log(os_level, "%s%s\n", prefix_str, linebuf);
			break;
		}
	}
}

/* ---- kasprintf -------------------------------------------------------- */

char *kvasprintf(gfp_t gfp, const char *fmt, va_list ap)
{
	va_list ap2;
	char *p;
	int len;

	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if (len < 0)
		return NULL;
	p = kmalloc((size_t)len + 1, gfp);
	if (!p)
		return NULL;
	vsnprintf(p, (size_t)len + 1, fmt, ap);
	return p;
}

char *kasprintf(gfp_t gfp, const char *fmt, ...)
{
	va_list ap;
	char *p;

	va_start(ap, fmt);
	p = kvasprintf(gfp, fmt, ap);
	va_end(ap);
	return p;
}

/* ---- kstrto* ---------------------------------------------------------- */

#define LKPI_KSTRTOX_OVERFLOW	(1U << 31)

static const char *lkpi_fixup_radix(const char *s, unsigned int *base)
{
	if (*base == 0) {
		if (s[0] == '0') {
			if (tolower((unsigned char)s[1]) == 'x' && isxdigit((unsigned char)s[2]))
				*base = 16;
			else
				*base = 8;
		} else {
			*base = 10;
		}
	}
	if (*base == 16 && s[0] == '0' && tolower((unsigned char)s[1]) == 'x')
		s += 2;
	return s;
}

/* Returns the number of digits consumed, with LKPI_KSTRTOX_OVERFLOW or'ed in. */
static unsigned int lkpi_parse_integer(const char *s, unsigned int base, unsigned long long *p)
{
	unsigned long long res = 0;
	unsigned int rv = 0;
	bool overflow = false;

	for (;;) {
		unsigned int c = (unsigned char)*s;
		unsigned int lc = c | 0x20;
		unsigned int val;

		if (c >= '0' && c <= '9')
			val = c - '0';
		else if (lc >= 'a' && lc <= 'f')
			val = lc - 'a' + 10;
		else
			break;
		if (val >= base)
			break;
		if (res > (~0ULL - val) / base)
			overflow = true;
		res = res * base + val;
		rv++;
		s++;
	}
	*p = res;
	if (overflow)
		rv |= LKPI_KSTRTOX_OVERFLOW;
	return rv;
}

static int lkpi_kstrtoull(const char *s, unsigned int base, unsigned long long *res)
{
	unsigned long long v;
	unsigned int rv;

	if (!s)
		return -EINVAL;
	if (s[0] == '+')
		s++;
	s = lkpi_fixup_radix(s, &base);
	rv = lkpi_parse_integer(s, base, &v);
	if (rv & LKPI_KSTRTOX_OVERFLOW)
		return -ERANGE;
	if (rv == 0)
		return -EINVAL;
	s += rv;
	if (*s == '\n')
		s++;
	if (*s)
		return -EINVAL;
	*res = v;
	return 0;
}

int kstrtoull(const char *s, unsigned int base, unsigned long long *res)
{
	if (s && s[0] == '+')
		s++;
	return lkpi_kstrtoull(s, base, res);
}

int kstrtoll(const char *s, unsigned int base, long long *res)
{
	unsigned long long tmp;
	int rv;

	if (!s)
		return -EINVAL;
	if (s[0] == '-') {
		rv = lkpi_kstrtoull(s + 1, base, &tmp);
		if (rv < 0)
			return rv;
		if ((long long)-tmp > 0)
			return -ERANGE;
		*res = -(long long)tmp;
	} else {
		rv = kstrtoull(s, base, &tmp);
		if (rv < 0)
			return rv;
		if ((long long)tmp < 0)
			return -ERANGE;
		*res = (long long)tmp;
	}
	return 0;
}

int kstrtoul(const char *s, unsigned int base, unsigned long *res)
{
	unsigned long long tmp;
	int rv = kstrtoull(s, base, &tmp);

	if (rv < 0)
		return rv;
	if (tmp != (unsigned long)tmp)
		return -ERANGE;
	*res = (unsigned long)tmp;
	return 0;
}

int kstrtol(const char *s, unsigned int base, long *res)
{
	long long tmp;
	int rv = kstrtoll(s, base, &tmp);

	if (rv < 0)
		return rv;
	if (tmp != (long)tmp)
		return -ERANGE;
	*res = (long)tmp;
	return 0;
}

int kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
	unsigned long long tmp;
	int rv = kstrtoull(s, base, &tmp);

	if (rv < 0)
		return rv;
	if (tmp != (unsigned int)tmp)
		return -ERANGE;
	*res = (unsigned int)tmp;
	return 0;
}

int kstrtoint(const char *s, unsigned int base, int *res)
{
	long long tmp;
	int rv = kstrtoll(s, base, &tmp);

	if (rv < 0)
		return rv;
	if (tmp != (int)tmp)
		return -ERANGE;
	*res = (int)tmp;
	return 0;
}

int kstrtou16(const char *s, unsigned int base, u16 *res)
{
	unsigned long long tmp;
	int rv = kstrtoull(s, base, &tmp);

	if (rv < 0)
		return rv;
	if (tmp != (u16)tmp)
		return -ERANGE;
	*res = (u16)tmp;
	return 0;
}

int kstrtos16(const char *s, unsigned int base, s16 *res)
{
	long long tmp;
	int rv = kstrtoll(s, base, &tmp);

	if (rv < 0)
		return rv;
	if (tmp != (s16)tmp)
		return -ERANGE;
	*res = (s16)tmp;
	return 0;
}

int kstrtou8(const char *s, unsigned int base, u8 *res)
{
	unsigned long long tmp;
	int rv = kstrtoull(s, base, &tmp);

	if (rv < 0)
		return rv;
	if (tmp != (u8)tmp)
		return -ERANGE;
	*res = (u8)tmp;
	return 0;
}

int kstrtos8(const char *s, unsigned int base, s8 *res)
{
	long long tmp;
	int rv = kstrtoll(s, base, &tmp);

	if (rv < 0)
		return rv;
	if (tmp != (s8)tmp)
		return -ERANGE;
	*res = (s8)tmp;
	return 0;
}

int kstrtobool(const char *s, bool *res)
{
	if (!s)
		return -EINVAL;
	switch (s[0]) {
	case 'y':
	case 'Y':
	case '1':
		*res = true;
		return 0;
	case 'n':
	case 'N':
	case '0':
		*res = false;
		return 0;
	case 'o':
	case 'O':
		switch (s[1]) {
		case 'n':
		case 'N':
			*res = true;
			return 0;
		case 'f':
		case 'F':
			*res = false;
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return -EINVAL;
}

/* ---- simple_strto* ------------------------------------------------------ */

unsigned long long simple_strtoull(const char *cp, char **endp, unsigned int base)
{
	unsigned long long result;
	unsigned int rv;

	cp = lkpi_fixup_radix(cp, &base);
	rv = lkpi_parse_integer(cp, base, &result);
	cp += rv & ~LKPI_KSTRTOX_OVERFLOW;
	if (endp)
		*endp = (char *)cp;
	return result;
}

unsigned long simple_strtoul(const char *cp, char **endp, unsigned int base)
{
	return (unsigned long)simple_strtoull(cp, endp, base);
}

long long simple_strtoll(const char *cp, char **endp, unsigned int base)
{
	if (*cp == '-')
		return -(long long)simple_strtoull(cp + 1, endp, base);
	return (long long)simple_strtoull(cp, endp, base);
}

long simple_strtol(const char *cp, char **endp, unsigned int base)
{
	if (*cp == '-')
		return -(long)simple_strtoul(cp + 1, endp, base);
	return (long)simple_strtoul(cp, endp, base);
}
