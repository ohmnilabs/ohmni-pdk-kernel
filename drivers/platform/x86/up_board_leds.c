/*
 * UP Board CPLD LEDs driver.
 *
 * Copyright (c) 2016, Emutex Ltd.  All rights reserved.
 *
 * Author: Javier Arteaga <javier@emutex.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

#include "up_board_leds.h"

/* Internal context information for this driver */
struct up_board_led {
	struct up_board_leds_pdata *pdata;
	unsigned int offset;
	const char *name;
	struct led_classdev cdev;
};

static void up_led_brightness_set(struct led_classdev *cdev,
				  enum led_brightness value)
{
	struct up_board_led *led = container_of(cdev,
						struct up_board_led,
						cdev);
	struct up_board_cpld_info *cpld_info = &led->pdata->cpld_info;

	cpld_info->reg_set_bit(cpld_info->cpld, led->offset, value != LED_OFF);
}

static int up_board_leds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct up_board_leds_pdata *pdata = dev_get_platdata(dev);
	struct up_board_led *up_led;
	int ret = 0;
	size_t i;

	for (i = 0; i < pdata->nled; i++) {
		struct up_board_led_info *led_info = &pdata->leds[i];

		up_led = devm_kzalloc(dev, sizeof(*up_led), GFP_KERNEL);
		if (!up_led)
			return -ENOMEM;

		up_led->pdata = pdata;
		up_led->offset = led_info->cpld_offset;
		up_led->cdev.brightness_set = up_led_brightness_set;
		up_led->cdev.name = led_info->name;

		ret = devm_led_classdev_register(dev, &up_led->cdev);
		if (ret)
			return ret;
	}

	return 0;
}

static struct platform_driver up_board_leds_driver = {
	.driver.name	= "up-board-leds",
	.driver.owner	= THIS_MODULE,
	.probe		= up_board_leds_probe,
};

module_platform_driver(up_board_leds_driver);

MODULE_AUTHOR("Javier Arteaga <javier@emutex.com>");
MODULE_DESCRIPTION("UP Board LEDs driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:up-board-leds");
