/*******************************************************************************
  Specialised functions for managing Ring mode

  Copyright(C) 2011  STMicroelectronics Ltd

  It defines all the functions used to handle the normal/enhanced
  descriptors in case of the DMA is configured to work in chained or
  in ring mode.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include "stmmac.h"

static unsigned int stmmac_jumbo_frm(void *pt, struct sk_buff *skb, int csum)
{
	struct stmmac_priv *priv = (struct stmmac_priv *)pt;
	unsigned int txsize = priv->dma_tx_size;
	unsigned int entry = priv->cur_tx % txsize;
	struct dma_extended_desc *p = priv->dma_etx + entry;
	struct dma_desc *desc = &p->basic;
	unsigned int nopaged_len = skb_headlen(skb);
	unsigned int bmax, len;
	dma_addr_t dma_phys;

	bmax = BUF_SIZE_8KiB;

	len = nopaged_len - bmax;

	if (nopaged_len > bmax*2) {

		desc->des2 = dma_phys = dma_map_single(priv->device, skb->data,
					    bmax, DMA_TO_DEVICE);
		desc->des3 = dma_phys >> 32;
		priv->tx_skbuff_dma[entry] = dma_phys;
		p->des6 = dma_phys + bmax;
		p->des7 = (dma_phys + bmax) >> 32;
		priv->hw->desc->prepare_tx_desc(desc, 1, bmax, csum,
						STMMAC_RING_MODE);
		wmb();
		entry = (++priv->cur_tx) % txsize;
		p = priv->dma_etx + entry;
		desc = &p->basic;

		desc->des2 = dma_phys = dma_map_single(priv->device, skb->data + bmax,
					    len, DMA_TO_DEVICE);
		desc->des3 = dma_phys >> 32;
		priv->tx_skbuff_dma[entry] = dma_phys;
		p->des6 = dma_phys + bmax;
		p->des7 = (dma_phys + bmax) >> 32;
		priv->hw->desc->prepare_tx_desc(desc, 0, len, csum,
						STMMAC_RING_MODE);
		wmb();
		priv->hw->desc->set_tx_owner(desc);
		priv->tx_skbuff[entry] = NULL;
	} else {
		desc->des2 = dma_phys = dma_map_single(priv->device, skb->data,
					    nopaged_len, DMA_TO_DEVICE);
		desc->des3 = dma_phys >> 32;
		priv->tx_skbuff_dma[entry] = dma_phys;
		p->des6 = dma_phys + bmax;
		p->des7 = (dma_phys + bmax) >> 32;
		priv->hw->desc->prepare_tx_desc(desc, 1, nopaged_len, csum,
						STMMAC_RING_MODE);
	}

	return entry;
}

static unsigned int stmmac_is_jumbo_frm(int len, int enh_desc)
{
	unsigned int ret = 0;

	if (len >= BUF_SIZE_16KiB)
		ret = 1;

	return ret;
}

static void stmmac_refill_desc3(void *priv_ptr, struct dma_desc *desc)
{
	struct stmmac_priv *priv = (struct stmmac_priv *)priv_ptr;
	struct dma_extended_desc *p = (struct dma_extended_desc *)desc;

		/* Fill DES3 in case of RING mode */
		if (priv->dma_buf_sz >= BUF_SIZE_8KiB)
		{
			p->des6 = p->basic.des2 + BUF_SIZE_8KiB;
			p->des7 = p->basic.des3;
		}
}

/* In ring mode we need to fill the desc3 because it is used as buffer */
static void stmmac_init_desc3(struct dma_desc *desc)
{
	struct dma_extended_desc *p = (struct dma_extended_desc *)desc;
	p->des6 = p->basic.des2 + BUF_SIZE_8KiB;
	p->des7 = p->basic.des3;
}

static void stmmac_clean_desc3(void *priv_ptr, struct dma_desc *p)
{
	if (unlikely(p->des3))
		p->des3 = 0;
}

static int stmmac_set_16kib_bfsize(int mtu)
{
	return ALIGN(mtu + ETH_HLEN + 4 + NET_IP_ALIGN, AXIWIDTH);
}

const struct stmmac_ring_mode_ops ring_mode64_ops = {
	.is_jumbo_frm = stmmac_is_jumbo_frm,
	.jumbo_frm = stmmac_jumbo_frm,
	.refill_desc3 = stmmac_refill_desc3,
	.init_desc3 = stmmac_init_desc3,
	.clean_desc3 = stmmac_clean_desc3,
	.set_16kib_bfsize = stmmac_set_16kib_bfsize,
};
