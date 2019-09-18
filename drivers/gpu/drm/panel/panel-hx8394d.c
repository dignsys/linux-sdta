/*
 * MIPI-DSI based HX8394D LCD panel driver.
 *
 * Copyright (c) 2017 Nexell Co., Ltd
 *
 * Sungwoo Park <swpark@nexell.co.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>
#include <linux/backlight.h>

#define PX0701C_WIDTH_MM		107  /* real: 94.2 */
#define PX0701C_HEIGHT_MM		172 /* real: 150.72 */

#define HFP_ALLO         256
#define HBP_ALLO         158
#define HSA_ALLO         5
#define VFP_ALLO         15
#define VBP_ALLO         10
#define VSA_ALLO         4

#define HACTIVE     800
#define VACTIVE     1280

#define DISPOFF		0x28
#define SLPIN		0x10
#define SLPOUT		0x11
#define DISPON		0x29

struct hx8394d {
	struct device *dev;
	struct drm_panel panel;

	struct regulator_bulk_data supplies[2];
	int reset_gpio;
	int enable_gpio;
	u32 power_on_delay;
	u32 reset_delay;
	u32 init_delay;
	struct videomode vm;
	u32 width_mm;
	u32 height_mm;
	bool is_power_on;

	struct backlight_device *bl_dev;
	int error;
};

static inline struct hx8394d *panel_to_hx8394d(struct drm_panel *panel)
{
	return container_of(panel, struct hx8394d, panel);
}

static int __maybe_unused hx8394d_clear_error(struct hx8394d *ctx)
{
	int ret = ctx->error;

	ctx->error = 0;
	return ret;
}

#ifndef CONFIG_DRM_CHECK_PRE_INIT
static void _dcs_write(struct hx8394d *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	if (ctx->error < 0)
		return;

	ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing dcs seq: %*ph\n", ret,
			(int)len, data);
		ctx->error = ret;
	}
}
#endif

static int __maybe_unused _dcs_read(struct hx8394d *ctx, u8 cmd, void *data,
				    size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (ctx->error < 0)
		return ctx->error;

	ret = mipi_dsi_dcs_read(dsi, cmd, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %d reading dcs seq(%#x)\n", ret, cmd);
		ctx->error = ret;
	}

	return ret;
}
#ifndef CONFIG_DRM_CHECK_PRE_INIT
#define hx8394d_dcs_write_seq(ctx, seq...) \
({ \
	const u8 d[] = { seq }; \
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, \
			 "DCS sequence too big for stack"); \
	_dcs_write(ctx, d, ARRAY_SIZE(d)); \
})

#define hx8394d_dcs_write_seq_static(ctx, seq...) \
({ \
	static const u8 d[] = { seq }; \
	_dcs_write(ctx, d, ARRAY_SIZE(d)); \
})

