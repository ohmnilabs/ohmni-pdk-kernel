/*
 * UP Board I/O Header CPLD driver.
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
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>

#include "up_board_cpld.h"
#include "up_board_pinctrl.h"
#include "up_board_gpio.h"
#include "up_board_leds.h"

/*
 * The UP Board features an external 40-pin header for I/O functions including
 * GPIO, I2C, UART, SPI, PWM and I2S, similar in layout to the Raspberry Pi 2.
 * At the heart of the UP Board is an Intel X5-Z8350 "Cherry Trail" SoC, which
 * provides the I/O functions for these pins at 1.8V logic levels.
 *
 * Additional buffers and mux switches are used between the SoC and the I/O pin
 * header to convert between the 1.8V SoC I/O and the 3.3V levels required at
 * the pin header, with sufficient current source/sink capability for LV-TTL
 * compatibility.  These buffers and mux switches require run-time configuration
 * based on the pin function or GPIO direction selected by the user.
 *
 * The purpose of this driver is to manage the complexity of the buffer
 * configuration so that application code can transparently access the I/O
 * functions on the external pins through standard kernel interfaces.  It
 * instantiates a gpio and pinctrl device, and effectively acts as a "shim"
 * between application code and the underlying Cherry Trail GPIO driver.
 */

/* The Cherry Trail SoC has 4 independent GPIO pin controllers */
#define SOC_GC_SW	"INT33FF:00"
#define SOC_GC_N	"INT33FF:01"
#define SOC_GC_E	"INT33FF:02"
#define SOC_GC_SE	"INT33FF:03"

#define SOC_GPIO(n, o, f)		\
	{				\
		.soc_gc_name	= (n),	\
		.soc_gc_offset	= (o),	\
		.soc_gpio_flags	= (f),	\
	}
#define SOC_GPIO_INPUT(c, o) SOC_GPIO(c, o, GPIOF_IN)
#define SOC_GPIO_OUTPUT(c, o) SOC_GPIO(c, o, GPIOF_OUT_INIT_LOW)

#define GPIO_PIN_INFO(d, m, f)				\
	{						\
		.dir_ctrl_offset	= (d),		\
		.mux_ctrl_offset	= (m),		\
		.func_dir		= (f),		\
		.func_enabled		= false,	\
	}

#define GPIO_PIN_INFO_NO_MUX(d, f)		\
	GPIO_PIN_INFO(d, UP_BOARD_UNASSIGNED, f)

#define PIN_GROUP(n, p)				\
	{					\
		.name = (n),			\
		.pins = (p),			\
		.npin = ARRAY_SIZE((p)),	\
	}

#define FUNCTION(n, g)				\
	{					\
		.name = (n),			\
		.groups = (g),			\
		.ngroup = ARRAY_SIZE((g)),	\
	}

/* Initial configuration assumes all pins as GPIO inputs */
#define CPLD_DIR_REG_INIT	(0x00FFFFFFFULL)

/* Internal context information for this driver */
struct up_board_cpld {
	struct device *dev;
	struct platform_device *pinctrl_pdev;
	struct platform_device *gpio_pdev;
	struct platform_device *leds_pdev;
	struct up_board_gpio_info strobe_gpio;
	struct up_board_gpio_info reset_gpio;
	struct up_board_gpio_info data_in_gpio;
	struct up_board_gpio_info data_out_gpio;
	struct up_board_gpio_info oe_gpio;
	u64 dir_reg;
	unsigned int dir_reg_size;
	/* Lock to prevent concurrent access to CPLD */
	spinlock_t lock;
};

static int up_board_cpld_reg_set_bit(struct up_board_cpld *cpld,
				     unsigned int offset, int value);

static struct up_board_cpld up_board_cpld = {
	.strobe_gpio		= SOC_GPIO_OUTPUT(SOC_GC_N, 21),
	.reset_gpio		= SOC_GPIO_OUTPUT(SOC_GC_E, 15),
	.data_in_gpio		= SOC_GPIO_OUTPUT(SOC_GC_E, 13),
	.data_out_gpio		= SOC_GPIO_INPUT(SOC_GC_E, 23),
	.oe_gpio		= SOC_GPIO_OUTPUT(SOC_GC_SW, 43),
	.dir_reg		= CPLD_DIR_REG_INIT,
	.dir_reg_size		= 34,
};

