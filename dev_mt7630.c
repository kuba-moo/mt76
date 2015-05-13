int mt76_phy_rf_wr(struct mt76_dev *dev, u8 bank, u8 reg, u8 val);

static const struct mt76_reg_pair_cond mt7630_bbp_bw_table[] = {
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC,  4), 0x1feda049 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC,  6), 0x00000045 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC,  8), 0x16343ef0 } },
	{ MT_CND_BW_20,			{ MT_BBP(AGC, 12), 0x05052879 } },
	{		 MT_CND_BW_40,	{ MT_BBP(AGC, 12), 0x050528f9 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC, 13), 0x35050004 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC, 14), 0x310f2e3c } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC, 26), 0x007c2005 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC, 27), 0x000000e1 } },
	{ MT_CND_BW_20,			{ MT_BBP(AGC, 28), 0x00060806 } },
	{		 MT_CND_BW_40,	{ MT_BBP(AGC, 28), 0x00050806 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC, 31), 0x00000f23 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC, 32), 0x00003218 } },
	{ MT_CND_BW_20,			{ MT_BBP(AGC, 35), 0x11111616 } },
	{		 MT_CND_BW_40,	{ MT_BBP(AGC, 35), 0x11111516 } },
	{ MT_CND_BW_20,			{ MT_BBP(AGC, 39), 0x2a2a3036 } },
	{		 MT_CND_BW_40,	{ MT_BBP(AGC, 39), 0x2a2a2c36 } },
	{ MT_CND_BW_20,			{ MT_BBP(AGC, 43), 0x27273438 } },
	{		 MT_CND_BW_40,	{ MT_BBP(AGC, 43), 0x27272d38 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC, 51), 0x17171c1c } },
	{ MT_CND_BW_20,			{ MT_BBP(AGC, 53), 0x26262a2f } },
	{		 MT_CND_BW_40,	{ MT_BBP(AGC, 53), 0x2626322f } },
	{ MT_CND_BW_20,			{ MT_BBP(AGC, 55), 0x40404e58 } },
	{		 MT_CND_BW_40,	{ MT_BBP(AGC, 55), 0x40405858 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{ MT_BBP(AGC, 58), 0x00001010 } },
};

static const struct mt76_reg_pair mt7630_bbp_dcoc_table[] = {
	{ MT_BBP(CAL, 47), 0x000010f0 },
	{ MT_BBP(CAL, 48), 0x00008080 },
	{ MT_BBP(CAL, 49), 0x00000f07 },
	{ MT_BBP(CAL, 50), 0x00000040 },
	{ MT_BBP(CAL, 51), 0x00000404 },
	{ MT_BBP(CAL, 52), 0x00080803 },
	{ MT_BBP(CAL, 53), 0x00000704 },
	{ MT_BBP(CAL, 54), 0x00002828 },
	{ MT_BBP(CAL, 55), 0x00005050 },
};

