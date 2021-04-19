/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __PTM_H__
#define __PTM_H__

#include "passthru.h"

#define VROOT_PORT_VENDOR           0x8086U
#define VROOT_PORT_DEVICE           0x9d12U

int ptm_probe(struct vmctx *ctx, struct passthru_dev *pdev, int *vrp_sec_bus);

#endif