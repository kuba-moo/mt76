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

#include <linux/delay.h>
#include "mt76.h"
#include "eeprom.h"
#include "mcu.h"

static bool
mt76_wait_for_mac(struct mt76_dev *dev)
{
	int i;

	for (i = 0; i < 500; i++) {
		switch (mt76_rr(dev, MT_MAC_CSR0)) {
		case 0:
		case ~0:
			break;
		default:
			return true;
		}
		msleep(5);
	}

	return false;
}

static bool
wait_for_wpdma(struct mt76_dev *dev)
{
	return mt76_poll(dev, MT_WPDMA_GLO_CFG,
			 MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
			 MT_WPDMA_GLO_CFG_RX_DMA_BUSY,
			 0, 1000);
}

static void
mt76_mac_pbf_init(struct mt76_dev *dev)
{
	u32 val;

	val = MT_PBF_SYS_CTRL_MCU_RESET |
	      MT_PBF_SYS_CTRL_DMA_RESET |
	      MT_PBF_SYS_CTRL_MAC_RESET |
	      MT_PBF_SYS_CTRL_PBF_RESET |
	      MT_PBF_SYS_CTRL_ASY_RESET;

	mt76_set(dev, MT_PBF_SYS_CTRL, val);
	mt76_clear(dev, MT_PBF_SYS_CTRL, val);

	mt76_wr(dev, MT_PBF_TX_MAX_PCNT, 0xefef3f1f);
	mt76_wr(dev, MT_PBF_RX_MAX_PCNT, 0xfebf);
}

static void
mt76_write_mac_initvals(struct mt76_dev *dev)
{
	static const struct mt76_reg_pair vals[] = {
		/* Copied from MediaTek reference source */
		{ MT_PBF_SYS_CTRL,		0x00080c00 },
		{ MT_PBF_CFG,			0x1efebcff },
		{ MT_FCE_PSE_CTRL,		0x00000001 },
		{ MT_MAC_SYS_CTRL,		0x0000000c },
		{ MT_MAX_LEN_CFG,		0x003e3fff },
		{ MT_AMPDU_MAX_LEN_20M1S,	0xaaa99887 },
		{ MT_AMPDU_MAX_LEN_20M2S,	0x000000aa },
		{ MT_XIFS_TIME_CFG,		0x33a40d0a },
		{ MT_BKOFF_SLOT_CFG,		0x00000209 },
		{ MT_TBTT_SYNC_CFG,		0x00422010 },
		{ MT_PWR_PIN_CFG,		0x00000000 },
		{ 0x1238,			0x001700c8 },
		{ MT_TX_SW_CFG0,		0x00101001 },
		{ MT_TX_SW_CFG1,		0x00010000 },
		{ MT_TX_SW_CFG2,		0x00000000 },
		{ MT_TXOP_CTRL_CFG,		0x0000583f },
		{ MT_TX_RTS_CFG,		0x00092b20 },
		{ MT_TX_TIMEOUT_CFG,		0x000a2290 },
		{ MT_TX_RETRY_CFG,		0x47f01f0f },
		{ MT_EXP_ACK_TIME,		0x002c00dc },
		{ MT_TX_PROT_CFG6,		0xe3f42004 },
		{ MT_TX_PROT_CFG7,		0xe3f42084 },
		{ MT_TX_PROT_CFG8,		0xe3f42104 },
		{ MT_PIFS_TX_CFG,		0x00060fff },
		{ MT_RX_FILTR_CFG,		0x00015f97 },
		{ MT_LEGACY_BASIC_RATE,		0x0000017f },
		{ MT_HT_BASIC_RATE,		0x00008003 },
		{ MT_PN_PAD_MODE,		0x00000002 },
		{ MT_TXOP_HLDR_ET,		0x00000002 },
		{ 0xa44,			0x00000000 },
		{ MT_HEADER_TRANS_CTRL_REG,	0x00000000 },
		{ MT_TSO_CTRL,			0x00000000 },
		{ MT_AUX_CLK_CFG,		0x00000000 },
		{ MT_DACCLK_EN_DLY_CFG,		0x00000000 },
		{ MT_TX_ALC_CFG_4,		0x00000000 },
		{ MT_TX_ALC_VGA3,		0x00000000 },
		{ MT_TX_PWR_CFG_0,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_1,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_2,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_3,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_4,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_7,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_8,		0x0000003a },
		{ MT_TX_PWR_CFG_9,		0x0000003a },
		{ MT_EFUSE_CTRL,		0x0000d000 },
		{ MT_PAUSE_ENABLE_CONTROL1,	0x0000000a },
		{ MT_FCE_WLAN_FLOW_CONTROL1,	0x60401c18 },
		{ MT_WPDMA_DELAY_INT_CFG,	0x94ff0000 },
		{ MT_TX_SW_CFG3,		0x00000004 },
		{ MT_HT_FBK_TO_LEGACY,		0x00001818 },
		{ MT_VHT_HT_FBK_CFG1,		0xedcba980 },
	};

	mt76_write_reg_pairs(dev, vals, ARRAY_SIZE(vals));
}

