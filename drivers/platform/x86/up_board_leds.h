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
#ifndef _UP_BOARD_LED_H_
#define _UP_BOARD_LED_H_

#include "up_board_cpld.h"

/**
 * struct up_board_led_info - information for an UP Board LED
 * @name:		LED name
 * @cpld_offset:	CPLD register bit offset for LED control
 *
 * Information for a single CPLD-controlled LED on the UP Board.
 */
struct up_board_led_info {
	const char *name;
	unsigned int cpld_offset;
};

/**
 * struct up_board_leds_pdata - platform driver data
 * @cpld_info:	CPLD configuration interface information
 * @leds:	Array of LED information structures
 * @nled:	Number of entries in leds array
 *
 * Platform data provided to UP Board CPLD LEDs platform device driver.
 * Provides information for each CPLD-controlled LED on the UP Board.
 */
struct up_board_leds_pdata {
	struct up_board_cpld_info cpld_info;
	struct up_board_led_info *leds;
	size_t nled;
};

#endif /* _UP_BOARD_LED_H_ */
