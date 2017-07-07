/*
 * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the
 *       distribution.
 *    3. Neither the name of XRADIO TECHNOLOGY CO., LTD. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DRIVER_CHIP_HAL_DEF_H_
#define _DRIVER_CHIP_HAL_DEF_H_

#include "driver/chip/device.h"
#include "kernel/os/os_common.h"
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_BIT(pos)						(1U << (pos))

#define HAL_SET_BIT(reg, mask)				((reg) |= (mask))
#define HAL_CLR_BIT(reg, mask)  			((reg) &= ~(mask))
#define HAL_GET_BIT(reg, mask) 				((reg) & (mask))
#define HAL_GET_BIT_VAL(reg, shift, vmask)	(((reg) >> (shift)) & (vmask))

#define HAL_MODIFY_REG(reg, clr_mask, set_mask)	\
	do {										\
		uint32_t __tmp = (reg);					\
		__tmp &= ~(clr_mask);					\
		__tmp |= (set_mask);					\
		(reg) = __tmp;							\
	} while (0)

#if 0 /* same as the above define */
#define HAL_MODIFY_REG(reg, clr_mask, set_mask) \
	((reg) = (((reg) & (~(clr_mask))) | (set_mask)))
#endif

/* access LSBs of a 32-bit register (little endian only) */
#define HAL_REG_32BIT(reg)		(*((__IO uint32_t *)(&(reg))))
#define HAL_REG_16BIT(reg)		(*((__IO uint16_t *)(&(reg))))
#define HAL_REG_8BIT(reg)		(*((__IO uint8_t  *)(&(reg))))

#define HAL_ARRAY_SIZE(a)	(sizeof((a)) / sizeof((a)[0]))

#define HAL_WAIT_FOREVER	OS_WAIT_FOREVER

typedef enum
{
	HAL_OK	    = 0,
	HAL_ERROR   = -1,
	HAL_BUSY	= -2,
	HAL_TIMEOUT = -3,
	HAL_INVALID = -4
} HAL_Status;

typedef enum {
	HAL_BR_PINMUX_INIT,
	HAL_BR_PINMUX_DEINIT,
	HAL_BR_PINMUX_GETINFO
} HAL_BoardReq;

typedef HAL_Status (*HAL_BoardCfg)(uint32_t id, HAL_BoardReq req, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _DRIVER_CHIP_HAL_DEF_H_ */
