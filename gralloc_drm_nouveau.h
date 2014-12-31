/* Constants and macros for nouveau */
/* Many copied from xf86-video-nouveau */

#define LOG_TAG "GRALLOC-NOUVEAU"

#include <cutils/log.h>
#include <stdlib.h>
#include <errno.h>
#include <drm.h>
#include <nouveau.h>

/* xf86-video-nouveau auto generated constants */
#include "nouveau/nv01_2d.xml.h"
#include "nouveau/nv10_3d.xml.h"
#include "nouveau/nv30-40_3d.xml.h"
#include "nouveau/nv50_2d.xml.h"
#include "nouveau/nv50_3d.xml.h"
#include "nouveau/nv50_defs.xml.h"
#include "nouveau/nv50_texture.h"
#include "nouveau/nv_3ddefs.xml.h"
#include "nouveau/nv_m2mf.xml.h"
#include "nouveau/nv_object.xml.h"
#include "nouveau/nvc0_3d.xml.h"
#include "nouveau/nvc0_m2mf.xml.h"


#include "nouveau/shader/xfrm2nvc0.vp"
#include "nouveau/shader/videonvc0.fp"

#include "nouveau/shader/exascnvc0.fp"
#include "nouveau/shader/exacmnvc0.fp"
#include "nouveau/shader/exacanvc0.fp"
#include "nouveau/shader/exasanvc0.fp"
#include "nouveau/shader/exas8nvc0.fp"
#include "nouveau/shader/exac8nvc0.fp"

#include "nouveau/shader/xfrm2nve0.vp"
#include "nouveau/shader/videonve0.fp"

#include "nouveau/shader/exascnve0.fp"
#include "nouveau/shader/exacmnve0.fp"
#include "nouveau/shader/exacanve0.fp"
#include "nouveau/shader/exasanve0.fp"
#include "nouveau/shader/exas8nve0.fp"
#include "nouveau/shader/exac8nve0.fp"

#include "nouveau/shader/xfrm2nvf0.vp"
#include "nouveau/shader/videonvf0.fp"

#include "nouveau/shader/exascnvf0.fp"
#include "nouveau/shader/exacmnvf0.fp"
#include "nouveau/shader/exacanvf0.fp"
#include "nouveau/shader/exasanvf0.fp"
#include "nouveau/shader/exas8nvf0.fp"
#include "nouveau/shader/exac8nvf0.fp"

#define TRUE 1
#define FALSE 0

#include "gralloc_drm.h"
#include "gralloc_drm_priv.h"

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define NVC0_TILE_HEIGHT(m) (8 << ((m) >> 4))
#define NV50_TILE_HEIGHT(m) (4 << ((m) >> 4))


#define NV_ARCH_03  0x03
#define NV_ARCH_04  0x04
#define NV_ARCH_10  0x10
#define NV_ARCH_20  0x20
#define NV_ARCH_30  0x30
#define NV_ARCH_40  0x40
#define NV_TESLA    0x50
#define NV_FERMI    0xc0
#define NV_KEPLER   0xe0
#define NV_MAXWELL  0x110


/* BEGIN nv50_accel.h */
/* scratch buffer offsets */
#define PVP_OFFSET  0x00000000 /* Vertex program */
#define PFP_OFFSET  0x00001000 /* Fragment program */
#define TIC_OFFSET_NV50  0x00002000 /* Texture Image Control */
#define TSC_OFFSET_NV50  0x00003000 /* Texture Sampler Control */
#define PVP_DATA_NV50    0x00004000 /* VP constbuf */
#define PFP_DATA_NV50    0x00004100 /* FP constbuf */
#define SOLID_NV50(i)   (0x00006000 + (i) * 0x100)

/* Fragment programs */
#define PFP_S_NV50     0x0000 /* (src) */
#define PFP_C_NV50     0x0100 /* (src IN mask) */
#define PFP_CCA_NV50   0x0200 /* (src IN mask) component-alpha */
#define PFP_CCASA_NV50 0x0300 /* (src IN mask) component-alpha src-alpha */
#define PFP_S_A8_NV50  0x0400 /* (src) a8 rt */
#define PFP_C_A8_NV50  0x0500 /* (src IN mask) a8 rt - same for CA and CA_SA */
#define PFP_NV12_NV50  0x0600 /* NV12 YUV->RGB */

/* Constant buffer assignments */
#define CB_PSH 0
#define CB_PVP 1
#define CB_PFP 2

/* END nv50_accel.h */


/* BEGIN nvc0_accel.h */
/* scratch buffer offsets */
#define CODE_OFFSET 0x00000 /* Code */
#define PVP_DATA_NVC0    0x01000 /* VP constants */
#define PFP_DATA_NVC0    0x01100 /* FP constants */
#define TB_OFFSET   0x01800 /* Texture bindings (kepler) */
#define TIC_OFFSET_NVC0  0x02000 /* Texture Image Control */
#define TSC_OFFSET_NVC0  0x03000 /* Texture Sampler Control */
#define SOLID_NVC0(i)   (0x04000 + (i) * 0x100)
#define NTFY_OFFSET 0x08000
#define SEMA_OFFSET 0x08100
#define MISC_OFFSET 0x10000

