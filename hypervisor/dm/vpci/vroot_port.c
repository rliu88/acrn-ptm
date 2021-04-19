/*
 * Copyright (c) 2019 Intel Corporation
 * All rights reserved.
 */

#include <vm.h>
#include <errno.h>
#include <logmsg.h>
#include <pci.h>
#include "vroot_port.h"

#include "vpci_priv.h"

static void init_vroot_port(struct pci_vdev *vdev)
{
	//uint8_t cap;
	// uint32_t hdr, pos, pre_pos = 0U;
	//uint8_t pcie_dev_type;
	uint32_t offset, val, voffset;
	union pci_bdf phybdf;
	struct acrn_vm *vm = vpci2vm(vdev->vpci);
	bool is_ptm_capable;
	uint32_t ptm_cap_offset;

	pr_acrnlog("%s enter:\n", __func__);
	if (is_postlaunched_vm(vm))
	{
		phybdf = vdev->pci_dev_config->pbdf;
		is_ptm_capable = vm->vroot_ports[vdev->pci_dev_config->vroot_port_idx].ptm_capable;
		ptm_cap_offset = vm->vroot_ports[vdev->pci_dev_config->vroot_port_idx].ptm_cap_offset;
	}
	else
	{
		phybdf = vdev->pdev->bdf;
		is_ptm_capable = vdev->pdev->ptm.is_capable;
		ptm_cap_offset = vdev->pdev->ptm.capoff;
	}

	if (vdev)
		pr_acrnlog("%s: vbdf=%x:%x.%x, pbdf=%x:%x.%x, is_ptm_capable=%d, ptm_cap_offset=0x%x.\n", __func__, 
			vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f, phybdf.bits.b, phybdf.bits.d, phybdf.bits.f,
			is_ptm_capable, ptm_cap_offset);

	/* read PCI config space to virtual space */
	for (offset = 0x00U; offset < 0x100U; offset += 4U) {
		val = pci_pdev_read_cfg(phybdf, offset, 4U);

		// cap = (uint8_t)val;

		// if (cap == PCIY_PCIE)
			pci_vdev_write_vcfg(vdev, offset, 4U, val);
	}

	pr_acrnlog("%s: emulate ptm...\n", __func__);
	/* emulate ptm */
	//if (vdev->pdev && vdev->pdev->ptm.is_capable)
	if (is_ptm_capable)
	{
		voffset = 0x100;

		/* read ptm reg header */
		//offset = vdev->pdev->ptm.capoff;
		offset = ptm_cap_offset;
		//val = pci_pdev_read_cfg(vdev->pdev->bdf, offset, PCI_PTM_CAP_LEN);
		val = pci_pdev_read_cfg(phybdf, offset, PCI_PTM_CAP_LEN);
		pci_vdev_write_vcfg(vdev, voffset, PCI_PTM_CAP_LEN, val);

		// /* read ptm capability reg */
		// offset = vdev->pdev->ptm.capoff + PCIR_PTM_CAP;
		// val = pci_pdev_read_cfg(vdev->pdev->bdf, offset, PCI_PTM_CAP_LEN);
		offset = ptm_cap_offset + PCIR_PTM_CAP;
		voffset = 0x100 + PCIR_PTM_CAP;
		val = pci_pdev_read_cfg(phybdf, offset, PCI_PTM_CAP_LEN);
		pci_vdev_write_vcfg(vdev, voffset, PCI_PTM_CAP_LEN, val);

		/* read ptm ctrl reg */
		// offset = vdev->pdev->ptm.capoff + PCIR_PTM_CTRL;
		// val = pci_pdev_read_cfg(vdev->pdev->bdf, offset, PCI_PTM_CAP_LEN);
		offset = ptm_cap_offset + PCIR_PTM_CTRL;
		voffset = 0x100 + PCIR_PTM_CTRL;
		val = pci_pdev_read_cfg(phybdf, offset, PCI_PTM_CAP_LEN);
		pci_vdev_write_vcfg(vdev, voffset, PCI_PTM_CAP_LEN, val);
	}

	pr_acrnlog("%s: emulate type info...\n", __func__);
	/* emulated for type info */
	pci_vdev_write_vcfg(vdev, PCIR_VENDOR, 2U, VROOT_PORT_VENDOR);
	pci_vdev_write_vcfg(vdev, PCIR_DEVICE, 2U, VROOT_PORT_DEVICE);

	pci_vdev_write_vcfg(vdev, PCIR_REVID, 1U, 0x02U);

	pci_vdev_write_vcfg(vdev, PCIR_HDRTYPE, 1U, PCIM_HDRTYPE_BRIDGE);
	pci_vdev_write_vcfg(vdev, PCIR_CLASS, 1U, PCIC_BRIDGE);
	pci_vdev_write_vcfg(vdev, PCIR_SUBCLASS, 1U, PCIS_BRIDGE_PCI);

	pr_acrnlog("%s: emulate bus number...\n", __func__);
	/* emulate bus numbers */
	pci_vdev_write_vcfg(vdev, PCIR_PRIBUS_1, 1U, 0x00);
	pci_vdev_write_vcfg(vdev, PCIR_SECBUS_1, 1U, 0x01);
	pci_vdev_write_vcfg(vdev, PCIR_SUBBUS_1, 1U, 0x01);

	vdev->parent_user = NULL;
	vdev->user = vdev;

	pr_acrnlog("%s exit.\n", __func__);
}

