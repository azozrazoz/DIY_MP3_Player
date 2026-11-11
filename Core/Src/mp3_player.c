/**
  ******************************************************************************
  * @file    mp3_player.c
  * @brief   MP3 player: SD card -> minimp3 -> I2S2 -> PCM5102A, UI on ST7735
  *
  * Screen 160x80:
  *   +----------------------+
  *   | >  3/12          MP3 |  header: state, track number
  *   | Kino - Gruppa krovi  |  file name (2 lines, Cyrillic -> Latin)
  *   |                      |
  *   | 01:23  44.1kHz 128k  |  time, format
  *   | =======-------------  |  progress
  *   +----------------------+
  ******************************************************************************
  */
#include "mp3_player.h"
#include "main.h"
#include "lcd.h"
#include "audio_out.h"
#include "sd_card.h"
#include "playlist.h"
#include "mp3_stream.h"
#include "FatFs/ff.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ settings --- */
#define KEY_DEBOUNCE_MS     30U
#define KEY_LONG_MS         800U
#define CARD_POLL_MS        500U     /* look for a card when there is none   */
#define CARD_CHECK_MS       1000U    /* check the card when paused / idle    */

#define COL_BG              BLACK
#define COL_HEADER_BG       DARKBLUE
#define COL_HEADER_FG       WHITE
#define COL_NAME            WHITE
#define COL_TIME            GREEN
#define COL_INFO            GRAY
#define COL_BAR             GREEN
#define COL_BAR_BG          0x2104   /* dark gray */

#define LCD_W               ((uint16_t)ST7735Ctx.Width)
#define LCD_H               ((uint16_t)ST7735Ctx.Height)
#define NAME_CHARS          20U      /* 160 px / 8 px (font 16) */

typedef enum
{
  ST_NO_CARD = 0,     /* waiting for a card            */
  ST_NO_FILES,        /* card ok, but nothing to play  */
  ST_PLAYING,
  ST_PAUSED
} PlayerState_t;

typedef enum { KEY_NONE = 0, KEY_SHORT, KEY_LONG } KeyEvent_t;

static FATFS         fs;
static PlayerState_t state = ST_NO_CARD;
static uint32_t      track_idx;
static uint32_t      next_check;
static uint32_t      shown_seconds;
static uint32_t      shown_bar;
static int           shown_msg = -1;

extern unsigned char WeActStudiologo_160_80[];

/* ================================================================== UI === */
static void ui_text(uint16_t x, uint16_t y, uint8_t size, uint16_t fg, uint16_t bg, const char *s)
{
  uint16_t old_fg = POINT_COLOR, old_bg = BACK_COLOR;

  POINT_COLOR = fg;
  BACK_COLOR = bg;
  LCD_ShowString(x, y, (uint16_t)(LCD_W - x), size, size, (uint8_t *)s);
  POINT_COLOR = old_fg;
  BACK_COLOR = old_bg;
}

static void ui_text_center(uint16_t y, uint8_t size, uint16_t fg, uint16_t bg, const char *s)
{
  uint32_t w = (uint32_t)strlen(s) * (size / 2U);
  uint16_t x = (w < LCD_W) ? (uint16_t)((LCD_W - w) / 2U) : 0U;

  ui_text(x, y, size, fg, bg, s);
}

static void ui_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if ((w > 0U) && (h > 0U))
  {
    ST7735_LCD_Driver.FillRect(&st7735_pObj, x, y, w, h, color);
  }
}

/* full-screen message, drawn only when it changes */
static void ui_message(int id, const char *l1, const char *l2, uint16_t bg)
{
  if (shown_msg == id)
  {
    return;
  }
  shown_msg = id;
  ui_fill(0, 0, LCD_W, LCD_H, bg);
  ui_text_center(18, 16, WHITE, bg, l1);
  if (l2 != NULL)
  {
    ui_text_center(46, 16, WHITE, bg, l2);
  }
}

static void ui_header(void)
{
  char s[24];

  snprintf(s, sizeof(s), "%s %lu/%lu", (state == ST_PAUSED) ? "||" : " >",
           (unsigned long)(track_idx + 1U), (unsigned long)Playlist_Count());
  ui_fill(0, 0, LCD_W, 18, COL_HEADER_BG);
  ui_text(2, 1, 16, COL_HEADER_FG, COL_HEADER_BG, s);
  ui_text((uint16_t)(LCD_W - 3U * 8U - 2U), 1, 16,
          (state == ST_PAUSED) ? YELLOW : GBLUE, COL_HEADER_BG, "MP3");
}

