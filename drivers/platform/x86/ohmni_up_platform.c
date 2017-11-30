/*
 * Platform integration for the UP-board based Ohmni robot
 *
 * Copyright (C) 2017, OhmniLabs, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio_keys.h>
#include <linux/io.h>
#include <linux/delay.h>

static struct gpio_keys_button ohmni_up_platform_button[] = {
	{ 
		.desc = "power-switch", 
		.gpio = 23,
		.active_low = 1,
		.code = KEY_POWER, 
		.type = EV_KEY,
		.debounce_interval = 30,
	}, 
};

// Note - MUST use the name "Power Button" as android-x86's
// system/core/libsuspend stuff looks specifically for input
// devices with that name to convert to long press.
static struct gpio_keys_platform_data ohmni_up_platform_button_data = {
	.buttons = ohmni_up_platform_button,
	.nbuttons = ARRAY_SIZE(ohmni_up_platform_button),
	.name = "Power Button",
};

static struct platform_device gpio_keys_device = {
	.name = "gpio-keys",
	.id = 0,
	.dev = {
		.platform_data = &ohmni_up_platform_button_data,
	}
};

static struct gpio_desc* power_gpio = NULL;

static void ohmni_up_platform_do_poweroff(void)
{
	/* Get from legacy GPIO */
	if (power_gpio == NULL) {
		// Print error
		return;
	}

        /* pull it low */
        gpiod_direction_output(power_gpio, 0);

        /* give it some time */
        mdelay(3000);
 
        WARN_ON(1);
}

static int __init ohmni_up_platform_init(void)
{
	int error;

	// Register the keys
	platform_device_register(&gpio_keys_device); 

	// Hold the gpio we use here
	error = gpio_request_one(24, GPIOF_IN, "power-gpio");
	if (error < 0) {
		pr_info("%s: unable to request gpio\n", __func__);
		return -EINVAL;
	}

	// Get the descriptor
	power_gpio = gpio_to_desc(24);
	if (power_gpio == NULL) {
		pr_info("%s: unable to get GPIO desc\n", __func__);
		return -EINVAL;
	}

	// Register the pm_power_off handler here
	pm_power_off = &ohmni_up_platform_do_poweroff;
	pr_info("%s: registered pm_power_off handler\n", __func__);

	return 0;
}

static void __exit ohmni_up_platform_exit(void)
{
	platform_device_unregister(&gpio_keys_device);

	if (pm_power_off == &ohmni_up_platform_do_poweroff)
		pm_power_off = NULL;
}

module_init(ohmni_up_platform_init);
module_exit(ohmni_up_platform_exit);

MODULE_DESCRIPTION("Platform support for Ohmni robot on UP board");
MODULE_AUTHOR("OhmniLabs");
MODULE_LICENSE("GPL");
