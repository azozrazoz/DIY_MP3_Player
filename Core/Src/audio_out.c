/**
  ******************************************************************************
  * @file    audio_out.c
  * @brief   Audio output: I2S2 (master TX) + DMA1 Stream1 -> PCM5102A
  *
  * Configured in code, NOT in CubeMX (.ioc), so "Generate Code" does not
  * touch it. Needs HAL_I2S_MODULE_ENABLED in stm32h7xx_hal_conf.h.
  *
  * Clock: SPI123 kernel clock = PLL3P (fractional), exact for both families:
  *   44.1 kHz family: 64 MHz HSI / 8 * 67.7375 / 12 = 45.1584 MHz
  *   48   kHz family: 64 MHz HSI / 8 * 73.7280 / 12 = 49.1520 MHz
  *   Fs = PLL3P / (64 * (2*I2SDIV + ODD))  ->  e.g. 45.1584 MHz / 1024 = 44100
  ******************************************************************************
  */
#include "audio_out.h"
#include <string.h>

#ifndef HAL_I2S_MODULE_ENABLED
#error "Enable HAL_I2S_MODULE_ENABLED in Core/Inc/stm32h7xx_hal_conf.h (CubeMX 'Generate Code' switches it off)"
#endif

I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_spi2_tx;

/* DMA ring: 2 halves. 32-byte aligned for D-cache maintenance.
 * Lives in AXI SRAM (RAM_D1), reachable by DMA1. */
static int16_t audio_buf[2 * AUDIO_HALF_SAMPLES] __attribute__((aligned(32)));

static volatile uint8_t  half_free[2];
static volatile uint32_t underruns;
static uint32_t cur_rate = 0;
static uint8_t  running = 0;

/* ---------------------------------------------------------------- clocks -- */
static HAL_StatusTypeDef Audio_ClockConfig(uint32_t rate)
{
  RCC_PeriphCLKInitTypeDef clk = {0};

  clk.PeriphClockSelection = RCC_PERIPHCLK_SPI123;
  clk.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL3;
  clk.PLL3.PLL3M = 8;                         /* 64 MHz / 8 = 8 MHz ref */
  if ((rate % 11025U) == 0U)
  {
    clk.PLL3.PLL3N = 67;                      /* VCO 541.9 MHz          */
    clk.PLL3.PLL3FRACN = 6042;
  }
  else
  {
    clk.PLL3.PLL3N = 73;                      /* VCO 589.8 MHz          */
    clk.PLL3.PLL3FRACN = 5964;
  }
  clk.PLL3.PLL3P = 12;                        /* 45.1584 / 49.152 MHz   */
  clk.PLL3.PLL3Q = 2;
  clk.PLL3.PLL3R = 2;
  clk.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_3;      /* 8..16 MHz input        */
  clk.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;

  return HAL_RCCEx_PeriphCLKConfig(&clk);
}

/* -------------------------------------------------------------- MSP (HW) -- */
void HAL_I2S_MspInit(I2S_HandleTypeDef *hi2s)
{
  GPIO_InitTypeDef gpio = {0};

  if (hi2s->Instance != SPI2)
  {
    return;
  }

  __HAL_RCC_SPI2_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* PB12 I2S2_WS, PB13 I2S2_CK, PB15 I2S2_SDO */
  gpio.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(GPIOB, &gpio);

  hdma_spi2_tx.Instance = DMA1_Stream1;
  hdma_spi2_tx.Init.Request = DMA_REQUEST_SPI2_TX;
  hdma_spi2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_spi2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_spi2_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_spi2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_spi2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma_spi2_tx.Init.Mode = DMA_CIRCULAR;
  hdma_spi2_tx.Init.Priority = DMA_PRIORITY_HIGH;
  hdma_spi2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_spi2_tx) != HAL_OK)
  {
    Error_Handler();
  }
  __HAL_LINKDMA(hi2s, hdmatx, hdma_spi2_tx);

  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
}

void HAL_I2S_MspDeInit(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance != SPI2)
  {
    return;
  }
  HAL_NVIC_DisableIRQ(DMA1_Stream1_IRQn);
  (void)HAL_DMA_DeInit(hi2s->hdmatx);
  HAL_GPIO_DeInit(GPIOB, GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15);
  __HAL_RCC_SPI2_CLK_DISABLE();
}

void DMA1_Stream1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi2_tx);
}

/* DMA finished the 1st half -> it is free; now playing the 2nd half */
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance == SPI2)
  {
    if (half_free[0]) { underruns++; }
    half_free[0] = 1;
  }
}

/* DMA finished the 2nd half -> it is free; now playing the 1st half */
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance == SPI2)
  {
    if (half_free[1]) { underruns++; }
    half_free[1] = 1;
  }
}

/* ------------------------------------------------------------------- API -- */
void Audio_Stop(void)
{
  if (hi2s2.State != HAL_I2S_STATE_RESET)
  {
    (void)HAL_I2S_DMAStop(&hi2s2);
    (void)HAL_I2S_DeInit(&hi2s2);
  }
  running = 0;
  cur_rate = 0;
}

HAL_StatusTypeDef Audio_Configure(uint32_t sample_rate)
{
  Audio_Stop();

  if (Audio_ClockConfig(sample_rate) != HAL_OK)
  {
    return HAL_ERROR;
  }

  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B_EXTENDED;   /* 16 bit in 32 bit slot */
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = sample_rate;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.FirstBit = I2S_FIRSTBIT_MSB;
  hi2s2.Init.WSInversion = I2S_WS_INVERSION_DISABLE;
  hi2s2.Init.Data24BitAlignment = I2S_DATA_24BIT_ALIGNMENT_RIGHT;
  hi2s2.Init.MasterKeepIOState = I2S_MASTER_KEEP_IO_STATE_ENABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK)
  {
    return HAL_ERROR;
  }

  memset(audio_buf, 0, sizeof(audio_buf));
  SCB_CleanDCache_by_Addr((uint32_t *)audio_buf, sizeof(audio_buf));
  half_free[0] = 1;
  half_free[1] = 1;
  underruns = 0;
  cur_rate = sample_rate;
  return HAL_OK;
}

HAL_StatusTypeDef Audio_Run(void)
{
  if ((cur_rate == 0U) || running)
  {
    return HAL_ERROR;
  }
  half_free[0] = 0;
  half_free[1] = 0;
  if (HAL_I2S_Transmit_DMA(&hi2s2, (uint16_t *)audio_buf, 2U * AUDIO_HALF_SAMPLES) != HAL_OK)
  {
    return HAL_ERROR;
  }
  running = 1;
  return HAL_OK;
}

uint8_t Audio_IsRunning(void)
{
  return running;
}

uint32_t Audio_GetRate(void)
{
  return cur_rate;
}

int16_t *Audio_GetFreeHalf(void)
{
  if (half_free[0])
  {
    return &audio_buf[0];
  }
  if (half_free[1])
  {
    return &audio_buf[AUDIO_HALF_SAMPLES];
  }
  return NULL;
}

void Audio_HalfFilled(int16_t *half)
{
  /* push the new samples from D-cache to RAM, DMA reads RAM */
  SCB_CleanDCache_by_Addr((uint32_t *)half, AUDIO_HALF_SAMPLES * sizeof(int16_t));

  if (half == &audio_buf[0])
  {
    half_free[0] = 0;
  }
  else
  {
    half_free[1] = 0;
  }
}

uint32_t Audio_GetUnderruns(void)
{
  return underruns;
}