/* Pin control information for the 28 GPIO pins on the UP Board I/O header */
static struct up_board_pin_info up_board_pins[] = {
	GPIO_PIN_INFO(9,  28, UP_BOARD_PDIR_OUT),	/*  0 */
	GPIO_PIN_INFO(23, 28, UP_BOARD_PDIR_OUT),	/*  1 */
	GPIO_PIN_INFO(0,  29, UP_BOARD_PDIR_OUT),	/*  2 */
	GPIO_PIN_INFO(1,  29, UP_BOARD_PDIR_OUT),	/*  3 */
	GPIO_PIN_INFO(2,  30, UP_BOARD_PDIR_IN),	/*  4 */
	GPIO_PIN_INFO_NO_MUX(10, UP_BOARD_PDIR_NONE),	/*  5 */
	GPIO_PIN_INFO_NO_MUX(11, UP_BOARD_PDIR_NONE),	/*  6 */
	GPIO_PIN_INFO_NO_MUX(22, UP_BOARD_PDIR_NONE),	/*  7 */
	GPIO_PIN_INFO_NO_MUX(21, UP_BOARD_PDIR_OUT),	/*  8 */
	GPIO_PIN_INFO_NO_MUX(7,  UP_BOARD_PDIR_IN),	/*  9 */
	GPIO_PIN_INFO_NO_MUX(6,  UP_BOARD_PDIR_OUT),	/* 10 */
	GPIO_PIN_INFO_NO_MUX(8,  UP_BOARD_PDIR_OUT),	/* 11 */
	GPIO_PIN_INFO_NO_MUX(24, UP_BOARD_PDIR_OUT),	/* 12 */
	GPIO_PIN_INFO_NO_MUX(12, UP_BOARD_PDIR_OUT),	/* 13 */
	GPIO_PIN_INFO_NO_MUX(15, UP_BOARD_PDIR_OUT),	/* 14 */
	GPIO_PIN_INFO_NO_MUX(16, UP_BOARD_PDIR_IN),	/* 15 */
	GPIO_PIN_INFO_NO_MUX(25, UP_BOARD_PDIR_IN),	/* 16 */
	GPIO_PIN_INFO_NO_MUX(3,  UP_BOARD_PDIR_OUT),	/* 17 */
	GPIO_PIN_INFO_NO_MUX(17, UP_BOARD_PDIR_OUT),	/* 18 */
	GPIO_PIN_INFO_NO_MUX(13, UP_BOARD_PDIR_OUT),	/* 19 */
	GPIO_PIN_INFO_NO_MUX(26, UP_BOARD_PDIR_IN),	/* 20 */
	GPIO_PIN_INFO_NO_MUX(27, UP_BOARD_PDIR_OUT),	/* 21 */
	GPIO_PIN_INFO_NO_MUX(5,  UP_BOARD_PDIR_OUT),	/* 22 */
	GPIO_PIN_INFO_NO_MUX(18, UP_BOARD_PDIR_OUT),	/* 23 */
	GPIO_PIN_INFO_NO_MUX(19, UP_BOARD_PDIR_OUT),	/* 24 */
	GPIO_PIN_INFO_NO_MUX(20, UP_BOARD_PDIR_OUT),	/* 25 */
	GPIO_PIN_INFO_NO_MUX(14, UP_BOARD_PDIR_OUT),	/* 26 */
	GPIO_PIN_INFO_NO_MUX(4,  UP_BOARD_PDIR_OUT),	/* 27 */
};

