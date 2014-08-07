/* Constants and macros for nouveau */
/* Many copied from xf86-video-nouveau */

#define LOG_TAG "GRALLOC-NOUVEAU"

#include <cutils/log.h>
#include <stdlib.h>
#include <errno.h>
#include <drm.h>
#include <nouveau.h>

#define TRUE 1
#define FALSE 0

#include "gralloc_drm.h"
#include "gralloc_drm_priv.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define NVC0_TILE_HEIGHT(m) (8 << ((m) >> 4))
#define NV50_TILE_HEIGHT(m) (4 << ((m) >> 4))

/* define NOUVEAU_CREATE_PIXMAP_ZETA constant */
#define NOUVEAU_CREATE_PIXMAP_ZETA 0x10000000

enum {
        NvDmaFB = 0xbeef0201,
        NvDmaTT = 0xbeef0202,
};

