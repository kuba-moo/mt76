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

#include <linux/of.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>
#include "mt76.h"
#include "eeprom.h"

#define EE_FIELD(_name, _value) [MT_EE_##_name] = (_value) | 1

static int
mt76_eeprom_copy(struct mt76_dev *dev, enum mt76_eeprom_field field,
		 void *dest, int len)
{
	if (field + len > dev->eeprom.size)
		return -1;

	memcpy(dest, dev->eeprom.data + field, len);
	return 0;
}

static int
mt76_eeprom_get_macaddr(struct mt76_dev *dev)
{
	void *src = dev->eeprom.data + MT_EE_MAC_ADDR;

	memcpy(dev->macaddr, src, ETH_ALEN);
	return 0;
}

static int mt76_get_of_eeprom(struct mt76_dev *dev, int len)
{
	int ret = -ENOENT;
#ifdef CONFIG_OF
	struct device_node *np = dev->dev->of_node;
	struct mtd_info *mtd;
	const __be32 *list;
	const char *part;
	phandle phandle;
	int offset = 0;
	int size;
	size_t retlen;

	if (!np)
		return -ENOENT;

	list = of_get_property(np, "mediatek,mtd-eeprom", &size);
	if (!list)
		return -ENOENT;

	phandle = be32_to_cpup(list++);
	if (!phandle)
		return -ENOENT;

	np = of_find_node_by_phandle(phandle);
	if (!np)
		return -EINVAL;

	part = of_get_property(np, "label", NULL);
	if (!part)
		part = np->name;

	mtd = get_mtd_device_nm(part);
	if (IS_ERR(mtd))
		return PTR_ERR(mtd);

	if (size <= sizeof(*list))
		return -EINVAL;

	offset = be32_to_cpup(list);
	ret = mtd_read(mtd, offset, len, &retlen, dev->eeprom.data);
	put_mtd_device(mtd);
	if (ret)
		return ret;

	if (retlen < len)
		return -EINVAL;
#endif
	return ret;
}

static void
mt76_eeprom_parse_hw_cap(struct mt76_dev *dev)
{
	if (is_mt76x2(dev)) {
		u16 val = MT_EE_NIC_CONF_0;

		switch (MT76_GET(MT_EE_NIC_CONF_0_BOARD_TYPE, val)) {
		case BOARD_TYPE_5GHZ:
			dev->cap.has_5ghz = true;
			break;
		case BOARD_TYPE_2GHZ:
			dev->cap.has_2ghz = true;
			break;
		default:
			dev->cap.has_2ghz = true;
			dev->cap.has_5ghz = true;
			break;
		}
	} else {
		dev->cap.has_2ghz = true;
	}
}

static void
mt76_get_of_overrides(struct mt76_dev *dev)
{
#ifdef CONFIG_OF
	struct device_node *np = dev->dev->of_node;
	const __be32 *val;
	int size;

	if (!np)
		return;

	val = of_get_property(np, "mediatek,2ghz", &size);
	if (val)
		dev->cap.has_2ghz = be32_to_cpup(val);

	val = of_get_property(np, "mediatek,5ghz", &size);
	if (val)
		dev->cap.has_5ghz = be32_to_cpup(val);
#endif
}

static int
mt76_efuse_read(struct mt76_dev *dev, u16 addr, u8 *data)
{
	u32 val;
	int i;

	val = mt76_rr(dev, MT_EFUSE_CTRL);
	val &= ~(MT_EFUSE_CTRL_AIN |
		 MT_EFUSE_CTRL_MODE);
	val |= MT76_SET(MT_EFUSE_CTRL_AIN, addr & ~0xf);
	val |= MT_EFUSE_CTRL_KICK;
	mt76_wr(dev, MT_EFUSE_CTRL, val);

	if (!mt76_poll(dev, MT_EFUSE_CTRL, MT_EFUSE_CTRL_KICK, 0, 1000))
		return -ETIMEDOUT;

	udelay(2);

	val = mt76_rr(dev, MT_EFUSE_CTRL);
	if ((val & MT_EFUSE_CTRL_AOUT) == MT_EFUSE_CTRL_AOUT) {
		memset(data, 0xff, 16);
		return 0;
	}

	for (i = 0; i < 4; i++) {
	    val = mt76_rr(dev, MT_EFUSE_DATA(i));
	    put_unaligned_le32(val, data + 4 * i);
	}

	return 0;
}

