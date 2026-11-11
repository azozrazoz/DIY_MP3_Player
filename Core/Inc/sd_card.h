/**
  ******************************************************************************
  * @file    sd_card.h
  * @brief   microSD card on SDMMC1 (WeAct MiniSTM32H7xx): init, presence, read
  ******************************************************************************
  */
#ifndef __SD_CARD_H__
#define __SD_CARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define SD_CARD_SECTOR_SIZE   512U

/* Init SDMMC1 + card. HAL_OK = card is present and ready.
 * No Error_Handler(): a missing card is a normal situation. */
HAL_StatusTypeDef SD_Card_Init(void);

/* Release SDMMC1 (call after the card was removed) */
void SD_Card_DeInit(void);

/* 1 = SD_Card_Init() succeeded and the card was not released since */
uint8_t SD_Card_IsReady(void);

/* 1 = card answers CMD13 right now (only meaningful when ready) */
uint8_t SD_Card_IsPresent(void);

/* Read 'count' sectors of 512 bytes. Any buffer alignment is OK. */
HAL_StatusTypeDef SD_Card_Read(uint8_t *buf, uint32_t sector, uint32_t count);

/* Card capacity in 512-byte sectors */
uint32_t SD_Card_SectorCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __SD_CARD_H__ */
