// SPDX-License-Identifier: MIT
/*
 * printf(9) for the FreeBSD side: a kvprintf() that knows the kernel's
 * extra conversions (%D hex dump, %b bit field, %r radix) and the plain
 * ones, feeding printk() line by line.  Written for this port; the format
 * grammar follows printf(3) plus FreeBSD's kernel additions.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/mutex.h>
#include <linux/printk.h>

int bootverbose;
int cold;
char kernelname[64] = "MT7925.kext";
const char *panicstr;

/* ---- the formatter ------------------------------------------------- */

#define PCHAR(c) do { int __c = (c); func(__c, arg); retval++; } while (0)

static char *
ksprintn(char *nbuf, size_t nbufsz, uintmax_t num, int base, int *lenp,
    int upper)
{
	char *p = nbuf + nbufsz - 1;
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	*p = '\0';
	do {
		*--p = digits[num % (unsigned)base];
		num /= (unsigned)base;
	} while (num != 0);
	if (lenp != NULL)
		*lenp = (int)(nbuf + nbufsz - 1 - p);
	return (p);
}

/*
 * Scaled down version of FreeBSD's kvprintf semantics: returns the number
 * of characters emitted.  @radix is the base used by %r.
 */
int
kvprintf(char const *fmt, void (*func)(int, void *), void *arg, int radix,
    va_list ap)
{
	char nbuf[sizeof(uintmax_t) * 8 + 2];
	const char *p, *percent, *q;
	u_char *up;
	int ch, n;
	uintmax_t num;
	int base, lflag, qflag, tmp, width, ladjust, sharpflag, neg, sign, dot;
	int cflag, hflag, jflag, tflag, zflag;
	int dwidth, upper;
	char padc;
	int stop = 0, retval = 0;

	num = 0;
	if (fmt == NULL)
		fmt = "(fmt null)\n";
	if (radix < 2 || radix > 36)
		radix = 10;

	for (;;) {
		padc = ' ';
		width = 0;
		while ((ch = (u_char)*fmt++) != '%' || stop) {
			if (ch == '\0')
				return (retval);
			PCHAR(ch);
		}
		percent = fmt - 1;
		qflag = 0; lflag = 0; ladjust = 0; sharpflag = 0; neg = 0;
		sign = 0; dot = 0; dwidth = 0; upper = 0;
		cflag = 0; hflag = 0; jflag = 0; tflag = 0; zflag = 0;
reswitch:	switch (ch = (u_char)*fmt++) {
		case '.':
			dot = 1;
			goto reswitch;
		case '#':
			sharpflag = 1;
			goto reswitch;
		case '+':
			sign = 1;
			goto reswitch;
		case '-':
			ladjust = 1;
			goto reswitch;
		case '%':
			PCHAR(ch);
			break;
		case '*':
			if (!dot) {
				width = va_arg(ap, int);
				if (width < 0) {
					ladjust = !ladjust;
					width = -width;
				}
			} else {
				dwidth = va_arg(ap, int);
			}
			goto reswitch;
		case '0':
			if (!dot) {
				padc = '0';
				goto reswitch;
			}
			/* FALLTHROUGH */
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			for (n = 0;; ++fmt) {
				n = n * 10 + ch - '0';
				ch = *fmt;
				if (ch < '0' || ch > '9')
					break;
			}
			if (dot)
				dwidth = n;
			else
				width = n;
			goto reswitch;
		case 'b':
			/* %b: value, then a bit-description string */
			num = (u_int)va_arg(ap, int);
			p = va_arg(ap, char *);
			for (q = ksprintn(nbuf, sizeof(nbuf), num, *p++, NULL, 0);
			    *q;)
				PCHAR(*q--);
			if (num == 0)
				break;
			for (tmp = 0; *p;) {
				n = *p++;
				if (num & (1 << (n - 1))) {
					PCHAR(tmp ? ',' : '<');
					for (; (n = *p) > ' '; ++p)
						PCHAR(n);
					tmp = 1;
				} else
					for (; *p > ' '; ++p)
						continue;
			}
			if (tmp)
				PCHAR('>');
			break;
		case 'c':
			width -= 1;
			if (!ladjust && width > 0)
				while (width--)
					PCHAR(padc);
			PCHAR(va_arg(ap, int));
			if (ladjust && width > 0)
				while (width--)
					PCHAR(padc);
			break;
		case 'D':
			/* %D: hex dump of @width bytes with the given separator */
			up = va_arg(ap, u_char *);
			p = va_arg(ap, char *);
			if (!width)
				width = 16;
			while (width--) {
				PCHAR("0123456789abcdef"[*up >> 4]);
				PCHAR("0123456789abcdef"[*up & 0x0f]);
				up++;
				if (width)
					for (q = p; *q; q++)
						PCHAR(*q);
			}
			break;
		case 'd':
		case 'i':
			base = 10;
			sign = 1;
			goto handle_sign;
		case 'h':
			if (hflag) {
				hflag = 0;
				cflag = 1;
			} else
				hflag = 1;
			goto reswitch;
		case 'j':
			jflag = 1;
			goto reswitch;
		case 'l':
			if (lflag) {
				lflag = 0;
				qflag = 1;
			} else
				lflag = 1;
			goto reswitch;
		case 'n':
			/* not supported in the kernel; consume the argument */
			(void)va_arg(ap, void *);
			break;
		case 'o':
			base = 8;
			goto handle_nosign;
		case 'p':
			base = 16;
			sharpflag = (width == 0);
			sign = 0;
			num = (uintptr_t)va_arg(ap, void *);
			goto number;
		case 'q':
			qflag = 1;
			goto reswitch;
		case 'r':
			base = radix;
			if (sign)
				goto handle_sign;
			goto handle_nosign;
		case 's':
			p = va_arg(ap, char *);
			if (p == NULL)
				p = "(null)";
			if (!dot)
				n = (int)strlen(p);
			else
				for (n = 0; n < dwidth && p[n]; n++)
					continue;
			width -= n;
			if (!ladjust && width > 0)
				while (width--)
					PCHAR(padc);
			while (n--)
				PCHAR(*p++);
			if (ladjust && width > 0)
				while (width--)
					PCHAR(padc);
			break;
		case 't':
			tflag = 1;
			goto reswitch;
		case 'u':
			base = 10;
			goto handle_nosign;
		case 'X':
			upper = 1;
			/* FALLTHROUGH */
		case 'x':
			base = 16;
			goto handle_nosign;
		case 'y':
			base = 16;
			sign = 1;
			goto handle_sign;
		case 'z':
			zflag = 1;
			goto reswitch;
handle_nosign:
			sign = 0;
			if (jflag)
				num = va_arg(ap, uintmax_t);
			else if (qflag)
				num = va_arg(ap, u_quad_t);
			else if (tflag)
				num = va_arg(ap, ptrdiff_t);
			else if (lflag)
				num = va_arg(ap, u_long);
			else if (zflag)
				num = va_arg(ap, size_t);
			else if (hflag)
				num = (u_short)va_arg(ap, int);
			else if (cflag)
				num = (u_char)va_arg(ap, int);
			else
				num = va_arg(ap, u_int);
			goto number;
handle_sign:
			if (jflag)
				num = va_arg(ap, intmax_t);
			else if (qflag)
				num = va_arg(ap, quad_t);
			else if (tflag)
				num = va_arg(ap, ptrdiff_t);
			else if (lflag)
				num = va_arg(ap, long);
			else if (zflag)
				num = va_arg(ap, ssize_t);
			else if (hflag)
				num = (short)va_arg(ap, int);
			else if (cflag)
				num = (char)va_arg(ap, int);
			else
				num = va_arg(ap, int);
number:
			if (sign && (intmax_t)num < 0) {
				neg = 1;
				num = -(intmax_t)num;
			}
			p = ksprintn(nbuf, sizeof(nbuf), num, base, &n, upper);
			tmp = 0;
			if (sharpflag && num != 0) {
				if (base == 8)
					tmp++;
				else if (base == 16)
					tmp += 2;
			}
			if (neg)
				tmp++;

			if (!ladjust && padc == '0')
				dwidth = width - tmp;
			width -= tmp + imax(dwidth, n);
			dwidth -= n;
			if (!ladjust)
				while (width-- > 0)
					PCHAR(' ');
			if (neg)
				PCHAR('-');
			if (sharpflag && num != 0) {
				if (base == 8) {
					PCHAR('0');
				} else if (base == 16) {
					PCHAR('0');
					PCHAR(upper ? 'X' : 'x');
				}
			}
			while (dwidth-- > 0)
				PCHAR('0');
			while (*p)
				PCHAR(*p++);
			if (ladjust)
				while (width-- > 0)
					PCHAR(' ');
			break;
		default:
			while (percent < fmt)
				PCHAR(*percent++);
			/*
			 * Since we ignore a formatting argument it is no
			 * longer safe to obey the remaining formatting
			 * arguments as the arguments will no longer match
			 * the format specs.
			 */
			stop = 1;
			break;
		}
	}
}

