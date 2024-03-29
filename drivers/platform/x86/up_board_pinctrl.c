/*
 * UP Board I/O Header CPLD Pin Control driver.
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
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>

#include "up_board_pinctrl.h"

/* Internal context information for this driver */
struct up_board_pinctrl {
	struct up_board_pinctrl_pdata *pdata;
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;
};

static int up_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return up_pinctrl->pdata->ngroup;
}

static const char *up_get_group_name(struct pinctrl_dev *pctldev,
				     unsigned int group)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return up_pinctrl->pdata->groups[group].name;
}

static int up_get_group_pins(struct pinctrl_dev *pctldev, unsigned int group,
			     const unsigned int **pins, unsigned int *npins)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = up_pinctrl->pdata->groups[group].pins;
	*npins = up_pinctrl->pdata->groups[group].npin;

	return 0;
}

static const struct pinctrl_ops up_pinctrl_ops = {
	.get_groups_count = up_get_groups_count,
	.get_group_name = up_get_group_name,
	.get_group_pins = up_get_group_pins,
};

static int up_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return up_pinctrl->pdata->nfunction;
}

static const char *up_get_function_name(struct pinctrl_dev *pctldev,
					unsigned int function)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);

	return up_pinctrl->pdata->functions[function].name;
}

static int up_get_function_groups(struct pinctrl_dev *pctldev,
				  unsigned int function,
				  const char * const **groups,
				  unsigned int * const ngroups)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = up_pinctrl->pdata->functions[function].groups;
	*ngroups = up_pinctrl->pdata->functions[function].ngroup;

	return 0;
}

static int up_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned int function,
			     unsigned int group)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);
	struct up_board_pinctrl_pdata *pdata = up_pinctrl->pdata;
	struct up_board_cpld_info *cpld_info = &up_pinctrl->pdata->cpld_info;
	const struct up_board_pinctrl_group *grp = &pdata->groups[group];
	int i, ret;

	for (i = 0; i < grp->npin; i++) {
		int offset = grp->pins[i];
		struct up_board_pin_info *pin = &pdata->pins[offset];

		if (pin->func_dir != UP_BOARD_PDIR_NONE) {
			ret = cpld_info->reg_set_bit(cpld_info->cpld,
						     pin->dir_ctrl_offset,
						     pin->func_dir);
			if (ret)
				return ret;
		}
		if (pin->mux_ctrl_offset != UP_BOARD_UNASSIGNED) {
			ret = cpld_info->reg_set_bit(cpld_info->cpld,
						     pin->mux_ctrl_offset,
						     UP_BOARD_PMUX_FUNC);
			if (ret)
				return ret;
		}
		pin->func_enabled = true;
	}

	return 0;
}

static int up_gpio_set_direction(struct pinctrl_dev *pctldev,
				 struct pinctrl_gpio_range *range,
				 unsigned int offset, bool input)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);
	struct up_board_pinctrl_pdata *pdata = up_pinctrl->pdata;
	struct up_board_cpld_info *cpld_info = &up_pinctrl->pdata->cpld_info;
	struct up_board_pin_info *pin = &pdata->pins[offset];
	int dir = input ? UP_BOARD_PDIR_IN : UP_BOARD_PDIR_OUT;

	return cpld_info->reg_set_bit(cpld_info->cpld,
				      pin->dir_ctrl_offset,
				      dir);
}

static int up_gpio_request_enable(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned int offset)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);
	struct up_board_pinctrl_pdata *pdata = up_pinctrl->pdata;
	struct up_board_cpld_info *cpld_info = &up_pinctrl->pdata->cpld_info;
	struct up_board_pin_info *pin = &pdata->pins[offset];
	int ret;

	if (pin->mux_ctrl_offset != UP_BOARD_UNASSIGNED) {
		ret = cpld_info->reg_set_bit(cpld_info->cpld,
					     pin->mux_ctrl_offset,
					     UP_BOARD_PMUX_GPIO);
		if (ret)
			return ret;
	}

	return 0;
}