static void
mt76_fixup_xtal(struct mt76_dev *dev)
{
	u16 eep_val;
	s8 offset = 0;

	eep_val = mt76_eeprom_get(dev, MT_EE_XTAL_TRIM_2);

	offset = eep_val & 0x7f;
	if ((eep_val & 0xff) == 0xff)
		offset = 0;
	else if (eep_val & 0x80)
		offset = 0 - offset;

	eep_val >>= 8;
	if (eep_val == 0x00 || eep_val == 0xff) {
		eep_val = mt76_eeprom_get(dev, MT_EE_XTAL_TRIM_1);
		eep_val &= 0xff;

		if (eep_val == 0x00 || eep_val == 0xff)
			eep_val = 0x14;
	}

	eep_val &= 0x7f;
	mt76_rmw_field(dev, MT_XO_CTRL5, MT_XO_CTRL5_C2_VAL, eep_val + offset);
	mt76_set(dev, MT_XO_CTRL6, MT_XO_CTRL6_C2_CTRL);

	eep_val = mt76_eeprom_get(dev, MT_EE_NIC_CONF_2);
	switch (MT76_GET(MT_EE_NIC_CONF_2_XTAL_OPTION, eep_val)) {
	case 0:
		mt76_wr(dev, MT_XO_CTRL7, 0x5c1fee80);
		break;
	case 1:
		mt76_wr(dev, MT_XO_CTRL7, 0x5c1feed0);
		break;
	default:
		break;
	}
}

static void
mt76_init_beacon_offsets(struct mt76_dev *dev)
{
	u16 base = MT_BEACON_BASE;
	u32 regs[4] = {};
	int i;

	for (i = 0; i < 16; i++) {
		u16 addr = dev->beacon_offsets[i];

		regs[i / 4] |= ((addr - base) / 64) << (8 * (i % 4));
	}

	for (i = 0; i < 4; i++)
		mt76_wr(dev, MT_BCN_OFFSET(i), regs[i]);
}

