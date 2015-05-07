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

#include "mt76.h"
#include "dma.h"

struct mt76_txwi_cache {
	struct mt76_txwi txwi;
	dma_addr_t dma_addr;
	struct list_head list;
};

static inline int
mt76_rx_buf_offset(struct mt76_dev *dev)
{
	BUILD_BUG_ON(MT_RX_HEADROOM < sizeof(struct mt76_rxwi));

	return MT_RX_HEADROOM - sizeof(struct mt76_rxwi);
}

static struct mt76_txwi_cache *
mt76_alloc_txwi(struct mt76_dev *dev)
{
	struct mt76_txwi_cache *t;
	dma_addr_t addr;
	int size;

	size = (sizeof(*t) + L1_CACHE_BYTES - 1) & ~(L1_CACHE_BYTES - 1);
	t = devm_kzalloc(dev->dev, size, GFP_ATOMIC);
	if (!t)
		return NULL;

	addr = dma_map_single(dev->dev, &t->txwi, sizeof(t->txwi), DMA_TO_DEVICE);
	t->dma_addr = addr;

	return t;
}

static struct mt76_txwi_cache *
__mt76_get_txwi(struct mt76_dev *dev)
{
	struct mt76_txwi_cache *t = NULL;

	spin_lock_bh(&dev->lock);
	if (!list_empty(&dev->txwi_cache)) {
		t = list_first_entry(&dev->txwi_cache, struct mt76_txwi_cache,
				     list);
		list_del(&t->list);
	}
	spin_unlock_bh(&dev->lock);

	return t;
}

static struct mt76_txwi_cache *
mt76_get_txwi(struct mt76_dev *dev)
{
	struct mt76_txwi_cache *t = __mt76_get_txwi(dev);

	if (t)
		return t;

	return mt76_alloc_txwi(dev);
}

static void
mt76_put_txwi(struct mt76_dev *dev, struct mt76_txwi_cache *t)
{
	if (!t)
		return;

	spin_lock_bh(&dev->lock);
	list_add(&t->list, &dev->txwi_cache);
	spin_unlock_bh(&dev->lock);
}

static int
mt76_alloc_queue(struct mt76_dev *dev, struct mt76_queue *q)
{
	int size;
	int i;

	size = q->ndesc * sizeof(struct mt76_desc);
	q->desc = dmam_alloc_coherent(dev->dev, size, &q->desc_dma, GFP_KERNEL);
	if (!q->desc)
		return -ENOMEM;

	size = q->ndesc * sizeof(*q->entry);
	q->entry = devm_kzalloc(dev->dev, size, GFP_KERNEL);
	if (!q->entry)
		return -ENOMEM;

	/* clear descriptors */
	for (i = 0; i < q->ndesc; i++)
		q->desc[i].ctrl = cpu_to_le32(MT_DMA_CTL_DMA_DONE);

	iowrite32(q->desc_dma, &q->regs->desc_base);
	iowrite32(0, &q->regs->cpu_idx);
	iowrite32(0, &q->regs->dma_idx);
	iowrite32(q->ndesc, &q->regs->ring_size);

	return 0;
}

void mt76_kick_queue(struct mt76_dev *dev, struct mt76_queue *q)
{
	iowrite32(q->head, &q->regs->cpu_idx);
}

static int
mt76_queue_add_buf(struct mt76_dev *dev, struct mt76_queue *q,
		   u32 buf0, int len0, u32 buf1, int len1, u32 info)
{
	struct mt76_desc *desc;
	u32 ctrl;
	int idx;

	ctrl = MT76_SET(MT_DMA_CTL_SD_LEN0, len0) |
	       MT76_SET(MT_DMA_CTL_SD_LEN1, len1) |
	       (len1 ? MT_DMA_CTL_LAST_SEC1 : MT_DMA_CTL_LAST_SEC0);

	idx = q->head;
	q->head = (q->head + 1) % q->ndesc;

	desc = &q->desc[idx];

	ACCESS_ONCE(desc->buf0) = cpu_to_le32(buf0);
	ACCESS_ONCE(desc->buf1) = cpu_to_le32(buf1);
	ACCESS_ONCE(desc->info) = cpu_to_le32(info);
	ACCESS_ONCE(desc->ctrl) = cpu_to_le32(ctrl);

	q->queued++;

	return idx;
}

