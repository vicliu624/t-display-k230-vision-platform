// SPDX-License-Identifier: GPL-2.0-only
/*
 * T-Display K230 radio profile selector.
 *
 * IO5 is physically shared by LR2021 reset and UART2 TX to the optional
 * nRF9151 modem. The board exposes one active profile at a time.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#define TDVP_NRF9151_SHUTDOWN_DELAY_MS	20
#define TDVP_NRF9151_STARTUP_DELAY_MS	100

enum tdvp_radio_profile {
	TDVP_RADIO_PROFILE_UNKNOWN,
	TDVP_RADIO_PROFILE_LORA,
	TDVP_RADIO_PROFILE_NRF9151,
};

struct tdvp_radio_mux {
	struct device *dev;
	struct mutex lock;
	struct pinctrl *pinctrl;
	struct pinctrl_state *lora_pins;
	struct pinctrl_state *nrf9151_pins;
	struct gpio_desc *lora_enable;
	struct gpio_desc *lora_reset;
	struct gpio_desc *nrf9151_enable;
	enum tdvp_radio_profile profile;
};

static int tdvp_radio_mux_select_lora(struct tdvp_radio_mux *mux)
{
	int ret;

	if (mux->profile == TDVP_RADIO_PROFILE_NRF9151) {
		gpiod_set_value_cansleep(mux->nrf9151_enable, 0);
		msleep(TDVP_NRF9151_SHUTDOWN_DELAY_MS);
	}

	ret = pinctrl_select_state(mux->pinctrl, mux->lora_pins);
	if (ret)
		return dev_err_probe(mux->dev, ret, "cannot select LoRa pins\n");

	ret = gpiod_direction_output(mux->nrf9151_enable, 0);
	if (ret)
		return dev_err_probe(mux->dev, ret, "cannot disable nRF9151\n");
	ret = gpiod_direction_output(mux->lora_enable, 0);
	if (ret)
		return dev_err_probe(mux->dev, ret, "cannot disable LoRa\n");
	ret = gpiod_direction_output(mux->lora_reset, 1);
	if (ret)
		return dev_err_probe(mux->dev, ret, "cannot assert LoRa reset\n");
	mux->profile = TDVP_RADIO_PROFILE_LORA;

	return 0;
}

static int tdvp_radio_mux_select_nrf9151(struct tdvp_radio_mux *mux)
{
	int ret;

	if (mux->profile == TDVP_RADIO_PROFILE_LORA) {
		gpiod_set_value_cansleep(mux->lora_enable, 0);
		gpiod_set_value_cansleep(mux->lora_reset, 1);
	}

	ret = pinctrl_select_state(mux->pinctrl, mux->nrf9151_pins);
	if (ret)
		return dev_err_probe(mux->dev, ret, "cannot select nRF9151 pins\n");

	ret = gpiod_direction_output(mux->nrf9151_enable, 1);
	if (ret)
		return dev_err_probe(mux->dev, ret, "cannot enable nRF9151\n");
	msleep(TDVP_NRF9151_STARTUP_DELAY_MS);
	mux->profile = TDVP_RADIO_PROFILE_NRF9151;

	return 0;
}

static ssize_t profile_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	struct tdvp_radio_mux *mux = dev_get_drvdata(dev);
	const char *profile;

	mutex_lock(&mux->lock);
	profile = mux->profile == TDVP_RADIO_PROFILE_NRF9151 ? "nrf9151" : "lora";
	mutex_unlock(&mux->lock);

	return sysfs_emit(buf, "%s\n", profile);
}

static ssize_t profile_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct tdvp_radio_mux *mux = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&mux->lock);
	if (sysfs_streq(buf, "lora"))
		ret = tdvp_radio_mux_select_lora(mux);
	else if (sysfs_streq(buf, "nrf9151"))
		ret = tdvp_radio_mux_select_nrf9151(mux);
	else
		ret = -EINVAL;
	mutex_unlock(&mux->lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(profile);

static int tdvp_radio_mux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tdvp_radio_mux *mux;
	int ret;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	mux->dev = dev;
	mutex_init(&mux->lock);
	mux->profile = TDVP_RADIO_PROFILE_UNKNOWN;

	mux->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(mux->pinctrl))
		return dev_err_probe(dev, PTR_ERR(mux->pinctrl),
				     "cannot get pinctrl\n");

	mux->lora_pins = pinctrl_lookup_state(mux->pinctrl, "lora");
	if (IS_ERR(mux->lora_pins))
		return dev_err_probe(dev, PTR_ERR(mux->lora_pins),
				     "cannot get LoRa pin state\n");

	mux->nrf9151_pins = pinctrl_lookup_state(mux->pinctrl, "nrf9151");
	if (IS_ERR(mux->nrf9151_pins))
		return dev_err_probe(dev, PTR_ERR(mux->nrf9151_pins),
				     "cannot get nRF9151 pin state\n");

	mux->lora_enable = devm_gpiod_get(dev, "lora-enable", GPIOD_ASIS);
	if (IS_ERR(mux->lora_enable))
		return dev_err_probe(dev, PTR_ERR(mux->lora_enable),
				     "cannot get LoRa enable GPIO\n");

	mux->lora_reset = devm_gpiod_get(dev, "lora-reset", GPIOD_ASIS);
	if (IS_ERR(mux->lora_reset))
		return dev_err_probe(dev, PTR_ERR(mux->lora_reset),
				     "cannot get LoRa reset GPIO\n");

	mux->nrf9151_enable = devm_gpiod_get(dev, "nrf9151-enable", GPIOD_ASIS);
	if (IS_ERR(mux->nrf9151_enable))
		return dev_err_probe(dev, PTR_ERR(mux->nrf9151_enable),
				     "cannot get nRF9151 enable GPIO\n");

	platform_set_drvdata(pdev, mux);
	ret = tdvp_radio_mux_select_lora(mux);
	if (ret)
		return ret;

	ret = device_create_file(dev, &dev_attr_profile);
	if (ret)
		return dev_err_probe(dev, ret, "cannot create profile attribute\n");

	dev_info(dev, "active profile: lora\n");
	return 0;
}

static void tdvp_radio_mux_remove(struct platform_device *pdev)
{
	struct tdvp_radio_mux *mux = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_profile);
	mutex_lock(&mux->lock);
	if (mux->profile == TDVP_RADIO_PROFILE_NRF9151)
		gpiod_set_value_cansleep(mux->nrf9151_enable, 0);
	mutex_unlock(&mux->lock);
}

static const struct of_device_id tdvp_radio_mux_of_match[] = {
	{ .compatible = "vicliu,tdvp-radio-mux" },
	{ }
};
MODULE_DEVICE_TABLE(of, tdvp_radio_mux_of_match);

static struct platform_driver tdvp_radio_mux_driver = {
	.probe = tdvp_radio_mux_probe,
	.remove_new = tdvp_radio_mux_remove,
	.driver = {
		.name = "tdvp-radio-mux",
		.of_match_table = tdvp_radio_mux_of_match,
	},
};
module_platform_driver(tdvp_radio_mux_driver);

MODULE_DESCRIPTION("T-Display K230 LoRa/nRF9151 profile selector");
MODULE_LICENSE("GPL");