/* ---- sinks ---------------------------------------------------------- */

struct snprintf_arg {
	char	*str;
	size_t	 remain;
};

static void
snprintf_func(int ch, void *arg)
{
	struct snprintf_arg *const info = arg;

	if (info->remain >= 2) {
		*info->str++ = (char)ch;
		info->remain--;
	}
}

int
bsdkpi_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct snprintf_arg info;
	int retval;

	info.str = buf;
	info.remain = size;
	retval = kvprintf(fmt, snprintf_func, &info, 10, ap);
	if (info.remain >= 1)
		*info.str++ = '\0';
	return (retval);
}

int
bsdkpi_snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int retval;

	va_start(ap, fmt);
	retval = bsdkpi_vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return (retval);
}

int
bsdkpi_vsprintf(char *buf, const char *fmt, va_list ap)
{
	return (bsdkpi_vsnprintf(buf, (size_t)-1 / 2, fmt, ap));
}

int
bsdkpi_sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	int retval;

	va_start(ap, fmt);
	retval = bsdkpi_vsprintf(buf, fmt, ap);
	va_end(ap);
	return (retval);
}

/*
 * Console output: format into a line buffer and hand complete lines to
 * printk().  Partial lines are flushed when the buffer fills or on the next
 * newline; the KERN_* prefix is derived from the log(9) level.
 */
