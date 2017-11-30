/*
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
#ifndef _UP_BOARD_GPIO_H_
#define _UP_BOARD_GPIO_H_

/**
 * struct up_board_gpio_info - information for an UP Board GPIO pin
 * @soc_gc_name:	Device name for corresponding SoC GPIO chip
 * @soc_gc_offset:	GPIO chip offset of corresponding SoC GPIO pin
 * @soc_gc:		SoC GPIO chip reference
 * @soc_gpiod:		SoC GPIO descriptor reference
 * @soc_gpio:		SoC GPIO assigned pin number
 * @soc_gpio_irq:	SoC GPIO assigned IRQ number
 * @soc_gpio_flags:	Optional GPIO flags to apply to SoC GPIO
 * @irq:		Assigned IRQ number for this GPIO pin
 *
 * Information for a single GPIO pin on the UP Board I/O header, including
 * details of the corresponding SoC GPIO mapped to this I/O header GPIO.
 */
struct up_board_gpio_info {
	char *soc_gc_name;
	unsigned int soc_gc_offset;
	struct gpio_chip *soc_gc;
	struct gpio_desc *soc_gpiod;
	int soc_gpio;
	int soc_gpio_irq;
	int soc_gpio_flags;
	int irq;
};

/**
 * struct up_board_gpio_pdata - platform driver data
 * @gpios:	Array of GPIO information structures.
 * @ngpio:	Number of entries in gpios array.
 *
 * Platform data provided to UP Board CPLD GPIO platform device driver.
 * Provides information for each GPIO pin on the UP Board I/O header.
 */
struct up_board_gpio_pdata {
	struct up_board_gpio_info *gpios;
	size_t ngpio;
};

#endif /* _UP_BOARD_GPIO_H_ */