static const struct mt76_rf_reg_pair mt7630_rf_central_table[] = {
	/* Bank 0 - For central blocks: BG, PLL, XTAL, LO, ADC/DAC */
	{ 0,  1, 0x01 },
	{ 0,  2, 0x11 },
	/* R3 ~ R7: VCO Cal. */
	{ 0,  3, 0x70 }, /* VCO Freq Cal - No Bypass, VCO Amp Cal - No Bypass */
	{ 0,  4, 0x30 }, /* R4 b<7>=1, VCO cal */
	{ 0,  5, 0x00 },
	{ 0,  6, 0x41 }, /* Set the open loop amplitude to middle
			  * since bypassing amplitude calibration */
	{ 0,  7, 0x00 },
	/* XO */
	{ 0,  8, 0x00 },
	{ 0,  9, 0x00 },
	{ 0, 10, 0x0c },
	{ 0, 11, 0x00 },
	{ 0, 12, 0x00 },
	/* BG */
	{ 0, 13, 0x00 },
	{ 0, 14, 0x00 },
	{ 0, 15, 0x00 },
	/* LDO */
	{ 0, 19, 0x20 },
	/* XO */
	{ 0, 20, 0x22 },
	{ 0, 21, 0x12 },
#if 0
	{ 0, 22, 0x26/*0x3F*/ }, /* XTAL Freq offset, varies */
#endif
	{ 0, 23, 0x00 },
	{ 0, 24, 0x33 }, /* See band selection for R24<1:0> */
	{ 0, 25, 0x00 },
	/* PLL, See Freq Selection */
	{ 0, 26, 0x00 },
	{ 0, 27, 0x00 },
	{ 0, 28, 0x00 },
	{ 0, 29, 0x00 },
	{ 0, 30, 0x00 },
	{ 0, 31, 0x00 },
	{ 0, 32, 0x00 },
	{ 0, 33, 0x00 },
	{ 0, 34, 0x00 },
	{ 0, 35, 0x00 },
	{ 0, 36, 0x00 },
	{ 0, 37, 0x00 },
	/* LO Buffer */
	{ 0, 38, 0x2f },
	/* Test Ports */
	{ 0, 64, 0x00 },
	{ 0, 65, 0x80 },
	{ 0, 66, 0x01 },
	{ 0, 67, 0x04 },
	/* ADC/DAC */
	{ 0, 68, 0x00 },
	{ 0, 69, 0x08 },
	{ 0, 70, 0x08 },
	{ 0, 71, 0x40 },
	{ 0, 72, 0xd0 },
	{ 0, 73, 0x93 },
};

static const struct mt76_rf_reg_pair mt7630_rf_2g_channel0_table = {
	/* Bank 5 - Channel 0 2G RF registers */
	/* RX logic operation */
	/* RF_R00 Change in SelectBand6590 */
	{ 5,  2, 0x1d }, /* 0x1D: 5G+2G+BT(MT7650E),  0x0C: 5G+2G(MT7610U) */
	{ 5,  3, 0x00 },
	/* TX logic operation */
	{ 5,  4, 0x00 },
	{ 5,  5, 0x84 },
	{ 5,  6, 0x02 },
	/* LDO */
	{ 5,  7, 0x00 },
	{ 5,  8, 0x00 },
	{ 5,  9, 0x00 },
	/* RX */
	{ 5, 10, 0x51 },
	{ 5, 11, 0x22 },
	{ 5, 12, 0x22 },
	{ 5, 13, 0x0f },
	{ 5, 14, 0x47 }, /* Increase mixer current for more gain */
	{ 5, 15, 0x25 },
	{ 5, 16, 0xc7 }, /* Tune LNA2 tank */
	{ 5, 17, 0x00 },
	{ 5, 18, 0x00 },
	{ 5, 19, 0x30 }, /* Improve max Pin */
	{ 5, 20, 0x33 },
	{ 5, 21, 0x02 },
	{ 5, 22, 0x32 }, /* Tune LNA1 tank */
	{ 5, 23, 0x00 },
	{ 5, 24, 0x25 },
#if 0 /* R25 is used for BT. Let BT driver write it. */
	{ 5, 25, 0x13 },
#endif
	{ 5, 26, 0x00 },
	{ 5, 27, 0x12 },
	{ 5, 28, 0x0f },
	{ 5, 29, 0x00 },
	/* LOGEN */
	{ 5, 30, 0x51 }, /* Tune LOGEN tank */
	{ 5, 31, 0x35 },
	{ 5, 32, 0x31 },
	{ 5, 33, 0x31 },
	{ 5, 34, 0x34 },
	{ 5, 35, 0x03 },
	{ 5, 36, 0x00 },
	/* TX */
	{ 5, 37, 0xdd }, /* Improve 3.2GHz spur */
	{ 5, 38, 0xb3 },
	{ 5, 39, 0x33 },
	{ 5, 40, 0xb1 },
	{ 5, 41, 0x71 },
	{ 5, 42, 0xf2 },
	{ 5, 43, 0x47 },
	{ 5, 44, 0x77 },
	{ 5, 45, 0x0e },
	{ 5, 46, 0x10 },
	{ 5, 47, 0x00 },
	{ 5, 48, 0x53 },
	{ 5, 49, 0x03 },
	{ 5, 50, 0xef },
	{ 5, 51, 0xc7 },
	{ 5, 52, 0x62 },
	{ 5, 53, 0x62 },
	{ 5, 54, 0x00 },
	{ 5, 55, 0x00 },
	{ 5, 56, 0x0f },
	{ 5, 57, 0x0f },
	{ 5, 58, 0x16 },
	{ 5, 59, 0x16 },
	{ 5, 60, 0x10 },
	{ 5, 61, 0x10 },
	{ 5, 62, 0xd0 },
	{ 5, 63, 0x6c },
	{ 5, 64, 0x58 },
	{ 5, 65, 0x58 },
	{ 5, 66, 0xf2 },
	{ 5, 67, 0xe8 },
	{ 5, 68, 0xf0 },
	{ 5, 69, 0xf0 },
	{ 5, 127, 0x04 },
};

