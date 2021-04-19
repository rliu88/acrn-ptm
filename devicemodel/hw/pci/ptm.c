/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pcireg.h"
#include "pciaccess.h"
#include "pci_core.h"
#include "ptm.h"
#include "passthru.h"
#include "vmmapi.h"


#define PCIZ_PTM    0x1fU   /* capability id of precision time measurement */

/* PTM register Definitions */
#define PCIR_PTM_CAP                0x04U   /* PTM capability register */
    #define PCIM_PTM_CAP_REQ            0x01U   /* Requestor capable */
    #define PCIM_PTM_CAP_ROOT           0x4U    /* Root capable */
    #define PCIM_PTM_GRANULARITY        0xFF00  /* Clock granularity */
#define PCIR_PTM_CTRL               0x08U   /* PTM control register */
    #define PCIM_PTM_CTRL_ENABLE        0x1U    /* PTM enable */
    #define PCIM_PTM_CTRL_ROOT_SELECT   0x2U    /* Root select */

/* virtual root port secondary bus, indexed by vm_id */
static int VROOT_PORTS_SEC_BUS_ARRAY[128] = {0};

struct vroot_port_priv
{
    uint32_t phy_bdf;
    uint8_t primary_bus;
    uint8_t secondary_bus;
    uint8_t subordinate_bus;
    uint8_t ptm_capable;
    uint32_t ptm_cap_offset;
};

/* find position of capability register cap_id */
static int
pci_find_capability(struct pci_device *pdev, const int cap_id)
{
    uint8_t cap_pos, cap_data;
    uint16_t status = 0;

    pci_device_cfg_read_u16(pdev, &status, PCIR_STATUS);
	if (status & PCIM_STATUS_CAPPRESENT) {

		pci_device_cfg_read_u8(pdev, &cap_pos, PCIR_CAP_PTR);

		while (cap_pos != 0 && cap_pos != 0xff) {
			pci_device_cfg_read_u8(pdev, &cap_data, cap_pos + PCICAP_ID);

			if (cap_data == cap_id)
				return cap_pos;

			pci_device_cfg_read_u8(pdev, &cap_pos, cap_pos + PCICAP_NEXTPTR);
        }
    }

    return 0;
}

/* find extend capability register position of cap_id */
static int
pci_find_ext_cap(struct pci_device *pdev, int cap_id)
{
	int offset = 0;
	uint32_t data = 0;

	offset = PCIR_EXTCAP;

	do {
		/* PCI Express Extended Capability must have 4 bytes header */
		pci_device_cfg_read_u32(pdev, &data, offset);
        
        if (PCI_EXTCAP_ID(data) == cap_id)
		    break;

        offset = PCI_EXTCAP_NEXTPTR(data);
	} while (offset != 0);
	
	return offset;
}

/* find pci-e device type */
static
int pci_pcie_type(struct pci_device *dev)
{
    uint8_t data = 0;
    int pcie_type;
    int pos = 0;

    if (dev == NULL)
        return -EINVAL;

    pos = pci_find_capability(dev, PCIY_EXPRESS);

    if (!pos)
        return -EINVAL;
    
    pci_device_cfg_read_u8(dev, &data, pos + PCIER_FLAGS);

    pcie_type = data & PCIEM_FLAGS_TYPE;

    return pcie_type;
}

/* find bdf of pci device pdev's root port */
static int
pci_find_root_port(const struct pci_device *pdev, int *bus_num, int *dev_num, int *func_num)
{
    int error = 0;
    struct pci_device_iterator *iter;
    struct pci_device *dev;
    int prim_bus, sec_bus, sub_bus;

    error = -ENODEV;
	iter = pci_slot_match_iterator_create(NULL);
	while ((dev = pci_device_next(iter)) != NULL) {
        pci_device_get_bridge_buses(dev, &prim_bus, &sec_bus, &sub_bus);

		if (sec_bus == pdev->bus) {
            *bus_num = dev->bus;
            *dev_num = dev->dev;
            *func_num = dev->func;
			error = 0;
			break;
		}
	}

	pci_iterator_destroy(iter);
    return error;
}

/* get ptm capability register value */
static int
get_ptm_cap(struct pci_device *pdev, int *offset)
{
    int pos;
    uint32_t cap;
    pos = pci_find_ext_cap(pdev, PCIZ_PTM);

    if (!pos)
    {
        *offset = 0;
        return 0;
    }

    *offset = pos;
    pci_device_cfg_read_u32(pdev, &cap, pos + PCIR_PTM_CAP);

    pr_notice("<PTM>-%s: device [%x:%x.%x]: ptm-cap=0x%x, offset=0x%x.\n", __func__, 
                pdev->bus, pdev->dev, pdev->func, cap, *offset);
    
    return cap;
}

