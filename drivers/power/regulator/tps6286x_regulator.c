// SPDX-License-Identifier: GPL-2.0+
/*
 * TI TPS6286x I2C step-down converter (TPS62864/6/8/9)
 */

#include <common.h>
#include <dm.h>
#include <i2c.h>
#include <dm/device_compat.h>
#include <power/regulator.h>

#define TPS6286X_VOUT1		0x01
#define TPS6286X_CONTROL	0x03
#define TPS6286X_CONTROL_SWEN	BIT(5)

#define TPS6286X_MIN_UV		400000
#define TPS6286X_STEP_UV	5000
#define TPS6286X_MAX_SEL	0xff

static int tps6286x_regulator_set_value(struct udevice *dev, int uV)
{
	int sel;

	if (uV < TPS6286X_MIN_UV)
		return -EINVAL;
	/* Round up to the next 5 mV step: never below the request. */
	sel = DIV_ROUND_UP(uV - TPS6286X_MIN_UV, TPS6286X_STEP_UV);
	if (sel > TPS6286X_MAX_SEL)
		return -EINVAL;

	return dm_i2c_reg_write(dev, TPS6286X_VOUT1, sel);
}

static int tps6286x_regulator_get_value(struct udevice *dev)
{
	int sel = dm_i2c_reg_read(dev, TPS6286X_VOUT1);

	if (sel < 0) {
		dev_err(dev, "i2c read failed: %d\n", sel);
		return sel;
	}

	return TPS6286X_MIN_UV + sel * TPS6286X_STEP_UV;
}

static int tps6286x_regulator_get_enable(struct udevice *dev)
{
	int ctrl = dm_i2c_reg_read(dev, TPS6286X_CONTROL);

	if (ctrl < 0)
		return ctrl;

	return !!(ctrl & TPS6286X_CONTROL_SWEN);
}

static int tps6286x_regulator_set_enable(struct udevice *dev, bool enable)
{
	return dm_i2c_reg_clrset(dev, TPS6286X_CONTROL, TPS6286X_CONTROL_SWEN,
				 enable ? TPS6286X_CONTROL_SWEN : 0);
}

static const struct dm_regulator_ops tps6286x_regulator_ops = {
	.get_value  = tps6286x_regulator_get_value,
	.set_value  = tps6286x_regulator_set_value,
	.get_enable = tps6286x_regulator_get_enable,
	.set_enable = tps6286x_regulator_set_enable,
};

static const struct udevice_id tps6286x_regulator_ids[] = {
	{ .compatible = "ti,tps62864" },
	{ .compatible = "ti,tps62866" },
	{ .compatible = "ti,tps62868" },
	{ .compatible = "ti,tps62869" },
	{ },
};

U_BOOT_DRIVER(tps6286x_regulator) = {
	.name = "tps6286x_regulator",
	.id = UCLASS_REGULATOR,
	.ops = &tps6286x_regulator_ops,
	.of_match = tps6286x_regulator_ids,
};