static const struct mt76_rf_reg_pair mt7630_rf_5g_channel0_table = {
	/* Bank 6 - Channel 0 5G RF registers */
	/* RX logic operation */
	/* RF_R00 Change in SelectBandMT76x0 */
	{ 6,  2, 0x0c },
	{ 6,  3, 0x00 },
	/* TX logic operation */
	{ 6,  4, 0x00 },
	{ 6,  5, 0x84 },
	{ 6,  6, 0x02 },
	/* LDO */
	{ 6,  7, 0x00 },
	{ 6,  8, 0x00 },
	{ 6,  9, 0x00 },
	/* RX */
	{ 6, 10, 0x00 },
	{ 6, 11, 0x01 },

	{ 6, 13, 0x23 },
	{ 6, 14, 0x00 },
	{ 6, 15, 0x04 },
	{ 6, 16, 0x22 },

	{ 6, 18, 0x08 },
	{ 6, 19, 0x00 },
	{ 6, 20, 0x00 },
	{ 6, 21, 0x00 },
	{ 6, 22, 0xfb },
	/* LOGEN5G */
	{ 6, 25, 0x76 },
	{ 6, 26, 0x24 },
	{ 6, 27, 0x04 },
	{ 6, 28, 0x00 },
	{ 6, 29, 0x00 },
	/* TX */
	{ 6, 37, 0xbb },
	{ 6, 38, 0xb3 },

	{ 6, 40, 0x33 },
	{ 6, 41, 0x33 },

	{ 6, 43, 0x03 },
	{ 6, 44, 0xb3 },

	{ 6, 46, 0x17 },
	{ 6, 47, 0x0e },
	{ 6, 48, 0x10 },
	{ 6, 49, 0x07 },

	{ 6, 62, 0x00 },
	{ 6, 63, 0x00 },
	{ 6, 64, 0xf1 },
	{ 6, 65, 0x0f },
};

static const struct mt76_rf_reg_pair mt7630_vga_channel0_table = {
	/* Bank 7 - Channel 0 VGA RF registers */
#if 0
	/* E2 0924 */
	{ 7,  0, 0x7f }, /* Allow BBP/MAC to do calibration */
#else
	/* E3 CR */
	{ 7,  0, 0x47 }, /* Allow BBP/MAC to do calibration */
#endif
	{ 7,  1, 0x00 },
	{ 7,  2, 0x00 },
	{ 7,  3, 0x00 },
	{ 7,  4, 0x00 },

	{ 7, 10, 0x13 },
	{ 7, 11, 0x0f },
	{ 7, 12, 0x13 }, /* For DCOC */
	{ 7, 13, 0x13 }, /* For DCOC */
	{ 7, 14, 0x13 }, /* For DCOC */
	{ 7, 15, 0x20 }, /* For DCOC */
	{ 7, 16, 0x22 }, /* For DCOC */

	{ 7, 17, 0x7c },

	{ 7, 18, 0x00 },
	{ 7, 19, 0x00 },
	{ 7, 20, 0x00 },
	{ 7, 21, 0xf1 },
	{ 7, 22, 0x11 },
	{ 7, 23, 0xc2 },
	{ 7, 24, 0x41 },
	{ 7, 25, 0x20 },
	{ 7, 26, 0x40 },
	{ 7, 27, 0xd7 },
	{ 7, 28, 0xa2 },
	{ 7, 29, 0x60 },
	{ 7, 30, 0x49 },
	{ 7, 31, 0x20 },
	{ 7, 32, 0x44 },
	{ 7, 33, 0xc1 },
	{ 7, 34, 0x60 },
	{ 7, 35, 0xc0 },

	{ 7, 61, 0x01 },

	{ 7, 72, 0x3c },
	{ 7, 73, 0x34 },
	{ 7, 74, 0x00 },
};

