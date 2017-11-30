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
#ifndef _UP_BOARD_CPLD_H_
#define _UP_BOARD_CPLD_H_

/* Forward declaration to internal CPLD info structure */
struct up_board_cpld;

/**
 * struct up_board_cpld_info - abstract interface for CPLD configuration
 * @cpld:		Opaque reference to internal CPLD info structure
 * @reg_set_bit:	Callback to update internal CPLD register bits
 *
 * Information passed to UP Board CPLD users to provide a method for updating
 * the CPLD configuration register
 */
struct up_board_cpld_info {
	struct up_board_cpld *cpld;
	int (*reg_set_bit)(struct up_board_cpld *cpld,
			   unsigned int offset, int value);
};

#endif /* _UP_BOARD_CPLD_H_ */