static void ui_time(void)
{
  char s[8];
  uint32_t sec = Mp3Stream_PlayedSeconds();
  uint32_t bar = Mp3Stream_Progress(LCD_W);

  if (sec != shown_seconds)
  {
    shown_seconds = sec;
    if (sec > 5999U) { sec = 5999U; }
    snprintf(s, sizeof(s), "%02lu:%02lu", (unsigned long)(sec / 60U), (unsigned long)(sec % 60U));
    ui_text(2, 58, 16, COL_TIME, COL_BG, s);
  }
  if (bar != shown_bar)
  {
    if (bar > shown_bar)
    {
      ui_fill(shown_bar, 76, (uint16_t)(bar - shown_bar), 4, COL_BAR);
    }
    else
    {
      ui_fill(0, 76, LCD_W, 4, COL_BAR_BG);
      ui_fill(0, 76, bar, 4, COL_BAR);
    }
    shown_bar = bar;
  }
}

static void ui_track(void)
{
  char name[96];
  char line[NAME_CHARS + 1U];
  char s[24];
  const Mp3Info_t *mi = Mp3Stream_Info();
  size_t len;

  shown_msg = -1;
  ui_fill(0, 0, LCD_W, LCD_H, COL_BG);
  ui_header();

  /* name: 2 lines of 20 chars, "~" if cut */
  Playlist_DisplayName(track_idx, name, sizeof(name));
  len = strlen(name);
  if (len > 2U * NAME_CHARS)
  {
    name[2U * NAME_CHARS - 1U] = '~';
    name[2U * NAME_CHARS] = 0;
    len = 2U * NAME_CHARS;
  }
  memcpy(line, name, (len < NAME_CHARS) ? len : NAME_CHARS);
  line[(len < NAME_CHARS) ? len : NAME_CHARS] = 0;
  ui_text(0, 20, 16, COL_NAME, COL_BG, line);
  if (len > NAME_CHARS)
  {
    ui_text(0, 38, 16, COL_NAME, COL_BG, &name[NAME_CHARS]);
  }

  /* format: "44.1kHz 128k" (+ " M" for mono) */
  snprintf(s, sizeof(s), "%lu.%lukHz %luk%s",
           (unsigned long)(mi->sample_rate / 1000U),
           (unsigned long)((mi->sample_rate % 1000U) / 100U),
           (unsigned long)mi->bitrate_kbps,
           (mi->channels == 1U) ? " M" : "");
  ui_text(52, 62, 12, COL_INFO, COL_BG, s);

  ui_fill(0, 76, LCD_W, 4, COL_BAR_BG);
  shown_bar = 0;
  shown_seconds = 0xFFFFFFFFU;
  ui_time();
}

