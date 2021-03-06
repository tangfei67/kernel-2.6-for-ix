/****************************************************************************
 * Driver for Solarflare network controllers -
 *          resource management for Xen backend, OpenOnload, etc
 *           (including support for SFE4001 10GBT NIC)
 *
 * This file provides workaround settings for EtherFabric NICs.
 *
 * Copyright 2005-2007: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Developed and maintained by Solarflare Communications:
 *                      <linux-xen-drivers@solarflare.com>
 *                      <onload-dev@solarflare.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#ifndef __CI_DRIVER_EFAB_WORKAROUNDS_H__
#define __CI_DRIVER_EFAB_WORKAROUNDS_H__

/*----------------------------------------------------------------------------
 *
 * Hardware workarounds which have global scope
 *
 *---------------------------------------------------------------------------*/

#if defined(__CI_HARDWARE_CONFIG_FALCON_B0__)
/*------------------------------- B0 ---------------------------------------*/

#define BUG2175_WORKAROUND 0	/* TX event batching for dual port operation.
				   This removes the effect (dup TX events)
				   of the fix
				   (TX event per packet + batch events) */
#define BUG5302_WORKAROUND 0	/* unstick TX DMAQ after out-of-range wr ptr */
#define BUG5762_WORKAROUND 0	/* Set all queues to jumbo mode */
#define BUG5391_WORKAROUND 0	/* Misaligned TX can't span 512-byte boundary */
#define BUG7916_WORKAROUND 0	/* RX flush gets lost */

#else
/*------------------------------- A0/A1 ------------------------------------*/

#define BUG2175_WORKAROUND 1	/* TX event batching for dual port operation.
				   This removes the effect (dup TX events)
				   of the fix
				   (TX event per packet + batch events) */
#define BUG5302_WORKAROUND 1	/* unstick TX DMAQ after out-of-range wr ptr */
#define BUG5762_WORKAROUND 1	/* Set all queues to jumbo mode */
#define BUG5391_WORKAROUND 1	/* Misaligned TX can't span 512-byte boundary */
#define BUG7916_WORKAROUND 1	/* RX flush gets lost */

#endif /* B0/A01 */

#endif /* __CI_DRIVER_EFAB_WORKAROUNDS_H__ */
