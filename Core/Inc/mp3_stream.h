/**
  ******************************************************************************
  * @file    mp3_stream.h
  * @brief   MP3 file -> 16-bit stereo PCM (FatFs + minimp3)
  ******************************************************************************
  */
#ifndef __MP3_STREAM_H__
#define __MP3_STREAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MP3S_OK          0
#define MP3S_IO_ERROR   (-1)   /* card read error (removed?)  */
#define MP3S_NO_AUDIO   (-2)   /* no MP3 frames in the file   */

typedef struct
{
  uint32_t sample_rate;    /* Hz, e.g. 44100              */
  uint32_t channels;       /* in the file: 1 or 2         */
  uint32_t bitrate_kbps;   /* of the first frame          */
} Mp3Info_t;

/* Open file, skip ID3v2 tag, decode the first frame.
 * Returns MP3S_OK / MP3S_IO_ERROR / MP3S_NO_AUDIO */
int Mp3Stream_Open(const char *path);
void Mp3Stream_Close(void);

/* Write up to 'count' int16 values of interleaved stereo PCM (count is even).
 * Mono files are duplicated to both channels.
 * Returns number of values written; less than 'count' = end of file or error. */
uint32_t Mp3Stream_Read(int16_t *dst, uint32_t count);

uint8_t  Mp3Stream_IsEnd(void);       /* 1 = end of file reached           */
uint8_t  Mp3Stream_IsError(void);     /* 1 = read error during playback    */
const Mp3Info_t *Mp3Stream_Info(void);
uint32_t Mp3Stream_PlayedSeconds(void);   /* by PCM samples output so far  */
uint32_t Mp3Stream_Progress(uint32_t scale); /* file position, 0..scale    */

#ifdef __cplusplus
}
#endif

#endif /* __MP3_STREAM_H__ */