static void _set_sequence(struct hx8394d *ctx)
{
	/* Datasheet: HX8394-D_DS_v02_150127.pdf */

	static u8 d[24][64] = {
		/*
		{data_length,
		data0,data1,...
		}
		*/

		/* SETEXTC: Set extension command (B9h) */
		{4,
		0xB9,0xFF,0x83,0x94},

		/* SETMIPI: Set MIPI control (BAh) */
		{3,
		0xBA,0x73,0x83},

		/* SETPOWER: Set power related register (B1h) */
		{16,
		0xB1,0x6c,0x12,0x12,0x24,0xe4,0x11,0xF1,0x80,0xE4,
		0xd7,0x23,0x80,0xC0,0xD2,0x58},

		/* SETDISP: Set display related register (B2h) */
		{12,
		0xB2,0x00,0x64,0x10,0x07,0x80,0x1C,0x08,0x08,0x1C,
		0x4D,0x00},

		/* SETCYC: Set display waveform cycles (B4h) */
		{13,
		0xB4,0x00,0xFF,0x03,0x5a,0x03,0x5a,0x03,0x5a,0x01,
		0x6a,0x01,0x6a},

		/* SETGIP_0: Set GIP option 0 (D3h) */
		{31,
		0xD3,0x00,0x06,0x00,0x40,0x1a,0x08,0x00,0x32,0x10,
		0x07,0x00,0x07,0x54,0x15,0x0F,0x05,0x04,0x02,0x12,
		0x10,0x05,0x07,0x33,0x33,0x0B,0x0B,0x37,0x10,0x07,
		0x07},

		/* UNKNOWN Command */
		{45,
		0xD5,0x19,0x19,0x18,0x18,0x1a,0x1a,0x1b,0x1b,0x04,
		0x05,0x06,0x07,0x00,0x01,0x02,0x03,0x20,0x21,0x18,
		0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
		0x18,0x22,0x23,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
		0x18,0x18,0x18,0x18,0x18},

		/* UNKNOWN Command */
		{45,
		0xD6,0x18,0x18,0x19,0x19,0x1A,0x1A,0x1B,0x1B,0x03,
		0x02,0x01,0x00,0x07,0x06,0x05,0x04,0x23,0x22,0x18,
		0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
		0x18,0x21,0x20,0x18,0x18,0x18,0x18,0x18,0x18,0x18,
		0x18,0x18,0x18,0x18,0x18},

		/* SETGAMMA: Set gamma curve related setting (E0h) */
		{43,
		0xE0,0x00,0x02,0x06,0x21,0x25,0x3F,0x17,0x3D,0x06,
		0x09,0x0C,0x17,0x0E,0x12,0x14,0x12,0x13,0x08,0x13,
		0x16,0x18,0x00,0x02,0x06,0x21,0x25,0x3F,0x17,0x3D,
		0x06,0x09,0x0C,0x17,0x0E,0x12,0x14,0x12,0x13,0x08,
		0x13,0x16,0x18},

		/* SET_BANK: Set register bank (BDh) */
		{2,
		0xBD,0x00},

		/* SETDGCLUT: Set DGC LUT (C1h) */
		{43,
		0xC1,0x01,0x00,0x03,0x07,0x0E,0x18,0x22,0x2C,0x35,
		0x3D,0x44,0x4B,0x54,0x5C,0x64,0x6C,0x73,0x7C,0x83,
		0x8B,0x93,0x9A,0xA1,0xA9,0xB0,0xB9,0xBF,0xC9,0xD1,
		0xD8,0xE0,0xE6,0xEF,0xF7,0x16,0x8B,0xB4,0x57,0x10,
		0xFB,0x34,0xAD},

		/* SET_BANK: Set register bank (BDh) */
		{2,
		0xBD,0x01},

		/* SETDGCLUT: Set DGC LUT (C1h) */
		{43,
		0xC1,0x00,0x03,0x06,0x0D,0x16,0x1E,0x28,0x32,0x3A,
		0x41,0x48,0x4E,0x57,0x5F,0x66,0x6D,0x75,0x7C,0x83,
		0x8A,0x91,0x99,0xA1,0xA8,0xAF,0xB5,0xBC,0xC5,0xCD,
		0xD4,0xDA,0xE3,0xE8,0x1F,0x9A,0x51,0x06,0x26,0xF0,
		0x3D,0x5E,0xC0},

		/* SET_BANK: Set register bank (BDh) */
		{2,
		0xBD,0x02},

		/* SETDGCLUT: Set DGC LUT (C1h) */
		{43,
		0xC1,0x00,0x03,0x07,0x0E,0x18,0x20,0x2B,0x34,0x3C,
		0x43,0x4A,0x52,0x5A,0x62,0x69,0x71,0x79,0x80,0x88,
		0x90,0x98,0x9F,0xA6,0xAD,0xB5,0xBC,0xC4,0xCC,0xD4,
		0xDA,0xE2,0xEA,0xF2,0x14,0x72,0x59,0x5E,0x60,0x16,
		0x6A,0x7E,0x40},

		/* SETVCOM: Set VCOM voltage (B6h) */
		{3,
		0xB6,0x3A,0x3A},

		/* SETPANEL: Set panel related register (CCh) */
		{2,
		0xCC,0x09},

		/* SETOFFSET (D2H) */
		{2,
		0xD2,0x55},

		/* SETSTBA: Set source option (C0h) */
		{3,
		0xC0,0x30,0x14},

		/* SETVDC: Set internal digital voltage (BCh) */
		{2,
		0xBC,0x07},

		/* SETPTBA: Set power option (BFh) */
		{4,
		0xBF,0x41,0x0E,0x01},

		/* SETTCONOPT: Set TCON option (C7h) */
		{5,
		0xC7,0x00,0xC0,0x40,0xC0},

		/* SETCEMODE: Set color enhancement mode (E4h) */
		{3,
		0xE4,0x02,0x01},

		/* UNKNOWN Command */
		{2,
		0xDF,0x8E},
	};
	int i, ret;
	u8 *pd;

	hx8394d_dcs_write_seq(ctx, DISPOFF);
	msleep(150);

	hx8394d_dcs_write_seq(ctx, SLPIN);
	msleep(150);

	for (i = 0; i < 24; ++i)
	{
		pd = &d[i][0];
		_dcs_write(ctx, pd + 1, pd[0]);
	}

	hx8394d_dcs_write_seq(ctx, SLPOUT);
	msleep(150);

	hx8394d_dcs_write_seq(ctx, DISPON);
	msleep(150);

	return 0;
}
#endif