int mt76_mac_reset(struct mt76_dev *dev, bool hard)
{
	static const u8 null_addr[ETH_ALEN] = {};
	u32 val;
	int i, k;

	if (!mt76_wait_for_mac(dev))
		return -ETIMEDOUT;

	val = mt76_rr(dev, MT_WPDMA_GLO_CFG);

	val &= ~(MT_WPDMA_GLO_CFG_TX_DMA_EN |
		 MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
		 MT_WPDMA_GLO_CFG_RX_DMA_EN |
		 MT_WPDMA_GLO_CFG_RX_DMA_BUSY |
		 MT_WPDMA_GLO_CFG_DMA_BURST_SIZE);
	val |= MT76_SET(MT_WPDMA_GLO_CFG_DMA_BURST_SIZE, 3);

	mt76_wr(dev, MT_WPDMA_GLO_CFG, val);

	mt76_mac_pbf_init(dev);
	mt76_write_mac_initvals(dev);
	mt76_fixup_xtal(dev);

	mt76_clear(dev, MT_MAC_SYS_CTRL,
		   MT_MAC_SYS_CTRL_RESET_CSR |
		   MT_MAC_SYS_CTRL_RESET_BBP);

	if (is_mt7612(dev))
		mt76_clear(dev, MT_COEXCFG0, MT_COEXCFG0_COEX_EN);

	mt76_set(dev, MT_EXT_CCA_CFG, 0x0000f000);
	mt76_clear(dev, MT_TX_ALC_CFG_4, BIT(31));

	mt76_wr(dev, MT_RF_BYPASS_0, 0x06000000);
	mt76_wr(dev, MT_RF_SETTING_0, 0x08800000);
	msleep(5);
	mt76_wr(dev, MT_RF_BYPASS_0, 0x00000000);

	mt76_wr(dev, MT_MCU_CLOCK_CTL, 0x1401);
	mt76_clear(dev, MT_FCE_L2_STUFF, MT_FCE_L2_STUFF_WR_MPDU_LEN_EN);

	mt76_wr(dev, MT_MAC_ADDR_DW0, get_unaligned_le32(dev->macaddr));
	mt76_wr(dev, MT_MAC_ADDR_DW1, get_unaligned_le16(dev->macaddr + 4));

	mt76_wr(dev, MT_MAC_BSSID_DW0, get_unaligned_le32(dev->macaddr));
	mt76_wr(dev, MT_MAC_BSSID_DW1, get_unaligned_le16(dev->macaddr + 4) |
		MT76_SET(MT_MAC_BSSID_DW1_MBSS_MODE, 3) | /* 8 beacons */
		MT_MAC_BSSID_DW1_MBSS_LOCAL_BIT);

	/* Fire a pre-TBTT interrupt 8 ms before TBTT */
	mt76_rmw_field(dev, MT_INT_TIMER_CFG, MT_INT_TIMER_CFG_PRE_TBTT,
		       8 << 4);
	mt76_wr(dev, MT_INT_TIMER_EN, 0);

	mt76_wr(dev, MT_BCN_BYPASS_MASK, 0xffff);
	if (!hard)
		return 0;

	for (i = 0; i < 256; i++)
		mt76_mac_wcid_setup(dev, i, 0, NULL);

	for (i = 0; i < 16; i++)
		for (k = 0; k < 4; k++)
			mt76_mac_shared_key_setup(dev, i, k, NULL);

	for (i = 0; i < 8; i++) {
		mt76_mac_set_bssid(dev, i, null_addr);
		mt76_mac_set_beacon(dev, i, NULL);
	}

	for (i = 0; i < 16; i++)
		mt76_rr(dev, MT_TX_STAT_FIFO);

	mt76_set(dev, MT_MAC_APC_BSSID_H(0), MT_MAC_APC_BSSID0_H_EN);
	mt76_init_beacon_offsets(dev);

	return 0;
}

int mt76_mac_start(struct mt76_dev *dev)
{
	int i;

	for (i = 0; i < 16; i++)
		mt76_rr(dev, MT_TX_AGG_CNT(i));

	for (i = 0; i < 16; i++)
		mt76_rr(dev, MT_TX_STAT_FIFO);

	memset(dev->aggr_stats, 0, sizeof(dev->aggr_stats));

	mt76_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);
	wait_for_wpdma(dev);
	udelay(50);

	mt76_set(dev, MT_WPDMA_GLO_CFG,
		 MT_WPDMA_GLO_CFG_TX_DMA_EN |
		 MT_WPDMA_GLO_CFG_RX_DMA_EN |
		 MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);

	mt76_wr(dev, MT_RX_FILTR_CFG, dev->rxfilter);

	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);

	mt76_irq_enable(dev, MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL | MT_INT_TX_STAT);

	return 0;
}

void mt76_mac_stop(struct mt76_dev *dev, bool force)
{
	bool stopped = false;
	u32 rts_cfg;
	int i;

	mt76_wr(dev, MT_MAC_SYS_CTRL, 0);

	rts_cfg = mt76_rr(dev, MT_TX_RTS_CFG);
	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg & ~MT_TX_RTS_CFG_RETRY_LIMIT);

	/* Wait for MAC to become idle */
	for (i = 0; i < 300; i++) {
		if (mt76_rr(dev, MT_MAC_STATUS) &
		    (MT_MAC_STATUS_RX | MT_MAC_STATUS_TX))
			continue;

		if (mt76_rr(dev, MT_BBP(IBI, 12)))
			continue;

		stopped = true;
		break;
	}

	if (force && !stopped) {
		mt76_set(dev, MT_BBP(CORE, 4), BIT(2));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(2));

		mt76_set(dev, MT_BBP(CORE, 4), BIT(1));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(1));
	}

	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg);
}

