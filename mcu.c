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
#include <linux/firmware.h>
#include <linux/delay.h>

#include "mt76.h"
#include "mcu.h"
#include "dma.h"
#include "eeprom.h"

struct mt76_fw_header {
	__le32 ilm_len;
	__le32 dlm_len;
	__le16 build_ver;
	__le16 fw_ver;
	u8 pad[4];
	char build_time[16];
};

struct mt76_patch_header {
	char build_time[16];
	char platform[4];
	char hw_version[4];
	char patch_version[4];
	u8 pad[2];
};

static struct sk_buff *
mt76_mcu_msg_alloc(struct mt76_dev *dev, const void *data, int len)
{
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_KERNEL);
	memcpy(skb_put(skb, len), data, len);

	return skb;
}

static struct sk_buff *
mt76_mcu_get_response(struct mt76_dev *dev, unsigned long expires)
{
	unsigned long timeout;

	if (!time_is_after_jiffies(expires))
		return NULL;

	timeout = expires - jiffies;
	wait_event_timeout(dev->mcu.wait, !skb_queue_empty(&dev->mcu.res_q),
			   timeout);
	return skb_dequeue(&dev->mcu.res_q);
}

static int
mt76_mcu_msg_send(struct mt76_dev *dev, struct sk_buff *skb, enum mcu_cmd cmd)
{
	unsigned long expires = jiffies + HZ;
	u32 info;
	int ret;
	u8 seq;

	if (!skb)
		return -EINVAL;

	mutex_lock(&dev->mcu.mutex);

	seq = ++dev->mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mcu.msg_seq & 0xf;

	info = MT_MCU_MSG_TYPE_CMD |
	       MT76_SET(MT_MCU_MSG_CMD_TYPE, cmd) |
	       MT76_SET(MT_MCU_MSG_CMD_SEQ, seq) |
	       MT76_SET(MT_MCU_MSG_PORT, CPU_TX_PORT) |
	       MT76_SET(MT_MCU_MSG_LEN, skb->len);

	ret = __mt76_tx_queue_skb(dev, MT_TXQ_MCU, skb, info);
	if (ret)
		goto out;

	while (1) {
		u32 *rxfce;
		bool check_seq = false;

		skb = mt76_mcu_get_response(dev, expires);
		if (!skb) {
			printk("MCU message %d (seq %d) timed out\n", cmd,
			       MT76_GET(MT_MCU_MSG_CMD_SEQ, info));
			ret = -ETIMEDOUT;
			break;
		}

		rxfce = (u32 *) skb->cb;

		if (seq == MT76_GET(MT_RX_FCE_INFO_CMD_SEQ, *rxfce))
			check_seq = true;

		dev_kfree_skb(skb);
		if (check_seq)
			break;
	}

out:
	mutex_unlock(&dev->mcu.mutex);

	return ret;
}

static void
write_data(struct mt76_dev *dev, u32 offset, __le32 *data, int len)
{
	__iowrite32_copy(dev->regs + offset, data, len / 4);
}

static int
mt76pci_load_rom_patch(struct mt76_dev *dev)
{
	const struct firmware *fw = NULL;
	struct mt76_patch_header *hdr;
	int len, ret = 0;
	__le32 *cur;
	u32 patch_mask, patch_reg;

	if (!mt76_poll(dev, MT_MCU_SEMAPHORE_03, 1, 1, 600)) {
		printk("Could not get hardware semaphore for ROM PATCH\n");
		return -ETIMEDOUT;
	}

	if (mt76xx_rev(dev) >= MT76XX_REV_E3) {
		patch_mask = BIT(0);
		patch_reg = MT_MCU_CLOCK_CTL;
	} else {
		patch_mask = BIT(1);
		patch_reg = MT_MCU_COM_REG0;
	}

	if (mt76_rr(dev, patch_reg) & patch_mask) {
		printk("ROM patch already applied\n");
		goto out;
	}

	ret = request_firmware(&fw, MT7662_ROM_PATCH, dev->dev);
	if (ret)
		goto out;

	if (!fw || !fw->data || fw->size <= sizeof(*hdr)) {
		ret = -EIO;
		printk("Failed to load firmware\n");
		goto out;
	}

	hdr = (struct mt76_patch_header *) fw->data;
	printk("ROM patch build: %.15s\n", hdr->build_time);

	mt76_wr(dev, MT_MCU_PCIE_REMAP_BASE4, MT_MCU_ROM_PATCH_OFFSET);

	cur = (__le32 *) (fw->data + sizeof(*hdr));
	len = fw->size - sizeof(*hdr);
	write_data(dev, MT_MCU_ROM_PATCH_ADDR, cur, len);

	mt76_wr(dev, MT_MCU_PCIE_REMAP_BASE4, 0);

	/* Trigger ROM */
	mt76_wr(dev, MT_MCU_INT_LEVEL, 4);

	if (!mt76_poll_msec(dev, patch_reg, patch_mask, patch_mask, 2000)) {
		printk("Failed to load ROM patch\n");
		ret = -ETIMEDOUT;
	}

out:
	/* release semaphore */
	mt76_wr(dev, MT_MCU_SEMAPHORE_03, 1);
	release_firmware(fw);
	return ret;
}

