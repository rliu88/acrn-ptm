/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __VROOT_PORT_H
#define __VROOT_PORT_H

#define VROOT_PORT_VENDOR           0x8086U
#define VROOT_PORT_DEVICE           0x9d12U

struct acrn_vroot_port
{
    uint32_t phy_bdf;
    uint8_t primary_bus;
    uint8_t secondary_bus;
    uint8_t subordinate_bus;
    uint8_t ptm_capable;
    uint32_t ptm_cap_offset;
};

extern const struct pci_vdev_ops vroot_port_ops;

int32_t create_vroot_port(struct acrn_vm *vm, struct acrn_emul_dev *dev);
int32_t destroy_vroot_port(struct pci_vdev *vdev);


#endif