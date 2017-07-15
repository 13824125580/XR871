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

#include "driver/chip/hal_timer.h"
#include "driver/chip/hal_dma.h"
#include "driver/chip/hal_uart.h"
#include "driver/chip/hal_ccm.h"
#include "driver/chip/hal_gpio.h"
#include "driver/chip/hal_i2s.h"
#include "sys/io.h"
#include "hal_os.h"
#include "pm/pm.h"
#include "hal_inc.h"

typedef struct {
	bool isHwInit;
	volatile bool txRunning;
	volatile bool rxRunning;

	uint8_t *txBuf;
	uint8_t *rxBuf;
	uint32_t rxLength;
	uint32_t txLength;

	uint8_t *readPointer;
	uint8_t *writePointer;

	DMA_Channel txDMAChan;
	DMA_Channel rxDMAChan;
	I2S_HWParam *hwParam;

	I2S_DataParam pdataParam;
	I2S_DataParam cdataParam;

	HAL_Semaphore txReady;
	HAL_Semaphore rxReady;

	volatile uint32_t txHalfCallCount;
	volatile uint32_t rxHalfCallCount;
	volatile uint32_t txEndCallCount;
	volatile uint32_t rxEndCallCount;
	uint8_t *txDmaPointer;
	uint8_t *rxDmaPointer;

	bool isTxSemaphore;
	bool isRxSemaphore;
	bool isTxInitiate;
	bool isRxInitiate;

	HAL_BoardCfg boardCfg;

	I2S_ItCallback txItCallback;
	I2S_ItCallback rxItCallback;

	HAL_Mutex devSetLock;
	HAL_Mutex devTriggerLock;

	uint32_t audioPllParam;
	uint32_t audioPllPatParam;
} I2S_Private;


#define AUDIO_PLL_SRC		(CCM_DAUDIO_MCLK_SRC_1X)
#define AUDIO_PLL_24		(24576000)
#define AUDIO_PLL_22		(22579200)

#define AUDIO_DEVICE_PLL    AUDIO_PLL_22

#define I2S_MEMCPY		memcpy
#define I2S_MALLOC		malloc
#define I2S_FREE		free
#define I2S_MEMSET		memset

#define UNDERRUN_THRESHOLD		3
#define OVERRUN_THRESHOLD		3

#ifdef RESERVERD_MEMORY_FOR_I2S_TX
static uint8_t I2STX_BUF[I2S_BUF_LENGTH];
#endif
#ifdef RESERVERD_MEMORY_FOR_I2S_RX
static uint8_t I2SRX_BUF[I2S_BUF_LENGTH];
#endif
static I2S_Private gI2sPrivate;
static uint32_t I2S_BUF_LENGTH = 0;

typedef struct {
	uint32_t hosc;
	uint32_t audio;
	uint32_t pllParam;
	uint32_t pllPatParam;
} HOSC_I2S_Type;

typedef enum {
	I2S_PLL_24M = 0U,
	I2S_PLL_22M = 1U,
} I2S_PLLMode;

/*default hw configuration*/
static I2S_HWParam gHwParam = {
	0,/*0:I2S,1:PCM*/
	DAIFMT_CBS_CFS,
	DAIFMT_I2S,
	DAIFMT_NB_NF,
	32, /*16,32,64,128,256*/
	I2S_SHORT_FRAME,
	I2S_TX_MSB_FIRST,
	I2S_RX_MSB_FIRST,
	0x40,
	0xF,
	{AUDIO_DEVICE_PLL,1},

};

static CLK_DIVRegval DivRegval[] = {
	{1,I2S_BCLKDIV_1,I2S_MCLKDIV_1},
	{2,I2S_BCLKDIV_2,I2S_MCLKDIV_2},
	{4,I2S_BCLKDIV_4,I2S_MCLKDIV_4},
	{6,I2S_BCLKDIV_6,I2S_MCLKDIV_6},
	{8,I2S_BCLKDIV_8,I2S_MCLKDIV_8},
	{12,I2S_BCLKDIV_12,I2S_MCLKDIV_12},
	{16,I2S_BCLKDIV_16,I2S_MCLKDIV_16},
	{24,I2S_BCLKDIV_24,I2S_MCLKDIV_24},
	{32,I2S_BCLKDIV_32,I2S_MCLKDIV_32},
	{48,I2S_BCLKDIV_48,I2S_MCLKDIV_48},
	{64,I2S_BCLKDIV_64,I2S_MCLKDIV_64},
	{96,I2S_BCLKDIV_96,I2S_MCLKDIV_96},
	{128,I2S_BCLKDIV_128,I2S_MCLKDIV_128},
	{176,I2S_BCLKDIV_176,I2S_MCLKDIV_176},
	{192,I2S_BCLKDIV_192,I2S_MCLKDIV_192},
	{}
};

static const HOSC_I2S_Type i2s_hosc_aud_type[] = {
	{HOSC_CLOCK_26M, I2S_PLL_24M, PRCM_AUD_PLL24M_PARAM_HOSC26M, PRCM_AUD_PLL24M_PAT_PARAM_HOSC26M},
	{HOSC_CLOCK_26M, I2S_PLL_22M, PRCM_AUD_PLL22M_PARAM_HOSC26M, PRCM_AUD_PLL22M_PAT_PARAM_HOSC26M},
	{HOSC_CLOCK_24M, I2S_PLL_24M, PRCM_AUD_PLL24M_PARAM_HOSC24M, PRCM_AUD_PLL24M_PAT_PARAM_HOSC24M},
	{HOSC_CLOCK_24M, I2S_PLL_22M, PRCM_AUD_PLL22M_PARAM_HOSC24M, PRCM_AUD_PLL22M_PAT_PARAM_HOSC24M},
	{HOSC_CLOCK_40M, I2S_PLL_24M, PRCM_AUD_PLL24M_PARAM_HOSC40M, PRCM_AUD_PLL24M_PAT_PARAM_HOSC40M},
	{HOSC_CLOCK_40M, I2S_PLL_22M, PRCM_AUD_PLL22M_PARAM_HOSC40M, PRCM_AUD_PLL22M_PAT_PARAM_HOSC40M},
	{HOSC_CLOCK_52M, I2S_PLL_24M, PRCM_AUD_PLL24M_PARAM_HOSC52M, PRCM_AUD_PLL24M_PAT_PARAM_HOSC52M},
	{HOSC_CLOCK_52M, I2S_PLL_22M, PRCM_AUD_PLL22M_PARAM_HOSC52M, PRCM_AUD_PLL22M_PAT_PARAM_HOSC52M},
};