static int
mt76pci_load_firmware(struct mt76_dev *dev)
{
	const struct firmware *fw;
	const struct mt76_fw_header *hdr;
	int i, len, ret;
	__le32 *cur;
	u32 offset, val;

	ret = request_firmware(&fw, MT7662_FIRMWARE, dev->dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr))
		goto error;

	hdr = (const struct mt76_fw_header *) fw->data;

	len = sizeof(*hdr);
	len += le32_to_cpu(hdr->ilm_len);
	len += le32_to_cpu(hdr->dlm_len);

	if (fw->size != len)
		goto error;

	val = le16_to_cpu(hdr->fw_ver);
	printk("Firmware Version: %d.%d.%02d\n",
		(val >> 12) & 0xf, (val >> 8) & 0xf, val & 0xf);

	val = le16_to_cpu(hdr->build_ver);
	printk("Build: %x\n", val);
	printk("Build Time: %.16s\n", hdr->build_time);

	cur = (__le32 *) (fw->data + sizeof(*hdr));
	len = le32_to_cpu(hdr->ilm_len);

	mt76_wr(dev, MT_MCU_PCIE_REMAP_BASE4, MT_MCU_ILM_OFFSET);
	write_data(dev, MT_MCU_ILM_ADDR, cur, len);

	cur += len / sizeof(*cur);
	len = le32_to_cpu(hdr->dlm_len);

	if (mt76xx_rev(dev) >= MT76XX_REV_E3)
		offset = MT_MCU_DLM_ADDR_E3;
	else
		offset = MT_MCU_DLM_ADDR;

	mt76_wr(dev, MT_MCU_PCIE_REMAP_BASE4, MT_MCU_DLM_OFFSET);
	write_data(dev, offset, cur, len);

	mt76_wr(dev, MT_MCU_PCIE_REMAP_BASE4, 0);

	/* trigger firmware */
	mt76_wr(dev, MT_MCU_INT_LEVEL, 2);
	for (i = 200; i > 0; i--) {
		val = mt76_rr(dev, MT_MCU_COM_REG0);

		if (val & 1)
			break;

		msleep(10);
	}

	if (!i) {
		printk("Firmware failed to start\n");
		release_firmware(fw);
		return -ETIMEDOUT;
	}

	printk("Firmware running!\n");

	release_firmware(fw);

	return ret;

error:
	printk("Invalid firmware\n");
	release_firmware(fw);
	return -ENOENT;
}

static int
mt76_mcu_function_select(struct mt76_dev *dev, enum mcu_function func, u32 val)
{
	struct sk_buff *skb;
	struct {
	    __le32 id;
	    __le32 value;
	} __packed __aligned(4) msg = {
	    .id = cpu_to_le32(func),
	    .value = cpu_to_le32(val),
	};

	skb = mt76_mcu_msg_alloc(dev, &msg, sizeof(msg));
	return mt76_mcu_msg_send(dev, skb, CMD_FUN_SET_OP);
}

int mt76_mcu_load_cr(struct mt76_dev *dev, u8 type, u8 temp_level, u8 channel)
{
	struct sk_buff *skb;
	struct {
		u8 cr_mode;
		u8 temp;
		u8 ch;
		u8 _pad0;

		__le32 cfg;
	} __packed __aligned(4) msg = {
		.cr_mode = type,
		.temp = temp_level,
		.ch = channel,
	};
	u32 val;

	val = BIT(31);
	val |= (mt76_eeprom_get(dev, MT_EE_NIC_CONF_0) >> 8) & 0x00ff;
	val |= (mt76_eeprom_get(dev, MT_EE_NIC_CONF_1) << 8) & 0xff00;
	msg.cfg = cpu_to_le32(val);

	/* first set the channel without the extension channel info */
	skb = mt76_mcu_msg_alloc(dev, &msg, sizeof(msg));
	return mt76_mcu_msg_send(dev, skb, CMD_LOAD_CR);
}

