/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_PRINTK_H_
#define _LKPI_LINUX_PRINTK_H_
#include <stdarg.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <lkpi/os.h>

#define KERN_SOH	"\001"
#define KERN_EMERG	KERN_SOH "0"
#define KERN_ALERT	KERN_SOH "1"
#define KERN_CRIT	KERN_SOH "2"
#define KERN_ERR	KERN_SOH "3"
#define KERN_WARNING	KERN_SOH "4"
#define KERN_NOTICE	KERN_SOH "5"
#define KERN_INFO	KERN_SOH "6"
#define KERN_DEBUG	KERN_SOH "7"
#define KERN_DEFAULT	""
#define KERN_CONT	KERN_SOH "c"

/* printk() parses the KERN_* prefix and routes to lkpi_os_log(). */
int printk(const char *fmt, ...) __printf(1, 2);
int vprintk(const char *fmt, va_list ap);
int _printk(const char *fmt, ...) __printf(1, 2);
#define no_printk(fmt, ...) ({ if (0) printk(fmt, ##__VA_ARGS__); 0; })

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif
#define pr_emerg(fmt, ...)	printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert(fmt, ...)	printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...)	printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...)	printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...)	printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warning		pr_warn
#define pr_notice(fmt, ...)	printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...)	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_cont(fmt, ...)	printk(KERN_CONT fmt, ##__VA_ARGS__)
#ifdef DEBUG
#define pr_debug(fmt, ...)	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...)	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif
#define pr_devel(fmt, ...)	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

#define printk_once(fmt, ...) ({ \
	static bool __print_once; \
	if (!__print_once) { __print_once = true; printk(fmt, ##__VA_ARGS__); } })
#define pr_err_once(fmt, ...)	printk_once(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_once(fmt, ...)	printk_once(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info_once(fmt, ...)	printk_once(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice_once(fmt, ...) printk_once(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug_once(fmt, ...)	no_printk(fmt, ##__VA_ARGS__)
#define printk_ratelimited(fmt, ...) printk(fmt, ##__VA_ARGS__)
#define pr_err_ratelimited(fmt, ...) printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_ratelimited(fmt, ...) printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info_ratelimited(fmt, ...) printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug_ratelimited(fmt, ...) no_printk(fmt, ##__VA_ARGS__)

enum { DUMP_PREFIX_NONE, DUMP_PREFIX_ADDRESS, DUMP_PREFIX_OFFSET };
void print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
    int rowsize, int groupsize, const void *buf, size_t len, bool ascii);
#define print_hex_dump_bytes(prefix, type, buf, len) \
	print_hex_dump(KERN_DEBUG, prefix, type, 16, 1, buf, len, true)
#define print_hex_dump_debug(prefix, type, rowsize, groupsize, buf, len, ascii) \
	do { } while (0)
int hex_dump_to_buffer(const void *buf, size_t len, int rowsize, int groupsize,
    char *linebuf, size_t linebuflen, bool ascii);

#define FW_BUG "[Firmware Bug]: "
#define FW_WARN "[Firmware Warn]: "
#define FW_INFO "[Firmware Info]: "
#define console_loglevel 7
#define CONSOLE_LOGLEVEL_DEFAULT 7
#endif