uint32_t I2S_PLLAUDIO_Update(I2S_PLLMode pll)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;

	if (pll != I2S_PLL_24M &&  pll != I2S_PLL_22M)
		return -1;

	uint32_t hoscClock = HAL_PRCM_GetHFClock();

	int i = 0;
	for (i = 0; i < ARRAY_SIZE(i2s_hosc_aud_type); i++) {
		if ((i2s_hosc_aud_type[i].hosc == hoscClock) && (i2s_hosc_aud_type[i].audio == pll)) {
			i2sPrivate->audioPllParam = i2s_hosc_aud_type[i].pllParam;
			i2sPrivate->audioPllPatParam = i2s_hosc_aud_type[i].pllPatParam;
			break;
		}
	}
	if (i == ARRAY_SIZE(i2s_hosc_aud_type)) {
		I2S_ERROR("Update audio pll failed....\n");
		return -1;
	}
	return 0;
}
static uint32_t I2S_GetTxEmptyRoom()
{
	return (HAL_GET_BIT(I2S->DA_FSTA, I2S_TXFIFO_EMPTY_MASK)>>I2S_TXFIFO_EMPTY_SHIFT);
}
static uint32_t I2S_GetRxAvaData()
{
	return (HAL_GET_BIT(I2S->DA_FSTA, I2S_RXFIFO_CNT_MASK) << I2S_RXFIFO_CNT_SHIFT);
}
static uint32_t I2S_GetIrqStatus()
{
	return HAL_GET_BIT(I2S->DA_ISTA, I2S_TX_FIFO_EMPTY_IT_BIT|I2S_RX_FIFO_AVABLE_IT_BIT);
}
#if 0
static uint32_t I2S_GetTxIrqStatus()
{
        return HAL_GET_BIT(I2S->DA_ISTA, I2S_TX_FIFO_EMPTY_IT_BIT);
}
#endif
static void I2S_ClearTxPendingIRQ()
{
	HAL_SET_BIT(I2S->DA_ISTA, I2S_TX_FIFO_EMPTY_IT_BIT);
}
#if 0
static uint32_t I2S_GetRxIrqStatus()
{
        return HAL_GET_BIT(I2S->DA_ISTA, I2S_RX_FIFO_AVABLE_IT_BIT);
}
#endif
static void I2S_ClearRxPendingIRQ()
{
	HAL_SET_BIT(I2S->DA_ISTA, I2S_RX_FIFO_AVABLE_IT_BIT);
}
static void I2S_EnableRxIRQ()
{
	HAL_SET_BIT(I2S->DA_INT, I2S_RXFIFO_AVABLE_ITEN_BIT);
}
static void I2S_DisableRxIRQ()
{
	HAL_CLR_BIT(I2S->DA_INT, I2S_RXFIFO_AVABLE_ITEN_BIT);
}

static void I2S_EnableTxIRQ()
{
	HAL_SET_BIT(I2S->DA_INT, I2S_TXFIFO_EMPTY_ITEN_BIT);
}
static void I2S_DisableTxIRQ()
{
	HAL_CLR_BIT(I2S->DA_INT, I2S_TXFIFO_EMPTY_ITEN_BIT);
}

static void I2S_DisableTx()
{
	HAL_CLR_BIT(I2S->DA_CTL, I2S_TX_EN_BIT);
}

static void I2S_EnableTx()
{
	HAL_SET_BIT(I2S->DA_CTL, I2S_TX_EN_BIT);
}

static void I2S_DisableRx()
{
	HAL_CLR_BIT(I2S->DA_CTL, I2S_RX_EN_BIT);
}

static void I2S_EnableRx()
{
	HAL_SET_BIT(I2S->DA_CTL, I2S_RX_EN_BIT);
}

#if 0
__weak void HAL_I2S_RxCpltCallback(I2S_Private *i2sPrivate)
{
}
__weak void HAL_I2S_TxCpltCallback(I2S_Private *i2sPrivate)
{
}
#endif
static void I2S_Read_IT(uint32_t count, uint8_t *buf)
{
	uint8_t *recvBuf = buf;
	uint32_t readCount = count;
	while (readCount) {
		*recvBuf ++ = I2S->DA_RXFIFO;
		readCount --;
	}
}

static void I2S_Write_IT(uint32_t count, uint8_t *buf)
{
	uint32_t writeCount =  count;
	uint8_t *writeBuf = buf;
	while (writeCount) {
		I2S->DA_TXFIFO = *writeBuf ++;
		writeCount --;
	}
}

void I2S_ItRxCallback(void *arg)
{

	I2S_Private *i2sPrivate = (I2S_Private *)arg;
	uint8_t *readBuf = i2sPrivate->readPointer;
	uint32_t length = i2sPrivate->rxLength;
	uint32_t avaLength = I2S_GetRxAvaData();
	if (avaLength > length)
		avaLength = length;

	I2S_Read_IT(avaLength, readBuf);
	i2sPrivate->rxLength -= avaLength;
	i2sPrivate->readPointer += avaLength;
	if (i2sPrivate->rxLength < i2sPrivate->hwParam->rxFifoLevel) {
		I2S_DisableRxIRQ();

		/*release sem*/
		HAL_SemaphoreRelease(&(i2sPrivate->rxReady));
	}
}

void I2S_ItTxCallback(void *arg)
{
	I2S_Private *i2sPrivate = (I2S_Private *)arg;
	uint8_t *writeBuf = i2sPrivate->writePointer;
	uint32_t length = i2sPrivate->txLength;

	uint32_t emptyLength = I2S_GetTxEmptyRoom();
	if (length > emptyLength) {
		length = emptyLength;
	}

	I2S_Write_IT(length,writeBuf);
	i2sPrivate->txLength -= length;
	i2sPrivate->writePointer += length;

	if (i2sPrivate->txLength == 0) {
		I2S_DisableTxIRQ();
		/*release sem*/
		HAL_SemaphoreRelease(&(i2sPrivate->txReady));
	}

}
static HAL_Status I2S_SET_Mclk(uint32_t isEnable, uint32_t clkSource, uint32_t pll)
{
	if (isEnable == 0) {
		HAL_CLR_BIT(I2S->DA_CLKD, I2S_MCLK_OUT_EN_BIT);
		return HAL_OK;
	}
	uint32_t mclkDiv = pll / clkSource;
	CLK_DIVRegval *divRegval = DivRegval;

	do {
		if (divRegval->clkDiv == mclkDiv) {
			HAL_MODIFY_REG(I2S->DA_CLKD, I2S_MCLKDIV_MASK, divRegval->mregVal);
			break;
		}
		divRegval++;
	} while (divRegval->mregVal < I2S_MCLKDIV_192);
	HAL_SET_BIT(I2S->DA_CLKD, I2S_MCLK_OUT_EN_BIT);
	return HAL_OK;
}
static HAL_Status I2S_SET_SampleResolution(I2S_DataParam *param)
{
	if (!param)
		return HAL_INVALID;
	I2S_SampleResolution res = param->resolution;

	if (I2S_SR8BIT <= res &&  res <= I2S_SR32BIT)
		HAL_MODIFY_REG(I2S->DA_FMT0, I2S_SR_MASK, res);
	else {
		I2S_ERROR("Invalid sample resolution (%d) failed\n",res);
		return HAL_ERROR;
	}
	return HAL_OK;
}

