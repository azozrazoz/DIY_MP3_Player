/**
  ******************************************************************************
  * @file    mp3_player.h
  * @brief   MP3 player: SD card -> minimp3 -> I2S2 -> PCM5102A, UI on ST7735
  *
  * KEY (PC13): short press = next track, long press (0.8 s) = pause / play
  ******************************************************************************
  */
#ifndef __MP3_PLAYER_H__
#define __MP3_PLAYER_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after MX_..._Init() */
void Player_Init(void);

/* Call all the time from while(1). Never blocks for long. */
void Player_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* __MP3_PLAYER_H__ */