struct line_arg {
	char	 buf[512];
	size_t	 len;
	const char *level;
};

static void
line_flush(struct line_arg *la)
{
	if (la->len == 0)
		return;
	la->buf[la->len] = '\0';
	printk("%s%s", la->level, la->buf);
	la->len = 0;
}

static void
line_func(int ch, void *arg)
{
	struct line_arg *la = arg;

	la->buf[la->len++] = (char)ch;
	if (ch == '\n' || la->len >= sizeof(la->buf) - 1)
		line_flush(la);
}

static int
bsdkpi_vlog_level(const char *level, const char *fmt, va_list ap)
{
	struct line_arg la;
	int retval;

	la.len = 0;
	la.level = level;
	retval = kvprintf(fmt, line_func, &la, 10, ap);
	line_flush(&la);
	return (retval);
}

int
bsdkpi_vprintf(const char *fmt, va_list ap)
{
	return (bsdkpi_vlog_level(KERN_DEFAULT, fmt, ap));
}

int
bsdkpi_printf(const char *fmt, ...)
{
	va_list ap;
	int retval;

	va_start(ap, fmt);
	retval = bsdkpi_vprintf(fmt, ap);
	va_end(ap);
	return (retval);
}

void
bsdkpi_vlog(int level, const char *fmt, va_list ap)
{
	const char *kern;

	switch (LOG_PRI(level)) {
	case LOG_EMERG:		kern = KERN_EMERG; break;
	case LOG_ALERT:		kern = KERN_ALERT; break;
	case LOG_CRIT:		kern = KERN_CRIT; break;
	case LOG_ERR:		kern = KERN_ERR; break;
	case LOG_WARNING:	kern = KERN_WARNING; break;
	case LOG_NOTICE:	kern = KERN_NOTICE; break;
	case LOG_INFO:		kern = KERN_INFO; break;
	default:		kern = KERN_DEBUG; break;
	}
	(void)bsdkpi_vlog_level(kern, fmt, ap);
}

void
bsdkpi_log(int level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	bsdkpi_vlog(level, fmt, ap);
	va_end(ap);
}

/* hexdump(9) */
void
bsdkpi_hexdump(const void *ptr, int length, const char *hdr, int flags)
{
	int i, j, k;
	int cols;
	const unsigned char *cp;
	char delim;

	if ((flags & HD_DELIM_MASK) != 0)
		delim = (flags & HD_DELIM_MASK) >> 8;
	else
		delim = ' ';

	if ((flags & HD_COLUMN_MASK) != 0)
		cols = flags & HD_COLUMN_MASK;
	else
		cols = 16;

	cp = ptr;
	for (i = 0; i < length; i += cols) {
		if (hdr != NULL)
			bsdkpi_printf("%s", hdr);

		if ((flags & HD_OMIT_COUNT) == 0)
			bsdkpi_printf("%04x  ", i);

		if ((flags & HD_OMIT_HEX) == 0) {
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k < length)
					bsdkpi_printf("%c%02x", delim, cp[k]);
				else
					bsdkpi_printf("   ");
			}
		}

		if ((flags & HD_OMIT_CHARS) == 0) {
			bsdkpi_printf("  |");
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k >= length)
					bsdkpi_printf(" ");
				else if (cp[k] >= ' ' && cp[k] <= '~')
					bsdkpi_printf("%c", cp[k]);
				else
					bsdkpi_printf(".");
			}
			bsdkpi_printf("|");
		}
		bsdkpi_printf("\n");
	}
}