static HAL_Status I2S_SET_ClkDiv(I2S_DataParam *param,  I2S_HWParam *hwParam)
{
	int32_t ret = HAL_OK;
	if (!param || !hwParam)
		return HAL_INVALID;
	uint32_t rate = 0;
	I2S_SampleRate SR = param->sampleRate;
	uint32_t Period = hwParam->lrckPeriod;
	uint16_t bclkDiv = 0;
	uint32_t audioPll = AUDIO_PLL_24;

	switch (SR) {
		case I2S_SR8K:/*  8000Hz  */
			rate = 8000;
			break;
		case I2S_SR12K:/*  12000Hz */
			rate = 12000;
			break;
		case I2S_SR16K:/*  16000Hz */
			rate = 16000;
			break;
		case I2S_SR24K:/*  24000Hz */
			rate = 24000;
			break;
		case I2S_SR32K:/*  32000Hz */
			rate = 32000;
			break;
		case I2S_SR48K:/*  48000Hz */
			rate = 48000;
			break;
		case I2S_SR11K:
			rate = 11025;
			break;
		case I2S_SR22K:
			rate = 22050;
			break;
		case I2S_SR44K:
			rate = 44100;
			break;
		default:
			I2S_ALERT("Invalid sample rate(%x) failed...\n",rate);
			return HAL_INVALID;
	}

	I2S_DEBUG("SAMPLE RATE:%d...\n",rate);
	if ((rate % 1000) != 0)
		audioPll = AUDIO_PLL_22;

	I2S_Private *i2sPrivate = &gI2sPrivate;

	/*set sysclk*/
	if (audioPll == AUDIO_PLL_24) {
		I2S_PLLAUDIO_Update(I2S_PLL_24M);
		HAL_PRCM_SetAudioPLLParam(i2sPrivate->audioPllParam);
		HAL_PRCM_SetAudioPLLPatternParam(i2sPrivate->audioPllPatParam);
	} else {
		I2S_PLLAUDIO_Update(I2S_PLL_22M);
		HAL_PRCM_SetAudioPLLParam(i2sPrivate->audioPllParam);
		HAL_PRCM_SetAudioPLLPatternParam(i2sPrivate->audioPllPatParam);
	}

	/*config bclk pll div*/
	if (!hwParam->i2sFormat)
		bclkDiv = audioPll/(2*Period*rate);
	else
		bclkDiv = audioPll/(Period*rate);

	CLK_DIVRegval *divRegval = DivRegval;
	do {
		if (divRegval->clkDiv == bclkDiv) {
			HAL_MODIFY_REG(I2S->DA_CLKD, I2S_BCLKDIV_MASK, divRegval->bregVal);
			break;
		}
		divRegval++;
	} while (divRegval->bregVal < I2S_BCLKDIV_192);

	return ret;
}

static HAL_Status I2S_SET_Format(I2S_HWParam *param)
{
	int32_t ret = HAL_OK;
	if (!param)
		return HAL_INVALID;

	/* config clk mode*/
	switch (param->clkMode) {
		case DAIFMT_CBM_CFM:
			HAL_MODIFY_REG(I2S->DA_CTL, I2S_BCLK_OUT_MASK|I2S_LRCK_OUT_MASK|I2S_SDO0_EN_BIT,
					I2S_BCLK_INPUT|I2S_LRCK_INPUT|I2S_SDO0_EN_BIT);

			break;
		case DAIFMT_CBS_CFS:
			HAL_MODIFY_REG(I2S->DA_CTL, I2S_BCLK_OUT_MASK|I2S_LRCK_OUT_MASK|I2S_SDO0_EN_BIT,
					I2S_BCLK_OUTPUT|I2S_LRCK_OUTPUT|I2S_SDO0_EN_BIT);
			break;
		default:
			ret = HAL_INVALID;
			I2S_ERROR("Invalid DAI format,failed (%d)...\n",param->clkMode);
			break;
	}

	/* config transfer format */
	switch (param->transferFormat) {
		case DAIFMT_I2S:
			HAL_MODIFY_REG(I2S->DA_CTL, I2S_MODE_SEL_MASK, I2S_LEFT_MODE);
			HAL_MODIFY_REG(I2S->DA_TX0CHSEL, I2S_TXN_OFFSET_MASK, I2S_TX_ONEBCLK_OFFSET);
			HAL_MODIFY_REG(I2S->DA_RXCHSEL, I2S_RXN_OFFSET_MASK, I2S_RX_ONEBCLK_OFFSET);
			break;
		case DAIFMT_RIGHT_J:
			HAL_MODIFY_REG(I2S->DA_CTL, I2S_MODE_SEL_MASK, I2S_RIGHT_MODE);
			break;
		case DAIFMT_LEFT_J:
			HAL_MODIFY_REG(I2S->DA_CTL, I2S_MODE_SEL_MASK, I2S_LEFT_MODE);
			HAL_MODIFY_REG(I2S->DA_TX0CHSEL, I2S_TXN_OFFSET_MASK, I2S_TX_NO_OFFSET);
			HAL_MODIFY_REG(I2S->DA_RXCHSEL, I2S_RXN_OFFSET_MASK, I2S_RX_NO_OFFSET);
			break;
		case DAIFMT_DSP_A:
			HAL_MODIFY_REG(I2S->DA_CTL, I2S_MODE_SEL_MASK, I2S_PCM_MODE);
			HAL_MODIFY_REG(I2S->DA_TX0CHSEL, I2S_TXN_OFFSET_MASK, I2S_TX_ONEBCLK_OFFSET);
			HAL_MODIFY_REG(I2S->DA_RXCHSEL, I2S_RXN_OFFSET_MASK, I2S_RX_ONEBCLK_OFFSET);
			break;
		case DAIFMT_DSP_B:
			HAL_MODIFY_REG(I2S->DA_CTL, I2S_MODE_SEL_MASK, I2S_PCM_MODE);
			HAL_MODIFY_REG(I2S->DA_TX0CHSEL, I2S_TXN_OFFSET_MASK, I2S_TX_NO_OFFSET);
			HAL_MODIFY_REG(I2S->DA_RXCHSEL, I2S_RXN_OFFSET_MASK, I2S_RX_NO_OFFSET);
			break;
		default:
			ret = HAL_INVALID;
			I2S_ERROR("Invalid transfer format,failed (%d)...\n",param->transferFormat);
			break;
	}

	/* config signal interval */
	switch (param->signalInterval) {
		case DAIFMT_NB_NF:
			HAL_CLR_BIT(I2S->DA_FMT0, I2S_LRCK_POLARITY_MASK);
			HAL_CLR_BIT(I2S->DA_FMT0, I2S_BCLK_POLARITY_MASK);
			break;
		case DAIFMT_NB_IF:
			HAL_SET_BIT(I2S->DA_FMT0, I2S_LRCK_POLARITY_MASK);
			HAL_CLR_BIT(I2S->DA_FMT0, I2S_BCLK_POLARITY_MASK);
			break;
		case DAIFMT_IB_NF:
			HAL_CLR_BIT(I2S->DA_FMT0, I2S_LRCK_POLARITY_MASK);
			HAL_SET_BIT(I2S->DA_FMT0, I2S_BCLK_POLARITY_MASK);
			break;
		case DAIFMT_IB_IF:
			HAL_SET_BIT(I2S->DA_FMT0, I2S_LRCK_POLARITY_MASK);
			HAL_SET_BIT(I2S->DA_FMT0, I2S_BCLK_POLARITY_MASK);
			break;
		default:
			ret = HAL_INVALID;
			I2S_ERROR("Invalid signal Interval,failed (%d)...\n",param->signalInterval);
			break;
	}
	return ret;
}
static HAL_Status I2S_SET_Channels(I2S_DataParam *param)
{
	uint8_t channel = 0;
	if (!param)
		return HAL_INVALID;

	if (param->direction == PLAYBACK) {/*play*/
		if (param->channels < I2S_TX_SLOT_NUM1 || param->channels > I2S_TX_SLOT_NUM8) {
			I2S_ERROR("Invalid usr tx channels num,failed (%d)...\n",param->channels);
			return HAL_INVALID;
		}
		HAL_MODIFY_REG(I2S->DA_CHCFG, I2S_TX_SLOT_NUM_MASK, ((param->channels - 1) << I2S_TX_SLOT_NUM_SHIFT));
		HAL_MODIFY_REG(I2S->DA_TX0CHSEL, I2S_TXN_CHANNEL_SEL_MASK, ((param->channels -1) << I2S_TXN_CHANNEL_SEL_SHIFT));
		HAL_MODIFY_REG(I2S->DA_TX0CHSEL, I2S_TXN_CHANNEL_SLOT_ENABLE_MASK, I2S_TXN_CHANNEL_SLOTS_ENABLE(param->channels));

		for (channel = 0; channel < param->channels; channel++) {
			//HAL_MODIFY_REG(I2S->DA_TX0CHMAP, I2S_TXN_CHX_MAP_MASK(channel), I2S_TXN_CHX_MAP(channel,channel));
			HAL_MODIFY_REG(I2S->DA_TX0CHMAP, I2S_TXN_CHX_MAP_MASK(channel),I2S_TXN_CHX_MAP(channel));
		}

	} else {/*record*/
		if (param->channels < I2S_RX_SLOT_NUM1 || param->channels > I2S_RX_SLOT_NUM8){
			I2S_ERROR("Invalid usr rx channels num,failed (%d)...\n",param->channels);
			return HAL_INVALID;
		}

		HAL_MODIFY_REG(I2S->DA_CHCFG, I2S_RX_SLOT_NUM_MASK, ((param->channels - 1) << I2S_RX_SLOT_NUM_SHIFT));
		HAL_MODIFY_REG(I2S->DA_RXCHSEL, I2S_RXN_CHANNEL_SEL_MASK, ((param->channels - 1) << I2S_RXN_CHANNEL_SEL_SHIFT));
		for (channel = 0; channel < param->channels; channel++) {
			HAL_MODIFY_REG(I2S->DA_RXCHMAP, I2S_RXN_CHX_MAP_MASK(channel), I2S_RXN_CHX_MAP(channel));
		}
	}
	return HAL_OK;
}
void HAL_I2S_Trigger(bool enable,I2S_StreamDir dir);
static int I2S_DMA_BUFFER_CHECK_Threshold(uint8_t dir)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	if (dir == 0) {
		if (i2sPrivate->txHalfCallCount >= UNDERRUN_THRESHOLD ||
			i2sPrivate->txEndCallCount >= UNDERRUN_THRESHOLD) {
			I2S_ERROR("Tx : underrun and stop dma tx....\n");
			HAL_I2S_Trigger(FALSE,PLAYBACK);/*stop*/
			i2sPrivate->txRunning =FALSE;
			i2sPrivate->writePointer = NULL;
			i2sPrivate->txHalfCallCount = 0;
			i2sPrivate->txEndCallCount = 0;
			i2sPrivate->txDmaPointer = NULL;
			return -1;
		}

	} else {
		if (i2sPrivate->rxHalfCallCount >= OVERRUN_THRESHOLD ||
			i2sPrivate->rxEndCallCount >= OVERRUN_THRESHOLD) {
			I2S_ERROR("Rx : overrun and stop dma rx....\n");
			HAL_I2S_Trigger(FALSE,RECORD);/*stop*/
			i2sPrivate->rxRunning =FALSE;
			i2sPrivate->rxHalfCallCount = 0;
			i2sPrivate->rxEndCallCount = 0;
			i2sPrivate->readPointer = NULL;
			i2sPrivate->rxDmaPointer = NULL;
			return -1;
		}

	}

	return 0;
}