static int
mt76_dma_rx_fill(struct mt76_dev *dev, struct mt76_queue *q)
{
	dma_addr_t addr;
	void *buf;
	int frames = 0;
	int len = SKB_WITH_OVERHEAD(q->buf_size);
	int idx;

	spin_lock_bh(&q->lock);

	while (q->queued < q->ndesc - 1) {
		int offset = mt76_rx_buf_offset(dev);

		buf = kzalloc(len, GFP_ATOMIC);
		if (!buf)
			break;

		addr = dma_map_single(dev->dev, buf, len, DMA_FROM_DEVICE);

		if (dma_mapping_error(dev->dev, addr)) {
			kfree(buf);
			break;
		}

		idx = mt76_queue_add_buf(dev, q, addr + offset, len - offset,
					 0, 0, 0);

		q->entry[idx].buf = buf;
		frames++;
	}

	if (frames)
		mt76_kick_queue(dev, q);

	spin_unlock_bh(&q->lock);

	return frames;
}

int __mt76_tx_queue_skb(struct mt76_dev *dev, enum mt76_txq_id qid,
			struct sk_buff *skb, u32 tx_info)
{
	struct mt76_queue *q = &dev->q_tx[qid];
	dma_addr_t addr;
	int idx;

	addr = dma_map_single(dev->dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev->dev, addr))
		return -ENOMEM;

	spin_lock_bh(&q->lock);
	idx = mt76_queue_add_buf(dev, q, addr, skb->len, 0, 0, tx_info);
	q->entry[idx].skb = skb;
	mt76_kick_queue(dev, q);
	spin_unlock_bh(&q->lock);

	return 0;
}

int mt76_tx_queue_skb(struct mt76_dev *dev, struct mt76_queue *q,
		      struct sk_buff *skb, struct mt76_wcid *wcid,
		      struct ieee80211_sta *sta)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76_txwi_cache *t;
	dma_addr_t addr;
	u32 tx_info = 0;
	int idx, ret, len;
	int qsel = MT_QSEL_EDCA;

	ret = -ENOMEM;
	t = mt76_get_txwi(dev);
	if (!t)
		goto free;

	dma_sync_single_for_cpu(dev->dev, t->dma_addr, sizeof(t->txwi),
				DMA_TO_DEVICE);
	mt76_mac_write_txwi(dev, &t->txwi, skb, wcid, sta);
	dma_sync_single_for_device(dev->dev, t->dma_addr, sizeof(t->txwi),
				   DMA_TO_DEVICE);

	ret = mt76_insert_hdr_pad(skb);
	if (ret)
		goto put_txwi;

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		qsel = 0;

	len = skb->len + sizeof(t->txwi);
	len += mt76_mac_skb_tx_overhead(dev, skb);
	tx_info = MT76_SET(MT_TXD_INFO_LEN, len) |
		  MT76_SET(MT_TXD_INFO_QSEL, qsel) |
		  MT_TXD_INFO_80211;

	if (!wcid || wcid->hw_key_idx == 0xff)
		tx_info |= MT_TXD_INFO_WIV;

	addr = dma_map_single(dev->dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev->dev, addr))
		goto put_txwi;

	idx = mt76_queue_add_buf(dev, q, t->dma_addr, sizeof(t->txwi),
				 addr, skb->len, tx_info);
	q->entry[idx].skb = skb;
	q->entry[idx].txwi = t;

	return idx;

put_txwi:
	mt76_put_txwi(dev, t);
free:
	ieee80211_free_txskb(dev->hw, skb);
	return ret;
}

static int
mt76_dma_dequeue(struct mt76_dev *dev, struct mt76_queue *q, bool flush)
{
	int ret = -1;

	spin_lock_bh(&q->lock);

	if (!q->queued)
		goto out;

	if (!flush && !(q->desc[q->tail].ctrl & cpu_to_le32(MT_DMA_CTL_DMA_DONE)))
		goto out;

	ret = q->tail;
	q->tail = (q->tail + 1) % q->ndesc;
	q->queued--;

out:
	spin_unlock_bh(&q->lock);
	return ret;
}

