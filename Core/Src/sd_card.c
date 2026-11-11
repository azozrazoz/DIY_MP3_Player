/**
  ******************************************************************************
  * @file    sd_card.c
  * @brief   microSD card on SDMMC1 (WeAct MiniSTM32H7xx): init, presence, read
  *
  * Polling mode (no DMA / IRQ): simple and cache-safe.
  * Clock: SDMMC kernel 150 MHz (PLL2R, see sdmmc.c) / (2*5) = 15 MHz, 4-bit bus
  ******************************************************************************
  */
#include "sd_card.h"
#include "sdmmc.h"

#define SD_RW_TIMEOUT_MS     500U
#define SD_READ_RETRIES      2U

static uint8_t sd_ready = 0;

HAL_StatusTypeDef SD_Card_Init(void)
{
  if (sd_ready)
  {
    return HAL_OK;
  }

  /* same settings as MX_SDMMC1_SD_Init(), but without Error_Handler() */
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd1.Init.ClockDiv = 5;

  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
    (void)HAL_SD_DeInit(&hsd1);   /* back to RESET, next try starts clean */
    return HAL_ERROR;
  }

  sd_ready = 1;
  return HAL_OK;
}

void SD_Card_DeInit(void)
{
  if (hsd1.State != HAL_SD_STATE_RESET)
  {
    (void)HAL_SD_DeInit(&hsd1);
  }
  sd_ready = 0;
}

uint8_t SD_Card_IsReady(void)
{
  return sd_ready;
}

uint8_t SD_Card_IsPresent(void)
{
  HAL_SD_CardStateTypeDef state;

  if (!sd_ready)
  {
    return 0;
  }

  hsd1.ErrorCode = HAL_SD_ERROR_NONE;
  state = HAL_SD_GetCardState(&hsd1);
  if (hsd1.ErrorCode != HAL_SD_ERROR_NONE)
  {
    return 0;
  }
  return (state == HAL_SD_CARD_TRANSFER)  ||
         (state == HAL_SD_CARD_SENDING)   ||
         (state == HAL_SD_CARD_RECEIVING) ||
         (state == HAL_SD_CARD_PROGRAMMING);
}

/* wait until the card is back in TRANSFER state after a command;
 * fails at once if the card does not answer (removed) */
static HAL_StatusTypeDef SD_WaitReady(void)
{
  uint32_t start = HAL_GetTick();

  for (;;)
  {
    HAL_SD_CardStateTypeDef state;

    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    state = HAL_SD_GetCardState(&hsd1);
    if (hsd1.ErrorCode != HAL_SD_ERROR_NONE)
    {
      return HAL_ERROR;
    }
    if (state == HAL_SD_CARD_TRANSFER)
    {
      return HAL_OK;
    }
    if ((HAL_GetTick() - start) >= SD_RW_TIMEOUT_MS)
    {
      return HAL_TIMEOUT;
    }
  }
}

HAL_StatusTypeDef SD_Card_Read(uint8_t *buf, uint32_t sector, uint32_t count)
{
  uint32_t attempt;

  if (!sd_ready)
  {
    return HAL_ERROR;
  }

  for (attempt = 0; attempt < SD_READ_RETRIES; attempt++)
  {
    if ((HAL_SD_ReadBlocks(&hsd1, buf, sector, count, SD_RW_TIMEOUT_MS) == HAL_OK) &&
        (SD_WaitReady() == HAL_OK))
    {
      return HAL_OK;
    }
    /* clear the error; try once more only if the card is still there */
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    hsd1.State = HAL_SD_STATE_READY;
    if (!SD_Card_IsPresent())
    {
      break;
    }
  }
  return HAL_ERROR;
}

uint32_t SD_Card_SectorCount(void)
{
  HAL_SD_CardInfoTypeDef info;

  if (!sd_ready || (HAL_SD_GetCardInfo(&hsd1, &info) != HAL_OK))
  {
    return 0;
  }
  /* LogBlockSize is 512 for all SDSC/SDHC/SDXC cards in HAL */
  return info.LogBlockNbr;
}