void mt76_mac_resume(struct mt76_dev *dev)
{
	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);
}


static void
mt76_power_on_rf_patch(struct mt76_dev *dev)
{
	mt76_set(dev, 0x10130, BIT(0) | BIT(16));
	udelay(1);

	mt76_clear(dev, 0x1001c, 0xff);
	mt76_set(dev, 0x1001c, 0x30);

	mt76_wr(dev, 0x10014, 0x484f);
	udelay(1);

	mt76_set(dev, 0x10130, BIT(17));
	udelay(125);

	mt76_clear(dev, 0x10130, BIT(16));
	udelay(50);

	mt76_set(dev, 0x1014c, BIT(19) | BIT(20));
}

static void
mt76_power_on_rf(struct mt76_dev *dev, int unit)
{
	int shift = unit ? 8 : 0;

	/* Enable RF BG */
	mt76_set(dev, 0x10130, BIT(0) << shift);
	udelay(10);

	/* Enable RFDIG LDO/AFE/ABB/ADDA */
	mt76_set(dev, 0x10130, (BIT(1) | BIT(3) | BIT(4) | BIT(5)) << shift);
	udelay(10);

	/* Switch RFDIG power to internal LDO */
	mt76_clear(dev, 0x10130, BIT(2) << shift);
	udelay(10);

	mt76_power_on_rf_patch(dev);

	mt76_set(dev, 0x530, 0xf);
}

static void
mt76_power_on(struct mt76_dev *dev)
{
	u32 val;

	/* Turn on WL MTCMOS */
	mt76_set(dev, MT_WLAN_MTC_CTRL, MT_WLAN_MTC_CTRL_MTCMOS_PWR_UP);

	val = MT_WLAN_MTC_CTRL_STATE_UP |
	      MT_WLAN_MTC_CTRL_PWR_ACK |
	      MT_WLAN_MTC_CTRL_PWR_ACK_S;

	mt76_poll(dev, MT_WLAN_MTC_CTRL, val, val, 1000);

	mt76_clear(dev, MT_WLAN_MTC_CTRL, 0x7f << 16);
	udelay(10);

	mt76_clear(dev, MT_WLAN_MTC_CTRL, 0xf << 24);
	udelay(10);

	mt76_set(dev, MT_WLAN_MTC_CTRL, 0xf << 24);
	mt76_clear(dev, MT_WLAN_MTC_CTRL, 0xfff);

	/* Turn on AD/DA power down */
	mt76_clear(dev, 0x11204, BIT(3));

	/* WLAN function enable */
	mt76_set(dev, 0x10080, BIT(0));

	/* Release BBP software reset */
	mt76_clear(dev, 0x10064, BIT(18));

	mt76_power_on_rf(dev, 0);
	mt76_power_on_rf(dev, 1);
}

static void
mt76_set_wlan_state(struct mt76_dev *dev, bool enable)
{
	u32 val = mt76_rr(dev, MT_WLAN_FUN_CTRL);

	if (enable)
		val |= (MT_WLAN_FUN_CTRL_WLAN_EN |
			MT_WLAN_FUN_CTRL_WLAN_CLK_EN);
	else
		val &= ~(MT_WLAN_FUN_CTRL_WLAN_EN |
			 MT_WLAN_FUN_CTRL_WLAN_CLK_EN);

	mt76_wr(dev, MT_WLAN_FUN_CTRL, val);
	udelay(50);
}

static void
mt76_reset_wlan(struct mt76_dev *dev, bool enable)
{
	u32 val;

	val = mt76_rr(dev, MT_WLAN_FUN_CTRL);

	val &= ~MT_WLAN_FUN_CTRL_FRC_WL_ANT_SEL;

	if (val & MT_WLAN_FUN_CTRL_WLAN_EN) {
		val |= MT_WLAN_FUN_CTRL_WLAN_RESET_RF;
		mt76_wr(dev, MT_WLAN_FUN_CTRL, val);
		udelay(50);

		val &= ~MT_WLAN_FUN_CTRL_WLAN_RESET_RF;
	}

	mt76_wr(dev, MT_WLAN_FUN_CTRL, val);
	udelay(50);

	mt76_set_wlan_state(dev, enable);
}