/* SoC GPIO mapping for the 28 GPIO pins on the UP Board I/O header */
static struct up_board_gpio_info up_board_gpios[] = {
	SOC_GPIO(SOC_GC_SW, 33, 0),	/*  0 */
	SOC_GPIO(SOC_GC_SW, 37, 0),	/*  1 */
	SOC_GPIO(SOC_GC_SW, 32, 0),	/*  2 */
	SOC_GPIO(SOC_GC_SW, 35, 0),	/*  3 */
	SOC_GPIO(SOC_GC_E,  18, 0),	/*  4 */
	SOC_GPIO(SOC_GC_E,  21, 0),	/*  5 */
	SOC_GPIO(SOC_GC_E,  12, 0),	/*  6 */
	SOC_GPIO(SOC_GC_SE, 48, 0),	/*  7 */
	SOC_GPIO(SOC_GC_SE,  7, 0),	/*  8 */
	SOC_GPIO(SOC_GC_SE,  3, 0),	/*  9 */
	SOC_GPIO(SOC_GC_SE,  6, 0),	/* 10 */
	SOC_GPIO(SOC_GC_SE,  4, 0),	/* 11 */
	SOC_GPIO(SOC_GC_SE,  5, 0),	/* 12 */
	SOC_GPIO(SOC_GC_SE,  1, 0),	/* 13 */
	SOC_GPIO(SOC_GC_SW, 13, 0),	/* 14 */
	SOC_GPIO(SOC_GC_SW,  9, 0),	/* 15 */
	SOC_GPIO(SOC_GC_SW, 11, 0),	/* 16 */
	SOC_GPIO(SOC_GC_SW,  8, 0),	/* 17 */
	SOC_GPIO(SOC_GC_SW, 50, 0),	/* 18 */
	SOC_GPIO(SOC_GC_SW, 54, 0),	/* 19 */
	SOC_GPIO(SOC_GC_SW, 52, 0),	/* 20 */
	SOC_GPIO(SOC_GC_SW, 55, 0),	/* 21 */
	SOC_GPIO(SOC_GC_SE, 12, 0),	/* 22 */
	SOC_GPIO(SOC_GC_SE, 15, 0),	/* 23 */
	SOC_GPIO(SOC_GC_SE, 18, 0),	/* 24 */
	SOC_GPIO(SOC_GC_SE, 11, 0),	/* 25 */
	SOC_GPIO(SOC_GC_SE, 14, 0),	/* 26 */
	SOC_GPIO(SOC_GC_SE,  8, 0),	/* 27 */
};

/* pinctrl descriptors for the 28 GPIO pins on the UP Board I/O header */
static const struct pinctrl_pin_desc up_board_pinctrl_descs[] = {
	PINCTRL_PIN(0,  "I2C0_SDA"),
	PINCTRL_PIN(1,  "I2C0_SCL"),
	PINCTRL_PIN(2,  "I2C1_SDA"),
	PINCTRL_PIN(3,  "I2C1_SCL"),
	PINCTRL_PIN(4,  "ADC"),
	PINCTRL_PIN(5,  "GPIO5"),
	PINCTRL_PIN(6,  "GPIO6"),
	PINCTRL_PIN(7,  "SPI_CS1"),
	PINCTRL_PIN(8,  "SPI_CS0"),
	PINCTRL_PIN(9,  "SPI_MISO"),
	PINCTRL_PIN(10, "SPI_MOSI"),
	PINCTRL_PIN(11, "SPI_CLK"),
	PINCTRL_PIN(12, "PWM0"),
	PINCTRL_PIN(13, "PWM1"),
	PINCTRL_PIN(14, "UART1_TX"),
	PINCTRL_PIN(15, "UART1_RX"),
	PINCTRL_PIN(16, "UART1_CTS"),
	PINCTRL_PIN(17, "UART1_RTS"),
	PINCTRL_PIN(18, "I2S_CLK"),
	PINCTRL_PIN(19, "I2S_FRM"),
	PINCTRL_PIN(20, "I2S_DIN"),
	PINCTRL_PIN(21, "I2S_DOUT"),
	PINCTRL_PIN(22, "GPIO22"),
	PINCTRL_PIN(23, "GPIO23"),
	PINCTRL_PIN(24, "GPIO24"),
	PINCTRL_PIN(25, "GPIO25"),
	PINCTRL_PIN(26, "GPIO26"),
	PINCTRL_PIN(27, "GPIO27"),
};

static const unsigned int uart1_pins[] = { 14, 15, 16, 17 };
static const unsigned int uart2_pins[] = { 25, 27 };
static const unsigned int i2c0_pins[]  = { 0, 1 };
static const unsigned int i2c1_pins[]  = { 2, 3 };
static const unsigned int spi2_pins[]  = { 8, 9, 10, 11 };
static const unsigned int i2s2_pins[]  = { 18, 19, 20, 21 };
static const unsigned int pwm0_pins[]  = { 12 };
static const unsigned int pwm1_pins[]  = { 13 };
static const unsigned int adc0_pins[]  = { 4 };

