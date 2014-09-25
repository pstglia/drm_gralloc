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

/* Used in nouveau_init2D_NVC0 */
#define NV50_SURFACE_FORMAT_B5G6R5_UNORM                   0x000000e8

#define NV01_SUBCHAN_OBJECT                                    0x00000000

#define NV50_2D_CLIP_ENABLE                                       0x00000290

#define NV50_2D_COLOR_KEY_ENABLE                          0x0000029c

#define NV50_2D_ROP                                               0x000002a0

#define NV50_2D_OPERATION                                 0x000002ac

#define NV50_2D_OPERATION_SRCCOPY                         0x00000003

#define NV50_2D_BLIT_DU_DX_FRACT                          0x000008c0

#define NV50_2D_DRAW_SHAPE                                        0x00000580

#define NV50_2D_PATTERN_COLOR_FORMAT                              0x000002e8

#define NVC0_M2MF_QUERY_ADDRESS_HIGH                             0x0000032c

#define NTFY_OFFSET 0x08000

#define SUBC_2DNVC0(mthd)    3, (mthd)

#define NV01_SUBC(subc, mthd) SUBC_##subc((NV01_SUBCHAN_##mthd))

/* SUBC_2D and NV50_2D are defined in nv50_accel.h and nvc0_accel.h */
/* I'm renaming macro names to use a single include file */
/* Please reestructure this if it seems a mess */
#define SUBC_2DNVC0(mthd)    3, (mthd)
#define NV50_2DNVC0(mthd)    SUBC_2DNVC0(NV50_2D_##mthd)

#define SUBC_M2MFNVC0(mthd)  2, (mthd)
#define NVC0_M2MF(mthd)  SUBC_M2MFNVC0(NVC0_M2MF_##mthd)


enum {
        NvDmaFB = 0xbeef0201,
        NvDmaTT = 0xbeef0202,
};

static inline uint32_t
PUSH_AVAIL(struct nouveau_pushbuf *push)
{
        return push->end - push->cur;
}

static inline int
PUSH_SPACE(struct nouveau_pushbuf *push, uint32_t size)
{
        if (PUSH_AVAIL(push) < size)
                return nouveau_pushbuf_space(push, size, 0, 0) == 0;
        return TRUE;
}

static inline void
PUSH_DATA(struct nouveau_pushbuf *push, uint32_t data)
{
        *push->cur++ = data;
}

static inline void
BEGIN_NVC0(struct nouveau_pushbuf *push, int subc, int mthd, int size)
{
        PUSH_DATA (push, 0x20000000 | (size << 16) | (subc << 13) | (mthd / 4));
}


