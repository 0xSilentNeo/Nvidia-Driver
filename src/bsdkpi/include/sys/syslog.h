/* SPDX-License-Identifier: MIT */
#ifndef _BSDKPI_SYS_SYSLOG_H_
#define _BSDKPI_SYS_SYSLOG_H_
#define LOG_EMERG	0
#define LOG_ALERT	1
#define LOG_CRIT	2
#define LOG_ERR		3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	6
#define LOG_DEBUG	7
#define LOG_PRIMASK	0x07
#define LOG_PRI(p)	((p) & LOG_PRIMASK)
#define LOG_MAKEPRI(fac, pri)	((fac) | (pri))
#define LOG_KERN	(0 << 3)
#define LOG_USER	(1 << 3)
#define LOG_DAEMON	(3 << 3)
#define LOG_AUTH	(4 << 3)
#define LOG_SECURITY	(13 << 3)
#define LOG_FACMASK	0x03f8
#define LOG_FAC(p)	(((p) & LOG_FACMASK) >> 3)
#endif