static const struct up_board_pinctrl_group up_board_pinctrl_groups[] = {
	PIN_GROUP("uart1_grp", uart1_pins),
	PIN_GROUP("uart2_grp", uart2_pins),
	PIN_GROUP("i2c0_grp", i2c0_pins),
	PIN_GROUP("i2c1_grp", i2c1_pins),
	PIN_GROUP("spi2_grp", spi2_pins),
	PIN_GROUP("i2s2_grp", i2s2_pins),
	PIN_GROUP("pwm0_grp", pwm0_pins),
	PIN_GROUP("pwm1_grp", pwm1_pins),
	PIN_GROUP("adc0_grp", adc0_pins),
};

static const char * const uart1_groups[] = { "uart1_grp" };
static const char * const uart2_groups[] = { "uart2_grp" };
static const char * const i2c0_groups[]  = { "i2c0_grp" };
static const char * const i2c1_groups[]  = { "i2c1_grp" };
static const char * const spi2_groups[]  = { "spi2_grp" };
static const char * const i2s2_groups[]  = { "i2s2_grp" };
static const char * const pwm0_groups[]  = { "pwm0_grp" };
static const char * const pwm1_groups[]  = { "pwm1_grp" };
static const char * const adc0_groups[]  = { "adc0_grp" };

static const struct up_board_pinctrl_function up_board_pinctrl_functions[] = {
	FUNCTION("uart1", uart1_groups),
	FUNCTION("uart2", uart2_groups),
	FUNCTION("i2c0",  i2c0_groups),
	FUNCTION("i2c1",  i2c1_groups),
	FUNCTION("spi2",  spi2_groups),
	FUNCTION("i2s2",  i2s2_groups),
	FUNCTION("pwm0",  pwm0_groups),
	FUNCTION("pwm1",  pwm1_groups),
	FUNCTION("adc0",  adc0_groups),
};

/* The CPLD controls the following 3 LEDs on the UP board */
static struct up_board_led_info up_board_leds[] = {
	{ .cpld_offset = 31, .name = "upboard:yellow:", },
	{ .cpld_offset = 32, .name = "upboard:green:", },
	{ .cpld_offset = 33, .name = "upboard:red:", },
};

static struct up_board_pinctrl_pdata up_board_pinctrl_pdata = {
	.cpld_info.cpld = &up_board_cpld,
	.cpld_info.reg_set_bit = up_board_cpld_reg_set_bit,
	.pins = up_board_pins,
	.npin = ARRAY_SIZE(up_board_pins),
	.descs = up_board_pinctrl_descs,
	.ndesc = ARRAY_SIZE(up_board_pinctrl_descs),
	.groups = up_board_pinctrl_groups,
	.ngroup = ARRAY_SIZE(up_board_pinctrl_groups),
	.functions = up_board_pinctrl_functions,
	.nfunction = ARRAY_SIZE(up_board_pinctrl_functions),
};

static struct up_board_gpio_pdata up_board_gpio_pdata = {
	.gpios = up_board_gpios,
	.ngpio = ARRAY_SIZE(up_board_gpios),
};

static struct up_board_leds_pdata up_board_leds_pdata = {
	.cpld_info.cpld = &up_board_cpld,
	.cpld_info.reg_set_bit = up_board_cpld_reg_set_bit,
	.leds = up_board_leds,
	.nled = ARRAY_SIZE(up_board_leds),
};

/*
 * On the UP board, the header pin level shifting and mux switching is
 * controlled by a dedicated CPLD with proprietary firmware.
 *
 * The CPLD is responsible for connecting and translating 1.8V GPIO signals from
 * the SoC to the 28 GPIO header pins at 3.3V, and for this it needs to be
 * configured with direction (input/output) for each GPIO.  In addition, it
 * manages 3 mux switches (2 for I2C bus pins, 1 for ADC pin) which need to be
 * configured on/off, and 3 LEDs.  A register value is loaded into the CPLD to
 * dynamically configure each of these.
 */
