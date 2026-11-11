/**
  ******************************************************************************
  * @file    audio_out.h
  * @brief   Audio output: I2S2 (master TX) + DMA -> PCM5102A
  *
  *   PCM5102A    STM32H743 (WeAct, header P1)
  *   BCK   <---  PB13  I2S2_CK
  *   LCK   <---  PB12  I2S2_WS
  *   DIN   <---  PB15  I2S2_SDO
  *   SCK   ---   GND   (PCM5102A makes its own clock from BCK)
  *   VIN/GND     3V3 (or 5V for the purple module) / GND
  *
  * Format: I2S Philips, 16-bit samples in 32-bit slots (BCK = 64*Fs),
  * stereo interleaved int16 (L, R, L, R ...).
  ******************************************************************************
  */
#ifndef __AUDIO_OUT_H__
#define __AUDIO_OUT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Stereo frames in one half of the DMA ring (2304 = 2 MP3 frames, ~52 ms @44.1k) */
#define AUDIO_HALF_FRAMES   2304U
#define AUDIO_HALF_SAMPLES  (AUDIO_HALF_FRAMES * 2U)   /* int16 values (L+R) */

/* Stop output (if running), set clocks + I2S for 'sample_rate' and mark both
 * halves as free. Fill them with Audio_GetFreeHalf()/Audio_HalfFilled(),
 * then call Audio_Run(). Supported: 8000 ... 48000 Hz (all MP3 rates). */
HAL_StatusTypeDef Audio_Configure(uint32_t sample_rate);

/* Start DMA streaming (after both halves were filled) */
HAL_StatusTypeDef Audio_Run(void);

/* Stop DMA and I2S */
void Audio_Stop(void);

uint8_t  Audio_IsRunning(void);
uint32_t Audio_GetRate(void);

/* Half of the ring that can be written now, or NULL.
 * Write AUDIO_HALF_SAMPLES int16 values, then call Audio_HalfFilled(). */
int16_t *Audio_GetFreeHalf(void);
void     Audio_HalfFilled(int16_t *half);

/* Number of times the DMA had to replay old data (main loop was too slow) */
uint32_t Audio_GetUnderruns(void);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_OUT_H__ */