static void I2S_DMAHalfCallback(void *arg)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	if (arg == &(i2sPrivate->txReady)) {
		i2sPrivate->txHalfCallCount ++;
		if (I2S_DMA_BUFFER_CHECK_Threshold(0) != 0)
			return;
		i2sPrivate->txDmaPointer = i2sPrivate->txBuf + I2S_BUF_LENGTH/2;
		if (i2sPrivate->isTxSemaphore) {
			i2sPrivate->isTxSemaphore = FALSE;
			HAL_SemaphoreRelease((HAL_Semaphore *)arg);
		}

	} else {
		i2sPrivate->rxHalfCallCount ++;
		if (I2S_DMA_BUFFER_CHECK_Threshold(1) != 0)
			return;
		i2sPrivate->rxDmaPointer = i2sPrivate->rxBuf + I2S_BUF_LENGTH/2;
		if (i2sPrivate->isRxSemaphore) {
			i2sPrivate->isRxSemaphore = FALSE;
			HAL_SemaphoreRelease((HAL_Semaphore *)arg);
		}
	}
}

static void I2S_DMAEndCallback(void *arg)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	if (arg == &(i2sPrivate->txReady)) {
		i2sPrivate->txEndCallCount ++;
		if (I2S_DMA_BUFFER_CHECK_Threshold(0) != 0)
			return;
		i2sPrivate->txDmaPointer = i2sPrivate->txBuf;
		if (i2sPrivate->isTxSemaphore) {
			i2sPrivate->isTxSemaphore = FALSE;
			HAL_SemaphoreRelease((HAL_Semaphore *)arg);
		}
	} else {
		i2sPrivate->rxEndCallCount ++;
		if (I2S_DMA_BUFFER_CHECK_Threshold(1) != 0)
			return;
		i2sPrivate->rxDmaPointer = i2sPrivate->rxBuf + I2S_BUF_LENGTH/2;
		if (i2sPrivate->isRxSemaphore) {
			i2sPrivate->isRxSemaphore = FALSE;
			HAL_SemaphoreRelease((HAL_Semaphore *)arg);
		}
	}
}

static void I2S_DMAStart(DMA_Channel chan, uint32_t srcAddr, uint32_t dstAddr, uint32_t datalen)
{
	HAL_DMA_Start(chan, srcAddr, dstAddr, datalen);
}

static void I2S_DMAStop(DMA_Channel chan)
{
	HAL_DMA_Stop(chan);
}

static void I2S_DMASet(DMA_Channel channel,I2S_StreamDir dir)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	DMA_ChannelInitParam dmaParam;
	HAL_Memset(&dmaParam, 0, sizeof(dmaParam));
	if (dir == PLAYBACK) {

		dmaParam.cfg = HAL_DMA_MakeChannelInitCfg(DMA_WORK_MODE_CIRCULAR,
				DMA_WAIT_CYCLE_2,
				DMA_BYTE_CNT_MODE_REMAIN,
				DMA_DATA_WIDTH_16BIT,
				DMA_BURST_LEN_1,
				DMA_ADDR_MODE_FIX,
				DMA_PERIPH_DAUDIO,
				DMA_DATA_WIDTH_16BIT,
				DMA_BURST_LEN_1,
				DMA_ADDR_MODE_INC,
				DMA_PERIPH_SRAM);

		dmaParam.endArg = &(i2sPrivate->txReady);
		dmaParam.halfArg = &(i2sPrivate->txReady);

	} else {

		dmaParam.cfg = HAL_DMA_MakeChannelInitCfg(DMA_WORK_MODE_CIRCULAR,
				DMA_WAIT_CYCLE_2,
				DMA_BYTE_CNT_MODE_REMAIN,
				DMA_DATA_WIDTH_16BIT,
				DMA_BURST_LEN_1,
				DMA_ADDR_MODE_INC,
				DMA_PERIPH_SRAM,
				DMA_DATA_WIDTH_16BIT,
				DMA_BURST_LEN_1,
				DMA_ADDR_MODE_FIX,
				DMA_PERIPH_DAUDIO);

		dmaParam.endArg = &(i2sPrivate->rxReady);
		dmaParam.halfArg = &(i2sPrivate->rxReady);

	}
	dmaParam.irqType = DMA_IRQ_TYPE_BOTH;
	dmaParam.endCallback = I2S_DMAEndCallback;
	dmaParam.halfCallback = I2S_DMAHalfCallback;
	HAL_DMA_Init(channel, &dmaParam);
}