/* add virtual root port to uos */
static int
add_vroot_port(struct vmctx *ctx, struct passthru_dev *ptdev, struct pci_device *root_port, int ptm_cap_offset)
{
    int error = 0;

    struct acrn_emul_dev root_port_emul_dev = {};
    struct vroot_port_priv vrp_priv = {};
    
	root_port_emul_dev.dev_id.fields.vendor_id = VROOT_PORT_VENDOR;
	root_port_emul_dev.dev_id.fields.device_id = VROOT_PORT_DEVICE;

    // virtual root port takes the same bdf as its downstream device
	root_port_emul_dev.slot = PCI_BDF(ptdev->dev->bus, ptdev->dev->slot, ptdev->dev->func);

    vrp_priv.phy_bdf = PCI_BDF(root_port->bus, root_port->dev, root_port->func);

    vrp_priv.primary_bus = ptdev->dev->bus;

    VROOT_PORTS_SEC_BUS_ARRAY[ctx->vmid]++;

    vrp_priv.secondary_bus = VROOT_PORTS_SEC_BUS_ARRAY[ctx->vmid];

    vrp_priv.subordinate_bus = 0;

    vrp_priv.ptm_capable = 1;

    vrp_priv.ptm_cap_offset = ptm_cap_offset;

    memcpy(root_port_emul_dev.args, (unsigned char *)&vrp_priv, sizeof(struct vroot_port_priv));

    pr_info("%s: virtual root port info: vbdf=0x%x, phy_bdf=0x%x, prim_bus=%x, sec_bus=%x, sub_bus=%x, ptm_cpa_offset=0x%x.\n", __func__,
        root_port_emul_dev.slot, vrp_priv.phy_bdf, vrp_priv.primary_bus, vrp_priv.secondary_bus, vrp_priv.subordinate_bus, vrp_priv.ptm_cap_offset);

	error = vm_add_hv_vdev(ctx, &root_port_emul_dev);
	if (error) {
		pr_err("failed to add virtual root.\n");
        return -1;
    }
    else {
        return vrp_priv.secondary_bus;
    }
}

/* Probe whether device and its root port support PTM */
int ptm_probe(struct vmctx *ctx, struct passthru_dev *ptdev, int *vrp_sec_bus)
{
    int error;
	int pos, pcie_type, cap, rootport_ptm_offset, device_ptm_offset;
    struct pci_device *phys_dev = ptdev->phys_dev;
    int rp_bus, rp_dev, rp_func;
    struct pci_device *root_port;

	if (!ptdev->pcie_cap) {
        pr_err("%s Error: %x:%x.%x is not a pci-e device.\n", __func__, phys_dev->bus, phys_dev->dev, phys_dev->func);
		return -EINVAL;
    }

	pos = pci_find_ext_cap(phys_dev, PCIZ_PTM);
	if (!pos) {
        pr_err("%s Error: %x:%x.%x doesn't support ptm.\n", __func__, phys_dev->bus, phys_dev->dev, phys_dev->func);
        return -EINVAL;
    }
	
    pcie_type = pci_pcie_type(phys_dev);

    /* PTM can only be enabled on RCIE or endpoint device for now */
    if (pcie_type == PCIEM_TYPE_ENDPOINT) {
        cap = get_ptm_cap(phys_dev, &device_ptm_offset);
        if (!(cap & PCIM_PTM_CAP_REQ)) {
            pr_err("%s Error: %x:%x.%x must be PTM requestor.\n", __func__, phys_dev->bus, phys_dev->dev, phys_dev->func);
            return -EINVAL;
        }

        /* Check whether its upstream device (root port) is ptm root */
        /* This is the only possible h/w configuration at the moment: rootport(ptm_root) -> endpoint(ptm requestor) */
        error = pci_find_root_port(phys_dev, &rp_bus, &rp_dev, &rp_func);
        if (error < 0) {
            pr_err("%s Error: Cannot find root port of %x:%x.%x.\n", __func__, phys_dev->bus, phys_dev->dev, phys_dev->func);
            return -ENODEV;
        }

        root_port = pci_device_find_by_slot(0, rp_bus, rp_dev, rp_func);

        pcie_type = pci_pcie_type(root_port);

        if (root_port == NULL || pcie_type != PCIEM_TYPE_ROOT_PORT) {
            pr_err("%s Error: Cannot find upstream root port of %x:%x.%x.\n", __func__, phys_dev->bus, phys_dev->dev, phys_dev->func);
            return -ENODEV;
        }

        cap = get_ptm_cap(root_port, &rootport_ptm_offset);
        if (!(cap & PCIM_PTM_CAP_ROOT)) {
            pr_err("%s Error: root port %x:%x.%x of %x:%x.%x is not PTM root.\n", __func__, root_port->bus, root_port->dev, root_port->func,
                        phys_dev->bus, phys_dev->dev, phys_dev->func);
            return -EINVAL;
        }

        // add virtual root port
        *vrp_sec_bus = add_vroot_port(ctx, ptdev, root_port, rootport_ptm_offset);
    } 
    else if (pcie_type == PCIEM_TYPE_ROOT_INT_EP ) {
        // No need to emulate root port if ptm requestor is RCIE
        *vrp_sec_bus = 0;
    }
	else {
        pr_err("%s Error: PTM can only be enabled on root complex integrated device or endpoint device.\n", __func__);
        return -EINVAL;
    }

	return 0;
}