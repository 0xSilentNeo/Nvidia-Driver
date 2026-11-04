/* SPDX-License-Identifier: MIT */
/*
 * ACPI.  Hackintosh systems do have ACPI tables, but the SAR/ASAR objects
 * mt792x_acpi_sar.c looks for live on the Wi-Fi device node in the OEM
 * DSDT, and IOKit only exposes them via IOACPIPlatformDevice.  The backend
 * can later wire lkpi_acpi_evaluate() to that; for now every lookup fails
 * and the driver falls back to its built-in power tables (the same path it
 * takes on machines without these tables on Linux).
 */
#ifndef _LKPI_LINUX_ACPI_H_
#define _LKPI_LINUX_ACPI_H_
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/slab.h>

typedef u32 acpi_status;
typedef void *acpi_handle;
typedef char *acpi_string;
typedef u64 acpi_size;
#define AE_OK			((acpi_status)0x0000)
#define AE_ERROR		((acpi_status)0x0001)
#define AE_NOT_FOUND		((acpi_status)0x0005)
#define AE_NO_MEMORY		((acpi_status)0x0004)
#define AE_BAD_PARAMETER	((acpi_status)0x1001)
#define ACPI_SUCCESS(a)		(!(a))
#define ACPI_FAILURE(a)		(a)
#define ACPI_ALLOCATE_BUFFER	(acpi_size)(-1)
#define ACPI_HANDLE(dev)	((acpi_handle)NULL)
#define ACPI_COMPANION(dev)	((struct acpi_device *)NULL)
#define ACPI_TYPE_ANY		0x00
#define ACPI_TYPE_INTEGER	0x01
#define ACPI_TYPE_STRING	0x02
#define ACPI_TYPE_BUFFER	0x03
#define ACPI_TYPE_PACKAGE	0x04
typedef u32 acpi_object_type;

union acpi_object {
	acpi_object_type type;
	struct {
		acpi_object_type type;
		u64 value;
	} integer;
	struct {
		acpi_object_type type;
		u32 length;
		char *pointer;
	} string;
	struct {
		acpi_object_type type;
		u32 length;
		u8 *pointer;
	} buffer;
	struct {
		acpi_object_type type;
		u32 count;
		union acpi_object *elements;
	} package;
};
struct acpi_buffer {
	acpi_size length;
	void *pointer;
};
struct acpi_object_list {
	u32 count;
	union acpi_object *pointer;
};
struct acpi_device { int dummy; };

static inline acpi_status acpi_get_handle(acpi_handle parent, const char *pathname, acpi_handle *ret)
{
	(void)parent; (void)pathname;
	*ret = NULL;
	return AE_NOT_FOUND;
}
static inline acpi_status acpi_evaluate_object(acpi_handle h, acpi_string path,
    struct acpi_object_list *params, struct acpi_buffer *ret)
{
	(void)h; (void)path; (void)params;
	if (ret) {
		ret->length = 0;
		ret->pointer = NULL;
	}
	return AE_NOT_FOUND;
}
static inline union acpi_object *acpi_evaluate_dsm(acpi_handle h, const void *guid, u64 rev, u64 func,
    union acpi_object *argv4)
{
	(void)h; (void)guid; (void)rev; (void)func; (void)argv4;
	return NULL;
}
#define acpi_dev_present(hid, uid, hrv) (false)
#define acpi_disabled (1)
#define ACPI_FREE(p) kfree(p)
#define acpi_dev_get_first_match_dev(h, u, r) ((struct acpi_device *)NULL)
#define acpi_dev_put(d) do { } while (0)
#endif