static void tx_enable(bool enable)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	/*clear tx tifo*/
	HAL_SET_BIT(I2S->DA_FCTL, I2S_TXFIFO_RESET_BIT);

	if (enable) {
		if (i2sPrivate->pdataParam.isEnbleDmaTx && i2sPrivate->txDMAChan != DMA_CHANNEL_INVALID)
			HAL_SET_BIT(I2S->DA_INT, I2S_TXFIFO_DMA_ITEN_BIT);
	} else {
		if (i2sPrivate->pdataParam.isEnbleDmaTx && i2sPrivate->txDMAChan != DMA_CHANNEL_INVALID)
			HAL_CLR_BIT(I2S->DA_INT, I2S_TXFIFO_DMA_ITEN_BIT);
	}
}
static void rx_enable(bool enable)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	/*clear rx tifo*/
	HAL_SET_BIT(I2S->DA_FCTL, I2S_RXFIFO_RESET_BIT);

	if (enable) {
		if (i2sPrivate->cdataParam.isEnbleDmaRx && i2sPrivate->rxDMAChan != DMA_CHANNEL_INVALID)
			HAL_SET_BIT(I2S->DA_INT, I2S_RXFIFO_DMA_ITEN_BIT);
	} else {

		if (i2sPrivate->cdataParam.isEnbleDmaRx && i2sPrivate->rxDMAChan != DMA_CHANNEL_INVALID)
			HAL_CLR_BIT(I2S->DA_INT, I2S_RXFIFO_DMA_ITEN_BIT);
	}
}

void HAL_I2S_Trigger(bool enable,I2S_StreamDir dir)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	HAL_MutexLock(&i2sPrivate->devTriggerLock, OS_WAIT_FOREVER);
	if (enable) {
		if (dir == PLAYBACK) {
			/*trigger tx*/
			tx_enable(enable);

			/* start dma*/
			if (i2sPrivate->pdataParam.isEnbleDmaTx && i2sPrivate->txDMAChan != DMA_CHANNEL_INVALID) {
				I2S_DMAStart(i2sPrivate->txDMAChan, (uint32_t)i2sPrivate->txBuf,
						(uint32_t)&(I2S->DA_TXFIFO), i2sPrivate->txLength);
			}
			i2sPrivate->txRunning = TRUE;
		} else {
			rx_enable(enable);
			if (i2sPrivate->cdataParam.isEnbleDmaRx && i2sPrivate->rxDMAChan != DMA_CHANNEL_INVALID)
				I2S_DMAStart(i2sPrivate->rxDMAChan, (uint32_t)&(I2S->DA_RXFIFO),
						(uint32_t)i2sPrivate->rxBuf, i2sPrivate->rxLength);
			i2sPrivate->rxRunning = TRUE;
		}
	} else {
		if (dir == PLAYBACK) {
			tx_enable(enable);
			if (i2sPrivate->txDMAChan != DMA_CHANNEL_INVALID && i2sPrivate->pdataParam.isEnbleDmaTx)
				I2S_DMAStop(i2sPrivate->txDMAChan);
			i2sPrivate->txRunning = FALSE;
		} else {
			rx_enable(enable);
			if (i2sPrivate->rxDMAChan != DMA_CHANNEL_INVALID && i2sPrivate->cdataParam.isEnbleDmaRx)
				I2S_DMAStop(i2sPrivate->rxDMAChan);
			i2sPrivate->rxRunning = FALSE;
		}
	}
	#if 0
        int i =0 ;
        printf("\n");
        for (i = 0; i < 0x68; i = i+4) {
                printf("0x%x: 0x%08x",i,(*(uint32_t *)(0x40042c00+i)));
                printf("\n");
        }
	#endif
	HAL_MutexUnlock(&i2sPrivate->devTriggerLock);
}

int32_t HAL_I2S_Write_DMA(uint8_t *buf, uint32_t size)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	if (!buf || size <= 0)
		return HAL_INVALID;
	void *pdata = buf;
	uint8_t *lastWritePointer = NULL;
	uint32_t toWrite = 0,writeSize = I2S_BUF_LENGTH/2;
	if (size < writeSize) {
		//  I2S_INFO("Tx : size is too small....\n");
		return HAL_INVALID;
	}
	while (size > 0) {
		if (size < writeSize)
			break;
		if (i2sPrivate->txRunning == FALSE) {
			if (!i2sPrivate->writePointer)
				i2sPrivate->writePointer = i2sPrivate->txBuf;

			lastWritePointer = i2sPrivate->writePointer;

			I2S_MEMCPY(lastWritePointer, pdata, writeSize);
			pdata += writeSize;
			lastWritePointer += writeSize;
			toWrite += writeSize;
			size -= writeSize;
			if (lastWritePointer >= i2sPrivate->txBuf + I2S_BUF_LENGTH)
				lastWritePointer = i2sPrivate->txBuf;
			i2sPrivate->writePointer = lastWritePointer;
			if (i2sPrivate->writePointer == i2sPrivate->txBuf) {
				I2S_DEBUG("Tx: play start...\n");
				HAL_I2S_Trigger(TRUE,PLAYBACK);/*play*/
				i2sPrivate->txRunning =TRUE;
			}
		} else {
			/*disable irq*/
			HAL_DisableIRQ();

			lastWritePointer = i2sPrivate->writePointer;
			if (i2sPrivate->txHalfCallCount && i2sPrivate->txEndCallCount) {
				I2S_ERROR("TxCount:(H:%d,F:%d)\n",i2sPrivate->txHalfCallCount,i2sPrivate->txEndCallCount);
				I2S_ERROR("Tx : underrun....\n");
				i2sPrivate->txHalfCallCount = 0;
				i2sPrivate->txEndCallCount = 0;

				if (i2sPrivate->txDmaPointer == i2sPrivate->txBuf) {
					lastWritePointer = i2sPrivate->txBuf + I2S_BUF_LENGTH/2;
				} else {
					lastWritePointer = i2sPrivate->txBuf;
				}
			} else if (i2sPrivate->txHalfCallCount) {
				i2sPrivate->txHalfCallCount --;
			} else if (i2sPrivate->txEndCallCount) {
				i2sPrivate->txEndCallCount --;
			} else {
				/**enable irq**/
				i2sPrivate->isTxSemaphore = TRUE;
				HAL_EnableIRQ();
				HAL_SemaphoreWait(&(i2sPrivate->txReady), HAL_WAIT_FOREVER);
				/**disable irq**/
				HAL_DisableIRQ();
				if (i2sPrivate->txHalfCallCount)
					i2sPrivate->txHalfCallCount --;
				if (i2sPrivate->txEndCallCount)
					i2sPrivate->txEndCallCount --;
			}

			I2S_MEMCPY(lastWritePointer, pdata, writeSize);
			pdata += writeSize;
			lastWritePointer += writeSize;
			toWrite += writeSize;
			size -= writeSize;
			if (lastWritePointer >= i2sPrivate->txBuf + I2S_BUF_LENGTH)
				lastWritePointer = i2sPrivate->txBuf;
			i2sPrivate->writePointer = lastWritePointer;

			/**enable irq**/
			HAL_EnableIRQ();
		}
	}
	return toWrite;
}