/* vertex/fragment programs */
#define SPO       ((info->arch < NV_KEPLER) ? 0x0000 : 0x0030)
#define PVP_PASS_NVC0  (0x0000 + SPO) /* vertex pass-through shader */
#define PFP_S_NVC0     (0x0200 + SPO) /* (src) */
#define PFP_C_NVC0     (0x0400 + SPO) /* (src IN mask) */
#define PFP_CCA_NVC0   (0x0600 + SPO) /* (src IN mask) component-alpha */
#define PFP_CCASA_NVC0 (0x0800 + SPO) /* (src IN mask) component-alpha src-alpha */
#define PFP_S_A8_NVC0  (0x0a00 + SPO) /* (src) a8 rt */
#define PFP_C_A8_NVC0  (0x0c00 + SPO) /* (src IN mask) a8 rt - same for CCA/CCASA */
#define PFP_NV12_NVC0  (0x0e00 + SPO) /* NV12 YUV->RGB */
/* END nvc0_accel.h */

/* BEGIN nv04_accel.h */
#define XV_TABLE_SIZE 512

/* scratch buffer offsets */
#define PFP_PASS          0x00000000
#define PFP_S_NV04             0x00000100
#define PFP_C_NV04             0x00000200
#define PFP_CCA_NV04           0x00000300
#define PFP_CCASA_NV04         0x00000400
#define PFP_S_A8_NV04          0x00000500
#define PFP_C_A8_NV04          0x00000600
#define PFP_NV12_BILINEAR 0x00000700
#define PFP_NV12_BICUBIC  0x00000800
#define XV_TABLE          0x00001000
#define SOLID_NV04(i)         (0x00002000 + (i) * 0x100)
/* END nv04_accel.h */


/* BEGIN nv40_exa.c */
#define NV30_3D_CHIPSET_4X_MASK 0x00000baf
#define NV44TCL_CHIPSET_4X_MASK 0x00005450
/* END nv40_exa.c */


#define SUBC_COPY_NV50(mthd)  2, (mthd)
#define SUBC_COPY_NVC0(mthd)  4, (mthd)

#define SUBC_P2MF(mthd)  2, (mthd)

#define SUBC_SF2D(mthd)  2, (mthd)

#define SUBC_2D(mthd)    2, (mthd)

#define SUBC_M2MF(mthd)  0, (mthd)

#define SUBC_IFC(mthd)   5, (mthd)

#define SUBC_3D(mthd)    7, (mthd)

#define NV01_CLIP(mthd)  SUBC_MISC(NV01_CLIP_##mthd)

#define NV01_BLIT(mthd)  SUBC_BLIT(NV01_BLIT_##mthd)

#define NV01_SUBC(subc, mthd) SUBC_##subc((NV01_SUBCHAN_##mthd))

#define NV01_BETA(mthd)  SUBC_MISC(NV01_BETA_##mthd)

#define NV01_IFC(mthd)   SUBC_IFC(NV01_IFC_##mthd)

#define NV01_PATT(mthd)  SUBC_MISC(NV01_PATTERN_##mthd)

#define NV01_ROP(mthd)   SUBC_MISC(NV01_ROP_##mthd)

#define NV03_SIFM(mthd)  SUBC_MISC(NV03_SIFM_##mthd)

#define NV03_M2MF(mthd)  SUBC_M2MF(NV03_M2MF_##mthd)

#define NV04_SF2D(mthd)  SUBC_SF2D(NV04_SURFACE_2D_##mthd)

#define NV04_RECT(mthd)  SUBC_RECT(NV04_GDI_##mthd)

#define NV04_BETA4(mthd) SUBC_MISC(NV04_BETA4_##mthd)

#define NV04_IFC(mthd)   SUBC_IFC(NV04_IFC_##mthd)

#define NV10_3D(mthd)    SUBC_3D(NV10_3D_##mthd)

#define NV05_SIFM(mthd)  SUBC_MISC(NV05_SIFM_##mthd)

#define NV30_3D(mthd)    SUBC_3D(NV30_3D_##mthd)

#define NV40_3D(mthd)    SUBC_3D(NV40_3D_##mthd)

#define NV50_2D(mthd)    SUBC_2D(NV50_2D_##mthd)

#define NV50_3D(mthd)    SUBC_3D(NV50_3D_##mthd)

#define SUBC_2DNVC0(mthd)    3, (mthd)

#define NVC0_3D(mthd)    SUBC_3D(NVC0_3D_##mthd)

#define SUBC_RECT(mthd)  3, (mthd)

#define SUBC_BLIT(mthd)  4, (mthd)

#define SUBC_MISC(mthd)  6, (mthd)

