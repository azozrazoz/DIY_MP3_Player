/*-----------------------------------------------------------------------*/
/* Low level disk I/O for FatFs: microSD on SDMMC1 (read-only player)    */
/*-----------------------------------------------------------------------*/
#include "ff.h"
#include "diskio.h"
#include "sd_card.h"

#define DEV_SD   0   /* the only drive: "0:" */

DSTATUS disk_status(BYTE pdrv)
{
  if (pdrv != DEV_SD)
  {
    return STA_NOINIT;
  }
  return SD_Card_IsReady() ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
  if (pdrv != DEV_SD)
  {
    return STA_NOINIT;
  }
  return (SD_Card_Init() == HAL_OK) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
  if ((pdrv != DEV_SD) || (count == 0))
  {
    return RES_PARERR;
  }
  if (!SD_Card_IsReady())
  {
    return RES_NOTRDY;
  }
  return (SD_Card_Read(buff, (uint32_t)sector, count) == HAL_OK) ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
  (void)pdrv; (void)buff; (void)sector; (void)count;
  return RES_WRPRT;   /* the player never writes to the card */
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  if (pdrv != DEV_SD)
  {
    return RES_PARERR;
  }
  if (!SD_Card_IsReady())
  {
    return RES_NOTRDY;
  }

  switch (cmd)
  {
    case CTRL_SYNC:
      return RES_OK;
    case GET_SECTOR_COUNT:
      *(LBA_t *)buff = (LBA_t)SD_Card_SectorCount();
      return RES_OK;
    case GET_SECTOR_SIZE:
      *(WORD *)buff = (WORD)SD_CARD_SECTOR_SIZE;
      return RES_OK;
    case GET_BLOCK_SIZE:
      *(DWORD *)buff = 1;
      return RES_OK;
    default:
      return RES_PARERR;
  }
}