static int cpld_reg_update(struct up_board_cpld *cpld)
{
	u64 dir_reg_verify = 0;
	int i;

	/* Reset the CPLD internal counters */
	gpiod_set_value(cpld->reset_gpio.soc_gpiod, 0);
	gpiod_set_value(cpld->reset_gpio.soc_gpiod, 1);

	/*
	 * Update the CPLD dir register
	 * data_in will be sampled on each rising edge of the strobe signal
	 */
	for (i = cpld->dir_reg_size - 1; i >= 0; i--) {
		gpiod_set_value(cpld->strobe_gpio.soc_gpiod, 0);
		gpiod_set_value(cpld->data_in_gpio.soc_gpiod,
				(cpld->dir_reg >> i) & 0x1);
		gpiod_set_value(cpld->strobe_gpio.soc_gpiod, 1);
	}

	/*
	 * Read back and verify the value
	 * data_out will be set on each rising edge of the strobe signal
	 */
	for (i = cpld->dir_reg_size - 1; i >= 0; i--) {
		int data_out;

		gpiod_set_value(cpld->strobe_gpio.soc_gpiod, 0);
		gpiod_set_value(cpld->strobe_gpio.soc_gpiod, 1);
		data_out = gpiod_get_value(cpld->data_out_gpio.soc_gpiod);
		dir_reg_verify |= (u64)data_out << i;
	}

	if (dir_reg_verify != cpld->dir_reg) {
		pr_err("CPLD verify error (expected: %llX, actual: %llX)\n",
		       cpld->dir_reg, dir_reg_verify);
		return -EIO;
	}

	/* Issue a dummy STB cycle to latch the dir register updates */
	gpiod_set_value(cpld->strobe_gpio.soc_gpiod, 0);
	gpiod_set_value(cpld->strobe_gpio.soc_gpiod, 1);

	return 0;
}

/**
 * up_board_cpld_reg_set_bit() - update CPLD configuration
 * @cpld:	CPLD internal context info reference
 * @offset:	bit offset in CPLD register to set
 * @value:	boolean value to set in CPLD register bit selected by offset
 *
 * Return:	Returns 0 if successful, or negative error value otherwise
 */
static int up_board_cpld_reg_set_bit(struct up_board_cpld *cpld,
				     unsigned int offset, int value)
{
	u64 old_regval;
	int ret = 0;

	spin_lock(&cpld->lock);

	old_regval = cpld->dir_reg;

	if (value)
		cpld->dir_reg |= 1ULL << offset;
	else
		cpld->dir_reg &= ~(1ULL << offset);

	/* Only update the CPLD register if it has changed */
	if (cpld->dir_reg != old_regval)
		ret = cpld_reg_update(cpld);

	spin_unlock(&cpld->lock);

	return ret;
}

static int up_gpiochip_match(struct gpio_chip *chip, void *data)
{
	return !strcmp(chip->label, data);
}

static int up_board_soc_gpio_setup(struct up_board_cpld *cpld,
				   struct up_board_gpio_info *gpio)
{
	gpio->soc_gc = gpiochip_find(gpio->soc_gc_name, up_gpiochip_match);
	if (!gpio->soc_gc)
		return -EPROBE_DEFER;

	gpio->soc_gpio = gpio->soc_gc->base + gpio->soc_gc_offset;
	gpio->soc_gpiod = gpio_to_desc(gpio->soc_gpio);
	if (!gpio->soc_gpiod) {
		dev_err(cpld->dev, "Failed to get descriptor for gpio %d\n",
			gpio->soc_gpio);
		return -EINVAL;
	}

	return 0;
}

static int up_board_cpld_setup(struct up_board_cpld *cpld)
{
	struct up_board_gpio_info *cpld_gpios[] = {
		&cpld->strobe_gpio,
		&cpld->reset_gpio,
		&cpld->data_in_gpio,
		&cpld->data_out_gpio,
		&cpld->oe_gpio,
	};
	int i, ret;

	spin_lock_init(&cpld->lock);

	/* Initialise the CPLD config input GPIOs as outputs, initially low */
	for (i = 0; i < ARRAY_SIZE(cpld_gpios); i++) {
		struct up_board_gpio_info *gpio = cpld_gpios[i];

		ret = up_board_soc_gpio_setup(cpld, gpio);
		if (ret)
			return ret;

		ret = devm_gpio_request_one(cpld->dev, gpio->soc_gpio,
					    gpio->soc_gpio_flags,
					    dev_name(cpld->dev));
		if (ret)
			return ret;
	}

	/* Load initial CPLD configuration (all pins set for GPIO input) */
	ret = cpld_reg_update(cpld);
	if (ret) {
		dev_err(cpld->dev, "CPLD initialisation failed\n");
		return ret;
	}

	/* Enable the CPLD outputs after a valid configuration has been set */
	gpiod_set_value(cpld->oe_gpio.soc_gpiod, 1);

	return 0;
}

