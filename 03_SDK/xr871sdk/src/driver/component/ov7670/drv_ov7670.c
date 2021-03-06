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
#include "stdio.h"
#include "string.h"
#include "kernel/os/os.h"
#include "driver/chip/hal_i2c.h"
#include "driver/chip/hal_csi.h"
#include "driver/chip/hal_dma.h"
#include "driver/chip/hal_uart.h"

#include "driver/component/ov7670/drv_ov7670.h"
#include "ov7670_cfg.h"

#define OV7670_DBG 1
#define LOG(flags, fmt, arg...)	\
	do {								\
		if (flags) 						\
			printf(fmt, ##arg);		\
	} while (0)

#define DRV_OV7670_DBG(fmt, arg...)	\
			LOG(OV7670_DBG, "[OV7670] "fmt, ##arg)


/****************************************************************************
*****************************************************************************/

#define OV7670_I2CID I2C1_ID
#define OV7670_IIC_CLK_FREQ	100000
#define OV7670_SCCB_ID 0X21  			//OV7670 дID

void Ov7670Sccb_Init()
{
	I2C_InitParam initParam;
	initParam.addrMode = I2C_ADDR_MODE_7BIT;
	initParam.clockFreq = OV7670_IIC_CLK_FREQ;
	HAL_I2C_Init(OV7670_I2CID, &initParam);
}

int Ov7670Sccb_Write(uint8_t sub_addr, uint8_t data)
{
	return HAL_I2C_SCCB_Master_Transmit_IT(OV7670_I2CID, OV7670_SCCB_ID, sub_addr, &data);
}

int Ov7670Sccb_Read(uint8_t sub_addr, uint8_t *data)
{
	return HAL_I2C_SCCB_Master_Receive_IT(OV7670_I2CID, OV7670_SCCB_ID, sub_addr, data);
}

void Drv_OV7670_Light_Mode(OV7670_LIGHT_MODE light_mode)
{
	uint8_t reg13val = 0XE7, reg01val = 0, reg02val = 0;
	switch(light_mode) {
		case LIGHT_AUTO:
			reg13val = 0XE7;
			reg01val = 0;
			reg02val = 0;
			break;
		case LIGHT_SUNNY:
			reg13val = 0XE5;
			reg01val = 0X5A;
			reg02val = 0X5C;
			break;
		case LIGHT_COLUDY:
			reg13val = 0XE5;
			reg01val = 0X58;
			reg02val = 0X60;
			break;
		case LIGHT_OFFICE:
			reg13val = 0XE5;
			reg01val = 0X84;
			reg02val = 0X4c;
			break;
		case LIGHT_HOME:
			reg13val = 0XE5;
			reg01val = 0X96;
			reg02val = 0X40;
			break;
	}
	Ov7670Sccb_Write(0X13, reg13val);
	Ov7670Sccb_Write(0X01, reg01val);
	Ov7670Sccb_Write(0X02, reg02val);
}

void Drv_OV7670_Color_Saturation(OV7670_COLOR_SATURATION sat)
{
	uint8_t reg4f5054val = 0X80, reg52val = 0X22, reg53val = 0X5E;
	switch(sat) {
		case COLOR_SATURATION_0://-2
			reg4f5054val = 0X40;
			reg52val = 0X11;
			reg53val = 0X2F;
			break;
		case COLOR_SATURATION_1://-1
			reg4f5054val = 0X66;
			reg52val = 0X1B;
			reg53val = 0X4B;
			break;
		case COLOR_SATURATION_2:
			reg4f5054val = 0X80;
			reg52val = 0X22;
			reg53val = 0X5E;
			break;
		case COLOR_SATURATION_3:
			reg4f5054val = 0X99;
			reg52val = 0X28;
			reg53val = 0X71;
			break;
		case COLOR_SATURATION_4:
			reg4f5054val = 0XC0;
			reg52val = 0X33;
			reg53val = 0X8D;
			break;
	}

	Ov7670Sccb_Write(0X4F, reg4f5054val);
	Ov7670Sccb_Write(0X50, reg4f5054val);
	Ov7670Sccb_Write(0X51, 0X00);
	Ov7670Sccb_Write(0X52, reg52val);
	Ov7670Sccb_Write(0X53, reg53val);
	Ov7670Sccb_Write(0X54, reg4f5054val);

}

void Drv_OV7670_Brightness(OV7670_BRIGHTNESS bright)
{
	uint8_t reg55val = 0X00;
	switch(bright) {
		case BRIGHT_0:		//-2
			reg55val = 0XB0;
			break;
		case BRIGHT_1:
			reg55val = 0X98;
			break;
		case BRIGHT_2:
			reg55val = 0X00;
			break;
		case BRIGHT_3:
			reg55val = 0X18;
			break;
		case BRIGHT_4:
			reg55val = 0X30;
			break;
	}
	Ov7670Sccb_Write(0X55,reg55val);
}

void Drv_OV7670_Contrast(OV7670_CONTARST contrast)
{
	uint8_t reg56val = 0X40;
	switch(contrast) {
		case CONTARST_0:	//-2
			reg56val = 0X30;
			break;
		case CONTARST_1:
			reg56val = 0X38;
			break;
		case CONTARST_2:
			reg56val = 0X40;
			break;
		case CONTARST_3:
			reg56val = 0X50;
			break;
		case CONTARST_4:
			reg56val = 0X60;
			break;
	}
	Ov7670Sccb_Write(0X56,reg56val);
}

void Drv_OV7670_Special_Effects(uint8_t eft)
{
	uint8_t reg3aval = 0X04;
	uint8_t reg67val = 0XC0;
	uint8_t reg68val = 0X80;
	switch(eft) {
		case IMAGE_NOMAL:	//nomal
			reg3aval = 0X04;
			reg67val = 0XC0;
			reg68val = 0X80;
			break;
		case IMAGE_NEGATIVE:
			reg3aval = 0X24;
			reg67val = 0X80;
			reg68val = 0X80;
			break;
		case IMAGE_BLACK_WHITE:
			reg3aval = 0X14;
			reg67val = 0X80;
			reg68val = 0X80;
			break;
		case IMAGE_SLANT_RED:
			reg3aval = 0X14;
			reg67val = 0Xc0;
			reg68val = 0X80;
			break;
		case IMAGE_SLANT_GREEN:
			reg3aval = 0X14;
			reg67val = 0X40;
			reg68val = 0X40;
			break;
		case IMAGE_SLANT_BLUE:
			reg3aval = 0X14;
			reg67val = 0X80;
			reg68val = 0XC0;
			break;
		case IMAGE_VINTAGE:
			reg3aval = 0X14;
			reg67val = 0XA0;
			reg68val = 0X40;
			break;
	}

	Ov7670Sccb_Write(0X3A, reg3aval);
	Ov7670Sccb_Write(0X68, reg67val);
	Ov7670Sccb_Write(0X67, reg68val);
}

void Drv_OV7670_Window_Set(uint16_t sx,uint16_t sy,uint16_t width,uint16_t height)
{
	uint16_t endx;
	uint16_t endy;
	uint8_t temp;
	endx = sx + width * 2;		//V*2
	endy = sy + height * 2;
	if(endy > 784)
		endy -= 784;

	Ov7670Sccb_Read(0X03, &temp);
	temp &= 0XF0;
	temp |= ((endx & 0X03) << 2) | (sx & 0X03);
	Ov7670Sccb_Write(0X03, temp);
	Ov7670Sccb_Write(0X19, sx>>2);
	Ov7670Sccb_Write(0X1A, endx>>2);
	Ov7670Sccb_Read(0X32, &temp);
	temp &= 0XC0;
	temp |= ((endy & 0X07) << 3) | (sy&0X07);
	Ov7670Sccb_Write(0X17, sy >> 3);
	Ov7670Sccb_Write(0X18, endy >> 3);
}


Component_Status OV7670_Init(void)
{
	uint8_t temp;
	uint16_t i = 0;
	uint8_t lightmode = LIGHT_OFFICE, saturation = COLOR_SATURATION_2, contrast = CONTARST_2, effect = IMAGE_NOMAL;
	if (Ov7670Sccb_Write(0x12, 0x80) != 1) {
		COMPONENT_WARN("ov7670 sccb write error\n");
		return COMP_ERROR;
	}
	OS_MSleep(50);

	if (Ov7670Sccb_Read(0x0b, &temp) != 1) {
		COMPONENT_WARN("ov7670 sccb read error\n");
		return COMP_ERROR;
	}

	if (temp != 0x73) {
		COMPONENT_WARN("ov7670 sccb read temp = %x\n", temp);
		return COMP_ERROR;
	}

	if (Ov7670Sccb_Read(0x0a, &temp) != 1) {
		COMPONENT_WARN("ov7670 sccb read error\n");
		return COMP_ERROR;
	}

	if (temp != 0x76) {
		COMPONENT_WARN("ov7670 sccb read temp = %x\n", temp);
		return COMP_ERROR;
	}

	for (i = 0; i < sizeof(ov7670_init_reg_tbl) / sizeof(ov7670_init_reg_tbl[0]); i++) {
		if (!Ov7670Sccb_Write(ov7670_init_reg_tbl[i][0], ov7670_init_reg_tbl[i][1])) {
			COMPONENT_WARN("ov7670 sccb read error\n");
			return COMP_ERROR;
		}
	}

	Drv_OV7670_Light_Mode(lightmode);
	Drv_OV7670_Color_Saturation(saturation);
	Drv_OV7670_Contrast(contrast);
 	Drv_OV7670_Special_Effects(effect);
	Drv_OV7670_Window_Set(12,176,240,320);
	return COMP_OK;
}

void Ov7620_Stop_Dma(void *arg)
{
	DMA_Channel ch = (DMA_Channel)arg;
	HAL_DMA_Stop(ch);
	HAL_DMA_Release(ch);
}

DMA_Channel Ov7620_Dma_Reque()
{
	DMA_Channel ch;
	ch = HAL_DMA_Request();
	if (ch == DMA_CHANNEL_INVALID) {
		COMPONENT_WARN("dma error\n");
		return DMA_CHANNEL_INVALID;
	}
	DMA_ChannelInitParam param;
	param.cfg =  HAL_DMA_MakeChannelInitCfg(DMA_WORK_MODE_SINGLE,
											DMA_WAIT_CYCLE_1,
											DMA_BYTE_CNT_MODE_NORMAL,

			                                DMA_DATA_WIDTH_32BIT,
			                                DMA_BURST_LEN_4,
											DMA_ADDR_MODE_INC,
			                                DMA_PERIPH_SRAM,

											DMA_DATA_WIDTH_32BIT,
											DMA_BURST_LEN_4,
											DMA_ADDR_MODE_INC,
											DMA_PERIPH_SRAM);
	param.endArg = (void *)ch;
	param.endCallback = Ov7620_Stop_Dma;
	param.irqType = DMA_IRQ_TYPE_END;
	HAL_DMA_Init(ch, &param);
	return ch;
}

volatile uint8_t private_frame_done = 0;
volatile uint32_t private_image_buff_addr = 0;
volatile uint32_t private_image_data_count = 0;
void Drv_Ov7670_Set_SaveImage_Buff(uint32_t image_buff_addr)
{
	private_image_data_count = 0;
	private_image_buff_addr = image_buff_addr;
}

void read_fifo_a(uint32_t len)
{
	if (private_image_buff_addr == 0) {
		COMPONENT_WARN("image_buff is invalid\n");
		return;
	}

	DMA_Channel ov7670_dma_ch = Ov7620_Dma_Reque();
	HAL_DMA_Start(ov7670_dma_ch, 0x40007800, (private_image_buff_addr + private_image_data_count), len);
	private_image_data_count += len;
}

void read_fifo_b(uint32_t len)
{
	if (private_image_buff_addr == 0) {
		COMPONENT_WARN("image_buff is invalid\n");
		return;
	}

	DMA_Channel ov7670_dma_ch = Ov7620_Dma_Reque();
	HAL_DMA_Start(ov7670_dma_ch, 0x40007a00, (private_image_buff_addr + private_image_data_count), len);
	private_image_data_count += len;
}

void Ov7670_Irq(void *arg)
{
	uint32_t irq_sta = HAL_CSI_Interrupt_Sta();
	HAL_CSI_Interrupt_Clear();
	CSI_FIFO_Data_Len len = HAL_CSI_FIFO_Data_Len();

    if (irq_sta & CSI_FIFO_0_A_READY_IRQ)
		read_fifo_a(len.FIFO_0_A_Data_Len);
	else if (irq_sta & CSI_FIFO_0_B_READY_IRQ)
		read_fifo_b(len.FIFO_0_B_Data_Len);
	else if (irq_sta & CSI_FRAME_DONE_IRQ)
		private_frame_done = 1;
}

void Ov7670_Csi_Init()
{
	HAL_CSI_Ctrl(CSI_DISABLE);
	CSI_Config csi_cfg;
	csi_cfg.src_Clk.clk =  CCM_AHB_PERIPH_CLK_SRC_HFCLK;
	csi_cfg.src_Clk.divN = CCM_PERIPH_CLK_DIV_N_1;
	csi_cfg.src_Clk.divM = CCM_PERIPH_CLK_DIV_M_2;

	HAL_CSI_Config(&csi_cfg);

	CSI_Sync_Signal signal;
	signal.herf = CSI_POSITIVE;
	signal.p_Clk = CSI_POSITIVE;
	signal.vsync = CSI_POSITIVE;
	HAL_CSI_SYNC_Signal_Polarity(&signal);

	CSI_Picture_Size size;
	size.hor_len = 640;
	size.hor_start = 0;
	HAL_CSI_Set_Picture_Size(&size);

	HAL_CSI_Interrupt_Ctrl(CSI_FRAME_DONE_IRQ, CSI_ENABLE);
	HAL_CSI_Interrupt_Ctrl(CSI_FIFO_0_A_READY_IRQ, CSI_ENABLE);
	HAL_CSI_Interrupt_Ctrl(CSI_FIFO_0_B_READY_IRQ, CSI_ENABLE);

	CSI_Call_Back cb;
	cb.arg = NULL;
	cb.callBack = Ov7670_Irq;
	HAL_CSI_CallBack(&cb, CSI_ENABLE);

	COMPONENT_TRACK("end\n");
}

Ov7670_PowerCtrlCfg private_ov7670_power;
void Drv_Ov7670_PowerInit(Ov7670_PowerCtrlCfg *cfg)
{
	private_ov7670_power = *cfg;
	GPIO_InitParam param;
	param.driving = GPIO_DRIVING_LEVEL_1;
	param.mode = GPIOx_Pn_F1_OUTPUT;
	param.pull = GPIO_PULL_NONE;

	HAL_GPIO_Init(cfg->Ov7670_Pwdn_Port, cfg->Ov7670_Pwdn_Pin, &param);
	HAL_GPIO_Init(cfg->Ov7670_Reset_Port, cfg->Ov7670_Reset_Pin, &param);
}

void Drv_Ov7670_Reset_Pin_Ctrl(GPIO_PinState state)
{

	HAL_GPIO_WritePin(private_ov7670_power.Ov7670_Reset_Port,
					  private_ov7670_power.Ov7670_Reset_Pin, state);
}

void Drv_Ov7670_Pwdn_Pin_Ctrl(GPIO_PinState state)
{
	HAL_GPIO_WritePin(private_ov7670_power.Ov7670_Pwdn_Port,
		              private_ov7670_power.Ov7670_Pwdn_Pin, state);

}

Component_Status Drv_Ov7670_Init()
{
	Ov7670_Csi_Init();
	Ov7670Sccb_Init();
	if (OV7670_Init() == -1) {
		COMPONENT_WARN("OV7670  Init error!!\n");
		return COMP_ERROR;
	}
	return COMP_OK;
	COMPONENT_TRACK("end\n");
}

void Drv_Ov7670_DeInit()
{
	HAL_CSI_DeInit();
	HAL_I2C_DeInit(OV7670_I2CID);
}

void Ov7670_Vcan_Sendimg(void *imgaddr, uint32_t imgsize)
{
	#define CMD_IMG 1
    uint8_t cmdf[2] = {CMD_IMG, ~CMD_IMG};
    uint8_t cmdr[2] = {~CMD_IMG, CMD_IMG};
    HAL_UART_Transmit_IT(OV7670_PICTURE_SEND_UART, cmdf, sizeof(cmdf));
	HAL_UART_Transmit_IT(OV7670_PICTURE_SEND_UART, (uint8_t *)imgaddr, imgsize);
    HAL_UART_Transmit_IT(OV7670_PICTURE_SEND_UART, cmdr, sizeof(cmdr));
}

void Drv_Ov7670_Uart_Send_Picture()
{
	while (!private_frame_done) {
		OS_MSleep(10);
	}
	private_frame_done = 0;

	DRV_OV7670_DBG("image_size %d\n", private_image_data_count);
	Ov7670_Vcan_Sendimg((void*)private_image_buff_addr, private_image_data_count);
}

void Drv_Ov7670_Reset_Image_buff()
{
	private_image_data_count = 0;
}


/************************demo********************************/
uint8_t image_buff[153600];
#define OV7670_PWON_IO
#define OV7670_RESET_IO

//this func can capture one picture and use uart send the picture data to pc.
Component_Status Ov7670_Demo()
{
	Ov7670_PowerCtrlCfg PowerCtrlcfg;
	PowerCtrlcfg.Ov7670_Pwdn_Port = GPIO_PORT_A;
	PowerCtrlcfg.Ov7670_Pwdn_Pin = GPIO_PIN_12;
	PowerCtrlcfg.Ov7670_Reset_Port = GPIO_PORT_A;
	PowerCtrlcfg.Ov7670_Reset_Pin = GPIO_PIN_13;

	Drv_Ov7670_PowerInit(&PowerCtrlcfg);
	Drv_Ov7670_Reset_Pin_Ctrl(GPIO_PIN_HIGH);
	Drv_Ov7670_Pwdn_Pin_Ctrl(GPIO_PIN_LOW);
	OS_MSleep(10);

	if (Drv_Ov7670_Init() == COMP_ERROR)
		return COMP_ERROR;
	else
		OS_MSleep(500);

	Drv_Ov7670_Set_SaveImage_Buff((uint32_t)image_buff);
	HAL_CSI_Ctrl(CSI_DISABLE);
	HAL_CSI_Ctrl(CSI_ENABLE);
	HAL_CSI_Capture_Mode(CSI_STILL_MODE, CSI_ENABLE);
	Drv_Ov7670_Uart_Send_Picture();

	Drv_Ov7670_Reset_Image_buff();
	Drv_Ov7670_DeInit();
	return COMP_OK;
}