int mt76_init_hardware(struct mt76_dev *dev)
{
	static const u16 beacon_offsets[16] = {
		/* 1024 byte per beacon */
		0xc000,
		0xc400,
		0xc800,
		0xcc00,
		0xd000,
		0xd400,
		0xd800,
		0xdc00,

		/* BSS idx 8-15 not used for beacons */
		0xc000,
		0xc000,
		0xc000,
		0xc000,
		0xc000,
		0xc000,
		0xc000,
		0xc000,
	};
	u32 val;
	int ret;

	dev->beacon_offsets = beacon_offsets;
	tasklet_init(&dev->pre_tbtt_tasklet, mt76_pre_tbtt_tasklet,
		     (unsigned long) dev);

	dev->chainmask = 0x202;

	val = mt76_rr(dev, MT_WPDMA_GLO_CFG);
	val &= MT_WPDMA_GLO_CFG_DMA_BURST_SIZE |
	       MT_WPDMA_GLO_CFG_BIG_ENDIAN |
	       MT_WPDMA_GLO_CFG_HDR_SEG_LEN;
	val |= MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE;
	mt76_wr(dev, MT_WPDMA_GLO_CFG, val);

	mt76_reset_wlan(dev, true);
	mt76_power_on(dev);

	ret = mt76_eeprom_init(dev);
	if (ret)
		return ret;

	ret = mt76_mac_reset(dev, true);
	if (ret)
		return ret;

	ret = mt76_dma_init(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->state);
	ret = mt76_mac_start(dev);
	if (ret)
		return ret;

	ret = mt76_mcu_init(dev);
	if (ret)
		return ret;

	mt76_mac_stop(dev, false);
	dev->rxfilter = mt76_rr(dev, MT_RX_FILTR_CFG);

	return 0;
}

void mt76_stop_hardware(struct mt76_dev *dev)
{
	cancel_delayed_work_sync(&dev->cal_work);
	cancel_delayed_work_sync(&dev->mac_work);
	mt76_mcu_set_radio_state(dev, false);
	mt76_mac_stop(dev, false);
}

void mt76_cleanup(struct mt76_dev *dev)
{
	mt76_stop_hardware(dev);
	mt76_dma_cleanup(dev);
	mt76_mcu_cleanup(dev);
}

static void mt76_free_ieee80211_hw(void *ptr)
{
	ieee80211_free_hw(ptr);
}

struct mt76_dev *mt76_alloc_device(struct device *pdev)
{
	struct ieee80211_hw *hw;
	struct mt76_dev *dev;

	hw = ieee80211_alloc_hw(sizeof(*dev), &mt76_ops);
	if (!hw)
		return NULL;
	devm_add_action(pdev, mt76_free_ieee80211_hw, hw);

	dev = hw->priv;
	dev->dev = pdev;
	dev->hw = hw;
	mutex_init(&dev->mutex);
	spin_lock_init(&dev->lock);
	spin_lock_init(&dev->irq_lock);

	return dev;
}

#define CHAN2G(_idx, _freq) {			\
	.band = IEEE80211_BAND_2GHZ,		\
	.center_freq = (_freq),			\
	.hw_value = (_idx),			\
	.max_power = 30,			\
}

#define CHAN5G(_idx, _freq) {			\
	.band = IEEE80211_BAND_5GHZ,		\
	.center_freq = (_freq),			\
	.hw_value = (_idx),			\
	.max_power = 30,			\
}

static const struct ieee80211_channel mt76_channels_2ghz[] = {
	CHAN2G(1, 2412),
	CHAN2G(2, 2417),
	CHAN2G(3, 2422),
	CHAN2G(4, 2427),
	CHAN2G(5, 2432),
	CHAN2G(6, 2437),
	CHAN2G(7, 2442),
	CHAN2G(8, 2447),
	CHAN2G(9, 2452),
	CHAN2G(10, 2457),
	CHAN2G(11, 2462),
	CHAN2G(12, 2467),
	CHAN2G(13, 2472),
	CHAN2G(14, 2484),
};