int32_t HAL_I2S_Read_DMA(uint8_t *buf, uint32_t size)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	if (!buf || size <= 0)
		return HAL_INVALID;
	void *pdata = buf;
	uint8_t *lastReadPointer = NULL;
	uint32_t readSize = I2S_BUF_LENGTH/2;
	uint32_t toRead = 0;

	if ((size / readSize) < 1) {
		I2S_ERROR("Rx buf size too small...\n");
		return HAL_ERROR;
	}

	while (size > 0) {

		if (size/readSize < 1)
			break;
		if (i2sPrivate->rxRunning == FALSE) {

			if (!i2sPrivate->readPointer)
				i2sPrivate->readPointer = i2sPrivate->rxBuf;
			I2S_DEBUG("Rx: record start...\n");
			HAL_I2S_Trigger(TRUE,RECORD);

		} else {
			/*disable irq*/
			HAL_DisableIRQ();
			lastReadPointer = i2sPrivate->readPointer;
			if (i2sPrivate->rxHalfCallCount && i2sPrivate->rxEndCallCount) {
				I2S_ERROR("RxCount:(H:%d,F:%d)\n",i2sPrivate->rxHalfCallCount,i2sPrivate->rxEndCallCount);
				I2S_ERROR("Rx : overrun....\n");
				i2sPrivate->rxHalfCallCount = 0;
				i2sPrivate->rxEndCallCount = 0;

				if (i2sPrivate->rxDmaPointer == i2sPrivate->rxBuf) {
					lastReadPointer = i2sPrivate->txBuf + I2S_BUF_LENGTH/2;
				} else {
					lastReadPointer = i2sPrivate->txBuf;
				}
			} else if (i2sPrivate->rxHalfCallCount) {
				i2sPrivate->rxHalfCallCount --;
			} else if (i2sPrivate->rxEndCallCount) {
				i2sPrivate->rxEndCallCount --;
			} else {
				/**enable irq**/
				i2sPrivate->isRxSemaphore = TRUE;
				HAL_EnableIRQ();
				HAL_SemaphoreWait(&(i2sPrivate->rxReady), HAL_WAIT_FOREVER);
				/**disable irq**/
				HAL_DisableIRQ();
				if (i2sPrivate->rxHalfCallCount)
					i2sPrivate->rxHalfCallCount --;
				if (i2sPrivate->rxEndCallCount)
					i2sPrivate->rxEndCallCount --;
			}
			I2S_MEMCPY(pdata, lastReadPointer, readSize);
			pdata += readSize;
			lastReadPointer += readSize;
			if (lastReadPointer >= i2sPrivate->rxBuf + I2S_BUF_LENGTH)
				lastReadPointer = i2sPrivate->rxBuf;
			i2sPrivate->readPointer = lastReadPointer;
			/**enable irq**/
			HAL_EnableIRQ();
			size -= readSize;
			toRead += readSize;
		}
	}
	return toRead;
}

void I2S_IRQHandler(void)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	uint32_t irqStatus = I2S_GetIrqStatus();
	if (irqStatus & I2S_RX_FIFO_AVABLE_IT_BIT) {
		I2S_ClearRxPendingIRQ();
		if (i2sPrivate->rxItCallback != NULL)
			i2sPrivate->rxItCallback(i2sPrivate);

	} else if (irqStatus & I2S_TX_FIFO_EMPTY_IT_BIT) {

		I2S_ClearTxPendingIRQ();
		if (i2sPrivate->txItCallback != NULL) {
			i2sPrivate->txItCallback(i2sPrivate);
		}

	} else {
		I2S_ERROR("ERR...\n");
	}
}

int32_t HAL_I2S_Read_IT(uint8_t *buf, uint32_t size)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;

	if (!buf)
		return HAL_INVALID;

	i2sPrivate->readPointer = i2sPrivate->rxBuf = buf;
	i2sPrivate->rxLength = size;

	if (!i2sPrivate->rxRunning) {
		HAL_I2S_Trigger(TRUE,RECORD);
		I2S_EnableRx();
		HAL_NVIC_SetPriority(I2S_IRQn, NVIC_PERIPHERAL_PRIORITY_DEFAULT);
		HAL_NVIC_EnableIRQ(I2S_IRQn);
	}

	I2S_EnableRxIRQ();

	HAL_SemaphoreWait(&(i2sPrivate->rxReady), HAL_WAIT_FOREVER);

	return (size - i2sPrivate->rxLength);
}

HAL_Status HAL_I2S_Write_IT(uint8_t *buf, uint32_t size)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;

	if (!buf)
		return HAL_INVALID;

	i2sPrivate->writePointer = i2sPrivate->txBuf = buf;
	i2sPrivate->txLength = size;

	if (!i2sPrivate->txRunning) {
		HAL_I2S_Trigger(TRUE,PLAYBACK);
		I2S_EnableTx();
		HAL_NVIC_SetPriority(I2S_IRQn, NVIC_PERIPHERAL_PRIORITY_DEFAULT);
		HAL_NVIC_EnableIRQ(I2S_IRQn);
	}

	I2S_EnableTxIRQ();
	HAL_SemaphoreWait(&(i2sPrivate->txReady), HAL_WAIT_FOREVER);

	return HAL_OK;
}

