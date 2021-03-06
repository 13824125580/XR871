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

#ifndef _AT_COMMAND_H_
#define _AT_COMMAND_H_

#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#include"at_types.h"
#include"at_config.h"
#include"at_status.h"

typedef enum
{
	ACC_ACT=0,/* activate config */
	ACC_MODE, /* switch to data mode */
	ACC_SAVE, /* save config to flash */
	ACC_LOAD, /* load config from flash */
	ACC_STATUS, /* update status */
	ACC_FACTORY, /* Restore factory default settings */
	ACC_PEER, /* get detail information of the peer */
	ACC_PING, /* ping test */
	ACC_SOCKON, /* Open a network socket */
	ACC_SOCKW, /* Write data to socket */
	ACC_SOCKQ, /* Query socket for pending data */
	ACC_SOCKR, /* Read data from socket */
	ACC_SOCKC, /* Close socket */
	ACC_SOCKD, /* Disable/Enable socket server. */
	ACC_WIFI, /* Disable/Enable WiFi */
	ACC_REASSOCIATE, /* Trigger a WiFi Roam */
	ACC_GPIOC, /*  Configure specified GPIO */
	ACC_GPIOR, /* Read specified GPIO */
	ACC_GPIOW, /* Write specified GPIO */
	ACC_SCAN, /* scan available AP */
}AT_CALLBACK_CMD;

typedef struct
{
	at_config_t *cfg;
	at_status_t *sts;
	union
	{
		struct
		{
			s32 num;
		}act;
		struct
		{
			char hostname[AT_PARA_MAX_SIZE];
		}ping;
		struct
		{
			s32 value;
		}wifi;
		struct
		{
			s32 method; /* 0: default 1: "a,r" 2: "p,r"*/
		}scan;
		struct
		{
			char hostname[AT_PARA_MAX_SIZE];
			s32 port;
			s32 protocol; /* 0: TCP 1: UDP */
			s32 ind; /* 0: disable 1: enable */
		}sockon;
		struct
		{
			s32 id;
			s32 len;
			u8 *buf;
		}sockw;
		struct
		{
			s32 id;
		}sockq;
		struct
		{
			s32 id;
			s32 len;
		}sockr;
		struct
		{
			s32 id;
		}sockc;
		struct
		{
			s32 port;
			s32 protocol; /* 0: TCP 1: UDP */
		}sockd;
		struct
		{
			s32 len; /* transparent transmission send buffer length */
			u8 *buf; /* transparent transmission send buffer */
		}mode;
	}u;
}at_callback_para_t;

typedef struct
{
	s32 type; /* 0: value 1: buffer pointer */
	void *vptr; /* value or buffer pointer */
	s32 vsize;
}at_callback_rsp_t;

typedef AT_ERROR_CODE (*at_callback_t)(AT_CALLBACK_CMD cmd, at_callback_para_t *para, at_callback_rsp_t *rsp);

extern at_callback_t at_callback;

extern AT_ERROR_CODE at_init(at_callback_t cb);
extern AT_ERROR_CODE at_parse(void);

#endif