static const struct ieee80211_channel mt76_channels_5ghz[] = {
	CHAN5G(36, 5180),
	CHAN5G(40, 5200),
	CHAN5G(44, 5220),
	CHAN5G(48, 5240),

	CHAN5G(52, 5260),
	CHAN5G(56, 5280),
	CHAN5G(60, 5300),
	CHAN5G(64, 5320),

	CHAN5G(100, 5500),
	CHAN5G(104, 5520),
	CHAN5G(108, 5540),
	CHAN5G(112, 5560),
	CHAN5G(116, 5580),
	CHAN5G(120, 5600),
	CHAN5G(124, 5620),
	CHAN5G(128, 5640),
	CHAN5G(132, 5660),
	CHAN5G(136, 5680),
	CHAN5G(140, 5700),

	CHAN5G(149, 5745),
	CHAN5G(153, 5765),
	CHAN5G(157, 5785),
	CHAN5G(161, 5805),
	CHAN5G(165, 5825),
};

#define CCK_RATE(_idx, _rate) {					\
	.bitrate = _rate,					\
	.flags = IEEE80211_RATE_SHORT_PREAMBLE,			\
	.hw_value = (MT_PHY_TYPE_CCK << 8) | _idx,		\
	.hw_value_short = (MT_PHY_TYPE_CCK << 8) | (8 + _idx),	\
}

#define OFDM_RATE(_idx, _rate) {				\
	.bitrate = _rate,					\
	.hw_value = (MT_PHY_TYPE_OFDM << 8) | _idx,		\
	.hw_value_short = (MT_PHY_TYPE_OFDM << 8) | _idx,	\
}

static struct ieee80211_rate mt76_rates[] = {
	CCK_RATE(0, 10),
	CCK_RATE(1, 20),
	CCK_RATE(2, 55),
	CCK_RATE(3, 110),
	OFDM_RATE(0, 60),
	OFDM_RATE(1, 90),
	OFDM_RATE(2, 120),
	OFDM_RATE(3, 180),
	OFDM_RATE(4, 240),
	OFDM_RATE(5, 360),
	OFDM_RATE(6, 480),
	OFDM_RATE(7, 540),
};

static int
mt76_init_sband(struct mt76_dev *dev, struct ieee80211_supported_band *sband,
		const struct ieee80211_channel *chan, int n_chan,
		struct ieee80211_rate *rates, int n_rates)
{
	struct ieee80211_sta_ht_cap *ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap;
	void *chanlist;
	u16 mcs_map;
	int size;

	size = n_chan * sizeof(*chan);
	chanlist = devm_kmemdup(dev->dev, chan, size, GFP_KERNEL);
	if (!chanlist)
		return -ENOMEM;

	sband->channels = chanlist;
	sband->n_channels = n_chan;
	sband->bitrates = rates;
	sband->n_bitrates = n_rates;

	ht_cap = &sband->ht_cap;
	ht_cap->ht_supported = true;
	ht_cap->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		      IEEE80211_HT_CAP_GRN_FLD |
		      IEEE80211_HT_CAP_SGI_20 |
		      IEEE80211_HT_CAP_SGI_40 |
		      IEEE80211_HT_CAP_LDPC_CODING |
		      IEEE80211_HT_CAP_TX_STBC |
		      (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);

	ht_cap->mcs.rx_mask[0] = 0xff;
	ht_cap->mcs.rx_mask[1] = 0xff;
	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_4;

	if (dev->cap.has_5ghz)
	{
		vht_cap = &sband->vht_cap;
		vht_cap->vht_supported = true;

		mcs_map = (IEEE80211_VHT_MCS_SUPPORT_0_9 << (0 * 2)) |
			  (IEEE80211_VHT_MCS_SUPPORT_0_9 << (1 * 2)) |
			  (IEEE80211_VHT_MCS_NOT_SUPPORTED << (2 * 2)) |
			  (IEEE80211_VHT_MCS_NOT_SUPPORTED << (3 * 2)) |
			  (IEEE80211_VHT_MCS_NOT_SUPPORTED << (4 * 2)) |
			  (IEEE80211_VHT_MCS_NOT_SUPPORTED << (5 * 2)) |
			  (IEEE80211_VHT_MCS_NOT_SUPPORTED << (6 * 2)) |
			  (IEEE80211_VHT_MCS_NOT_SUPPORTED << (7 * 2));

		vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(mcs_map);
		vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(mcs_map);
		vht_cap->cap = IEEE80211_VHT_CAP_RXLDPC |
			       IEEE80211_VHT_CAP_TXSTBC |
			       IEEE80211_VHT_CAP_RXSTBC_1 |
			       IEEE80211_VHT_CAP_SHORT_GI_80;
	}

