/* SPDX-License-Identifier: MIT */
/* LED class: the MT7925 M.2 modules have no host-visible LED; registration succeeds and does nothing. */
#ifndef _LKPI_LINUX_LEDS_H_
#define _LKPI_LINUX_LEDS_H_
#include <linux/types.h>
#include <linux/list.h>
struct device;
enum led_brightness { LED_OFF = 0, LED_ON = 1, LED_HALF = 127, LED_FULL = 255 };
struct led_classdev {
	const char *name;
	unsigned int brightness;
	unsigned int max_brightness;
	int flags;
	const char *default_trigger;
	void (*brightness_set)(struct led_classdev *led_cdev, enum led_brightness brightness);
	int (*brightness_set_blocking)(struct led_classdev *led_cdev, enum led_brightness brightness);
	enum led_brightness (*brightness_get)(struct led_classdev *led_cdev);
	int (*blink_set)(struct led_classdev *led_cdev, unsigned long *delay_on, unsigned long *delay_off);
	struct device *dev;
	struct list_head node;
};
static inline int led_classdev_register(struct device *parent, struct led_classdev *led_cdev) { (void)parent; led_cdev->dev = parent; return 0; }
static inline void led_classdev_unregister(struct led_classdev *led_cdev) { (void)led_cdev; }
static inline int devm_led_classdev_register(struct device *parent, struct led_classdev *led_cdev) { return led_classdev_register(parent, led_cdev); }
static inline void led_set_brightness(struct led_classdev *led_cdev, enum led_brightness b) { led_cdev->brightness = b; }
#define led_trigger_register_simple(n, t) do { } while (0)
#define led_trigger_unregister_simple(t) do { } while (0)
#define led_trigger_event(t, b) do { } while (0)
struct led_trigger { const char *name; };
#endif