static int
mt76_get_efuse_data(struct mt76_dev *dev, int len)
{
	int ret, i;

	for (i = 0; i + 16 <= len; i += 16) {
		ret = mt76_efuse_read(dev, i, dev->eeprom.data + i);
		if (ret)
			return ret;
	}

	return 0;
}

static int
mt76_eeprom_load(struct mt76_dev *dev)
{
	dev->eeprom.size = dev->param->eeprom_size;
	dev->eeprom.data = devm_kzalloc(dev->dev, dev->eeprom.size, GFP_KERNEL);
	if (!dev->eeprom.data)
		return -ENOMEM;

	if (!mt76_get_of_eeprom(dev, dev->eeprom.size))
		return 0;

	if (!mt76_get_efuse_data(dev, dev->eeprom.size))
		return 0;

	return -ENOENT;
}

static inline int
mt76_sign_extend(u32 val, unsigned size)
{
	bool sign = val & BIT(size - 1);
	val &= BIT(size - 1) - 1;
	return sign ? val : -val;
}

static inline int
mt76_sign_extend_optional(u32 val, unsigned size)
{
	bool enable = val & BIT(size);
	return enable ? mt76_sign_extend(val, size) : 0;
}

static bool
field_valid(u8 val)
{
	return val != 0 && val != 0xff;
}

static void
mt76_set_rx_gain_group(struct mt76_dev *dev, u8 val)
{
	s8 *dest = dev->cal.rx.high_gain;

	if (!field_valid(val)) {
		dest[0] = 0;
		dest[1] = 0;
		return;
	}

	dest[0] = mt76_sign_extend(val, 4);
	dest[1] = mt76_sign_extend(val >> 4, 4);
}

static void
mt76_set_rssi_offset(struct mt76_dev *dev, int chain, u8 val)
{
	s8 *dest = dev->cal.rx.rssi_offset;

	if (!field_valid(val)) {
		dest[chain] = 0;
		return;
	}

	dest[chain] = mt76_sign_extend(val, 6);
}

static enum mt76_cal_channel_group
mt76_get_cal_channel_group(int channel)
{
	if (channel >= 184 && channel <= 196)
		return MT_CH_5G_JAPAN;
	if (channel <= 48)
		return MT_CH_5G_UNII_1;
	if (channel <= 64)
		return MT_CH_5G_UNII_2;
	if (channel <= 114)
		return MT_CH_5G_UNII_2E_1;
	if (channel <= 144)
		return MT_CH_5G_UNII_2E_2;
	return MT_CH_5G_UNII_3;
}

static u8
mt76_get_5g_rx_gain(struct mt76_dev *dev, u8 channel)
{
	enum mt76_cal_channel_group group = mt76_get_cal_channel_group(channel);

	switch (group) {
	case MT_CH_5G_JAPAN:
		return mt76_eeprom_get(dev, MT_EE_RF_5G_GRP0_1_RX_HIGH_GAIN);
	case MT_CH_5G_UNII_1:
		return mt76_eeprom_get(dev, MT_EE_RF_5G_GRP0_1_RX_HIGH_GAIN) >> 8;
	case MT_CH_5G_UNII_2:
		return mt76_eeprom_get(dev, MT_EE_RF_5G_GRP2_3_RX_HIGH_GAIN);
	case MT_CH_5G_UNII_2E_1:
		return mt76_eeprom_get(dev, MT_EE_RF_5G_GRP2_3_RX_HIGH_GAIN) >> 8;
	case MT_CH_5G_UNII_2E_2:
		return mt76_eeprom_get(dev, MT_EE_RF_5G_GRP4_5_RX_HIGH_GAIN);
	default:
		return mt76_eeprom_get(dev, MT_EE_RF_5G_GRP4_5_RX_HIGH_GAIN) >> 8;
	}
}