HAL_Status HAL_I2S_Open(I2S_DataParam *param)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;

	I2S_DataParam *dataParam = param;

	if (param->direction == PLAYBACK) {
		if (i2sPrivate->isTxInitiate == TRUE) {
			I2S_ERROR("Tx device opened already.open faied...\n");
			return HAL_ERROR;
		}
		i2sPrivate->isTxInitiate = TRUE;

		I2S_MEMCPY(&(i2sPrivate->pdataParam), param, sizeof(*param));
	} else {
		if (i2sPrivate->isRxInitiate == TRUE) {
			I2S_ERROR("Rx device opened already.open faied...\n");
			return HAL_ERROR;
		}
		i2sPrivate->isRxInitiate = TRUE;

		I2S_MEMCPY(&(i2sPrivate->cdataParam), param, sizeof(*param));
	}
	I2S_BUF_LENGTH = dataParam->bufSize;
	if (param->direction == PLAYBACK) {

		if (param->isEnbleDmaTx) {
			i2sPrivate->txDMAChan = DMA_CHANNEL_INVALID;
			i2sPrivate->txLength = I2S_BUF_LENGTH;
			i2sPrivate->txHalfCallCount = 0;
			i2sPrivate->txEndCallCount = 0;
#ifdef RESERVERD_MEMORY_FOR_I2S_TX
			i2sPrivate->txBuf = I2STX_BUF;
#else
			i2sPrivate->txBuf = I2S_MALLOC(I2S_BUF_LENGTH);
			if(i2sPrivate->txBuf)
				I2S_MEMSET(i2sPrivate->txBuf,0,I2S_BUF_LENGTH);
			else {
				I2S_ERROR("Malloc tx buf(for DMA),faild...\n");
				return HAL_ERROR;
			}
#endif
			/*request DMA channel*/
			i2sPrivate->txDMAChan = HAL_DMA_Request();
			if (i2sPrivate->txDMAChan == DMA_CHANNEL_INVALID) {
				I2S_ERROR("Obtain I2S tx DMA channel,faild...\n");
				return HAL_ERROR;
			} else
				I2S_DMASet(i2sPrivate->txDMAChan, PLAYBACK);
		} else {
			i2sPrivate->txItCallback = I2S_ItTxCallback;
		}
		HAL_SemaphoreInitBinary(&i2sPrivate->txReady);
	} else {
		if (param->isEnbleDmaRx) {
			i2sPrivate->rxDMAChan = DMA_CHANNEL_INVALID;
			i2sPrivate->rxLength = I2S_BUF_LENGTH;
			i2sPrivate->rxHalfCallCount = 0;
			i2sPrivate->rxEndCallCount = 0;
#ifdef RESERVERD_MEMORY_FOR_I2S_RX
			i2sPrivate->rxBuf = I2SRX_BUF;
#else
			i2sPrivate->rxBuf = I2S_MALLOC(I2S_BUF_LENGTH);
			if(i2sPrivate->rxBuf)
				I2S_MEMSET(i2sPrivate->rxBuf,0,I2S_BUF_LENGTH);
			else {
				I2S_ERROR("Malloc rx buf(for DMA),faild...\n");
				return HAL_ERROR;
			}
#endif
			i2sPrivate->rxDMAChan = HAL_DMA_Request();
			if (i2sPrivate->rxDMAChan == DMA_CHANNEL_INVALID) {
				I2S_ERROR("Obtain I2S rx DMA channel,faild...\n");
				return HAL_ERROR;
			} else
				I2S_DMASet(i2sPrivate->rxDMAChan, RECORD);
		}else {
			i2sPrivate->rxItCallback = I2S_ItRxCallback;
		}
		HAL_SemaphoreInitBinary(&i2sPrivate->rxReady);
	}

	HAL_MutexLock(&i2sPrivate->devSetLock, OS_WAIT_FOREVER);
	/*set bclk*/
	I2S_SET_ClkDiv(dataParam, i2sPrivate->hwParam);

	/*set sample resolution*/
	I2S_SET_SampleResolution(dataParam);

	/*set channel*/
	I2S_SET_Channels(dataParam);

	if (param->direction == PLAYBACK) {
		I2S_EnableTx();
	} else {
		I2S_EnableRx();
	}
	HAL_MutexUnlock(&i2sPrivate->devSetLock);

	return HAL_OK;
}

HAL_Status HAL_I2S_Close(uint32_t dir)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;

	if (dir == PLAYBACK) {//play
		HAL_I2S_Trigger(FALSE,PLAYBACK);
		I2S_DisableTx();
		i2sPrivate->txRunning = FALSE;
		i2sPrivate->isTxInitiate = FALSE;
		if (i2sPrivate->txDMAChan != DMA_CHANNEL_INVALID) {
			HAL_DMA_DeInit(i2sPrivate->txDMAChan);
			HAL_DMA_Release(i2sPrivate->txDMAChan);
			i2sPrivate->txDMAChan = DMA_CHANNEL_INVALID;
			I2S_MEMSET(&(i2sPrivate->pdataParam), 0, sizeof(I2S_DataParam));
#ifndef RESERVERD_MEMORY_FOR_I2S_TX
			I2S_FREE(i2sPrivate->txBuf);
#endif
		} else {
			I2S_DisableTxIRQ();
			if (!(i2sPrivate->rxRunning))
				HAL_NVIC_DisableIRQ(I2S_IRQn);
		}
		HAL_SemaphoreDeinit(&i2sPrivate->txReady);
		i2sPrivate->txBuf = NULL;
		i2sPrivate->txLength = 0;
		i2sPrivate->writePointer = NULL;
		i2sPrivate->txHalfCallCount = 0;
		i2sPrivate->txEndCallCount = 0;
	} else {
		HAL_I2S_Trigger(FALSE,RECORD);
		I2S_DisableRx();
		i2sPrivate->isRxInitiate = FALSE;
		i2sPrivate->rxRunning = FALSE;
		if (i2sPrivate->rxDMAChan != DMA_CHANNEL_INVALID) {
			HAL_DMA_DeInit(i2sPrivate->rxDMAChan);
			HAL_DMA_Release(i2sPrivate->rxDMAChan);
			i2sPrivate->rxDMAChan = DMA_CHANNEL_INVALID;
			I2S_MEMSET(&(i2sPrivate->cdataParam), 0, sizeof(I2S_DataParam));
#ifndef RESERVERD_MEMORY_FOR_I2S_TX
			I2S_FREE(i2sPrivate->rxBuf);
#endif
		} else {
			I2S_DisableRxIRQ();
			if (!(i2sPrivate->txRunning))
				HAL_NVIC_DisableIRQ(I2S_IRQn);
		}
		HAL_SemaphoreDeinit(&i2sPrivate->rxReady);
		i2sPrivate->rxBuf = NULL;
		i2sPrivate->rxLength = 0;
		i2sPrivate->readPointer = NULL;

		i2sPrivate->rxHalfCallCount = 0;
		i2sPrivate->rxEndCallCount = 0;
	}

	return HAL_OK;
}

static inline HAL_Status I2S_PINS_Init()
{
	I2S_Private *i2sPrivate = &gI2sPrivate;

	if (i2sPrivate->boardCfg)
		i2sPrivate->boardCfg(0, HAL_BR_PINMUX_INIT, NULL);
	else
		return HAL_INVALID;
	return HAL_OK;

}
static inline HAL_Status I2S_PINS_Deinit()
{
	I2S_Private *i2sPrivate = &gI2sPrivate;

	if (i2sPrivate->boardCfg)
		i2sPrivate->boardCfg(0, HAL_BR_PINMUX_DEINIT, NULL);
	else
		return HAL_INVALID;
	return HAL_OK;
}

static inline HAL_Status I2S_HwInit(I2S_HWParam *param)
{
	if (!param)
		return HAL_INVALID;

	/*config device clk source*/
	if (param->codecClk.isDevclk != 0) {
		I2S_SET_Mclk(TRUE,param->codecClk.clkSource, AUDIO_DEVICE_PLL);
	}

	/* set lrck period /frame mode */
	HAL_MODIFY_REG(I2S->DA_FMT0, I2S_LRCK_PERIOD_MASK|I2S_LRCK_WIDTH_MASK|I2S_SW_SEC_MASK,
			I2S_LRCK_PERIOD(param->lrckPeriod)|param->frameMode|I2S_SLOT_WIDTH_BIT32);

	/* set first transfer bit */
	// HAL_MODIFY_REG(I2S->DA_FMT1, I2S_TX_MLS_MASK|I2S_RX_MLS_MASK|I2S_SW_SEC_MASK|
	HAL_MODIFY_REG(I2S->DA_FMT1, I2S_TX_MLS_MASK|I2S_RX_MLS_MASK|
			I2S_PCM_TXMODE_MASK|I2S_PCM_RXMODE_MASK|I2S_SEXT_MASK,
			//param->txMsbFirst|param->rxMsbFirst|I2S_SLOT_WIDTH_BIT32|
			param->txMsbFirst|param->rxMsbFirst|
			I2S_TX_LINEAR_PCM|I2S_RX_LINEAR_PCM|I2S_ZERO_SLOT);
	/* global enable */
	HAL_SET_BIT(I2S->DA_CTL, I2S_GLOBE_EN_BIT);

	HAL_MODIFY_REG(I2S->DA_FCTL, I2S_RXFIFO_LEVEL_MASK|I2S_TXFIFO_LEVEL_MASK|I2S_RX_FIFO_MODE_SHIFT|I2S_TX_FIFO_MODE_MASK,
			I2S_RXFIFO_TRIGGER_LEVEL(param->rxFifoLevel)|I2S_TXFIFO_TRIGGER_LEVEL(param->txFifoLevel)|I2S_RX_FIFO_MODE3|I2S_TX_FIFO_MODE1);
	return I2S_SET_Format(param);
}

