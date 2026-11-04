/* SPDX-License-Identifier: MIT */
/* USB is not supported by this port; mt76.h needs the types to compile. */
#ifndef _LKPI_LINUX_USB_H_
#define _LKPI_LINUX_USB_H_
#include <linux/types.h>
#include <linux/device.h>
#include <linux/errno.h>
struct urb {
	int status;
	u32 actual_length;
	u32 transfer_buffer_length;
	void *transfer_buffer;
	void *context;
};
struct usb_device {
	struct device dev;
};
struct usb_interface {
	struct device dev;
	struct usb_device *udev;
};
#define to_usb_interface(d) container_of(d, struct usb_interface, dev)
static inline struct usb_device *interface_to_usbdev(struct usb_interface *intf) { return intf->udev; }
static inline unsigned int usb_sndbulkpipe(struct usb_device *dev, unsigned int ep) { (void)dev; (void)ep; return 0; }
static inline unsigned int usb_rcvbulkpipe(struct usb_device *dev, unsigned int ep) { (void)dev; (void)ep; return 0; }
static inline int usb_bulk_msg(struct usb_device *dev, unsigned int pipe, void *data, int len,
    int *actual, int timeout)
{
	(void)dev; (void)pipe; (void)data; (void)len; (void)timeout;
	if (actual) *actual = 0;
	return -ENODEV;
}
#endif