static void
mt76_tx_cleanup_entry(struct mt76_dev *dev, struct mt76_queue *q, int idx,
		      bool flush)
{
	struct mt76_queue_entry *e = &q->entry[idx];
	struct sk_buff *skb = e->skb;
	struct mt76_txwi_cache *txwi = e->txwi;
	dma_addr_t skb_addr;
	bool schedule = e->schedule;

	if (txwi)
		skb_addr = ACCESS_ONCE(q->desc[idx].buf1);
	else
		skb_addr = ACCESS_ONCE(q->desc[idx].buf0);

	dma_unmap_single(dev->dev, skb_addr, skb->len, DMA_TO_DEVICE);
	e->skb = NULL;
	e->txwi = NULL;
	e->schedule = false;

	if (txwi) {
		skb_orphan(skb);
		mt76_mac_queue_txdone(dev, skb, &txwi->txwi);
		mt76_put_txwi(dev, txwi);

		if (schedule) {
			spin_lock_bh(&q->lock);
			q->swq_queued--;
			if (!flush)
				mt76_txq_schedule(dev, q);
			spin_unlock_bh(&q->lock);
		}
	} else {
		dev_kfree_skb_any(skb);
	}
}

static void
mt76_tx_cleanup_queue(struct mt76_dev *dev, struct mt76_queue *q, bool flush)
{
	int idx;

	do {
		idx = mt76_dma_dequeue(dev, q, flush);
		if (idx < 0)
			break;

		mt76_tx_cleanup_entry(dev, q, idx, flush);
	} while (1);
}

static void *
mt76_rx_get_buf(struct mt76_dev *dev, struct mt76_queue *q, int idx, int *len)
{
	struct mt76_queue_entry *e;
	dma_addr_t buf_addr;
	void *buf;

	e = &q->entry[idx];
	buf = e->buf;
	buf_addr = ACCESS_ONCE(q->desc[idx].buf0);
	if (len) {
		u32 ctl = ACCESS_ONCE(q->desc[idx].ctrl);
		*len = MT76_GET(MT_DMA_CTL_SD_LEN0, ctl);
	}
	dma_unmap_single(dev->dev, buf_addr, q->buf_size, DMA_FROM_DEVICE);
	e->buf = NULL;

	return buf;
}

static void
mt76_rx_cleanup(struct mt76_dev *dev, struct mt76_queue *q)
{
	void *buf;
	int idx;

	do {
		idx = mt76_dma_dequeue(dev, q, true);
		if (idx < 0)
			break;

		buf = mt76_rx_get_buf(dev, q, idx, NULL);
		kfree(buf);
	} while (1);
}

static int
mt76_init_tx_queue(struct mt76_dev *dev, struct mt76_queue *q,
		   int idx, int n_desc, bool mcu)
{
	int ret;

	q->regs = dev->regs + MT_TX_RING_BASE + idx * MT_RING_SIZE;
	q->ndesc = n_desc;

	ret = mt76_alloc_queue(dev, q);
	if (ret)
		return ret;

	mt76_irq_enable(dev, MT_INT_TX_DONE(idx));

	return 0;
}

static void
mt76_process_rx_skb(struct mt76_dev *dev, struct mt76_queue *q,
		    struct sk_buff *skb, u32 info)
{
	void *rxwi = skb->data - sizeof(struct mt76_rxwi);

	if (q == &dev->mcu.q_rx) {
		u32 *rxfce;

		/* No RXWI header, data starts with payload */
		skb_push(skb, sizeof(struct mt76_rxwi));

		rxfce = (u32 *) skb->cb;
		*rxfce = info;

		skb_queue_tail(&dev->mcu.res_q, skb);
		wake_up(&dev->mcu.wait);
		return;
	}

	memset(skb->cb, 0, sizeof(skb->cb));
	if (mt76_mac_process_rx(dev, skb, rxwi)) {
	    dev_kfree_skb(skb);
	    return;
	}

	mt76_rx(dev, skb);
}

static int
mt76_process_rx_queue(struct mt76_dev *dev, struct mt76_queue *q, int budget)
{
	struct mt76_desc *desc;
	struct sk_buff *skb;
	unsigned char *data;
	int idx, len;
	int done = 0;

	while (done < budget) {
		idx = mt76_dma_dequeue(dev, q, false);
		if (idx < 0)
			break;

		data = mt76_rx_get_buf(dev, q, idx, &len);
		skb = build_skb(data, 0);
		if (!skb) {
			kfree(data);
			continue;
		}

		skb_reserve(skb, MT_RX_HEADROOM);
		skb_put(skb, len);

		desc = &q->desc[idx];
		mt76_process_rx_skb(dev, q, skb, le32_to_cpu(desc->info));
		done++;
	}

	mt76_dma_rx_fill(dev, q);
	return done;
}

static int
mt76_init_rx_queue(struct mt76_dev *dev, struct mt76_queue *q,
		   int idx, int n_desc, int bufsize)
{
	int ret;

	q->regs = dev->regs + MT_RX_RING_BASE + idx * MT_RING_SIZE;
	q->ndesc = n_desc;
	q->buf_size = bufsize;

	ret = mt76_alloc_queue(dev, q);
	if (ret)
		return ret;

	mt76_irq_enable(dev, MT_INT_RX_DONE(idx));

	return 0;
}

