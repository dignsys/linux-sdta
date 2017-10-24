/*
 * Reboot Notifier for Tizen ARTIK
 *
 * Copyright (C) 2017, Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#define ALIVE_BASE_ADDR	0xC0010800
#define SCRATCHRSTREG7	0xF4
#define SCRATCHSETREG7	0xF8
#define SCRATCHREADREG7	0XFC

#define REBOOT_MODE_PREFIX	0x12345670
#define REBOOT_MODE_NONE	0
#define REBOOT_MODE_DOWNLOAD	1
#define REBOOT_MODE_RECOVERY	2
#define REBOOT_MODE_FOTA	3

static int tizen_artik_reboot_notify(struct notifier_block *this,
		unsigned long code, void *unused)
{
	char *cmd = (char *)unused;
	static void __iomem *base;
	u32 val = REBOOT_MODE_PREFIX;

	base = ioremap(ALIVE_BASE_ADDR, 0x100);
	if (unlikely(!base))
		return NOTIFY_BAD;

	/*
	 * In order to maintain the reboot value written in SCRATCH7 register,
	 * set all bits of RST register as 1 before setting the reboot value.
	 */
	__raw_writel(0xFFFFFFFF, base + SCRATCHRSTREG7);

	if (cmd && !strncmp(cmd, "download", 8))
		val |= REBOOT_MODE_DOWNLOAD;
	else if (cmd && !strncmp(cmd, "recovery", 8))
		val |= REBOOT_MODE_RECOVERY;
	else if (cmd && !strncmp(cmd, "fota", 4))
		val |= REBOOT_MODE_FOTA;
	else
		val |= REBOOT_MODE_NONE;

	__raw_writel(val, base + SCRATCHSETREG7);

	return NOTIFY_DONE;
}

static struct notifier_block tizen_artik_reboot_handler = {
	.notifier_call = tizen_artik_reboot_notify,
	.priority = 128,
};

static int __init tizen_artik_reboot_init(void)
{
	int ret;

	ret = register_reboot_notifier(&tizen_artik_reboot_handler);
	if (ret)
		printk("%s is failed = %d\n",__func__, ret);

	return ret;
}
subsys_initcall(tizen_artik_reboot_init);
