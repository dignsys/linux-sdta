/*
 * Himax hx8394d panel driver.
 *
 * Copyright (C) 2019 Dignsys
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#define WRDISBV		0x51
#define WRCTRLD		0x53
#define WRCABC		0x55
#define SETPOWER	0xb1
#define SETDISP		0xb2
#define SETCYC		0xb4
#define SETVCOM		0xb6
#define SETEXTC		0xb9
#define SETMIPI		0xba
#define SETPANEL	0xcc
#define SETGIP		0xd5
#define SETGAMMA	0xe0
#define DISPOFF		0x28
#define SLPIN		0x10
#define SLPOUT		0x11
#define DISPON		0x29

#define H_ACTIVE     		800
#define V_ACTIVE     		1280
#define HS_LOW_PULSE_WIDTH	8
#define H_BACK_PORCH		30
#define H_FRONT_PORCH		10
#define VS_LOW_PULSE_WIDTH	2
#define V_BACK_PORCH		2
#define V_FRONT_PORCH		2
#define VREFRESH			60
#define WIDTH_MM			107
#define HEIGHT_MM			172

#define HX8394D_MIN_BRIGHTNESS	0x00
#define HX8394D_MAX_BRIGHTNESS	0xff

struct hx8394d_panel_desc {
	const struct drm_display_mode *mode;
};

struct hx8394d {
	struct device *dev;
	struct drm_panel panel;

	const struct hx8394d_panel_desc *pd;
	int reset_gpio;
	bool is_power_on;
	bool is_prepared;
	int error;
};

static inline struct hx8394d *panel_to_hx8394d(struct drm_panel *panel)
{
	return container_of(panel, struct hx8394d, panel);
}

static int hx8394d_dcs_write(struct hx8394d *ctx, const char *func,
			     const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;

	ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	ctx->error = ret;

	if (ret < 0)
	{
		dev_err(ctx->dev, "%s failed: %d\n", func, ret);
	}

	return ret;
}

#define hx8394d_dcs_write_seq(ctx, seq...) \
({ \
	const u8 d[] = { seq }; \
	BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64, \
			 "DCS sequence too big for stack"); \
	hx8394d_dcs_write(ctx, __func__, d, ARRAY_SIZE(d)); \
})

#define hx8394d_dcs_write_seq_static(ctx, seq...) \
({ \
	static const u8 d[] = { seq }; \
	hx8394d_dcs_write(ctx, __func__, d, ARRAY_SIZE(d)); \
})

static void hx8394d_dsi_write_display_brightness(struct hx8394d *ctx,
						u8 brightness)
{
	hx8394d_dcs_write_seq(ctx, WRDISBV, brightness);
}

static int hx8394d_tft_mipi_init(struct hx8394d *ctx)
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
		ret = hx8394d_dcs_write(ctx, __func__, pd + 1, pd[0]);
		if (ret)
			return ret;
	}

	hx8394d_dcs_write_seq(ctx, SLPOUT);
	msleep(150);

	hx8394d_dcs_write_seq(ctx, DISPON);
	msleep(150);

	return 0;
}

static int hx8394d_dsi_panel_init(struct hx8394d *ctx)
{
	int (*funcs[])(struct hx8394d *ctx) = {
		hx8394d_tft_mipi_init,
	};
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(funcs); ++i) {
		ret = funcs[i](ctx);
		if (ret)
			return ret;
	}

	return 0;
}

static int hx8394d_power_on(struct hx8394d *ctx)
{
	if (ctx->is_power_on)
		return 0;

#ifndef CONFIG_DRM_CHECK_PRE_INIT
printk("### HBAHN[%s:%s:%d] : \n",__FILE__,__FUNCTION__, __LINE__);
	gpio_direction_output(ctx->reset_gpio, 1);
	msleep(10);
	gpio_set_value(ctx->reset_gpio, 0);
	msleep(20);
	gpio_set_value(ctx->reset_gpio, 1);

	/* The controller needs 120ms to recover from reset */
	msleep(120);
#endif
	ctx->is_power_on = true;

	return 0;
}

#ifndef CONFIG_DRM_CHECK_PRE_INIT
static int hx8394d_dsi_set_sequence(struct hx8394d *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

printk("### HBAHN[%s:%s:%d] : \n",__FILE__,__FUNCTION__, __LINE__);
	ret = hx8394d_power_on(ctx);
	if (ret < 0)
		goto out;

	ret = hx8394d_dsi_panel_init(ctx);
	if (ret < 0)
		goto out;
out:
	return ret;
}
#else
static int hx8394d_dsi_set_sequence(struct hx8394d *ctx)
{
	return 0;
}
#endif /* CONFIG_DRM_CHECK_PRE_INIT */

static int hx8394d_dsi_disable(struct drm_panel *panel)
{
	struct hx8394d *ctx = panel_to_hx8394d(panel);

printk("### HBAHN[%s:%s:%d] : \n",__FILE__,__FUNCTION__, __LINE__);
	hx8394d_dcs_write_seq(ctx, DISPOFF);
	if (ctx->error < 0)
		return ctx->error;
	msleep(150);

	hx8394d_dcs_write_seq(ctx, SLPIN);
	if (ctx->error < 0)
		return ctx->error;
	msleep(150);

	return 0;
}

static int hx8394d_power_off(struct hx8394d *ctx)
{
	if (!ctx->is_power_on)
		return 0;

#ifndef CONFIG_DRM_CHECK_PRE_INIT
	gpio_set_value(ctx->reset_gpio, 0);
#endif
	ctx->is_power_on = false;

	return 0;
}