static void led(uint8_t on)
{
  HAL_GPIO_WritePin(PE3_GPIO_Port, PE3_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ================================================================= KEY === */
static KeyEvent_t key_poll(void)
{
  static uint8_t  raw_last, stable, long_sent;
  static uint32_t t_change, t_press;
  uint32_t now = HAL_GetTick();
  uint8_t raw = (HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_SET);   /* pressed = 1 */

  if (raw != raw_last)
  {
    raw_last = raw;
    t_change = now;
  }
  if (((now - t_change) >= KEY_DEBOUNCE_MS) && (raw != stable))
  {
    stable = raw;
    if (stable)
    {
      t_press = now;
      long_sent = 0;
    }
    else if (!long_sent)
    {
      return KEY_SHORT;
    }
  }
  if (stable && !long_sent && ((now - t_press) >= KEY_LONG_MS))
  {
    long_sent = 1;
    return KEY_LONG;
  }
  return KEY_NONE;
}

/* ============================================================ playback === */
static void card_lost(void)
{
  Audio_Stop();
  Mp3Stream_Close();
  (void)f_mount(NULL, "", 0);
  SD_Card_DeInit();
  led(0);
  state = ST_NO_CARD;
  next_check = HAL_GetTick() + CARD_POLL_MS;
  ui_message(1, "NO SD CARD", "INSERT CARD", RED);
}

static void no_files(const char *l1)
{
  Audio_Stop();
  Mp3Stream_Close();
  led(0);
  state = ST_NO_FILES;
  next_check = HAL_GetTick() + CARD_CHECK_MS;
  ui_message(2, l1, "KEY = RESCAN", DARKBLUE);
}

/* fill every free half of the DMA ring: music or silence (pause) */
static void feed_audio(void)
{
  int16_t *half;

  while ((half = Audio_GetFreeHalf()) != NULL)
  {
    uint32_t n = 0;

    if (state == ST_PLAYING)
    {
      n = Mp3Stream_Read(half, AUDIO_HALF_SAMPLES);
    }
    if (n < AUDIO_HALF_SAMPLES)
    {
      memset(&half[n], 0, (AUDIO_HALF_SAMPLES - n) * sizeof(int16_t));
    }
    Audio_HalfFilled(half);

    if ((state == ST_PLAYING) && (Mp3Stream_IsEnd() || Mp3Stream_IsError()))
    {
      break;   /* let Player_Process switch the track first */
    }
  }
}

/* open track 'idx' (skipping broken files) and start / continue the output */
static void start_track(uint32_t idx)
{
  uint32_t tries;
  uint32_t count = Playlist_Count();

  for (tries = 0; tries < count; tries++)
  {
    int r;

    track_idx = (idx + tries) % count;
    r = Mp3Stream_Open(Playlist_Path(track_idx));

    if (r == MP3S_IO_ERROR)
    {
      if (!SD_Card_IsPresent())
      {
        card_lost();
        return;
      }
      continue;
    }
    if (r == MP3S_NO_AUDIO)
    {
      continue;
    }

    /* same sample rate -> keep streaming (no click), else restart I2S */
    if (!Audio_IsRunning() || (Audio_GetRate() != Mp3Stream_Info()->sample_rate))
    {
      if (Audio_Configure(Mp3Stream_Info()->sample_rate) != HAL_OK)
      {
        continue;
      }
      if (state != ST_PAUSED)
      {
        state = ST_PLAYING;
      }
      feed_audio();             /* pre-fill both halves */
      (void)Audio_Run();
    }
    if (state != ST_PAUSED)
    {
      state = ST_PLAYING;
    }
    led(state == ST_PLAYING);
    ui_track();
    return;
  }

  no_files("NO PLAYABLE MP3");
}

static void try_mount(void)
{
  FRESULT fr = f_mount(&fs, "", 1);   /* calls disk_initialize -> SD_Card_Init */

  if (fr != FR_OK)
  {
    (void)f_mount(NULL, "", 0);
    SD_Card_DeInit();
    if (fr == FR_NO_FILESYSTEM)
    {
      ui_message(3, "CARD NOT FAT32", "OR exFAT", RED);
    }
    else
    {
      ui_message(1, "NO SD CARD", "INSERT CARD", RED);
    }
    return;
  }

  ui_message(4, "READING CARD...", NULL, DARKBLUE);
  if (Playlist_Scan() == 0U)
  {
    no_files("NO MP3 FILES");
    return;
  }
  state = ST_PLAYING;
  start_track(0);
}

/* ================================================================= API === */
void Player_Init(void)
{
  LCD_Init();
  ST7735_LCD_Driver.DrawBitmap(&st7735_pObj, 0, 0, WeActStudiologo_160_80);
  LCD_Light(800, 300);
  HAL_Delay(700);

  led(0);
  state = ST_NO_CARD;
  next_check = HAL_GetTick();
  shown_msg = -1;
}

void Player_Process(void)
{
  KeyEvent_t key = key_poll();
  uint32_t now = HAL_GetTick();

  switch (state)
  {
    case ST_NO_CARD:
      if ((int32_t)(now - next_check) >= 0)
      {
        next_check = now + CARD_POLL_MS;
        try_mount();
      }
      break;

    case ST_NO_FILES:
      if (key != KEY_NONE)
      {
        shown_msg = -1;
        ui_message(4, "READING CARD...", NULL, DARKBLUE);
        if (Playlist_Scan() > 0U)
        {
          state = ST_PLAYING;
          start_track(0);
        }
        else
        {
          shown_msg = -1;
          no_files("NO MP3 FILES");
        }
      }
      else if ((int32_t)(now - next_check) >= 0)
      {
        next_check = now + CARD_CHECK_MS;
        if (!SD_Card_IsPresent())
        {
          card_lost();
        }
      }
      break;

    case ST_PLAYING:
    case ST_PAUSED:
      feed_audio();

      if (key == KEY_SHORT)
      {
        start_track(track_idx + 1U);
        break;
      }
      if (key == KEY_LONG)
      {
        state = (state == ST_PLAYING) ? ST_PAUSED : ST_PLAYING;
        led(state == ST_PLAYING);
        ui_header();
        break;
      }

      if (state == ST_PLAYING)
      {
        if (Mp3Stream_IsError())
        {
          if (!SD_Card_IsPresent())
          {
            card_lost();
          }
          else
          {
            start_track(track_idx + 1U);   /* damaged file: skip */
          }
          break;
        }
        if (Mp3Stream_IsEnd())
        {
          start_track(track_idx + 1U);     /* next, wraps to the first */
          break;
        }
        ui_time();
      }
      else if ((int32_t)(now - next_check) >= 0)
      {
        next_check = now + CARD_CHECK_MS;  /* paused: notice card removal */
        if (!SD_Card_IsPresent())
        {
          card_lost();
        }
      }
      break;

    default:
      state = ST_NO_CARD;
      break;
  }
}