static inline HAL_Status I2S_HwDeInit(I2S_HWParam *param)
{
	if (!param)
		return HAL_INVALID;
	/* global disable */
	HAL_CLR_BIT(I2S->DA_CTL, I2S_GLOBE_EN_BIT);
	I2S_SET_Mclk(FALSE, 0, 0);
	return HAL_OK;
}

#ifdef CONFIG_PM
static int i2s_suspend(struct soc_device *dev, enum suspend_state_t state)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	switch (state) {
		case PM_MODE_SLEEP:
		case PM_MODE_STANDBY:
		case PM_MODE_HIBERNATION:
		case PM_MODE_POWEROFF:
			HAL_MutexLock(&i2sPrivate->devSetLock, OS_WAIT_FOREVER);
			I2S_HwDeInit(i2sPrivate->hwParam);
			HAL_MutexUnlock(&i2sPrivate->devSetLock);

			I2S_PINS_Deinit();
			HAL_CCM_DAUDIO_DisableMClock();
			HAL_CCM_BusDisablePeriphClock(CCM_BUS_PERIPH_BIT_DAUDIO);
			HAL_PRCM_DisableAudioPLL();
			HAL_PRCM_DisableAudioPLLPattern();
			break;
		default:
			break;
	}
	return 0;
}

static int i2s_resume(struct soc_device *dev, enum suspend_state_t state)
{
	I2S_Private *i2sPrivate = &gI2sPrivate;

	switch (state) {
		case PM_MODE_SLEEP:
		case PM_MODE_STANDBY:
		case PM_MODE_HIBERNATION:
			I2S_PINS_Init();
			/*init and enable clk*/
			I2S_PLLAUDIO_Update(I2S_PLL_22M);
			HAL_PRCM_SetAudioPLLParam(i2sPrivate->audioPllParam);
			HAL_PRCM_SetAudioPLLPatternParam(i2sPrivate->audioPllPatParam);
			HAL_PRCM_EnableAudioPLL();
			HAL_PRCM_EnableAudioPLLPattern();

			HAL_CCM_BusEnablePeriphClock(CCM_BUS_PERIPH_BIT_DAUDIO);
			HAL_CCM_BusReleasePeriphReset(CCM_BUS_PERIPH_BIT_DAUDIO);
			HAL_CCM_DAUDIO_SetMClock(AUDIO_PLL_SRC);
			HAL_CCM_DAUDIO_EnableMClock();

			HAL_MutexLock(&i2sPrivate->devSetLock, OS_WAIT_FOREVER);
			I2S_HwInit(i2sPrivate->hwParam);
			HAL_MutexUnlock(&i2sPrivate->devSetLock);
			break;
		default:
			break;
	}
	return 0;
}

static struct soc_device_driver i2s_drv = {
	.name = "I2S",
	.suspend = i2s_suspend,
	.resume = i2s_resume,
};

static struct soc_device i2s_dev = {
	.name = "I2S",
	.driver = &i2s_drv,
};

#define I2S_DEV (&i2s_dev)
#else
#define I2S_DEV NULL
#endif

HAL_Status HAL_I2S_Init(I2S_Param *param)

{
	int32_t ret = 0;
	if (!param)
		return HAL_INVALID;

	/*Init pins for i2S*/
	I2S_Private *i2sPrivate = &gI2sPrivate;

	if (i2sPrivate->isHwInit == TRUE)
		return HAL_OK;
	I2S_MEMSET(i2sPrivate,0,sizeof(*i2sPrivate));
	i2sPrivate->isHwInit = TRUE;

	if (param->hwParam == NULL)
		i2sPrivate->hwParam = &gHwParam;
	else
		i2sPrivate->hwParam = param->hwParam;

	HAL_MutexInit(&i2sPrivate->devSetLock);
	HAL_MutexInit(&i2sPrivate->devTriggerLock);

	if (param->boardCfg != NULL)
		i2sPrivate->boardCfg = param->boardCfg;
	else
		return HAL_INVALID;
	I2S_PINS_Init();

	/*init and enable clk*/
	I2S_PLLAUDIO_Update(I2S_PLL_22M);
	HAL_PRCM_SetAudioPLLParam(i2sPrivate->audioPllParam);
	HAL_PRCM_EnableAudioPLL();
	HAL_PRCM_SetAudioPLLPatternParam(i2sPrivate->audioPllPatParam);
	HAL_PRCM_EnableAudioPLLPattern();

	HAL_CCM_BusEnablePeriphClock(CCM_BUS_PERIPH_BIT_DAUDIO);
	HAL_CCM_BusReleasePeriphReset(CCM_BUS_PERIPH_BIT_DAUDIO);
	HAL_CCM_DAUDIO_SetMClock(AUDIO_PLL_SRC);
	HAL_CCM_DAUDIO_EnableMClock();

	HAL_MutexLock(&i2sPrivate->devSetLock, OS_WAIT_FOREVER);
	ret = I2S_HwInit(i2sPrivate->hwParam);

#ifdef CONFIG_PM
	pm_register_ops(I2S_DEV);
#endif

	HAL_MutexUnlock(&i2sPrivate->devSetLock);
	/*init I2S HW*/
	return ret;
}
#if 0
void HAL_I2S_REG_DEBUG()
{
	int i = 0;
	for (i = 0; i < 0x58; i = i+4)
		printf("REG:0X%x,VAL:0X%x\n",i,(*((__IO uint32_t *)(0x40042c00+i))));
}
#endif
void HAL_I2S_DeInit()
{
	I2S_Private *i2sPrivate = &gI2sPrivate;
	HAL_MutexLock(&i2sPrivate->devSetLock, OS_WAIT_FOREVER);
	i2sPrivate->isHwInit = FALSE;
	I2S_HwDeInit(i2sPrivate->hwParam);
	HAL_MutexUnlock(&i2sPrivate->devSetLock);

	HAL_MutexDeinit(&i2sPrivate->devSetLock);
	HAL_MutexDeinit(&i2sPrivate->devTriggerLock);
	I2S_PINS_Deinit();

	HAL_CCM_DAUDIO_DisableMClock();
	HAL_CCM_BusDisablePeriphClock(CCM_BUS_PERIPH_BIT_DAUDIO);
	HAL_PRCM_DisableAudioPLL();
	HAL_PRCM_DisableAudioPLLPattern();

	I2S_MEMSET(i2sPrivate,0,sizeof(struct I2S_Private *));
}

