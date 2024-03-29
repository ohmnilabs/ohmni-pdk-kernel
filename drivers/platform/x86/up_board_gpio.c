/*
 * UP Board I/O Header CPLD GPIO driver.
 *
 * Copyright (c) 2016, Emutex Ltd.  All rights reserved.
 *
 * Author: Dan O'Donovan <dan@emutex.com>
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
#include <linux/gpio.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include "up_board_gpio.h"

/* Internal context information for this driver */
struct up_board_gpio {
	struct up_board_gpio_pdata *pdata;
	struct gpio_chip chip;
};

static irqreturn_t up_gpio_irq_handler(int irq, void *data)
{
	struct up_board_gpio_info *gpio = data;

	generic_handle_irq(gpio->irq);
	return IRQ_HANDLED;
}

static unsigned int up_gpio_irq_startup(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct up_board_gpio *up_gpio = gpiochip_get_data(gc);
	unsigned int offset = irqd_to_hwirq(data);
	struct up_board_gpio_info *gpio = &up_gpio->pdata->gpios[offset];

	return request_irq(gpio->soc_gpio_irq, up_gpio_irq_handler,
			   IRQF_ONESHOT, gc->label, gpio);
}

static void up_gpio_irq_shutdown(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct up_board_gpio *up_gpio = gpiochip_get_data(gc);
	unsigned int offset = irqd_to_hwirq(data);
	struct up_board_gpio_info *gpio = &up_gpio->pdata->gpios[offset];

	free_irq(gpio->soc_gpio_irq, gpio);
}

static struct irq_chip up_gpio_irqchip = {
	.irq_startup = up_gpio_irq_startup,
	.irq_shutdown = up_gpio_irq_shutdown,
	.irq_enable = irq_chip_enable_parent,
	.irq_disable = irq_chip_disable_parent,
	.irq_mask = irq_chip_mask_parent,
	.irq_unmask = irq_chip_unmask_parent,
	.irq_ack = irq_chip_ack_parent,
	.irq_set_type = irq_chip_set_type_parent,
};

static int up_gpio_dir_in(struct gpio_chip *gc, unsigned int offset)
{
	struct up_board_gpio *up_gpio = gpiochip_get_data(gc);
	struct up_board_gpio_info *gpio = &up_gpio->pdata->gpios[offset];
	int ret;

	ret = gpiod_direction_input(gpio->soc_gpiod);
	if (ret)
		return ret;

	return pinctrl_gpio_direction_input(gc->base + offset);
}

static int up_gpio_dir_out(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct up_board_gpio *up_gpio = gpiochip_get_data(gc);
	struct up_board_gpio_info *gpio = &up_gpio->pdata->gpios[offset];
	int ret;

	ret = pinctrl_gpio_direction_output(gc->base + offset);
	if (ret)
		return ret;

	return gpiod_direction_output(gpio->soc_gpiod, value);
}

static int up_gpio_get_dir(struct gpio_chip *gc, unsigned int offset)
{
	struct up_board_gpio *up_gpio = gpiochip_get_data(gc);
	struct up_board_gpio_info *gpio = &up_gpio->pdata->gpios[offset];

	return gpiod_get_direction(gpio->soc_gpiod);
}

static int up_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
	struct up_board_gpio *up_gpio = gpiochip_get_data(gc);
	struct up_board_gpio_info *gpio = &up_gpio->pdata->gpios[offset];
	int ret;

	ret = pinctrl_request_gpio(gc->base + offset);
	if (ret)
		return ret;

	if (gpiod_get_direction(gpio->soc_gpiod))
		ret = pinctrl_gpio_direction_input(gc->base + offset);
	else
		ret = pinctrl_gpio_direction_output(gc->base + offset);
	if (ret)
		return ret;

	return gpio_request(gpio->soc_gpio, gc->label);
}

static void up_gpio_free(struct gpio_chip *gc, unsigned int offset)
{
	struct up_board_gpio *up_gpio = gpiochip_get_data(gc);
	struct up_board_gpio_info *gpio = &up_gpio->pdata->gpios[offset];

	pinctrl_free_gpio(gc->base + offset);
	gpio_free(gpio->soc_gpio);
}

