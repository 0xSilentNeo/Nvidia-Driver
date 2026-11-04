/* SPDX-License-Identifier: MIT */
/* Minimal test harness: no dependencies, prints TAP-ish output. */
#ifndef _LKPI_TEST_H_
#define _LKPI_TEST_H_
#include <stdio.h>
#include <stdlib.h>

static int test_failures, test_count;

#define CHECK(cond) do { \
	test_count++; \
	if (!(cond)) { \
		test_failures++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	long long __a = (long long)(a), __b = (long long)(b); \
	test_count++; \
	if (__a != __b) { \
		test_failures++; \
		fprintf(stderr, "FAIL %s:%d: %s == %lld, expected %s == %lld\n", \
		    __FILE__, __LINE__, #a, __a, #b, __b); \
	} \
} while (0)

#define RUN(fn) do { fprintf(stderr, "-- %s\n", #fn); fn(); } while (0)

static inline int test_report(const char *name)
{
	fprintf(stderr, "%s: %d checks, %d failures\n", name, test_count, test_failures);
	return test_failures ? 1 : 0;
}
#endif
