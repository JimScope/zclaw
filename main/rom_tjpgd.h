/*
 * TJpgDec (C)ChaN, 2012 - the JPEG decoder in the ESP32-C6 ROM.
 *
 * ESP-IDF ships this header for ESP32/C3/C5/C61/S3 but not for C6, whose ROM
 * exports the same jd_prepare/jd_decomp (esp32c6.rom.ld). The C3/C5/C61
 * headers are identical, so this is a copy of their declarations.
 * Baseline JPEG only; output is RGB888 (3 bytes/pixel); ~3.1 KB work pool.
 */
#ifndef ROM_TJPGD_H
#define ROM_TJPGD_H

#include "esp_rom_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int    UINT;
typedef unsigned char   BYTE;
typedef short           SHORT;
typedef unsigned short  WORD;
typedef long            LONG;

typedef enum {
    JDR_OK = 0, JDR_INTR, JDR_INP, JDR_MEM1, JDR_MEM2, JDR_PAR, JDR_FMT1, JDR_FMT2, JDR_FMT3
} JRESULT;

typedef struct {
    WORD left, right, top, bottom;
} JRECT;

typedef struct JDEC JDEC;
struct JDEC {
    UINT dctr;
    BYTE *dptr;
    BYTE *inbuf;
    BYTE dmsk;
    BYTE scale;
    BYTE msx, msy;
    BYTE qtid[3];
    SHORT dcv[3];
    WORD nrst;
    UINT width, height;
    BYTE *huffbits[2][2];
    WORD *huffcode[2][2];
    BYTE *huffdata[2][2];
    LONG *qttbl[4];
    void *workbuf;
    BYTE *mcubuf;
    void *pool;
    UINT sz_pool;
    UINT (*infunc)(JDEC *, BYTE *, UINT);
    void *device;
};

JRESULT jd_prepare(JDEC *, UINT (*)(JDEC *, BYTE *, UINT), void *, UINT, void *);
JRESULT jd_decomp(JDEC *, UINT (*)(JDEC *, void *, JRECT *), BYTE);

#ifdef __cplusplus
}
#endif

#endif // ROM_TJPGD_H