	dev->chandef.chan = &sband->channels[0];

	return 0;
}

static int
mt76_init_sband_2g(struct mt76_dev *dev)
{
	if (!dev->cap.has_2ghz)
		return 0;

	dev->hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &dev->sband_2g;
	return mt76_init_sband(dev, &dev->sband_2g,
			       mt76_channels_2ghz,
			       ARRAY_SIZE(mt76_channels_2ghz),
			       mt76_rates, ARRAY_SIZE(mt76_rates));
}

static int
mt76_init_sband_5g(struct mt76_dev *dev)
{
	if (!dev->cap.has_5ghz)
		return 0;

	dev->hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &dev->sband_5g;
	return mt76_init_sband(dev, &dev->sband_5g,
			       mt76_channels_5ghz,
			       ARRAY_SIZE(mt76_channels_5ghz),
			       mt76_rates + 4, ARRAY_SIZE(mt76_rates) - 4);
}

static const struct ieee80211_iface_limit if_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_ADHOC)
	}, {
		.max = 8,
		.types = BIT(NL80211_IFTYPE_STATION) |
#ifdef CONFIG_MAC80211_MESH
			 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
			 BIT(NL80211_IFTYPE_AP)
	 },
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = 8,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	}
};

int mt76_register_device(struct mt76_dev *dev)
{
	struct ieee80211_hw *hw = dev->hw;
	struct wiphy *wiphy = hw->wiphy;
	void *status_fifo;
	int fifo_size;
	int i, ret;

	fifo_size = roundup_pow_of_two(32 * sizeof(struct mt76_tx_status));
	status_fifo = devm_kzalloc(dev->dev, fifo_size, GFP_KERNEL);
	if (!status_fifo)
		return -ENOMEM;

	kfifo_init(&dev->txstatus_fifo, status_fifo, fifo_size);

	ret = mt76_init_hardware(dev);
	if (ret)
		return ret;

	SET_IEEE80211_DEV(hw, dev->dev);

	hw->queues = 4;
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_PS_NULLFUNC_STACK |
		    IEEE80211_HW_SUPPORTS_HT_CCK_RATES |
		    IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
		    IEEE80211_HW_AMPDU_AGGREGATION |
		    IEEE80211_HW_SUPPORTS_RC_TABLE;
	hw->max_rates = 1;
	hw->max_report_rates = 7;
	hw->max_rate_tries = 1;

	hw->sta_data_size = sizeof(struct mt76_sta);
	hw->vif_data_size = sizeof(struct mt76_vif);
	hw->txq_data_size = sizeof(struct mt76_txq);

	dev->macaddr[0] &= ~BIT(1);
	SET_IEEE80211_PERM_ADDR(hw, dev->macaddr);

	for (i = 0; i < ARRAY_SIZE(dev->macaddr_list); i++) {
		u8 *addr = dev->macaddr_list[i].addr;
		memcpy(addr, dev->macaddr, ETH_ALEN);

		if (!i)
			continue;

		addr[0] |= BIT(1);
		addr[0] ^= ((i - 1) << 2);
	}
	wiphy->addresses = dev->macaddr_list;
	wiphy->n_addresses = ARRAY_SIZE(dev->macaddr_list);

	wiphy->features |= NL80211_FEATURE_ACTIVE_MONITOR;

	wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_AP) |
#ifdef CONFIG_MAC80211_MESH
		BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
		BIT(NL80211_IFTYPE_ADHOC);

	wiphy->iface_combinations = if_comb;
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);

	ret = mt76_init_sband_2g(dev);
	if (ret)
		goto fail;

	ret = mt76_init_sband_5g(dev);
	if (ret)
		goto fail;

	INIT_LIST_HEAD(&dev->txwi_cache);
	INIT_DELAYED_WORK(&dev->cal_work, mt76_phy_calibrate);
	INIT_DELAYED_WORK(&dev->mac_work, mt76_mac_work);

	ret = ieee80211_register_hw(hw);
	if (ret)
		goto fail;

	mt76_init_debugfs(dev);

	return 0;

fail:
	mt76_stop_hardware(dev);
	return ret;
}


