/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ipi_chip.h"
#include "registers.h"

#if SCP_CORE_SN == 0
void ipi_op_wake_ap(void)
{
	SCP_SCP2SPM_IPC_SET = IPC_SCP2HOST;
}

int ipi_op_scp2ap_is_irq_set(void)
{
	return SCP_SCP2APMCU_IPC_SET & IPC_SCP2HOST;
}

void ipi_op_scp2ap_irq_set(void)
{
	SCP_SCP2APMCU_IPC_SET = IPC_SCP2HOST;
}

void ipi_op_ap2scp_irq_clr(void)
{
	SCP_GIPC_IN_CLR = GIPC_IN(0);
}

int ipi_op_ap2scp_is_irq_set(void)
{
	return SCP_GIPC_IN_SET & GIPC_IN(0);
}
#else
void ipi_op_wake_ap(void)
{
}

int ipi_op_scp2ap_is_irq_set(void)
{
	return SCP_SSHUB2APMCU_IPC_SET & IPC_SCP2HOST;
}

void ipi_op_scp2ap_irq_set(void)
{
	SCP_SSHUB2APMCU_IPC_SET = IPC_SCP2HOST;
}

void ipi_op_ap2scp_irq_clr(void)
{
	SCP_GIPC_IN_CLR = GIPC_IN(4);
}

int ipi_op_ap2scp_is_irq_set(void)
{
	return SCP_GIPC_IN_SET & GIPC_IN(4);
}
#endif