static void deinit_vroot_port(__unused struct pci_vdev *vdev)
{
	vdev->parent_user = NULL;
	vdev->user = NULL;
}

static int32_t read_vroot_port_cfg(const struct pci_vdev *vdev, uint32_t offset,
	uint32_t bytes, uint32_t *val)
{
	// if ((offset + bytes) <= 0x100U) {
	// 	*val = pci_vdev_read_vcfg(vdev, offset, bytes);
	// } else {
	// 	/* just passthru read to physical device when read PCIE sapce > 0x100 */
	// 	*val = pci_pdev_read_cfg(vdev->pdev->bdf, offset, bytes);
	// }

	*val = pci_vdev_read_vcfg(vdev, offset, bytes);

	return 0;
}

static int32_t write_vroot_port_cfg(__unused struct pci_vdev *vdev, __unused uint32_t offset,
	__unused uint32_t bytes, __unused uint32_t val)
{
	return 0;
}

int32_t create_vroot_port(struct acrn_vm *vm, struct acrn_emul_dev *dev)
{
	struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);
	struct acrn_vm_pci_dev_config *dev_config = NULL;
	union pci_bdf vbdf, phy_bdf;

	struct acrn_vroot_port *vrp_priv;

	int i;

	vbdf.value = dev->slot;

	pr_acrnlog("%s:, vm_id=%d, vendor_id=0x%x, dev_id=0x%x, vm_config->pci_dev_num=%d, vpci->pci_vdev_cnt=%d.\n", __func__, vm->vm_id, 
		dev->dev_id.fields.vendor_id, dev->dev_id.fields.device_id, vm_config->pci_dev_num, vm->vpci.pci_vdev_cnt);

	vrp_priv = (struct acrn_vroot_port*)dev->args;
	phy_bdf.value = vrp_priv->phy_bdf;

	pr_acrnlog("%s: virtual root port phy_bdf=[%x:%x.%x], vbdf=[%x:%x.%x], primary_bus=0x%x, secondary_bus=0x%x, sub_bus=0x%x.\n",
			__func__, phy_bdf.bits.b, phy_bdf.bits.d, phy_bdf.bits.f, 
			vbdf.bits.b, vbdf.bits.d, vbdf.bits.f, 
			vrp_priv->primary_bus, vrp_priv->secondary_bus, vrp_priv->subordinate_bus);
	
	for (i = 0U; i < vm_config->pci_dev_num; i++) {
		dev_config = &vm_config->pci_devs[i];
		if (dev_config->emu_type == PCI_DEV_TYPE_VROOT_PORT && dev_config->vroot_port_idx == vrp_priv->secondary_bus) {
			dev_config->vbdf.value = vbdf.value;
			dev_config->pbdf.value = phy_bdf.value;

			vm->vroot_ports[dev_config->vroot_port_idx].primary_bus = vrp_priv->primary_bus;
			vm->vroot_ports[dev_config->vroot_port_idx].secondary_bus = vrp_priv->secondary_bus;
			vm->vroot_ports[dev_config->vroot_port_idx].subordinate_bus = vrp_priv->subordinate_bus;
			vm->vroot_ports[dev_config->vroot_port_idx].phy_bdf = vrp_priv->phy_bdf;
			vm->vroot_ports[dev_config->vroot_port_idx].ptm_capable = vrp_priv->ptm_capable;
			vm->vroot_ports[dev_config->vroot_port_idx].ptm_cap_offset = vrp_priv->ptm_cap_offset;

			(void) vpci_init_vdev(&vm->vpci, dev_config, NULL);
			break;
		}
	}

	// struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);
	// struct acrn_vm_pci_dev_config *dev_config = NULL;
	// int32_t ret = -EINVAL;

	pr_acrnlog("%s exit.\n", __func__);

	return 0;
}

int32_t destroy_vroot_port(struct pci_vdev *vdev)
{
	pr_acrnlog("%s enter, vdev bdf=%x:%x.%x.\n", __func__, vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f);
	pr_acrnlog("%s exit.\n", __func__);

	return 0;
}

const struct pci_vdev_ops vroot_port_ops = {
	.init_vdev         = init_vroot_port,
	.deinit_vdev       = deinit_vroot_port,
	.write_vdev_cfg    = write_vroot_port_cfg,
	.read_vdev_cfg     = read_vroot_port_cfg,
};