static void up_gpio_disable_free(struct pinctrl_dev *pctldev,
				 struct pinctrl_gpio_range *range,
				 unsigned int offset)
{
	struct up_board_pinctrl *up_pinctrl = pinctrl_dev_get_drvdata(pctldev);
	struct up_board_pinctrl_pdata *pdata = up_pinctrl->pdata;
	struct up_board_cpld_info *cpld_info = &up_pinctrl->pdata->cpld_info;
	struct up_board_pin_info *pin = &pdata->pins[offset];

	if (pin->func_enabled) {
		if (pin->func_dir != UP_BOARD_PDIR_NONE) {
			cpld_info->reg_set_bit(cpld_info->cpld,
					       pin->dir_ctrl_offset,
					       pin->func_dir);
		}
		if (pin->mux_ctrl_offset != UP_BOARD_UNASSIGNED) {
			cpld_info->reg_set_bit(cpld_info->cpld,
					       pin->mux_ctrl_offset,
					       UP_BOARD_PMUX_FUNC);
		}
	}
}

static const struct pinmux_ops up_pinmux_ops = {
	.get_functions_count = up_get_functions_count,
	.get_function_name = up_get_function_name,
	.get_function_groups = up_get_function_groups,
	.set_mux = up_pinmux_set_mux,
	.gpio_request_enable = up_gpio_request_enable,
	.gpio_disable_free = up_gpio_disable_free,
	.gpio_set_direction = up_gpio_set_direction,
};

static int up_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
			 unsigned long *config)
{
	return -ENOTSUPP;
}

static int up_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
			 unsigned long *configs, unsigned int nconfigs)
{
	return 0;
}

static const struct pinconf_ops up_pinconf_ops = {
	.is_generic = true,
	.pin_config_set = up_config_set,
	.pin_config_get = up_config_get,
};

static struct pinctrl_desc up_pinctrl_desc = {
	.owner = THIS_MODULE,
	.pctlops = &up_pinctrl_ops,
	.pmxops = &up_pinmux_ops,
	.confops = &up_pinconf_ops,
};

static int up_board_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct up_board_pinctrl_pdata *pdata = dev_get_platdata(dev);
	struct up_board_pinctrl *up_pinctrl;

	if (!pdata)
		return -EINVAL;

	up_pinctrl = devm_kzalloc(dev, sizeof(*up_pinctrl), GFP_KERNEL);
	if (!up_pinctrl)
		return -ENOMEM;

	platform_set_drvdata(pdev, up_pinctrl);

	up_pinctrl->pdata = pdata;
	up_pinctrl->pctldesc = up_pinctrl_desc;
	up_pinctrl->pctldesc.pins = pdata->descs;
	up_pinctrl->pctldesc.npins = pdata->ndesc;
	up_pinctrl->pctldesc.name = dev_name(dev);
	up_pinctrl->pctldev = pinctrl_register(&up_pinctrl->pctldesc,
					       dev, up_pinctrl);
	if (IS_ERR(up_pinctrl->pctldev)) {
		dev_err(dev, "failed to register pinctrl driver\n");
		return PTR_ERR(up_pinctrl->pctldev);
	}

	return 0;
}

static int up_board_pinctrl_remove(struct platform_device *pdev)
{
	struct up_board_pinctrl *up_pinctrl = platform_get_drvdata(pdev);

	pinctrl_unregister(up_pinctrl->pctldev);

	return 0;
}

static struct platform_driver up_board_pinctrl_driver = {
	.driver.name	= "up-board-pinctrl",
	.driver.owner	= THIS_MODULE,
	.probe		= up_board_pinctrl_probe,
	.remove		= up_board_pinctrl_remove,
};

static int __init up_board_pinctrl_init(void)
{
	return platform_driver_register(&up_board_pinctrl_driver);
}
subsys_initcall(up_board_pinctrl_init);

static void __exit up_board_pinctrl_exit(void)
{
	platform_driver_unregister(&up_board_pinctrl_driver);
}
module_exit(up_board_pinctrl_exit);

MODULE_AUTHOR("Dan O'Donovan <dan@emutex.com>");
MODULE_DESCRIPTION("UP Board I/O Header CPLD Pin Control driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:up-board-pinctrl");