static int hx8394d_power_on(struct hx8394d *ctx)
{
#ifndef CONFIG_DRM_CHECK_PRE_INIT
	int ret;

	if (ctx->is_power_on)
		return 0;

	gpio_direction_output(ctx->reset_gpio, 1);
	gpio_set_value(ctx->reset_gpio, 1);

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	msleep(ctx->power_on_delay);
	gpio_set_value(ctx->reset_gpio, 0);
	msleep(ctx->reset_delay);
	gpio_set_value(ctx->reset_gpio, 1);
	msleep(ctx->init_delay);
#endif
	ctx->is_power_on = true;

	return 0;
}

static int hx8394d_power_off(struct hx8394d *ctx)
{
	if (!ctx->is_power_on)
		return 0;

	/* gpio_set_value(ctx->reset_gpio, 0); */
	/* usleep_range(5000, 6000); */
        /*  */
	/* regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies); */
	/* ctx->is_power_on = false; */

	return 0;
}

static int hx8394d_enable(struct drm_panel *panel)
{

	struct hx8394d *ctx = panel_to_hx8394d(panel);

	if (ctx->bl_dev) {
		ctx->bl_dev->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->bl_dev);
	}

#if 0
	if (ctx->enable_gpio > 0) {
		gpio_direction_output(ctx->enable_gpio, 1);
		gpio_set_value(ctx->enable_gpio, 1);
	}
#endif

	return 0;
}

static int hx8394d_disable(struct drm_panel *panel)
{
	struct hx8394d *ctx = panel_to_hx8394d(panel);

	if (ctx->bl_dev) {
		ctx->bl_dev->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->bl_dev);
	}

#if 0
	if (ctx->enable_gpio > 0)
		gpio_set_value(ctx->enable_gpio, 0);
#endif

	return 0;
}

static int hx8394d_prepare(struct drm_panel *panel)
{
	struct hx8394d *ctx = panel_to_hx8394d(panel);
	int ret;

	ret = hx8394d_power_on(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to power on\n");
		return ret;
	}

#ifndef CONFIG_DRM_CHECK_PRE_INIT
	_set_sequence(ctx);
	ret = ctx->error;
	if (ret < 0) {
		dev_err(ctx->dev, "failed to set_sequence\n");
		return ret;
	}
#endif

	return 0;
}

static int hx8394d_unprepare(struct drm_panel *panel)
{
	struct hx8394d *ctx = panel_to_hx8394d(panel);

	return hx8394d_power_off(ctx);
}

static const struct drm_display_mode default_mode = {
	.clock = (int)(((HACTIVE + HBP_ALLO + HFP_ALLO + HSA_ALLO) *
				(VACTIVE + VBP_ALLO + VFP_ALLO + VSA_ALLO) * 60)/1000),
	.hdisplay = HACTIVE,
	.hsync_start = HACTIVE + HBP_ALLO,	/* hactive + hbackporch */
	.hsync_end = HACTIVE + HBP_ALLO + HSA_ALLO,	/* hsync_start + hsyncwidth */
	.htotal = HACTIVE + HBP_ALLO + HSA_ALLO + HFP_ALLO,	/* hsync_end + hfrontporch */
	.vdisplay = VACTIVE,
	.vsync_start = VACTIVE + VBP_ALLO,	/* vactive + vbackporch */
	.vsync_end = VACTIVE + VBP_ALLO + VSA_ALLO,	/* vsync_start + vsyncwidth */
	.vtotal = VACTIVE + VBP_ALLO + VSA_ALLO + VFP_ALLO,	/* vsync_end + vfrontporch */
	.vrefresh = 60,			/* Hz */
};

/**
 * HACK
 * return value
 * 1: success
 * 0: failure
 */