static void
mt76_tx_tasklet(unsigned long data)
{
	struct mt76_dev *dev = (struct mt76_dev *) data;
	int i;

	mt76_mac_process_tx_status_fifo(dev);

	for (i = ARRAY_SIZE(dev->q_tx) - 1; i >= 0; i--)
		mt76_tx_cleanup_queue(dev, &dev->q_tx[i], false);

	mt76_mac_poll_tx_status(dev, false);
	mt76_irq_enable(dev, MT_INT_TX_DONE_ALL);
}

static int
mt76_dma_rx_poll(struct napi_struct *napi, int budget)
{
	struct mt76_dev *dev = container_of(napi, struct mt76_dev, napi);
	int done;

	done = mt76_process_rx_queue(dev, &dev->q_rx, budget);

	if (done < budget) {
		napi_complete(napi);
		mt76_irq_enable(dev, MT_INT_RX_DONE(0));
	}

	return done;
}

static void
mt76_rx_tasklet(unsigned long data)
{
	struct mt76_dev *dev = (struct mt76_dev *) data;

	mt76_process_rx_queue(dev, &dev->mcu.q_rx, dev->mcu.q_rx.ndesc);

	mt76_irq_enable(dev, MT_INT_RX_DONE(1));
}

int mt76_dma_init(struct mt76_dev *dev)
{
	static const u8 wmm_queue_map[] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 2,
		[IEEE80211_AC_VO] = 3,
	};
	int ret;
	int i;

	init_waitqueue_head(&dev->mcu.wait);
	skb_queue_head_init(&dev->mcu.res_q);

	spin_lock_init(&dev->q_rx.lock);
	spin_lock_init(&dev->mcu.q_rx.lock);
	for (i = 0; i < ARRAY_SIZE(dev->q_tx); i++) {
		spin_lock_init(&dev->q_tx[i].lock);
		INIT_LIST_HEAD(&dev->q_tx[i].swq);
	}

	init_dummy_netdev(&dev->napi_dev);
	netif_napi_add(&dev->napi_dev, &dev->napi, mt76_dma_rx_poll, 64);

	tasklet_init(&dev->tx_tasklet, mt76_tx_tasklet, (unsigned long) dev);
	tasklet_init(&dev->rx_tasklet, mt76_rx_tasklet, (unsigned long) dev);

	mt76_wr(dev, MT_WPDMA_RST_IDX, ~0);

	for (i = 0; i < ARRAY_SIZE(wmm_queue_map); i++) {
		ret = mt76_init_tx_queue(dev, &dev->q_tx[i], wmm_queue_map[i],
					 MT_TX_RING_SIZE, false);
		if (ret)
			return ret;
	}

	ret = mt76_init_tx_queue(dev, &dev->q_tx[MT_TXQ_PSD],
				 MT_TX_HW_QUEUE_MGMT, MT_TX_RING_SIZE, false);
	if (ret)
		return ret;

	ret = mt76_init_tx_queue(dev, &dev->q_tx[MT_TXQ_MCU],
				 MT_TX_HW_QUEUE_MCU, MT_MCU_RING_SIZE, true);
	if (ret)
		return ret;

	ret = mt76_init_rx_queue(dev, &dev->mcu.q_rx, 1, MT_MCU_RING_SIZE,
				 MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	ret = mt76_init_rx_queue(dev, &dev->q_rx, 0,
				 MT_RX_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	mt76_dma_rx_fill(dev, &dev->q_rx);
	mt76_dma_rx_fill(dev, &dev->mcu.q_rx);

	return 0;
}

void mt76_dma_cleanup(struct mt76_dev *dev)
{
	struct mt76_txwi_cache *t;
	int i;

	tasklet_kill(&dev->tx_tasklet);
	tasklet_kill(&dev->rx_tasklet);
	for (i = 0; i < ARRAY_SIZE(dev->q_tx); i++)
		mt76_tx_cleanup_queue(dev, &dev->q_tx[i], true);
	mt76_rx_cleanup(dev, &dev->q_rx);
	mt76_rx_cleanup(dev, &dev->mcu.q_rx);

	while ((t = __mt76_get_txwi(dev)) != NULL)
		dma_unmap_single(dev->dev, t->dma_addr, sizeof(t->txwi),
				 DMA_TO_DEVICE);
}