int mt76_mcu_set_channel(struct mt76_dev *dev, u8 channel, u8 bw, u8 bw_index,
			 bool scan)
{
	struct sk_buff *skb;
	struct {
		u8 idx;
		u8 scan;
		u8 bw;
		u8 _pad0;

		__le16 chainmask;
		u8 ext_chan;
		u8 _pad1;

	} __packed __aligned(4) msg = {
		.idx = channel,
		.scan = scan,
		.bw = bw,
		.chainmask = cpu_to_le16(dev->chainmask),
	};

	/* first set the channel without the extension channel info */
	skb = mt76_mcu_msg_alloc(dev, &msg, sizeof(msg));
	mt76_mcu_msg_send(dev, skb, CMD_SWITCH_CHANNEL_OP);

	msleep(5);

	msg.ext_chan = 0xe0 + bw_index;
	skb = mt76_mcu_msg_alloc(dev, &msg, sizeof(msg));
	return mt76_mcu_msg_send(dev, skb, CMD_SWITCH_CHANNEL_OP);
}

int mt76_mcu_set_radio_state(struct mt76_dev *dev, bool on)
{
	struct sk_buff *skb;
	struct {
		__le32 mode;
		__le32 level;
	} __packed __aligned(4) msg = {
		.mode = cpu_to_le32(on ? RADIO_ON : RADIO_OFF),
		.level = cpu_to_le32(0),
	};

	skb = mt76_mcu_msg_alloc(dev, &msg, sizeof(msg));
	return mt76_mcu_msg_send(dev, skb, CMD_POWER_SAVING_OP);
}

int mt76_mcu_calibrate(struct mt76_dev *dev, enum mcu_calibration type,
		       u32 param)
{
	struct sk_buff *skb;
	struct {
		__le32 id;
		__le32 value;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(type),
		.value = cpu_to_le32(param),
	};
	int ret;

	mt76_clear(dev, MT_MCU_COM_REG0, BIT(31));

	skb = mt76_mcu_msg_alloc(dev, &msg, sizeof(msg));
	ret = mt76_mcu_msg_send(dev, skb, CMD_CALIBRATION_OP);
	if (ret)
		return ret;

	if (WARN_ON(!mt76_poll_msec(dev, MT_MCU_COM_REG0,
				    BIT(31), BIT(31), 100)))
		return -ETIMEDOUT;

	return 0;
}

int mt76_mcu_tssi_comp(struct mt76_dev *dev, struct mt76_tssi_comp *tssi_data)
{
	struct sk_buff *skb;
	struct {
		__le32 id;
		struct mt76_tssi_comp data;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(MCU_CAL_TSSI_COMP),
		.data = *tssi_data,
	};

	skb = mt76_mcu_msg_alloc(dev, &msg, sizeof(msg));
	return mt76_mcu_msg_send(dev, skb, CMD_CALIBRATION_OP);
}

int mt76_mcu_init_gain(struct mt76_dev *dev, u8 channel, u32 gain, bool force)
{
	struct sk_buff *skb;
	struct {
		__le32 channel;
		__le32 gain_val;
	} __packed __aligned(4) msg = {
		.channel = cpu_to_le32(channel),
		.gain_val = cpu_to_le32(gain),
	};

	if (force)
		msg.channel |= cpu_to_le32(BIT(31));

	skb = mt76_mcu_msg_alloc(dev, &msg, sizeof(msg));
	return mt76_mcu_msg_send(dev, skb, CMD_INIT_GAIN_OP);
}

int mt76_mcu_init(struct mt76_dev *dev)
{
	int ret;

	mutex_init(&dev->mcu.mutex);

	ret = mt76pci_load_rom_patch(dev);
	if (ret)
		return ret;

	ret = mt76pci_load_firmware(dev);
	if (ret)
		return ret;

	mt76_mcu_function_select(dev, Q_SELECT, 1);
	return 0;
}

int mt76_mcu_cleanup(struct mt76_dev *dev)
{
	mt76_wr(dev, MT_MCU_INT_LEVEL, 1);
	msleep(20);

	return 0;
}
