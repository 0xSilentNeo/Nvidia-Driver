// SPDX-License-Identifier: MIT
/*
 * request_firmware() and friends.
 *
 * Blobs live in the kext bundle; lkpi_os_firmware_load() returns a buffer
 * owned by us that release_firmware() hands back with lkpi_os_free().  The
 * buffer pointer is kept in fw->priv so request_firmware_into_buf(), whose
 * data points into the caller's memory, can leave priv NULL.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <lkpi/os.h>

static int lkpi_firmware_load(const struct firmware **fw_p, const char *name,
    struct device *dev, bool warn)
{
	struct firmware *fw;
	void *data = NULL;
	size_t size = 0;
	int ret;

	if (!fw_p)
		return -EINVAL;
	*fw_p = NULL;
	if (!name || !*name)
		return -EINVAL;

	fw = kzalloc(sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return -ENOMEM;

	ret = lkpi_os_firmware_load(name, &data, &size);
	if (ret) {
		if (warn) {
			if (dev)
				dev_err(dev, "firmware: failed to load %s (%d)\n", name, ret);
			else
				pr_err("firmware: failed to load %s (%d)\n", name, ret);
		}
		kfree(fw);
		return ret < 0 ? ret : -ENOENT;
	}
	fw->size = size;
	fw->data = data;
	fw->priv = data;
	if (dev)
		dev_dbg(dev, "firmware: loaded %s (%zu bytes)\n", name, size);
	*fw_p = fw;
	return 0;
}

int request_firmware(const struct firmware **fw, const char *name, struct device *dev)
{
	return lkpi_firmware_load(fw, name, dev, true);
}

int firmware_request_nowarn(const struct firmware **fw, const char *name, struct device *dev)
{
	return lkpi_firmware_load(fw, name, dev, false);
}

void release_firmware(const struct firmware *fw)
{
	if (!fw)
		return;
	if (fw->priv)
		lkpi_os_free(fw->priv);
	kfree(fw);
}

int request_firmware_nowait(struct module *module, bool uevent, const char *name,
    struct device *dev, gfp_t gfp, void *context,
    void (*cont)(const struct firmware *fw, void *context))
{
	const struct firmware *fw = NULL;

	(void)module; (void)uevent; (void)gfp;
	if (!cont)
		return -EINVAL;
	/* Loading is synchronous here; the callback gets NULL on failure. */
	lkpi_firmware_load(&fw, name, dev, true);
	cont(fw, context);
	return 0;
}

int request_firmware_into_buf(const struct firmware **fw_p, const char *name,
    struct device *dev, void *buf, size_t size)
{
	const struct firmware *loaded;
	struct firmware *fw;
	int ret;

	if (!buf)
		return -EINVAL;
	ret = lkpi_firmware_load(&loaded, name, dev, true);
	if (ret)
		return ret;
	if (loaded->size > size) {
		if (dev)
			dev_err(dev, "firmware: %s (%zu bytes) does not fit in %zu-byte buffer\n",
				name, loaded->size, size);
		release_firmware(loaded);
		return -EFBIG;
	}
	memcpy(buf, loaded->data, loaded->size);
	fw = (struct firmware *)loaded;
	lkpi_os_free(fw->priv);
	fw->priv = NULL;
	fw->data = buf;
	*fw_p = fw;
	return 0;
}
