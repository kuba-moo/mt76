/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt76.h"
#include "mcu.h"
#include "trace.h"

enum mt76_boards {
	board_mt7630,
	board_mt7662,
};

static const struct mt76_dev_param mt7630_param = {
	.ram_code_protect = true,
	.eeprom_size = 272,
	.ilm_offset = 0,
	.dlm_offset = 0x80000,
	.dlm_addr = 0x80000,
	.fw_trgr_reg = MT_MCU_INT_LEVEL,
	.fw_trgr_val = 3,
	.firmware_name = MT7630_FIRMWARE,
};

static const struct mt76_dev_param mt7662_param = {
	.rom_code_protect = true,
	.eeprom_size = 512,
	.ilm_offset = 0x80000,
	.dlm_offset = 0x110000,
	.dlm_addr = 0x90000,
	.fw_trgr_reg = MT_MCU_INT_LEVEL,
	.fw_trgr_val = 2,
	.firmware_name = MT7662_FIRMWARE,
	.rom_patch_name = MT7662_ROM_PATCH,
};

static const struct mt76_dev_param *mt76_info_tbl[] = {
	[board_mt7630] = &mt7630_param,
	[board_mt7662] = &mt7662_param,
};

static const struct pci_device_id mt76pci_device_table[] = {
	{ PCI_DEVICE(0x14c3, 0x7662), .driver_data = board_mt7662, },
	{ },
};

u32 mt76_rr(struct mt76_dev *dev, u32 offset)
{
	u32 val;

	val = ioread32(dev->regs + offset);
	trace_reg_read(dev, offset, val);

	return val;
}

void mt76_wr(struct mt76_dev *dev, u32 offset, u32 val)
{
	trace_reg_write(dev, offset, val);
	iowrite32(val, dev->regs + offset);
}

u32 mt76_rmw(struct mt76_dev *dev, u32 offset, u32 mask, u32 val)
{
	val |= mt76_rr(dev, offset) & ~mask;
	mt76_wr(dev, offset, val);
	return val;
}

void mt76_wr_copy(struct mt76_dev *dev, u32 offset, const void *data, int len)
{
	__iowrite32_copy(dev->regs + offset, data, len >> 2);
}

static int
mt76pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mt76_dev *dev;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	dev = mt76_alloc_device(&pdev->dev);
	if (!dev)
		return -ENOMEM;

	dev->regs = pcim_iomap_table(pdev)[0];
	dev->param = mt76_info_tbl[id->driver_data];

	pci_set_drvdata(pdev, dev);

	dev->rev = mt76_rr(dev, MT_ASIC_VERSION);
	printk("ASIC revision: %08x\n", dev->rev);

	ret = devm_request_irq(dev->dev, pdev->irq, mt76_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		return ret;

	ret = mt76_register_device(dev);
	if (ret)
		return ret;

	/* Fix up ASPM configuration */

	/* RG_SSUSB_G1_CDR_BIR_LTR = 0x9 */
	mt76_rmw_field(dev, 0x15a10, 0x1f << 16, 0x9);

	/* RG_SSUSB_G1_CDR_BIC_LTR = 0xf */
	mt76_rmw_field(dev, 0x15a0c, 0xf << 28, 0xf);

	/* RG_SSUSB_CDR_BR_PE1D = 0x3 */
	mt76_rmw_field(dev, 0x15c58, 0x3 << 6, 0x3);

	printk("pci device driver attached\n");
	return 0;
}

static void
mt76pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *dev = pci_get_drvdata(pdev);

	ieee80211_unregister_hw(dev->hw);
	mt76_cleanup(dev);
	printk("pci device driver detached\n");
}

MODULE_DEVICE_TABLE(pci, mt76pci_device_table);
MODULE_FIRMWARE(MT7662_FIRMWARE);
MODULE_FIRMWARE(MT7662_ROM_PATCH);
MODULE_LICENSE("GPL");

static struct pci_driver mt76pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76pci_device_table,
	.probe		= mt76pci_probe,
	.remove		= mt76pci_remove,
};

module_pci_driver(mt76pci_driver);
