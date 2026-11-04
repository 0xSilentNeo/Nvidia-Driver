/* SPDX-License-Identifier: MIT */
#ifndef _LKPI_LINUX_FIRMWARE_H_
#define _LKPI_LINUX_FIRMWARE_H_
#include <linux/types.h>
#include <linux/gfp.h>
struct device;
struct module;
struct firmware {
	size_t size;
	const u8 *data;
	void *priv;
};
int request_firmware(const struct firmware **fw, const char *name, struct device *dev);
int firmware_request_nowarn(const struct firmware **fw, const char *name, struct device *dev);
#define request_firmware_direct(fw, name, dev) request_firmware(fw, name, dev)
#define firmware_request_platform(fw, name, dev) request_firmware(fw, name, dev)
int request_firmware_nowait(struct module *module, bool uevent, const char *name, struct device *dev,
    gfp_t gfp, void *context, void (*cont)(const struct firmware *fw, void *context));
void release_firmware(const struct firmware *fw);
int request_firmware_into_buf(const struct firmware **fw, const char *name, struct device *dev,
    void *buf, size_t size);
#define FW_ACTION_UEVENT true
#define FW_ACTION_NOUEVENT false
#endif
