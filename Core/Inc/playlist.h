/**
  ******************************************************************************
  * @file    playlist.h
  * @brief   List of .mp3 files on the SD card (root + subfolders), sorted
  ******************************************************************************
  */
#ifndef __PLAYLIST_H__
#define __PLAYLIST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PLAYLIST_MAX_TRACKS   256U   /* more files are ignored              */
#define PLAYLIST_MAX_PATH     256U   /* full path incl. folders, UTF-8      */
#define PLAYLIST_MAX_DEPTH    4U     /* root = 0, so 4 levels of subfolders */

/* Scan the mounted card. Returns number of tracks found. */
uint32_t Playlist_Scan(void);

uint32_t    Playlist_Count(void);
const char *Playlist_Path(uint32_t index);   /* "/Music/song.mp3" */

/* File name without folder and ".mp3", converted to plain ASCII for the
 * LCD font: Cyrillic is transliterated (Кино -> Kino), other symbols -> '?' */
void Playlist_DisplayName(uint32_t index, char *out, uint32_t out_size);

/* The same conversion for any UTF-8 string */
void Text_Utf8ToAscii(const char *in, char *out, uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* __PLAYLIST_H__ */
