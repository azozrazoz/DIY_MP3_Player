/**
  ******************************************************************************
  * @file    mp3_stream.c
  * @brief   MP3 file -> 16-bit stereo PCM (FatFs + minimp3)
  *
  * Input is read from the card in big chunks into in_buf; minimp3 decodes one
  * frame (1152 samples) at a time into pcm[]; Mp3Stream_Read() copies it out.
  ******************************************************************************
  */
#include "mp3_stream.h"
#include "FatFs/ff.h"
#include "minimp3/minimp3.h"
#include <string.h>

#define IN_BUF_SIZE       (32U * 1024U)   /* input buffer                    */
#define IN_REFILL_LEVEL   (16U * 1024U)   /* refill when less data than this */

static FIL      file;
static uint8_t  file_open;
static FSIZE_t  file_size;
static uint8_t  file_eof;     /* nothing more to read from the card   */
static uint8_t  end_flag;     /* decoder has no more frames           */
static uint8_t  err_flag;

static mp3dec_t mp3d;
static uint8_t  in_buf[IN_BUF_SIZE] __attribute__((aligned(32)));
static uint32_t in_len, in_pos;

static int16_t  pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];   /* one frame, stereo */
static uint32_t pcm_len, pcm_pos;

static Mp3Info_t info;
static uint64_t  out_frames;  /* stereo frames given to the caller */

/* ---------------------------------------------------------------- input -- */
/* force = 1: read even if there is still enough data in the buffer */
static int input_refill(uint8_t force)
{
  uint32_t avail = in_len - in_pos;
  uint32_t want;
  UINT br = 0;

  if (file_eof || (!force && (avail >= IN_REFILL_LEVEL)) || (avail >= IN_BUF_SIZE))
  {
    return MP3S_OK;
  }

  memmove(in_buf, &in_buf[in_pos], avail);
  in_pos = 0;
  in_len = avail;

  want = IN_BUF_SIZE - in_len;
  if (f_read(&file, &in_buf[in_len], want, &br) != FR_OK)
  {
    return MP3S_IO_ERROR;
  }
  in_len += br;
  if (br < want)
  {
    file_eof = 1;
  }
  return MP3S_OK;
}

/* skip an ID3v2 tag at the start (it may contain a big cover picture) */
static int skip_id3v2(void)
{
  uint8_t h[10];
  UINT br = 0;
  FSIZE_t skip = 0;

  if (f_read(&file, h, sizeof(h), &br) != FR_OK)
  {
    return MP3S_IO_ERROR;
  }
  if ((br == sizeof(h)) && (h[0] == 'I') && (h[1] == 'D') && (h[2] == '3') &&
      (((h[6] | h[7] | h[8] | h[9]) & 0x80U) == 0U))
  {
    skip = 10U + (((FSIZE_t)h[6] << 21) | ((FSIZE_t)h[7] << 14) |
                  ((FSIZE_t)h[8] << 7)  |  (FSIZE_t)h[9]);
    if (h[5] & 0x10U)
    {
      skip += 10U;   /* footer */
    }
    if (skip > file_size)
    {
      skip = 0;      /* broken tag, let the decoder search */
    }
  }
  return (f_lseek(&file, skip) == FR_OK) ? MP3S_OK : MP3S_IO_ERROR;
}

