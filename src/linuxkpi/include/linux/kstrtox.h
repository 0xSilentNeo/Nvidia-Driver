/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_KSTRTOX_H_
#define _LKPI_LINUX_KSTRTOX_H_
#include <linux/types.h>
int kstrtoull(const char *s, unsigned int base, unsigned long long *res);
int kstrtoll(const char *s, unsigned int base, long long *res);
int kstrtoul(const char *s, unsigned int base, unsigned long *res);
int kstrtol(const char *s, unsigned int base, long *res);
int kstrtouint(const char *s, unsigned int base, unsigned int *res);
int kstrtoint(const char *s, unsigned int base, int *res);
int kstrtou16(const char *s, unsigned int base, u16 *res);
int kstrtos16(const char *s, unsigned int base, s16 *res);
int kstrtou8(const char *s, unsigned int base, u8 *res);
int kstrtos8(const char *s, unsigned int base, s8 *res);
int kstrtobool(const char *s, bool *res);
#define kstrtou32 kstrtouint
#define kstrtos32 kstrtoint
#define kstrtou64 kstrtoull
#define kstrtos64 kstrtoll
unsigned long simple_strtoul(const char *cp, char **endp, unsigned int base);
long simple_strtol(const char *cp, char **endp, unsigned int base);
unsigned long long simple_strtoull(const char *cp, char **endp, unsigned int base);
long long simple_strtoll(const char *cp, char **endp, unsigned int base);
#endif
