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
#ifndef _UP_BOARD_PINCTRL_H_
#define _UP_BOARD_PINCTRL_H_

#include <linux/pinctrl/pinctrl.h>

#include "up_board_cpld.h"

#define UP_BOARD_PDIR_NONE	-1
#define UP_BOARD_PDIR_OUT	 0
#define UP_BOARD_PDIR_IN	 1

#define UP_BOARD_PMUX_GPIO	 0
#define UP_BOARD_PMUX_FUNC	 1

#define UP_BOARD_UNASSIGNED	-1

/**
 * struct up_board_pinctrl_group - information for a single pinctrl group
 * @name: group name
 * @pins: array of pins associated with this group
 * @npin: size of pins array
 */
struct up_board_pinctrl_group {
	const char *name;
	const unsigned int *pins;
	size_t npin;
};

/**
 * struct up_board_pinctrl_function - information for a single pinctrl function
 * @name:	function name
 * @groups:	array of groups associated with this function
 * @ngroup:	size of groups array
 */
struct up_board_pinctrl_function {
	const char *name;
	const char * const *groups;
	size_t ngroup;
};

/**
 * struct up_board_pin_info - information for each UP Board GPIO pin
 * @dir_ctrl_offset:	CPLD register bit offset for pin direction control
 * @mux_ctrl_offset:	CPLD register bit offset for pin mux control
 * @func_dir:		Pin dir to set when alternate pin function is selected
 * @func_enabled:	Flag to indicate if alternate pin function is enabled
 *
 * Information for a single GPIO pin on the UP Board I/O header, including
 * details of CPLD parameters for managing pin direction and function selection.
 */
struct up_board_pin_info {
	int dir_ctrl_offset;
	int mux_ctrl_offset;
	int func_dir;
	bool func_enabled;
};

/**
 * struct up_board_pinctrl_pdata - platform driver data
 * @cpld_info:	CPLD configuration interface information
 * @pins:	Array of pin information structures
 * @npin:	Number of entries in pins array
 * @descs:	Array of pinctrl pin descriptors
 * @ndesc:	Number of entries in pin_descs array
 * @groups:	Array of pin groups
 * @ngroup:	Number of entries in groups array
 * @functions:	Array of pin functions
 * @nfunction:	Number of entries in functions array
 *
 * Platform data provided to UP Board CPLD pinctrl platform device driver.
 * Provides information for each GPIO pin on the UP Board I/O header.
 */
struct up_board_pinctrl_pdata {
	struct up_board_cpld_info cpld_info;
	struct up_board_pin_info *pins;
	size_t npin;
	const struct pinctrl_pin_desc *descs;
	size_t ndesc;
	const struct up_board_pinctrl_group *groups;
	size_t ngroup;
	const struct up_board_pinctrl_function *functions;
	size_t nfunction;
};

#endif /* _UP_BOARD_PINCTRL_H_ */
