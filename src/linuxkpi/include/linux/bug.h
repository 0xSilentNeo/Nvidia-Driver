/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_BUG_H_
#define _LKPI_LINUX_BUG_H_
#include <linux/compiler.h>
#include <linux/build_bug.h>
#include <linux/printk.h>

void lkpi_warn(const char *file, int line, const char *fmt, ...) __printf(3, 4);
void lkpi_bug(const char *file, int line) __noreturn;

#define BUG() lkpi_bug(__FILE__, __LINE__)
#define BUG_ON(cond) do { if (unlikely(cond)) BUG(); } while (0)

#define __WARN_printf(fmt, ...) lkpi_warn(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define WARN(cond, fmt, ...) ({ \
	int __ret_warn_on = !!(cond); \
	if (unlikely(__ret_warn_on)) __WARN_printf(fmt, ##__VA_ARGS__); \
	unlikely(__ret_warn_on); })
#define WARN_ON(cond) ({ \
	int __ret_warn_on = !!(cond); \
	if (unlikely(__ret_warn_on)) __WARN_printf("WARN_ON(%s)", #cond); \
	unlikely(__ret_warn_on); })
#define WARN_ONCE(cond, fmt, ...) ({ \
	static bool __warned; \
	int __ret_warn_once = !!(cond); \
	if (unlikely(__ret_warn_once && !__warned)) { __warned = true; __WARN_printf(fmt, ##__VA_ARGS__); } \
	unlikely(__ret_warn_once); })
#define WARN_ON_ONCE(cond) ({ \
	static bool __warned; \
	int __ret_warn_once = !!(cond); \
	if (unlikely(__ret_warn_once && !__warned)) { __warned = true; __WARN_printf("WARN_ON_ONCE(%s)", #cond); } \
	unlikely(__ret_warn_once); })
#define WARN_TAINT(cond, taint, fmt, ...) WARN(cond, fmt, ##__VA_ARGS__)
#define WARN_TAINT_ONCE(cond, taint, fmt, ...) WARN_ONCE(cond, fmt, ##__VA_ARGS__)
#define WARN_ON_SMP(cond) WARN_ON(cond)
#define MAYBE_BUILD_BUG_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define dump_stack() lkpi_os_dump_stack()
#endif