/* ---------------------------------------------------------------- decode -- */
/* 1 = new frame in pcm[], 0 = end of file, <0 = error */
static int decode_frame(void)
{
  mp3dec_frame_info_t fi;
  int samples;

  for (;;)
  {
    uint32_t avail;

    if (input_refill(0) != MP3S_OK)
    {
      return MP3S_IO_ERROR;
    }
    avail = in_len - in_pos;
    if (avail == 0U)
    {
      return 0;
    }

    samples = mp3dec_decode_frame(&mp3d, &in_buf[in_pos], (int)avail, pcm, &fi);

    if (fi.frame_bytes == 0)
    {
      /* no complete frame in the buffer */
      if (file_eof)
      {
        return 0;
      }
      if (avail >= (IN_BUF_SIZE / 2U))
      {
        in_pos = in_len - 3U;   /* lots of junk without a frame: drop it */
      }
      if (input_refill(1) != MP3S_OK)
      {
        return MP3S_IO_ERROR;
      }
      continue;
    }

    in_pos += (uint32_t)fi.frame_bytes;
    if (samples == 0)
    {
      continue;   /* skipped junk / tag / damaged frame */
    }

    if (info.sample_rate == 0U)
    {
      info.sample_rate = (uint32_t)fi.hz;
      info.channels = (uint32_t)fi.channels;
      info.bitrate_kbps = (uint32_t)fi.bitrate_kbps;
    }

    if (fi.channels == 1)
    {
      /* mono -> stereo, in place, from the end */
      int i;
      for (i = samples - 1; i >= 0; i--)
      {
        pcm[2 * i + 1] = pcm[i];
        pcm[2 * i] = pcm[i];
      }
    }
    pcm_len = (uint32_t)samples * 2U;
    pcm_pos = 0;
    return 1;
  }
}

/* ------------------------------------------------------------------ API -- */
void Mp3Stream_Close(void)
{
  if (file_open)
  {
    (void)f_close(&file);
    file_open = 0;
  }
}

int Mp3Stream_Open(const char *path)
{
  int r;

  Mp3Stream_Close();

  memset(&info, 0, sizeof(info));
  in_len = in_pos = 0;
  pcm_len = pcm_pos = 0;
  file_eof = end_flag = err_flag = 0;
  out_frames = 0;
  mp3dec_init(&mp3d);

  if (f_open(&file, path, FA_READ) != FR_OK)
  {
    return MP3S_IO_ERROR;
  }
  file_open = 1;
  file_size = f_size(&file);

  if (skip_id3v2() != MP3S_OK)
  {
    Mp3Stream_Close();
    return MP3S_IO_ERROR;
  }

  r = decode_frame();
  if (r < 0)
  {
    Mp3Stream_Close();
    return MP3S_IO_ERROR;
  }
  if ((r == 0) || (info.sample_rate == 0U))
  {
    Mp3Stream_Close();
    return MP3S_NO_AUDIO;
  }
  return MP3S_OK;
}

uint32_t Mp3Stream_Read(int16_t *dst, uint32_t count)
{
  uint32_t n = 0;

  while (n < count)
  {
    uint32_t k;

    if (pcm_pos >= pcm_len)
    {
      int r;

      if (end_flag || err_flag || !file_open)
      {
        break;
      }
      r = decode_frame();
      if (r <= 0)
      {
        if (r < 0)
        {
          err_flag = 1;
        }
        end_flag = 1;
        break;
      }
    }

    k = pcm_len - pcm_pos;
    if (k > (count - n))
    {
      k = count - n;
    }
    memcpy(&dst[n], &pcm[pcm_pos], k * sizeof(int16_t));
    n += k;
    pcm_pos += k;
  }

  out_frames += n / 2U;
  return n;
}

uint8_t Mp3Stream_IsEnd(void)
{
  return end_flag && (pcm_pos >= pcm_len);
}

uint8_t Mp3Stream_IsError(void)
{
  return err_flag;
}

const Mp3Info_t *Mp3Stream_Info(void)
{
  return &info;
}

uint32_t Mp3Stream_PlayedSeconds(void)
{
  return (info.sample_rate != 0U) ? (uint32_t)(out_frames / info.sample_rate) : 0U;
}

uint32_t Mp3Stream_Progress(uint32_t scale)
{
  FSIZE_t pos;

  if (!file_open || (file_size == 0U))
  {
    return end_flag ? scale : 0U;
  }
  pos = f_tell(&file) - (in_len - in_pos);   /* what the decoder has consumed */
  if (pos > file_size)
  {
    pos = file_size;
  }
  return (uint32_t)(((uint64_t)pos * scale) / file_size);
}
