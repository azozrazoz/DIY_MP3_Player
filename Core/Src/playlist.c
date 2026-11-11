/**
  ******************************************************************************
  * @file    playlist.c
  * @brief   List of .mp3 files on the SD card (root + subfolders), sorted
  ******************************************************************************
  */
#include "playlist.h"
#include "FatFs/ff.h"
#include <string.h>
#include <stdlib.h>

static char     tracks[PLAYLIST_MAX_TRACKS][PLAYLIST_MAX_PATH];
static uint32_t track_count;

/* ------------------------------------------------------------ helpers ---- */
static char to_lower(char c)
{
  return ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : c;
}

static int str_icmp(const char *a, const char *b)
{
  while (*a && (to_lower(*a) == to_lower(*b)))
  {
    a++;
    b++;
  }
  return (int)(unsigned char)to_lower(*a) - (int)(unsigned char)to_lower(*b);
}

static int is_mp3(const char *name)
{
  size_t n = strlen(name);
  return (n > 4U) && (str_icmp(&name[n - 4U], ".mp3") == 0);
}

static int cmp_tracks(const void *a, const void *b)
{
  return str_icmp((const char *)a, (const char *)b);
}

/* path: buffer of PLAYLIST_MAX_PATH bytes, "" = root; modified and restored */
static void scan_dir(char *path, uint32_t depth)
{
  DIR dir;
  FILINFO fno;
  size_t len = strlen(path);

  if (f_opendir(&dir, (len == 0U) ? "/" : path) != FR_OK)
  {
    return;
  }

  for (;;)
  {
    if ((f_readdir(&dir, &fno) != FR_OK) || (fno.fname[0] == 0))
    {
      break;
    }
    /* skip ".", "..", hidden/system entries and macOS junk ("._x.mp3", ".Trashes") */
    if ((fno.fname[0] == '.') || (fno.fattrib & (AM_HID | AM_SYS)))
    {
      continue;
    }
    if ((len + 1U + strlen(fno.fname) + 1U) > PLAYLIST_MAX_PATH)
    {
      continue;   /* path too long */
    }

    if (fno.fattrib & AM_DIR)
    {
      if (depth < PLAYLIST_MAX_DEPTH)
      {
        path[len] = '/';
        strcpy(&path[len + 1U], fno.fname);
        scan_dir(path, depth + 1U);
        path[len] = 0;
      }
    }
    else if (is_mp3(fno.fname) && (track_count < PLAYLIST_MAX_TRACKS))
    {
      memcpy(tracks[track_count], path, len);
      tracks[track_count][len] = '/';
      strcpy(&tracks[track_count][len + 1U], fno.fname);
      track_count++;
    }
  }
  (void)f_closedir(&dir);
}

/* ---------------------------------------------------------------- API ---- */
uint32_t Playlist_Scan(void)
{
  char path[PLAYLIST_MAX_PATH];

  track_count = 0;
  path[0] = 0;
  scan_dir(path, 0);
  qsort(tracks, track_count, PLAYLIST_MAX_PATH, cmp_tracks);
  return track_count;
}

uint32_t Playlist_Count(void)
{
  return track_count;
}

const char *Playlist_Path(uint32_t index)
{
  return (index < track_count) ? tracks[index] : "";
}

void Playlist_DisplayName(uint32_t index, char *out, uint32_t out_size)
{
  const char *path = Playlist_Path(index);
  const char *name = strrchr(path, '/');
  size_t n;

  Text_Utf8ToAscii((name != NULL) ? (name + 1) : path, out, out_size);

  n = strlen(out);
  if ((n > 4U) && (str_icmp(&out[n - 4U], ".mp3") == 0))
  {
    out[n - 4U] = 0;
  }
}

/* --------------------------------------------- UTF-8 -> ASCII (translit) -- */
/* U+0410..U+042F (А..Я); lower case = same table in lower case */
static const char *const cyr[32] =
{
  "A", "B", "V", "G", "D", "E", "Zh", "Z", "I", "Y", "K", "L", "M", "N", "O", "P",
  "R", "S", "T", "U", "F", "Kh", "Ts", "Ch", "Sh", "Sch", "", "Y", "", "E", "Yu", "Ya"
};

static const char *translit(uint32_t cp, uint8_t *lower)
{
  *lower = 0;
  if ((cp >= 0x0410U) && (cp <= 0x042FU)) { return cyr[cp - 0x0410U]; }
  if ((cp >= 0x0430U) && (cp <= 0x044FU)) { *lower = 1; return cyr[cp - 0x0430U]; }
  switch (cp)
  {
    case 0x0401U: return "Yo";                    /* Ё */
    case 0x0451U: *lower = 1; return "Yo";        /* ё */
    case 0x0404U: return "Ye";                    /* Є */
    case 0x0454U: *lower = 1; return "Ye";        /* є */
    case 0x0406U: return "I";                     /* І */
    case 0x0456U: *lower = 1; return "I";         /* і */
    case 0x0407U: return "Yi";                    /* Ї */
    case 0x0457U: *lower = 1; return "Yi";        /* ї */
    case 0x040EU: return "U";                     /* Ў */
    case 0x045EU: *lower = 1; return "U";         /* ў */
    case 0x0490U: return "G";                     /* Ґ */
    case 0x0491U: *lower = 1; return "G";         /* ґ */
    case 0x2013U: case 0x2014U: return "-";       /* – — */
    case 0x2018U: case 0x2019U: return "'";       /* ‘ ’ */
    case 0x201CU: case 0x201DU: case 0x00ABU: case 0x00BBU: return "\"";
    case 0x2116U: return "N";                     /* № */
    default: return "?";
  }
}

void Text_Utf8ToAscii(const char *in, char *out, uint32_t out_size)
{
  const unsigned char *s = (const unsigned char *)in;
  uint32_t o = 0;

  if (out_size == 0U)
  {
    return;
  }

  while ((*s != 0U) && ((o + 1U) < out_size))
  {
    uint32_t cp;
    uint32_t extra;

    if (*s < 0x80U)                { cp = *s;          extra = 0; }
    else if ((*s & 0xE0U) == 0xC0U) { cp = *s & 0x1FU; extra = 1; }
    else if ((*s & 0xF0U) == 0xE0U) { cp = *s & 0x0FU; extra = 2; }
    else if ((*s & 0xF8U) == 0xF0U) { cp = *s & 0x07U; extra = 3; }
    else                           { cp = '?';         extra = 0; }
    s++;
    while ((extra > 0U) && ((*s & 0xC0U) == 0x80U))
    {
      cp = (cp << 6) | (*s & 0x3FU);
      s++;
      extra--;
    }
    if (extra > 0U)
    {
      cp = '?';   /* broken sequence */
    }

    if ((cp >= 0x20U) && (cp <= 0x7EU))
    {
      out[o++] = (char)cp;
    }
    else if (cp < 0x80U)
    {
      out[o++] = ' ';   /* control chars */
    }
    else
    {
      uint8_t lower;
      const char *t = translit(cp, &lower);
      while ((*t != 0) && ((o + 1U) < out_size))
      {
        /* "Zh" -> "zh" for lower case; "ZH" style is not used */
        out[o++] = lower ? to_lower(*t) : *t;
        t++;
      }
    }
  }
  out[o] = 0;
}