static int up_board_setup(struct up_board_cpld *cpld,
			  struct up_board_gpio_pdata *gpio_pdata)
{
	size_t i;
	int ret;

	/* Ensure the GPIO pins are configured as inputs initially */
	for (i = 0; i < gpio_pdata->ngpio; i++) {
		struct up_board_gpio_info *gpio = &gpio_pdata->gpios[i];

		ret = up_board_soc_gpio_setup(cpld, gpio);
		if (ret)
			return ret;

		ret = gpiod_direction_input(gpio->soc_gpiod);
		if (ret) {
			dev_err(cpld->dev, "GPIO direction init failed\n");
			return ret;
		}
	}

	return up_board_cpld_setup(cpld);
}

static int up_board_cpld_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct up_board_cpld *cpld = &up_board_cpld;
	int ret;

	cpld->dev = dev;
	ret = up_board_setup(cpld, &up_board_gpio_pdata);
	if (ret)
		return ret;

	cpld->pinctrl_pdev =
		platform_device_register_data(dev, "up-board-pinctrl",
					      PLATFORM_DEVID_NONE,
					      &up_board_pinctrl_pdata,
					      sizeof(up_board_pinctrl_pdata));
	if (IS_ERR(cpld->pinctrl_pdev)) {
		ret = PTR_ERR(cpld->pinctrl_pdev);
		goto fail_register_pinctrl_pdev;
	}

	cpld->gpio_pdev =
		platform_device_register_data(dev, "up-board-gpio",
					      PLATFORM_DEVID_NONE,
					      &up_board_gpio_pdata,
					      sizeof(up_board_gpio_pdata));
	if (IS_ERR(cpld->gpio_pdev)) {
		ret = PTR_ERR(cpld->gpio_pdev);
		goto fail_register_gpio_pdev;
	}

	cpld->leds_pdev =
		platform_device_register_data(dev, "up-board-leds",
					      PLATFORM_DEVID_NONE,
					      &up_board_leds_pdata,
					      sizeof(up_board_leds_pdata));
	if (IS_ERR(cpld->leds_pdev)) {
		ret = PTR_ERR(cpld->leds_pdev);
		goto fail_register_leds_pdev;
	}

	return 0;

fail_register_leds_pdev:
	platform_device_unregister(cpld->gpio_pdev);
fail_register_gpio_pdev:
	platform_device_unregister(cpld->pinctrl_pdev);
fail_register_pinctrl_pdev:

	return ret;
}

static int up_board_cpld_remove(struct platform_device *pdev)
{
	struct up_board_cpld *cpld = &up_board_cpld;

	platform_device_unregister(cpld->leds_pdev);
	platform_device_unregister(cpld->gpio_pdev);
	platform_device_unregister(cpld->pinctrl_pdev);

	/* Disable the CPLD outputs */
	gpiod_set_value(cpld->oe_gpio.soc_gpiod, 0);

	return 0;
}

static struct platform_driver up_board_cpld_driver = {
	.driver.name	= "up-board-cpld",
	.driver.owner	= THIS_MODULE,
	.probe		= up_board_cpld_probe,
	.remove		= up_board_cpld_remove,
};

static int __init up_board_cpld_init(void)
{
	return platform_driver_register(&up_board_cpld_driver);
}
subsys_initcall(up_board_cpld_init);

static void __exit up_board_cpld_exit(void)
{
	platform_driver_unregister(&up_board_cpld_driver);
}
module_exit(up_board_cpld_exit);

MODULE_AUTHOR("Dan O'Donovan <dan@emutex.com>");
MODULE_DESCRIPTION("UP Board I/O Header CPLD driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:up-board-cpld");