static int hx8394d_dsi_unprepare(struct drm_panel *panel)
{
	struct hx8394d *ctx = panel_to_hx8394d(panel);
	int ret;

	if (!ctx->is_prepared)
		return 0;

	ret = hx8394d_power_off(ctx);
	ctx->is_prepared = false;

	return ret;
}

static int hx8394d_dsi_prepare(struct drm_panel *panel)
{
	struct hx8394d *ctx = panel_to_hx8394d(panel);

	if (ctx->is_prepared)
		return 0;

	ctx->is_prepared = true;
	hx8394d_dsi_set_sequence(ctx);

	return ctx->error;
}

static int hx8394d_dsi_enable(struct drm_panel *panel)
{
	struct hx8394d *ctx = panel_to_hx8394d(panel);

printk("### HBAHN[%s:%s:%d] : \n",__FILE__,__FUNCTION__, __LINE__);
	hx8394d_dcs_write_seq(ctx, SLPOUT);
	if (ctx->error < 0)
		return ctx->error;
	msleep(150);

	hx8394d_dcs_write_seq(ctx, DISPON);
	if (ctx->error < 0)
		return ctx->error;
	msleep(150);

	return 0;
}

static int hx8394d_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct drm_device *drm = panel->drm;
	struct hx8394d *ctx = panel_to_hx8394d(panel);
	struct drm_display_mode *mode;
	const struct drm_display_mode *m = ctx->pd->mode;

	mode = drm_mode_duplicate(drm, m);
	if (!mode) {
		dev_err(drm->dev, "failed to add mode %ux%u@%u\n",
			m->hdisplay, m->vdisplay, m->vrefresh);
		return 0;
	}

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs hx8394d_dsi_drm_funcs = {
	.disable	= hx8394d_dsi_disable,
	.unprepare	= hx8394d_dsi_unprepare,
	.prepare	= hx8394d_dsi_prepare,
	.enable		= hx8394d_dsi_enable,
	.get_modes	= hx8394d_get_modes,
};

static const struct drm_display_mode avdtt80wxcn039a_mode = {
	.clock			= (int)(((H_ACTIVE + H_BACK_PORCH + H_FRONT_PORCH + HS_LOW_PULSE_WIDTH) *
						(V_ACTIVE + V_BACK_PORCH + V_FRONT_PORCH + VS_LOW_PULSE_WIDTH) * VREFRESH) / 1000),

	.hdisplay		= H_ACTIVE,
	.hsync_start	= H_ACTIVE + H_BACK_PORCH,
	.hsync_end		= H_ACTIVE + H_BACK_PORCH + HS_LOW_PULSE_WIDTH,
	.htotal			= H_ACTIVE + H_BACK_PORCH + HS_LOW_PULSE_WIDTH + H_FRONT_PORCH,

	.vdisplay		= V_ACTIVE,
	.vsync_start	= V_ACTIVE + V_BACK_PORCH,
	.vsync_end		= V_ACTIVE + V_BACK_PORCH + VS_LOW_PULSE_WIDTH,
	.vtotal			= V_ACTIVE + V_BACK_PORCH + VS_LOW_PULSE_WIDTH + V_FRONT_PORCH,

	.vrefresh		= VREFRESH, /* Hz */

	.width_mm		= WIDTH_MM,
	.height_mm		= HEIGHT_MM,
};

static const struct hx8394d_panel_desc avdtt80wxcn039a_dsi = {
	.mode = &avdtt80wxcn039a_mode,
};

static const struct of_device_id hx8394d_dsi_of_match[] = {
	{
		.compatible = "shenzhen,avdtt80wxcn039a",
		.data = &avdtt80wxcn039a_dsi,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, hx8394d_dsi_of_match);

static int hx8394d_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct of_device_id *of_id = of_match_device(hx8394d_dsi_of_match, dev);
	struct hx8394d *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	if (of_id) {
		ctx->pd = of_id->data;
	} else {
		dev_err(dev, "cannot find compatible device\n");
		return -ENODEV;
	}

	ctx->is_power_on = false;
	ctx->is_prepared = false;
	ctx->reset_gpio = of_get_named_gpio(dev->of_node, "reset-gpio", 0);
	if (ctx->reset_gpio < 0) {
		dev_info(dev, "cannot get reset-gpios %d\n", ctx->reset_gpio);
		return ctx->reset_gpio;
	}

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &hx8394d_dsi_drm_funcs;

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO
						| MIPI_DSI_MODE_VIDEO_HFP
						| MIPI_DSI_MODE_VIDEO_HBP
						| MIPI_DSI_MODE_VIDEO_HSA
						| MIPI_DSI_MODE_VSYNC_FLUSH;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

	return ret;
}

static int hx8394d_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct hx8394d *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return hx8394d_power_off(ctx);
}

static struct mipi_dsi_driver hx8394d_dsi_driver = {
	.probe = hx8394d_dsi_probe,
	.remove = hx8394d_dsi_remove,
	.driver = {
		.name = "panel-hx8394d-dsi",
		.of_match_table = hx8394d_dsi_of_match,
	},
};
module_mipi_dsi_driver(hx8394d_dsi_driver);

MODULE_DESCRIPTION("AVD-TT80WX-CN-039-A driver");
MODULE_AUTHOR("Hackseung Lee <lhs@dignsys.com>");
MODULE_LICENSE("GPL v2");

