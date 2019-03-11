/*
 * Diag Lock Driver for LGE
 *
 * Copyright (C) 2016 LG Electronics Inc. All rights reserved.
 * Author : Hansun Lee <hansun.lee@lge.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include "diag_lock.h"

#include <soc/qcom/lge/board_lge.h>
#if defined(CONFIG_LGE_USB_DIAG_LOCK_SPR) || defined(CONFIG_LGE_ONE_BINARY_SKU)
#include <soc/qcom/smem.h>
#endif

static diag_lock_state_t diag_lock_state = DIAG_LOCK_STATE_LOCK;

#ifndef CONFIG_LGE_ONE_BINARY_SKU
static unsigned char allowed_commands[] = {
#ifdef CONFIG_LGE_USB_DIAG_LOCK_SPR
	0xA1, // port lock
#else
	0x29, // testmode reset
	0x3A, // dload reset
	0x7E, // async hdlc flag
	0xA1, // port lock
	0xEF, // web download
#ifdef CONFIG_LGE_USB_DIAG_LOCK_VZW
	0xF8, // vzw at lock
#endif
	0xFA, // testmode
#endif
};
#endif

#if defined(CONFIG_LGE_USB_DIAG_LOCK_SPR) || defined(CONFIG_LGE_ONE_BINARY_SKU)
static diag_lock_state_t get_diag_lock_state_from_smem(void)
{
	struct _smem_id_vendor0 {
		uint32_t sku_rev;
		uint32_t hw_rev;
		uint32_t sub_rev;
		uint32_t vari_mem;
		uint32_t vari_main;
		uint32_t ant_rev;
		char model_name[16];
		char sw_version[64];
		char operator_name[16];
		char ntcode[2048];
		char lg_modem_name[20];
		unsigned int sim_num;
		uint32_t flag_gpio; /* flex gpio */
		char diag_enable;
	} *smem_id_vendor0 = NULL;
	unsigned int size;

	smem_id_vendor0 = smem_get_entry(SMEM_ID_VENDOR0, &size, 0, 0);
	if (IS_ERR_OR_NULL(smem_id_vendor0))
		return 0;

	WARN(sizeof(struct _smem_id_vendor0) != size,
	     "size of smem_id_vendor0 is not correct. (%lu/%d)\n",
	     sizeof(struct _smem_id_vendor0), size);

	pr_info("%s: diag_enable(%d)\n", __func__,
		smem_id_vendor0->diag_enable);

	return smem_id_vendor0->diag_enable ?
		DIAG_LOCK_STATE_UNLOCK : DIAG_LOCK_STATE_LOCK;
}
#endif

bool diag_lock_is_allowed(void)
{
#if defined(CONFIG_LGE_USB_FACTORY) && !defined(CONFIG_LGE_USB_DIAG_LOCK_SPR)
	if (lge_get_factory_boot() && lge_get_laop_operator()!= OP_SPR_US)
		return true;
#endif
	return diag_lock_state == DIAG_LOCK_STATE_UNLOCK;
}
EXPORT_SYMBOL(diag_lock_is_allowed);

bool diag_lock_is_allowed_command(const unsigned char *buf)
{
#ifndef CONFIG_LGE_ONE_BINARY_SKU
	int i;
#else
	int opcode = -1;
#endif


	if (!buf)
		return false;

	if (diag_lock_is_allowed())
		return true;

#ifdef CONFIG_LGE_ONE_BINARY_SKU
	opcode = lge_get_laop_operator();
	pr_info("%s: opcode = %d\n",__func__, opcode);
	switch(opcode)
	{
		case OP_SPR_US:
			switch(buf[0])
			{
				case 0xA1:	//port lock
					return true;
				default:
					break;
			}
			break;

		case OP_VZW_POSTPAID:
		case OP_VZW_PREPAID:
			switch(buf[0])
			{
				case 0xF8:	//vzw at lock
					return true;
			}
		default:
			switch(buf[0])
			{
				case 0x29: //testmode reset
				case 0x3A: //dload reset
				case 0x7E: //async hdlc flag
				case 0xA1: //port lock
				case 0xEF: //web download
				case 0xFA: //testmode
					return true;
				default:
					break;
			}
			break;
	}
#else
	for (i = 0; i < sizeof(allowed_commands); i++) {
		if (allowed_commands[i] == buf[0])
			return true;
	}
#endif

	return false;
}
EXPORT_SYMBOL(diag_lock_is_allowed_command);

#if defined(CONFIG_LGE_USB_DIAG_LOCK_SPR) || defined(CONFIG_LGE_ONE_BINARY_SKU)
void diag_lock_set_allowed(bool allowed)
{
	diag_lock_state = allowed;
}
#endif

static ssize_t diag_enable_show(struct device *pdev,
				struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d",
			diag_lock_state == DIAG_LOCK_STATE_UNLOCK);
}

static ssize_t diag_enable_store(struct device *pdev,
				 struct device_attribute *attr,
				 const char *buff, size_t size)
{
	int enabled = 0;

	if (sscanf(buff, "%d", &enabled) != 1)
		return -EINVAL;

	diag_lock_state = !!enabled ?
		DIAG_LOCK_STATE_UNLOCK : DIAG_LOCK_STATE_LOCK;

	return size;
}

static DEVICE_ATTR(diag_enable, S_IRUGO | S_IWUSR,
		   diag_enable_show, diag_enable_store);

static int diag_lock_probe(struct platform_device *pdev)
{
	return device_create_file(&pdev->dev, &dev_attr_diag_enable);
}

static struct platform_driver diag_lock_driver = {
	.probe          = diag_lock_probe,
	.driver         = {
		.name = "lg_diag_cmd",
		.owner  = THIS_MODULE,
	},
};

static struct platform_device diag_lock_device = {
	.name = "lg_diag_cmd",
	.id = -1,
};

static int __init diag_lock_init(void)
{
	int rc;

#if defined(CONFIG_LGE_ONE_BINARY_SKU)
	if (lge_get_laop_operator() == OP_SPR_US)
	{
		pr_info("%s: LAOP SPR get_diag_lock_state_from_smem[%d]\n",__func__, diag_lock_state);
		diag_lock_state = get_diag_lock_state_from_smem();
	}
#elif defined(CONFIG_LGE_USB_DIAG_LOCK_SPR)
	diag_lock_state = get_diag_lock_state_from_smem();
#endif

	rc = platform_device_register(&diag_lock_device);
	if (rc) {
		pr_err("%s: platform_device_register fail\n", __func__);
		return rc;
	}

	rc = platform_driver_register(&diag_lock_driver);
	if (rc) {
		pr_err("%s: platform_driver_register fail\n", __func__);
		platform_device_unregister(&diag_lock_device);
		return rc;
	}

	return 0;
}
module_init(diag_lock_init);

MODULE_DESCRIPTION("LGE DIAG LOCK");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Hansun Lee <hansun.lee@lge.com>");