static int up_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct up_board_gpio *up_gpio = gpiochip_get_data(gc);
	struct up_board_gpio_info *gpio = &up_gpio->pdata->gpios[offset];

	return gpiod_get_value(gpio->soc_gpiod);
}

static void up_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct up_board_gpio *up_gpio = gpiochip_get_data(gc);
	struct up_board_gpio_info *gpio = &up_gpio->pdata->gpios[offset];

	gpiod_set_value(gpio->soc_gpiod, value);
}

static struct gpio_chip up_gpio_chip = {
	.owner			= THIS_MODULE,
	.request		= up_gpio_request,
	.free			= up_gpio_free,
	.get_direction		= up_gpio_get_dir,
	.direction_input	= up_gpio_dir_in,
	.direction_output	= up_gpio_dir_out,
	.get			= up_gpio_get,
	.set			= up_gpio_set,
};

static int up_board_gpio_setup(struct up_board_gpio *up_gpio)
{
	struct up_board_gpio_pdata *pdata = up_gpio->pdata;
	size_t i;

	for (i = 0; i < pdata->ngpio; i++) {
		struct up_board_gpio_info *gpio = &pdata->gpios[i];
		struct irq_data *irq_data;

		/*
		 * Create parent linkage with SoC GPIO IRQs to simplify
		 * IRQ handling by enabling use of irq_chip_*_parent()
		 * functions
		 */
		gpio->soc_gpio_irq = gpiod_to_irq(gpio->soc_gpiod);
		gpio->irq = irq_find_mapping(up_gpio->chip.irqdomain, i);
		irq_set_parent(gpio->irq, gpio->soc_gpio_irq);
		irq_data = irq_get_irq_data(gpio->irq);
		irq_data->parent_data = irq_get_irq_data(gpio->soc_gpio_irq);
	}

	return 0;
}

static int up_board_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct up_board_gpio_pdata *pdata = dev_get_platdata(dev);
	struct up_board_gpio *up_gpio;
	int ret;

	if (!pdata)
		return -EINVAL;

	up_gpio = devm_kzalloc(dev, sizeof(*up_gpio), GFP_KERNEL);
	if (!up_gpio)
		return -ENOMEM;

	up_gpio->pdata = pdata;
	up_gpio->chip = up_gpio_chip;
	up_gpio->chip.parent = dev;
	up_gpio->chip.ngpio = pdata->ngpio;
	up_gpio->chip.label = dev_name(dev);

	ret = devm_gpiochip_add_data(dev, &up_gpio->chip, up_gpio);
	if (ret) {
		dev_err(dev, "failed to add gpio chip: %d\n", ret);
		return ret;
	}

	ret = gpiochip_add_pin_range(&up_gpio->chip, "up-board-pinctrl", 0, 0,
				     pdata->ngpio);
	if (ret) {
		dev_err(dev, "failed to add GPIO pin range\n");
		return ret;
	}

	up_gpio_irqchip.name = up_gpio->chip.label;
	ret = gpiochip_irqchip_add(&up_gpio->chip, &up_gpio_irqchip, 0,
				   handle_simple_irq, IRQ_TYPE_NONE);
	if (ret) {
		dev_err(dev, "failed to add IRQ chip\n");
		goto fail_irqchip_add;
	}

	ret = up_board_gpio_setup(up_gpio);
	if (ret)
		goto fail_gpio_setup;

	return 0;

fail_gpio_setup:
fail_irqchip_add:
	gpiochip_remove_pin_ranges(&up_gpio->chip);

	return ret;
}

static struct platform_driver up_board_gpio_driver = {
	.driver.name	= "up-board-gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= up_board_gpio_probe,
};

module_platform_driver(up_board_gpio_driver);

MODULE_AUTHOR("Dan O'Donovan <dan@emutex.com>");
MODULE_DESCRIPTION("UP Board I/O Header CPLD GPIO driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:up-board-gpio");