void mt76_read_rx_gain(struct mt76_dev *dev)
{
	struct ieee80211_channel *chan = dev->chandef.chan;
	int channel = chan->hw_value;
	s8 lna_5g[3], lna_2g;
	u16 val;

	if (chan->band == IEEE80211_BAND_2GHZ)
		val = mt76_eeprom_get(dev, MT_EE_RF_2G_RX_HIGH_GAIN) >> 8;
	else
		val = mt76_get_5g_rx_gain(dev, channel);

	mt76_set_rx_gain_group(dev, val);

	if (chan->band == IEEE80211_BAND_2GHZ) {
		val = mt76_eeprom_get(dev, MT_EE_RSSI_OFFSET_2G_0);
		mt76_set_rssi_offset(dev, 0, val);
		mt76_set_rssi_offset(dev, 1, val >> 8);
	} else {
		val = mt76_eeprom_get(dev, MT_EE_RSSI_OFFSET_5G_0);
		mt76_set_rssi_offset(dev, 0, val);
		mt76_set_rssi_offset(dev, 1, val >> 8);
	}

	val = mt76_eeprom_get(dev, MT_EE_LNA_GAIN);
	lna_2g = val & 0xff;
	lna_5g[0] = val >> 8;

	val = mt76_eeprom_get(dev, MT_EE_RSSI_OFFSET_2G_1);
	lna_5g[1] = val >> 8;

	val = mt76_eeprom_get(dev, MT_EE_RSSI_OFFSET_5G_1);
	lna_5g[2] = val >> 8;

	if (!field_valid(lna_5g[1]))
		lna_5g[1] = lna_5g[0];

	if (!field_valid(lna_5g[2]))
		lna_5g[2] = lna_5g[0];

	dev->cal.rx.mcu_gain =  (lna_2g & 0xff);
	dev->cal.rx.mcu_gain |= (lna_5g[0] & 0xff) << 8;
	dev->cal.rx.mcu_gain |= (lna_5g[1] & 0xff) << 16;
	dev->cal.rx.mcu_gain |= (lna_5g[2] & 0xff) << 24;

	val = mt76_eeprom_get(dev, MT_EE_NIC_CONF_1);
	if (val & MT_EE_NIC_CONF_1_LNA_EXT_2G)
		lna_2g = 0;
	if (val & MT_EE_NIC_CONF_1_LNA_EXT_5G)
		memset(lna_5g, 0, sizeof(lna_5g));

	if (chan->band == IEEE80211_BAND_2GHZ)
		dev->cal.rx.lna_gain = lna_2g;
	else if (channel <= 64)
		dev->cal.rx.lna_gain = lna_5g[0];
	else if (channel <= 128)
		dev->cal.rx.lna_gain = lna_5g[1];
	else
		dev->cal.rx.lna_gain = lna_5g[2];

}

static s8
mt76_rate_power_val(u8 val)
{
	if (!field_valid(val))
		return 0;

	return mt76_sign_extend_optional(val, 7);
}