#define SUBC_NVSW(mthd)  1, (mthd)

/* SUBC_2D and NV50_2D are defined in nv50_accel.h and nvc0_accel.h */
/* I'm renaming macro names to use a single include file */
/* Please reestructure this if it seems a mess */

#define NV04_BLIT(mthd)  SUBC_BLIT(NV04_BLIT_##mthd)

#define NV15_BLIT(mthd)  SUBC_BLIT(NV15_BLIT_##mthd)

#define NV50_2DNVC0(mthd)    SUBC_2DNVC0(NV50_2D_##mthd)

#define NV50_M2MF(mthd)  SUBC_M2MF(NV50_M2MF_##mthd)

#define SUBC_M2MFNVC0(mthd)  2, (mthd)
#define NVC0_M2MF(mthd)  SUBC_M2MFNVC0(NVC0_M2MF_##mthd)

#define NVC0_2D(mthd)    SUBC_2D(NVC0_2D_##mthd)

#define NV04_GRAPH(subc, mthd) SUBC_##subc((NV04_GRAPH_##mthd))


enum {
	NvNullObject		= 0x00000000,
	NvContextSurfaces	= 0x80000010, 
	NvRop			= 0x80000011, 
	NvImagePattern		= 0x80000012, 
	NvClipRectangle		= 0x80000013, 
	NvSolidLine		= 0x80000014, 
	NvImageBlit		= 0x80000015, 
	NvRectangle		= 0x80000016, 
	NvScaledImage		= 0x80000017, 
	NvMemFormat		= 0x80000018,
	Nv3D			= 0x80000019,
	NvImageFromCpu		= 0x8000001A,
	NvContextBeta1		= 0x8000001B,
	NvContextBeta4		= 0x8000001C,
	Nv2D			= 0x80000020,
	NvSW			= 0x80000021,
	NvDmaFB			= 0xbeef0201,
	NvDmaTT			= 0xbeef0202,
	NvDmaNotifier0		= 0xD8000003,
	NvVBlankSem		= 0xD8000004,	

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
BEGIN_NV04(struct nouveau_pushbuf *push, int subc, int mthd, int size)
{
	PUSH_DATA (push, 0x00000000 | (size << 18) | (subc << 13) | mthd);
}

static inline void
BEGIN_NI04(struct nouveau_pushbuf *push, int subc, int mthd, int size)
{
	PUSH_DATA (push, 0x40000000 | (size << 18) | (subc << 13) | mthd);
}

static inline void
BEGIN_NIC0(struct nouveau_pushbuf *push, int subc, int mthd, int size)
{
	PUSH_DATA (push, 0x60000000 | (size << 16) | (subc << 13) | (mthd / 4));
}

static __inline__ void
PUSH_DATAu(struct nouveau_pushbuf *push, struct nouveau_bo *bo,
	   unsigned delta, unsigned dwords)
{
	const unsigned idx = (delta & 0x000000fc) >> 2;
	const unsigned off = (delta & 0xffffff00);
	BEGIN_NV04(push, NV50_3D(CB_DEF_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (bo->offset + off) >> 32);
	PUSH_DATA (push, (bo->offset + off));
	PUSH_DATA (push, (CB_PSH << NV50_3D_CB_DEF_SET_BUFFER__SHIFT) | 0x2000);
	BEGIN_NV04(push, NV50_3D(CB_ADDR), 1);
	PUSH_DATA (push, CB_PSH | (idx << NV50_3D_CB_ADDR_ID__SHIFT));
	BEGIN_NI04(push, NV50_3D(CB_DATA(0)), dwords);
}

static inline void
PUSH_DATAf(struct nouveau_pushbuf *push, float v)
{
	union { float f; uint32_t i; } d = { .f = v };
	PUSH_DATA (push, d.i);
}

/* For NV40 FP upload, deal with the weird-arse big-endian swap */
static __inline__ void
PUSH_DATAs(struct nouveau_pushbuf *push, unsigned data)
{
#if (X_BYTE_ORDER != X_LITTLE_ENDIAN)
	data = (data >> 16) | ((data & 0xffff) << 16);
#endif
	PUSH_DATA(push, data);
}

static inline void
PUSH_DATAp(struct nouveau_pushbuf *push, const void *data, uint32_t size)
{
	memcpy(push->cur, data, size * 4);
	push->cur += size;
}


static inline void
BEGIN_NVC0(struct nouveau_pushbuf *push, int subc, int mthd, int size)
{
        PUSH_DATA (push, 0x20000000 | (size << 16) | (subc << 13) | (mthd / 4));
}

#define NVC0PushProgram(info,addr,code) do {                                    \
	const unsigned size = sizeof(code) / sizeof(code[0]);                  \
	PUSH_DATAu((info)->pushbuf, (info)->scratch, (addr), size);              \
	PUSH_DATAp((info)->pushbuf, (code), size);                              \
} while(0)