static int hx8394d_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct hx8394d *ctx = panel_to_hx8394d(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);

	if (!mode) {
		DRM_ERROR("failed to create a new display mode\n");
		return 0;
	}

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	drm_mode_set_name(mode);
	mode->width_mm = ctx->width_mm;
	mode->height_mm = ctx->height_mm;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs hx8394d_drm_funcs = {
	.enable = hx8394d_enable,
	.disable = hx8394d_disable,
	.prepare = hx8394d_prepare,
	.unprepare = hx8394d_unprepare,
	.get_modes = hx8394d_get_modes,
};

static int hx8394d_parse_dt(struct hx8394d *ctx)
{
	struct device *dev = ctx->dev;
	struct device_node *np = dev->of_node;

	of_property_read_u32(np, "power-on-delay", &ctx->power_on_delay);
	of_property_read_u32(np, "reset-delay", &ctx->reset_delay);
	of_property_read_u32(np, "init-delay", &ctx->init_delay);

	return 0;
}

static int hx8394d_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *backlight;
	struct hx8394d *ctx;
	int ret;

#if 0
	if (!drm_panel_connected("hx8394d"))
		return -ENODEV;
#endif

	ctx = devm_kzalloc(dev, sizeof(struct hx8394d), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	ctx->is_power_on = false;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO
		| MIPI_DSI_MODE_VIDEO_HFP
		| MIPI_DSI_MODE_VIDEO_HBP
		| MIPI_DSI_MODE_VIDEO_HSA
		| MIPI_DSI_MODE_VSYNC_FLUSH
		;

	ret = hx8394d_parse_dt(ctx);
	if (ret)
		return ret;

	ctx->supplies[0].supply = "vci";
	ctx->supplies[1].supply = "vdd3";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		dev_warn(dev, "failed to get regulators: %d\n", ret);

	ctx->reset_gpio = of_get_named_gpio(dev->of_node, "reset-gpio", 0);
	if (ctx->reset_gpio < 0) {
		dev_err(dev, "cannot get reset-gpio %d\n", ctx->reset_gpio);
		return -EINVAL;
	}

	ret = devm_gpio_request(dev, ctx->reset_gpio, "reset-gpio");
	if (ret) {
		dev_err(dev, "failed to request reset-gpio\n");
		return ret;
	}

#if 0
	ctx->enable_gpio = of_get_named_gpio(dev->of_node, "enable-gpio", 0);
	if (ctx->enable_gpio < 0)
		dev_warn(dev, "cannot get enable-gpio %d\n", ctx->enable_gpio);
	else {
		ret = devm_gpio_request(dev, ctx->enable_gpio, "enable-gpio");
		if (ret) {
			dev_err(dev, "failed to request enable-gpio\n");
			return ret;
		}
	}
#endif

#if 0
	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->bl_dev = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->bl_dev)
			return -EPROBE_DEFER;
	}
#endif

	ctx->width_mm = PX0701C_WIDTH_MM;
	ctx->height_mm = PX0701C_HEIGHT_MM;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &hx8394d_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0) {
		backlight_device_unregister(ctx->bl_dev);
		return ret;
	}

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		backlight_device_unregister(ctx->bl_dev);
		drm_panel_remove(&ctx->panel);
	}
	return ret;
}

static int hx8394d_remove(struct mipi_dsi_device *dsi)
{
	struct hx8394d *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
	backlight_device_unregister(ctx->bl_dev);
	hx8394d_power_off(ctx);

	return 0;
}

static void hx8394d_shutdown(struct mipi_dsi_device *dsi)
{
	struct hx8394d *ctx = mipi_dsi_get_drvdata(dsi);

	hx8394d_power_off(ctx);
}

static const struct of_device_id hx8394d_of_match[] = {
	{ .compatible = "hx8394d" },
	{ }
};
MODULE_DEVICE_TABLE(of, hx8394d_of_match);

static struct mipi_dsi_driver hx8394d_driver = {
	.probe = hx8394d_probe,
	.remove = hx8394d_remove,
	.shutdown = hx8394d_shutdown,
	.driver = {
		.name = "panel-hx8394d",
		.of_match_table = hx8394d_of_match,
	},
};

module_mipi_dsi_driver(hx8394d_driver);

MODULE_DESCRIPTION("MIPI-SDI based hx8394d series LCD Panel Driver");
MODULE_AUTHOR("Hackseung Lee <lhs@dignsys.com>");
MODULE_LICENSE("GPL v2");