void mt76_get_rate_power(struct mt76_dev *dev, struct mt76_rate_power *t)
{
	bool is_5ghz = false;
	u16 val;

	is_5ghz = dev->chandef.chan->band == IEEE80211_BAND_5GHZ;

	memset(t, 0, sizeof(*t));

	val = mt76_eeprom_get(dev, MT_EE_TX_POWER_CCK);
	t->cck[0] = mt76_rate_power_val(val);
	t->cck[1] = mt76_rate_power_val(val >> 8);

	if (is_5ghz)
		val = mt76_eeprom_get(dev, MT_EE_TX_POWER_OFDM_5G_6M);
	else
		val = mt76_eeprom_get(dev, MT_EE_TX_POWER_OFDM_2G_6M);
	t->ofdm[0] = mt76_rate_power_val(val);
	t->ofdm[1] = mt76_rate_power_val(val >> 8);

	if (is_5ghz)
		val = mt76_eeprom_get(dev, MT_EE_TX_POWER_OFDM_2G_24M);
	else
		val = mt76_eeprom_get(dev, MT_EE_TX_POWER_OFDM_5G_24M);
	t->ofdm[2] = mt76_rate_power_val(val);
	t->ofdm[3] = mt76_rate_power_val(val >> 8);

	val = mt76_eeprom_get(dev, MT_EE_TX_POWER_HT_MCS0);
	t->ht[0] = mt76_rate_power_val(val);
	t->ht[1] = mt76_rate_power_val(val >> 8);

	val = mt76_eeprom_get(dev, MT_EE_TX_POWER_HT_MCS4);
	t->ht[2] = mt76_rate_power_val(val);
	t->ht[3] = mt76_rate_power_val(val >> 8);

	val = mt76_eeprom_get(dev, MT_EE_TX_POWER_HT_MCS8);
	t->ht[4] = mt76_rate_power_val(val);
	t->ht[5] = mt76_rate_power_val(val >> 8);

	val = mt76_eeprom_get(dev, MT_EE_TX_POWER_HT_MCS12);
	t->ht[6] = mt76_rate_power_val(val);
	t->ht[7] = mt76_rate_power_val(val >> 8);

	val = mt76_eeprom_get(dev, MT_EE_TX_POWER_VHT_MCS0);
	t->vht[0] = mt76_rate_power_val(val);
	t->vht[1] = mt76_rate_power_val(val >> 8);

	val = mt76_eeprom_get(dev, MT_EE_TX_POWER_VHT_MCS4);
	t->vht[2] = mt76_rate_power_val(val);
	t->vht[3] = mt76_rate_power_val(val >> 8);

	val = mt76_eeprom_get(dev, MT_EE_TX_POWER_VHT_MCS8);
	if (!is_5ghz)
		val >>= 8;
	t->vht[4] = mt76_rate_power_val(val);
}

static void
mt76_get_power_info_2g(struct mt76_dev *dev, struct mt76_tx_power_info *t,
		       int chain, int offset)
{
	int channel = dev->chandef.chan->hw_value;
	int delta_idx;
	u8 data[6];
	u16 val;

	if (channel < 6)
		delta_idx = 3;
	else if (channel < 11)
		delta_idx = 4;
	else
		delta_idx = 5;

	mt76_eeprom_copy(dev, offset, data, sizeof(data));

	t->chain[chain].tssi_slope = data[0];
	t->chain[chain].tssi_offset = data[1];
	t->chain[chain].target_power = data[2];
	t->chain[chain].delta = data[delta_idx];

	val = mt76_eeprom_get(dev, MT_EE_RF_2G_TSSI_OFF_TXPOWER);
	t->target_power = val >> 8;
}

static void
mt76_get_power_info_5g(struct mt76_dev *dev, struct mt76_tx_power_info *t,
		       int chain, int offset)
{
	int channel = dev->chandef.chan->hw_value;
	enum mt76_cal_channel_group group = mt76_get_cal_channel_group(channel);
	int delta_idx;
	u16 val;
	u8 data[5];

	offset += group * MT_TX_POWER_GROUP_SIZE_5G;

	if (channel >= 192)
		delta_idx = 4;
	else if (channel >= 484)
		delta_idx = 3;
	else if (channel < 44)
		delta_idx = 3;
	else if (channel < 52)
		delta_idx = 4;
	else if (channel < 58)
		delta_idx = 3;
	else if (channel < 98)
		delta_idx = 4;
	else if (channel < 106)
		delta_idx = 3;
	else if (channel < 116)
		delta_idx = 4;
	else if (channel < 130)
		delta_idx = 3;
	else if (channel < 149)
		delta_idx = 4;
	else if (channel < 157)
		delta_idx = 3;
	else
		delta_idx = 4;

	mt76_eeprom_copy(dev, offset, data, sizeof(data));

	t->chain[chain].tssi_slope = data[0];
	t->chain[chain].tssi_offset = data[1];
	t->chain[chain].target_power = data[2];
	t->chain[chain].delta = data[delta_idx];

	val = mt76_eeprom_get(dev, MT_EE_RF_2G_RX_HIGH_GAIN);
	t->target_power = val & 0xff;
}