static const struct mt76_rf_reg_pair_cond mt7630_rf_bw_table[] = {
	{ MT_CND_BW_20 | MT_CND_BW_40,	{  0, 17, 0x00 } },
	// TODO: need to check B7.R6 & B7.R7 setting for 2.4G again @20121112
	{ MT_CND_BW_20,			{  7,  6, 0x40 } },
	{		 MT_CND_BW_40,	{  7,  6, 0x1c } },
	{ MT_CND_BW_20,			{  7,  7, 0x40 } },
	{		 MT_CND_BW_40,	{  7,  7, 0x20 } },
	{ MT_CND_BW_20,			{  7,  8, 0x03 } },
	{		 MT_CND_BW_40,	{  7,  8, 0x01 } },
	// TODO: need to check B7.R58 & B7.R59 setting for 2.4G again @20121112
	{ MT_CND_BW_20 | MT_CND_BW_40,	{  7, 58, 0x40 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{  7, 59, 0x40 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{  7, 60, 0xaa } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{  7, 76, 0x40 } },
	{ MT_CND_BW_20 | MT_CND_BW_40,	{  7, 77, 0x40 } },
};

static const struct mt76_rf_reg_pair mt7630_rf_band_table[] = {
	{ 0,  16, 0x20 },
	{ 0,  18, 0x00 },
	{ 0,  39, 0x36 },
	{ 6, 127, 0x84 },
	{ 7,  05, 0x40 },
	{ 7,  09, 0x00 },
	{ 7,  70, 0x00 },
	{ 7,  71, 0x00 },
	{ 7,  78, 0x00 },
	{ 7,  79, 0x00 },
};

struct mt76_freq_item {
	u8 Channel;
	u8 pllR37;
	u8 pllR36;
	u8 pllR35;
	u8 pllR34;
	u8 pllR33;
	u8 pllR32_b7b5;
	u8 pllR32_b4b0; /* PLL_DEN */
	u8 pllR31_b7b5;
	u8 pllR31_b4b0; /* PLL_K */
	u8 pllR30_b7; /* sdm_reset_n */
	u8 pllR30_b6b2; /* sdmmash_prbs,sin */
	u8 pllR30_b1; /* sdm_bp */
	u16 pll_n; /* R30<0>, R29<7:0> (hex) */
	u8 pllR28_b7b6; /* isi,iso */
	u8 pllR28_b5b4; /* pfd_dly */
	u8 pllR28_b3b2; /* clksel option */
	u32 Pll_sdm_k; /* R28<1:0>, R27<7:0>, R26<7:0> (hex) SDM_k */
	u8 pllR24_b1b0; /* xo_div */
};
static const struct mt76_freq_item mt7630_freq_table[] = {
	{  1, 0x02, 0x3f, 0x28, 0xdd, 0xe2, 0x40, 0x02, 0x40, 0x02,
	   0, 0, 1, 0x28, 0, 0x30, 0, 0, 0x3 },
	{  2, 0x02, 0x3f, 0x3c, 0xdd, 0xe4, 0x40, 0x07, 0x40, 0x02,
	   0, 0, 1, 0xa1, 0, 0x30, 0, 0, 0x1 },
	{  3, 0x02, 0x3f, 0x3c, 0xdd, 0xe2, 0x40, 0x07, 0x40, 0x0b,
	   0, 0, 1, 0x50, 0, 0x30, 0, 0, 0x0 },
	{  4, 0x02, 0x3f, 0x28, 0xdd, 0xd4, 0x40, 0x02, 0x40, 0x09,
	   0, 0, 1, 0x50, 0, 0x30, 0, 0, 0x0 },
	{  5, 0x02, 0x3f, 0x3c, 0xdd, 0xd4, 0x40, 0x07, 0x40, 0x02,
	   0, 0, 1, 0xa2, 0, 0x30, 0, 0, 0x1 },
	{  6, 0x02, 0x3f, 0x3c, 0xdd, 0xd4, 0x40, 0x07, 0x40, 0x07,
	   0, 0, 1, 0xa2, 0, 0x30, 0, 0, 0x1 },
	{  7, 0x02, 0x3f, 0x28, 0xdd, 0xe2, 0x40, 0x02, 0x40, 0x07,
	   0, 0, 1, 0x28, 0, 0x30, 0, 0, 0x3 },
	{  8, 0x02, 0x3f, 0x3c, 0xdd, 0xd4, 0x40, 0x07, 0x40, 0x02,
	   0, 0, 1, 0xa3, 0, 0x30, 0, 0, 0x1 },
	{  9, 0x02, 0x3f, 0x3c, 0xdd, 0xf2, 0x40, 0x07, 0x40, 0x0d,
	   0, 0, 1, 0x28, 0, 0x30, 0, 0, 0x3 },
	{ 10, 0x02, 0x3f, 0x28, 0xdd, 0xd4, 0x40, 0x02, 0x40, 0x09,
	   0, 0, 1, 0x51, 0, 0x30, 0, 0, 0x0 },
	{ 11, 0x02, 0x3f, 0x3c, 0xdd, 0xd4, 0x40, 0x07, 0x40, 0x02,
	   0, 0, 1, 0xa4, 0, 0x30, 0, 0, 0x1 },
	{ 12, 0x02, 0x3f, 0x3c, 0xdd, 0xd4, 0x40, 0x07, 0x40, 0x07,
	   0, 0, 1, 0xa4, 0, 0x30, 0, 0, 0x1 },
	{ 13, 0x02, 0x3f, 0x28, 0xdd, 0xf2, 0x40, 0x02, 0x40, 0x02,
	   0, 0, 1, 0x29, 0, 0x30, 0, 0, 0x3 },
	{ 14, 0x02, 0x3f, 0x28, 0xdd, 0xf2, 0x40, 0x02, 0x40, 0x04,
	   0, 0, 1, 0x29, 0, 0x30, 0, 0, 0x3 },
};

static bool
mt76_wait_for_bbp(struct mt76_dev *dev)
{
	int i;

	for (i = 0; i < 5; i++) {
		switch (mt76_rr(dev, MT_BBP(CORE, 0))) {
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

static void
mt7630_write_bbp_initvals(struct mt76_dev *dev)
{
	static mt76_reg_pair vals[] = {
		{ MT_BBP(CORE, 1),		0x00000002 },
		{ MT_BBP(CORE, 4),		0x00000000 },
		{ MT_BBP(CORE, 24),		0x00000000 },
		{ MT_BBP(CORE, 32),		0x4003000a },
		{ MT_BBP(CORE, 42),		0x00000000 },
		{ MT_BBP(CORE, 44),		0x00000000 },

		/* (0x212C) disable Tx fifo empty check to avoid hang issue
		 * - MT7650E3_BBP_CR_20121225.xls */

		{ MT_BBP(IBI, 11),		0x00000080 },

		/* 0x2300[5] Default Antenna:
		 *		0 for WIFI main antenna
		 *		1 for WIFI aux  antenna	*/
		{ MT_BBP(AGC1, 0),		0x00021400 },

		{ MT_BBP(AGC1, 1),		0x00000003 },
		{ MT_BBP(AGC1, 2),		0x003a6464 },

		{ MT_BBP(AGC1, 15),		0x88a28cb8 },
		{ MT_BBP(AGC1, 22),		0x00001e21 },
		{ MT_BBP(AGC1, 23),		0x0000272c },
		{ MT_BBP(AGC1, 24),		0x00002f3a },
		{ MT_BBP(AGC1, 25),		0x8000005a },

		{ MT_BBP(AGC1, 33),		0x00003218 },
		{ MT_BBP(AGC1, 34),		0x000a0c0c },
		{ MT_BBP(AGC1, 37),		0x2121262c },
		{ MT_BBP(AGC1, 41),		0x38383e45 },
		{ MT_BBP(AGC1, 57),		0x00001010 },

		{ MT_BBP(AGC1, 59),		0xbaa20e96 },
		{ MT_BBP(AGC1, 63),		0x00000001 },

		{ MT_BBP(TXC, 0),		0x00280403 },
		{ MT_BBP(TXC, 1),		0x00000000 },
		{ MT_BBP(TXC, 2),		0x00005555 },

		{ MT_BBP(RXC, 1),		0x00000012 },
		{ MT_BBP(RXC, 2),		0x00000011 },
		{ MT_BBP(RXC, 3),		0x00000005 },
		{ MT_BBP(RXC, 4),		0x00000000 },
		{ MT_BBP(RXC, 5),		0xf977c4ec },
		{ MT_BBP(RXC, 7),		0x00000090 },

		{ MT_BBP(TXO, 8),		0x00000000 },

		{ MT_BBP(TXBE, 0),		0x00000000 },
		{ MT_BBP(TXBE, 4),		0x00000004 },
		{ MT_BBP(TXBE, 6),		0x00000000 },
		{ MT_BBP(TXBE, 8),		0x00000014 },
		{ MT_BBP(TXBE, 9),		0x20000000 },
		{ MT_BBP(TXBE, 10),		0x00000000 },
		{ MT_BBP(TXBE, 12),		0x00000000 },
		{ MT_BBP(TXBE, 13),		0x00000000 },
		{ MT_BBP(TXBE, 14),		0x00000000 },
		{ MT_BBP(TXBE, 15),		0x00000000 },
		{ MT_BBP(TXBE, 16),		0x00000000 },
		{ MT_BBP(TXBE, 17),		0x00000000 },

		{ MT_BBP(RXFE, 0),		0x895000e0 },		// TODO: This value is for 5G. 2G has different setting.
		//{ MT_BBP(RXFE, 0),		0x005000e0 },
		{ MT_BBP(RXFE, 1),		0x00008800 },		/* Add for E3 */
		{ MT_BBP(RXFE, 3),		0x00000000 },
		{ MT_BBP(RXFE, 4),		0x00000000 },

		{ MT_BBP(RXO, 13),		0x00000092 },
		{ MT_BBP(RXO, 14),		0x00060612 },
		{ MT_BBP(RXO, 15),		0xc8321b18 },
		{ MT_BBP(RXO, 16),		0x0000001e },
		{ MT_BBP(RXO, 17),		0x00000000 },
		{ MT_BBP(RXO, 18),		0xcc00a993 },
		{ MT_BBP(RXO, 19),		0xb9cb9cb9 },
		{ MT_BBP(RXO, 21),		0x00000001 },
		{ MT_BBP(RXO, 24),		0x00000006 },
		{ MT_BBP(RXO, 28),		0x0000003f },
	};

	mt76_write_reg_pairs(dev, vals, ARRAY_SIZE(vals));
}

static void
mt7630_switch_bbp_bw_vals(struct mt76_dev *dev, enum mt76_phy_bandwidth bw)
{
	mt76_write_reg_pairs_cond(dev, mt7630_bbp_bw_table,
				  ARRAY_SIZE(mt7630_bbp_bw_table),
				  MT_CND_BW_20, MT_CND_BW_20);
}

static void
mt7630_coex_init(struct mt76_dev *dev)
{
	u32 wlan, cmb, coex0, coex3;

	wlan = mt76_rr(dev, MT_WLAN_FUN_CTRL);
	cmb = mt76_rr(dev, MT_CMB_CTRL);
	coex0 = mt76_rr(dev, MT_COEXCFG0);
	coex3 = mt76_rr(dev, MT_COEXCFG3);

	wlan &= ~MT_WLAN_FUN_CTRL_FRC_WL_ANT_SEL;
	wlan |=  MT_WLAN_FUN_CTRL_INV_ANT_SEL;
	cmb |= MT_CMB_CTRL_RESV |
	       MT_CMB_CTRL_TRSW0_AS_WL_ANT_SEL |
	       MT_CMB_CTRL_TRSW1_AS_GPIO;
	coex0 &= ~BIT(2);
	coex3 &= ~GENMASK(5, 1);
	coex3 |= BIT(3);

	mt76_wr(dev, MT_WLAN_FUN_CTRL, wlan);
	mt76_wr(dev, MT_CMB_CTRL, cmb);
	mt76_wr(dev, MT_COEXCFG0, coex0);
	mt76_wr(dev, MT_COEXCFG3, coex3);
}

int mt7630_phy_init(struct mt76_dev *dev)
{
	int i, ret;

	if (!mt76_wait_for_bbp(dev))
		return -EIO;

	mt7630_write_bbp_initvals(dev);
	mt7630_switch_bbp_bw_vals(dev, MT_CND_BW_20);
	mt76_write_reg_pairs(dev, mt7630_bbp_dcoc_table,
			     ARRAY_SIZE(mt7630_bbp_dcoc_table));

	mt76_set(dev, MT_MAX_LEN_CFG, 0x3fff);
	mt76_wr(dev, MT_TXOP_CTRL_CFG, 0x0000583f); /* mac_init does this too */

	mt7630_coex_init(dev);

	mt76_wr(dev, MT_TX_PWR_CFG_0, 0x00000000);
	mt76_wr(dev, MT_TX_PWR_CFG_1, 0x00000000);
	mt76_wr(dev, MT_TX_PWR_CFG_2, 0x00000000);
	mt76_wr(dev, MT_TX_PWR_CFG_3, 0x3c3c0000);
	mt76_wr(dev, MT_TX_PWR_CFG_4, 0x3b000000);
	mt76_wr(dev, MT_TX_PWR_CFG_8, 0x00000000);
	mt76_wr(dev, MT_TX_PWR_CFG_7, 0x00000000);
	mt76_wr(dev, MT_TX_PWR_CFG_9, 0x00000000);

	msleep(10);

	for (i = 0; i < ARRAY_SIZE(mt7630_rf_central_table); i++) {
		ret = mt76_phy_rf_pair_wr(dev, mt7630_rf_central_table[i]);
		if (ret)
			return ret;
	}
	for (i = 0; i < ARRAY_SIZE(mt7630_rf_2g_channel0_table); i++) {
		ret = mt76_phy_rf_pair_wr(dev, mt7630_rf_2g_channel0_table[i]);
		if (ret)
			return ret;
	}
	for (i = 0; i < ARRAY_SIZE(mt7630_rf_5g_channel0_table); i++) {
		ret = mt76_phy_rf_pair_wr(dev, mt7630_rf_5g_channel0_table[i]);
		if (ret)
			return ret;
	}
	for (i = 0; i < ARRAY_SIZE(mt7630_vga_channel0_table); i++) {
		ret = mt76_phy_rf_pair_wr(dev, mt7630_vga_channel0_table[i]);
		if (ret)
			return ret;
	}
	for (i = 0; i < ARRAY_SIZE(mt7630_rf_bw_table); i++) {
		if (!(mt7630_rf_bw_table[i].flags & MT_CND_BW_20))
			continue;
		ret = mt76_phy_rf_pair_wr(dev, mt7630_rf_bw_table[i]);
		if (ret)
			return ret;
	}
	for (i = 0; i < ARRAY_SIZE(mt7630_rf_band_table); i++) {
		ret = mt76_phy_rf_pair_wr(dev, mt7630_rf_band_table[i]);
		if (ret)
			return ret;
	}

	return 0;
}