void mt76_get_power_info(struct mt76_dev *dev, struct mt76_tx_power_info *t)
{
	u16 bw40, bw80;
	memset(t, 0, sizeof(*t));

	bw40 = mt76_eeprom_get(dev, MT_EE_TX_POWER_DELTA_BW40);
	bw80 = mt76_eeprom_get(dev, MT_EE_TX_POWER_DELTA_BW80);

	if (dev->chandef.chan->band == IEEE80211_BAND_5GHZ) {
		bw40 >>= 8;
		mt76_get_power_info_5g(dev, t, 0, MT_EE_TX_POWER_0_START_5G);
		mt76_get_power_info_5g(dev, t, 1, MT_EE_TX_POWER_1_START_5G);
	} else {
		mt76_get_power_info_2g(dev, t, 0, MT_EE_TX_POWER_0_START_2G);
		mt76_get_power_info_2g(dev, t, 1, MT_EE_TX_POWER_1_START_2G);
	}

	if (mt76_tssi_enabled(dev) || !field_valid(t->target_power))
		t->target_power = t->chain[0].target_power;

	t->delta_bw40 = mt76_rate_power_val(bw40);
	t->delta_bw80 = mt76_rate_power_val(bw80);
}

int mt76_get_temp_comp(struct mt76_dev *dev, struct mt76_temp_comp *t)
{
	enum ieee80211_band band = dev->chandef.chan->band;
	u16 val, slope;
	u8 bounds;

	memset(t, 0, sizeof(*t));

	val = mt76_eeprom_get(dev, MT_EE_NIC_CONF_1);
	if (!(val & MT_EE_NIC_CONF_1_TEMP_TX_ALC))
		return -EINVAL;

	if (!mt76_ext_pa_enabled(dev, band))
		return -EINVAL;

	val = mt76_eeprom_get(dev, MT_EE_TX_POWER_EXT_PA_5G) >> 8;
	if (!(val & BIT(7)))
		return -EINVAL;

	t->temp_25_ref = val & 0x7f;
	if (band == IEEE80211_BAND_5GHZ) {
		slope = mt76_eeprom_get(dev, MT_EE_RF_TEMP_COMP_SLOPE_5G);
		bounds = mt76_eeprom_get(dev, MT_EE_TX_POWER_EXT_PA_5G);
	} else {
		slope = mt76_eeprom_get(dev, MT_EE_RF_TEMP_COMP_SLOPE_2G);
		bounds = mt76_eeprom_get(dev, MT_EE_TX_POWER_DELTA_BW80) >> 8;
	}

	t->high_slope = slope & 0xff;
	t->low_slope = slope >> 8;
	t->lower_bound = 0 - (bounds & 0xf);
	t->upper_bound = (bounds >> 4) & 0xf;

	return 0;
}

bool mt76_ext_pa_enabled(struct mt76_dev *dev, enum ieee80211_band band)
{
	u16 conf1 = mt76_eeprom_get(dev, MT_EE_NIC_CONF_0);

	if (band == IEEE80211_BAND_5GHZ)
		return !(conf1 & MT_EE_NIC_CONF_0_PA_INT_5G);
	else
		return !(conf1 & MT_EE_NIC_CONF_0_PA_INT_2G);
}

int mt76_eeprom_init(struct mt76_dev *dev)
{
	int ret;

	ret = mt76_eeprom_load(dev);
	if (ret)
		return ret;

	mt76_eeprom_parse_hw_cap(dev);
	mt76_get_of_overrides(dev);

	mt76_eeprom_get_macaddr(dev);
	if (!is_valid_ether_addr(dev->macaddr)) {
		eth_random_addr(dev->macaddr);
		dev_printk(KERN_INFO, dev->dev,
			   "Invalid MAC address, using random address %pM\n",
			   dev->macaddr);
	}

	return 0;
}
