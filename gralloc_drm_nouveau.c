/*
 * Copyright (C) 2011 Chia-I Wu <olvaffe@gmail.com>
 * Copyright (C) 2011 LunarG Inc.
 *
 * Based on xf86-video-nouveau, which has
 *
 * Copyright © 2007 Red Hat, Inc.
 * Copyright © 2008 Maarten Maathuis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <cutils/log.h>
#include <stdlib.h>
#include <errno.h>
#include <drm.h>
#include <nouveau.h>

#include "gralloc_drm.h"
#include "gralloc_drm_priv.h"
#include "gralloc_drm_nouveau.h"

struct nouveau_info {
	struct gralloc_drm_drv_t base;

	int fd;
	struct nouveau_device *dev;
	struct nouveau_object *chan;
	struct nouveau_client *client;
        struct nouveau_pushbuf *pushbuf;
        struct nouveau_bufctx *bufctx;
	struct nouveau_object *ce_channel;
	struct nouveau_pushbuf *ce_pushbuf;
	struct nouveau_object *Nv2D;
	struct nouveau_object *Nv3D;
	struct nouveau_object *NvMemFormat;
	struct nouveau_bo *scratch;
	struct nouveau_object *NvSW;
	struct nouveau_object *notify0;
	struct nouveau_object *NvContextBeta1;
	struct nouveau_object *NvContextBeta4;
	struct nouveau_object *NvNull;
	struct nouveau_object *NvContextSurfaces;
	struct nouveau_object *NvImagePattern;
	struct nouveau_object *NvImageBlit;
	struct nouveau_object *NvImageFromCpu;
	struct nouveau_object *NvClipRectangle;
	struct nouveau_object *NvScaledImage;
	struct nouveau_object *NvRectangle;
	struct nouveau_object *NvRop;
	struct nouveau_object *vblank_sem;
	struct nouveau_object *NvCOPY;
	int              currentRop;
	int arch;
	int tiled_scanout;
};

struct nouveau_buffer {
	struct gralloc_drm_bo_t base;

	struct nouveau_bo *bo;
};

/* Copied from xf86-video-nouveau (nouveau_local.h) */
static inline int log2i(int i)
{
        int r = 0;

        if (i & 0xffff0000) {
                i >>= 16;
                r += 16;
        }
        if (i & 0x0000ff00) {
                i >>= 8;
                r += 8;
        }
        if (i & 0x000000f0) {
                i >>= 4;
                r += 4;
        }
        if (i & 0x0000000c) {
                i >>= 2;
                r += 2;
        }
        if (i & 0x00000002) {
                r += 1;
        }
        return r;
}

/* Copied from xf86-video-nouveau (nouveau_local.h) */
static inline int round_down_pow2(int x)
{
        return 1 << log2i(x);
}


static struct nouveau_bo *alloc_bo(struct nouveau_info *info,
		int width, int height, int cpp, int usage, int *pitch)
{
	struct nouveau_bo *bo = NULL;
	int flags, tile_mode, tile_flags;
	int tiled, scanout,  sw_indicator;
	unsigned int align;
	union nouveau_bo_config bo_config;

	flags = NOUVEAU_BO_MAP | NOUVEAU_BO_VRAM;
	tile_mode = 0;
	tile_flags = 0;

	scanout = (usage & GRALLOC_USAGE_HW_FB);

	tiled = !(usage & (GRALLOC_USAGE_SW_READ_OFTEN |
			   GRALLOC_USAGE_SW_WRITE_OFTEN));

	/* pstglia note: */
	/* Noted when using GRALLOC_USAGE_SW_READ_OFTEN or GRALLOC_USAGE_SW_WRITE_OFTEN, tiled must be false */
	/* Otherwise, mouse cursor and overlay windows are corrupted (setting a memtype supposed to be used for tiled bo's */	
	/* If this is not correct, please fix it */
	sw_indicator = (usage & (GRALLOC_USAGE_SW_READ_OFTEN |
			   GRALLOC_USAGE_SW_WRITE_OFTEN));

	if (!info->chan)
		tiled = 0;
	else if (scanout && info->tiled_scanout)
		tiled = 1;

	/* calculate pitch align */
	align = 64;
	if (info->arch >= NV_TESLA) {
		if (scanout && !info->tiled_scanout)
			align = 256;
		else
			tiled = 1;
	}

	*pitch = ALIGN(width * cpp, align);

	ALOGI("DEBUG PST - inside function alloc_bo: tiled: %d; scanout: %d; usage: %d", tiled, scanout, usage);
	ALOGI("DEBUG PST - inside function alloc_bo (2): cpp: %d; pitch: %d; width: %d;height: %d", cpp, *pitch, width, height);

	if (tiled) {
		if (info->arch >= NV_FERMI) {
			ALOGI("DEBUG PST - arch is NV_FERMI or higher - setting tile_flags, align, height");
			if (height > 64)
				tile_mode = 0x040;
			else if (height > 32)
				tile_mode = 0x030;
			else if (height > 16)
				tile_mode = 0x020;
			else if (height > 8)
				tile_mode = 0x010;
			else
				tile_mode = 0x000;

			if ( sw_indicator ) {
				tile_flags = 0x00;
			}
			else {
				tile_flags = 0xfe;
			}

			align = NVC0_TILE_HEIGHT(tile_mode);
			height = ALIGN(height, align);
		}
		else if (info->arch >= NV_TESLA) {
			ALOGI("DEBUG PST - arch is 0x50 - setting tile_flags, align, height");
			if (height > 32)
				tile_mode = 0x040;
			else if (height > 16)
				tile_mode = 0x030;
			else if (height > 8)
				tile_mode = 0x020;
			else if (height > 4)
				tile_mode = 0x010;
			else
				tile_mode = 0x000;

			if (sw_indicator) {
				tile_flags = 0x00;
			}
			else {
				if (scanout)
					tile_flags = (cpp == 2) ? 0x070 : 0x07a;
				else {
					tile_flags = 0x070;
				}
			}

			/*align = 1 << (tile_mode + 2);*/
			align = NV50_TILE_HEIGHT(tile_mode);
			height = ALIGN(height, align);
		}
		else {
			
			ALOGI("DEBUG PST - arch is 0x4n or lower - setting align and tile_mode");

			align = MAX((info->dev->chipset >= 0x40) ? 1024 : 256,
					round_down_pow2(*pitch / 4));

			/* adjust pitch */
			*pitch = ALIGN(*pitch, align);

			tile_mode = *pitch;
		}
	}

	/* setting tile_mode and memtype(tile_flags?) - START */
	if (info->arch >= NV_FERMI) {
		bo_config.nvc0.memtype = tile_flags;
		bo_config.nvc0.tile_mode = tile_mode;
	}
	else if (info->arch >= NV_TESLA) {
		bo_config.nv50.memtype = tile_flags;
		bo_config.nv50.tile_mode = tile_mode;
	}
	else {
		if ( cpp == 2 )
			bo_config.nv04.surf_flags |= NV04_BO_16BPP;
		if ( cpp == 4 )
			bo_config.nv04.surf_flags |= NV04_BO_32BPP;
		bo_config.nv04.surf_pitch = tile_mode;
	}
	/* setting tile_mode and memtype(tile_flags?) - END */

	/* pstglia: xf86-video-nouveau do this - I'll copy */
	if (scanout) {
		flags |= NOUVEAU_BO_CONTIG;
	}

	if (nouveau_bo_new(info->dev, flags, 0, *pitch * height,
				&bo_config, &bo)) {
		ALOGE("failed to allocate bo (flags 0x%x, size %d, tile_mode 0x%x, tile_flags 0x%x)",
				flags, *pitch * height, tile_mode, tile_flags);
		bo = NULL;
	}

	if (bo->map != NULL) {
		ALOGI("PST DEBUG - bo->map is not not just after nouveau_bo_new");
	}

	/* Setting created bo map to NULL */
	bo->map = NULL;

	return bo;
}

static struct gralloc_drm_bo_t *
nouveau_alloc(struct gralloc_drm_drv_t *drv, struct gralloc_drm_handle_t *handle)
{
	struct nouveau_info *info = (struct nouveau_info *) drv;
	struct nouveau_buffer *nb;
	int cpp;

	cpp = gralloc_drm_get_bpp(handle->format);
	if (!cpp) {
		ALOGE("unrecognized format 0x%x", handle->format);
		return NULL;
	}

	nb = calloc(1, sizeof(*nb));
	if (!nb)
		return NULL;

	if (handle->name) {
		if (nouveau_bo_name_ref(info->dev, handle->name, &nb->bo)) {
			ALOGE("failed to create nouveau bo from name %u",
					handle->name);
			free(nb);
			return NULL;
		}
	}
	else {
		int width, height, pitch;

		width = handle->width;
		height = handle->height;
		gralloc_drm_align_geometry(handle->format, &width, &height);

		nb->bo = alloc_bo(info, width, height,
				cpp, handle->usage, &pitch);
		if (!nb->bo) {
			ALOGE("failed to allocate nouveau bo %dx%dx%d",
					handle->width, handle->height, cpp);
			free(nb);
			return NULL;
		}

		if (nouveau_bo_name_get(nb->bo,
					(uint32_t *) &handle->name)) {
			ALOGE("failed to flink nouveau bo");
			nouveau_bo_ref(NULL, &nb->bo);
			free(nb);
			return NULL;
		}

		handle->stride = pitch;
	}

	if (handle->usage & GRALLOC_USAGE_HW_FB)
		nb->base.fb_handle = nb->bo->handle;

	nb->base.handle = handle;

	return &nb->base;
}

static void nouveau_free(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
	struct nouveau_buffer *nb = (struct nouveau_buffer *) bo;
	nouveau_bo_ref(NULL, &nb->bo);
	free(nb);
}

static int nouveau_map(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo, int x, int y, int w, int h,
		int enable_write, void **addr)
{
	struct nouveau_info *info = (struct nouveau_info *) drv;
	struct nouveau_buffer *nb = (struct nouveau_buffer *) bo;
	uint32_t flags;
	int err;


	flags = NOUVEAU_BO_RD;
	if (enable_write)
		flags |= NOUVEAU_BO_WR;

	ALOGI("DEBUG PST -Trying nouveau_bo_map - enable_write: %d", enable_write);

	/* Setting nb->bo->map as NULL before calling nouveau_bo_map) */
	/* Setting now after bo creation */
	/*nb->bo->map = NULL;*/

	/* TODO if tiled, allocate a linear copy of bo in GART and map it */
	/*err = nouveau_bo_map(nb->bo, flags, client);*/
	err = nouveau_bo_map(nb->bo, flags, info->client);
	ALOGI("MAURO DEBUG - Value of err %d", err);

	if (!err) {
		*addr = nb->bo->map;
	}
	else {
		ALOGE("DEBUG PST - Error on nouveau_map");
	}

	return err;
}

static void nouveau_unmap(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_bo_t *bo)
{
	//struct nouveau_buffer *nb = (struct nouveau_buffer *) bo;
	/* TODO if tiled, unmap the linear bo and copy back */

	//ALOGI("DEBUG PST - Inside nouveau_unmap");

	/* Replaced noveau_bo_unmap (not existant in libdrm anymore) by these 2 cmd  */
	/* nouveau_bo_del execute these cmds, but it also release the bo... */
	/* TODO: Confirm if this is the best way for unmapping bo */
	//munmap(nb->bo->map, nb->bo->size);
	//nb->bo->map = NULL;
}

static void nouveau_init_kms_features(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_t *drm)
{
	struct nouveau_info *info = (struct nouveau_info *) drv;

	switch (drm->primary.fb_format) {
	case HAL_PIXEL_FORMAT_BGRA_8888:
	/* pstglia: This mode (HAL_PIXEL_FORMAT_RGB_565) was not supported in my tests - so I disabled it*/
	/* please confirm it */
	case HAL_PIXEL_FORMAT_RGB_565:
		break;
	default:
		drm->primary.fb_format = HAL_PIXEL_FORMAT_BGRA_8888;
		break;
	}

	drm->mode_quirk_vmwgfx = 0;
	drm->swap_mode = (info->chan) ? DRM_SWAP_FLIP : DRM_SWAP_SETCRTC;
	drm->mode_sync_flip = 1;
	drm->swap_interval = 1;
	drm->vblank_secondary = 0;
}

// TakeDown DMA Allocation (channels, buffers, etc) 
// pstglia NOTE: It's a copy from xf86-video-nouveau NVTakedownDma 

void nouveau_takedown_dma(struct nouveau_info *info)
{
        if (info->ce_channel) {
                struct nouveau_fifo *fifo = info->ce_channel->data;
                int chid = fifo->channel;

                nouveau_pushbuf_del(&info->ce_pushbuf);
                nouveau_object_del(&info->ce_channel);

		ALOGI("PST DEBUG - Closed GPU CE channel %d\n", chid);

        }

        if (info->chan) {
                struct nouveau_fifo *fifo = info->chan->data;
                int chid = fifo->channel;

                nouveau_bufctx_del(&info->bufctx);
                nouveau_pushbuf_del(&info->pushbuf);
                nouveau_object_del(&info->chan);

		ALOGI("PST DEBUG - Closed GPU channel %d\n", chid);
        }
}

// Destroy objects used for Accel
void nouveau_accel_free(struct nouveau_info *info)
{
        nouveau_object_del(&info->Nv2D);
        nouveau_object_del(&info->NvMemFormat);
	nouveau_bo_ref(NULL, &info->scratch);
}

/* The filtering function used for video scaling. We use a cubic filter as
 * defined in  "Reconstruction Filters in Computer Graphics" Mitchell &
 * Netravali in SIGGRAPH '88
 */
static float filter_func(float x)
{
	const double B=0.75;
	const double C=(1.0-B)/2.0;
	double x1=fabs(x);
	double x2=fabs(x)*x1;
	double x3=fabs(x)*x2;

	if (fabs(x)<1.0)
		return ( (12.0-9.0*B-6.0*C)*x3+(-18.0+12.0*B+6.0*C)*x2+(6.0-2.0*B) )/6.0; 
	else
		return ( (-B-6.0*C)*x3+(6.0*B+30.0*C)*x2+(-12.0*B-48.0*C)*x1+(8.0*B+24.0*C) )/6.0;
}

static int8_t f32tosb8(float v)
{
	return (int8_t)(v*127.0);
}

void
NVXVComputeBicubicFilter(struct nouveau_bo *bo, unsigned offset, unsigned size)
{
	int8_t *t = (int8_t *)(bo->map + offset);
	int i;

	for(i = 0; i < size; i++) {
		float  x = (i + 0.5) / size;
		float w0 = filter_func(x+1.0);
		float w1 = filter_func(x);
		float w2 = filter_func(x-1.0);
		float w3 = filter_func(x-2.0);

		t[4*i+2]=f32tosb8(1.0+x-w1/(w0+w1));
		t[4*i+1]=f32tosb8(1.0-x+w3/(w2+w3));
		t[4*i+0]=f32tosb8(w0+w1);
		t[4*i+3]=f32tosb8(0.0);
	}
}


int NVAccelInitDmaNotifier0(struct nouveau_info *info)
{
        struct nouveau_object *chan = info->chan;
        struct nv04_notify ntfy = { .length = 32 };

        if (nouveau_object_new(chan, NvDmaNotifier0, NOUVEAU_NOTIFIER_CLASS,
                               &ntfy, sizeof(ntfy), &info->notify0))
                return FALSE;

        return TRUE;
}

int NVAccelInitNull(struct nouveau_info *info)
{

        if (nouveau_object_new(info->chan, NvNullObject, NV01_NULL_CLASS,
                               NULL, 0, &info->NvNull))
                return FALSE;

        return TRUE;
}


int NVAccelInitContextSurfaces(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;
	struct nv04_fifo *fifo = info->chan->data;
	uint32_t class;

	class = (info->arch >= NV_ARCH_10) ? NV10_SURFACE_2D_CLASS :
						    NV04_SURFACE_2D_CLASS;

	if (nouveau_object_new(info->chan, NvContextSurfaces, class,
			       NULL, 0, &info->NvContextSurfaces))
		return FALSE;

	if (!PUSH_SPACE(push, 8))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(SF2D, OBJECT), 1);
	PUSH_DATA (push, info->NvContextSurfaces->handle);
	BEGIN_NV04(push, NV04_SF2D(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->NvNull->handle);
	BEGIN_NV04(push, NV04_SF2D(DMA_IMAGE_SOURCE), 2);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, fifo->vram);
	return TRUE;
}

int NVAccelInitContextBeta1(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;

	if (nouveau_object_new(info->chan, NvContextBeta1, NV01_BETA_CLASS,
			       NULL, 0, &info->NvContextBeta1))
		return FALSE;

	if (!PUSH_SPACE(push, 4))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(MISC, OBJECT), 1);
	PUSH_DATA (push, info->NvContextBeta1->handle);
	BEGIN_NV04(push, NV01_BETA(BETA_1D31), 1); /*alpha factor*/
	PUSH_DATA (push, 0xff << 23);
	return TRUE;
}


int NVAccelInitContextBeta4(struct nouveau_info *info)
{
	
	struct nouveau_pushbuf *push = info->pushbuf;
	
	if (nouveau_object_new(info->chan, NvContextBeta4, NV04_BETA4_CLASS,
			       NULL, 0, &info->NvContextBeta4))
		return FALSE;

	if (!PUSH_SPACE(push, 4))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(MISC, OBJECT), 1);
	PUSH_DATA (push, info->NvContextBeta4->handle);
	BEGIN_NV04(push, NV04_BETA4(BETA_FACTOR), 1); /*RGBA factor*/
	PUSH_DATA (push, 0xffff0000);
	return TRUE;
}

int NVAccelInitImagePattern(struct nouveau_info *info)
{
	
	struct nouveau_pushbuf *push = info->pushbuf;

	if (nouveau_object_new(info->chan, NvImagePattern, NV04_PATTERN_CLASS,
			       NULL, 0, &info->NvImagePattern))
		return FALSE;

	if (!PUSH_SPACE(push, 8))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(MISC, OBJECT), 1);
	PUSH_DATA (push, info->NvImagePattern->handle);
	BEGIN_NV04(push, NV01_PATT(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->NvNull->handle);
	BEGIN_NV04(push, NV01_PATT(MONOCHROME_FORMAT), 3);
#if X_BYTE_ORDER == X_BIG_ENDIAN
	PUSH_DATA (push, NV01_PATTERN_MONOCHROME_FORMAT_LE);
#else
	PUSH_DATA (push, NV01_PATTERN_MONOCHROME_FORMAT_CGA6);
#endif
	PUSH_DATA (push, NV01_PATTERN_MONOCHROME_SHAPE_8X8);
	PUSH_DATA (push, NV04_PATTERN_PATTERN_SELECT_MONO);

	return TRUE;
}

int
NVAccelInitRasterOp(struct nouveau_info *info)
{
	
	struct nouveau_pushbuf *push = info->pushbuf;

	if (nouveau_object_new(info->chan, NvRop, NV03_ROP_CLASS,
			       NULL, 0, &info->NvRop))
		return FALSE;

	if (!PUSH_SPACE(push, 4))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(MISC, OBJECT), 1);
	PUSH_DATA (push, info->NvRop->handle);
	BEGIN_NV04(push, NV01_ROP(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->NvNull->handle);

	info->currentRop = ~0;
	return TRUE;
}

int
NVAccelInitRectangle(struct nouveau_info *info)
{
	
	struct nouveau_pushbuf *push = info->pushbuf;

	if (nouveau_object_new(info->chan, NvRectangle, NV04_GDI_CLASS,
			       NULL, 0, &info->NvRectangle))
		return FALSE;

	if (!PUSH_SPACE(push, 16))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(RECT, OBJECT), 1);
	PUSH_DATA (push, info->NvRectangle->handle);
	BEGIN_NV04(push, NV04_RECT(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->notify0->handle);
	BEGIN_NV04(push, NV04_RECT(DMA_FONTS), 1);
	PUSH_DATA (push, info->NvNull->handle);
	BEGIN_NV04(push, NV04_RECT(SURFACE), 1);
	PUSH_DATA (push, info->NvContextSurfaces->handle);
	BEGIN_NV04(push, NV04_RECT(ROP), 1);
	PUSH_DATA (push, info->NvRop->handle);
	BEGIN_NV04(push, NV04_RECT(PATTERN), 1);
	PUSH_DATA (push, info->NvImagePattern->handle);
	BEGIN_NV04(push, NV04_RECT(OPERATION), 1);
	PUSH_DATA (push, NV04_GDI_OPERATION_ROP_AND);
	BEGIN_NV04(push, NV04_RECT(MONOCHROME_FORMAT), 1);
	/* XXX why putting 1 like renouveau dump, swap the text */
#if 1 || X_BYTE_ORDER == X_BIG_ENDIAN
	PUSH_DATA (push, NV04_GDI_MONOCHROME_FORMAT_LE);
#else
	PUSH_DATA (push, NV04_GDI_MONOCHROME_FORMAT_CGA6);
#endif

	return TRUE;
}

int
NVAccelInitImageBlit(struct nouveau_info *info)
{
	
	struct nouveau_pushbuf *push = info->pushbuf;
	uint32_t class;

	class = (info->dev->chipset >= 0x11) ? NV15_BLIT_CLASS : NV04_BLIT_CLASS;

	if (nouveau_object_new(info->chan, NvImageBlit, class,
			       NULL, 0, &info->NvImageBlit))
		return FALSE;

	if (!PUSH_SPACE(push, 16))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(BLIT, OBJECT), 1);
	PUSH_DATA (push, info->NvImageBlit->handle);
	BEGIN_NV04(push, NV01_BLIT(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->notify0->handle);
	BEGIN_NV04(push, NV01_BLIT(COLOR_KEY), 1);
	PUSH_DATA (push, info->NvNull->handle);
	BEGIN_NV04(push, NV04_BLIT(SURFACES), 1);
	PUSH_DATA (push, info->NvContextSurfaces->handle);
	BEGIN_NV04(push, NV01_BLIT(CLIP), 3);
	PUSH_DATA (push, info->NvNull->handle);
	PUSH_DATA (push, info->NvImagePattern->handle);
	PUSH_DATA (push, info->NvRop->handle);
	BEGIN_NV04(push, NV01_BLIT(OPERATION), 1);
	PUSH_DATA (push, NV01_BLIT_OPERATION_ROP_AND);
	if (info->NvImageBlit->oclass == NV15_BLIT_CLASS) {
		BEGIN_NV04(push, NV15_BLIT(FLIP_SET_READ), 3);
		PUSH_DATA (push, 0);
		PUSH_DATA (push, 1);
		PUSH_DATA (push, 2);
	}

	return TRUE;
}

int
NVAccelInitScaledImage(struct nouveau_info *info)
{
	
	struct nouveau_pushbuf *push = info->pushbuf;
	struct nv04_fifo *fifo = info->chan->data;
	uint32_t class;

	switch (info->arch) {
	case NV_ARCH_04:
		class = NV04_SIFM_CLASS;
		break;
	case NV_ARCH_10:
	case NV_ARCH_20:
	case NV_ARCH_30:
		class = NV10_SIFM_CLASS;
		break;
	case NV_ARCH_40:
	default:
		class = NV40_SIFM_CLASS;
		break;
	}

	if (nouveau_object_new(info->chan, NvScaledImage, class,
			       NULL, 0, &info->NvScaledImage))
		return FALSE;

	if (!PUSH_SPACE(push, 16))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(MISC, OBJECT), 1);
	PUSH_DATA (push, info->NvScaledImage->handle);
	BEGIN_NV04(push, NV03_SIFM(DMA_NOTIFY), 7);
	PUSH_DATA (push, info->notify0->handle);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, info->NvNull->handle);
	PUSH_DATA (push, info->NvNull->handle);
	PUSH_DATA (push, info->NvContextBeta1->handle);
	PUSH_DATA (push, info->NvContextBeta4->handle);
	PUSH_DATA (push, info->NvContextSurfaces->handle);
	if (info->arch>=NV_ARCH_10) {
		BEGIN_NV04(push, NV05_SIFM(COLOR_CONVERSION), 1);
		PUSH_DATA (push, NV05_SIFM_COLOR_CONVERSION_DITHER);
	}
	BEGIN_NV04(push, NV03_SIFM(OPERATION), 1);
	PUSH_DATA (push, NV03_SIFM_OPERATION_SRCCOPY);

	return TRUE;
}

int
NVAccelInitClipRectangle(struct nouveau_info *info)
{
	
	struct nouveau_pushbuf *push = info->pushbuf;

	if (nouveau_object_new(info->chan, NvClipRectangle, NV01_CLIP_CLASS,
			       NULL, 0, &info->NvClipRectangle))
		return FALSE;

	if (!PUSH_SPACE(push, 4))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(MISC, OBJECT), 1);
	PUSH_DATA (push, info->NvClipRectangle->handle);
	BEGIN_NV04(push, NV01_CLIP(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->NvNull->handle);
	return TRUE;
}

int
NVAccelInitMemFormat(struct nouveau_info *info)
{
	
	struct nouveau_pushbuf *push = info->pushbuf;

	if (nouveau_object_new(info->chan, NvMemFormat, NV03_M2MF_CLASS,
			       NULL, 0, &info->NvMemFormat))
		return FALSE;

	if (!PUSH_SPACE(push, 4))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(M2MF, OBJECT), 1);
	PUSH_DATA (push, info->NvMemFormat->handle);
	BEGIN_NV04(push, NV03_M2MF(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->notify0->handle);
	return TRUE;
}

int
NVAccelInitImageFromCpu(struct nouveau_info *info)
{
	
	struct nouveau_pushbuf *push = info->pushbuf;
	uint32_t class;

	switch (info->arch) {
	case NV_ARCH_04:
		class = NV04_IFC_CLASS;
		break;
	case NV_ARCH_10:
	case NV_ARCH_20:
	case NV_ARCH_30:
	case NV_ARCH_40:
	default:
		class = NV10_IFC_CLASS;
		break;
	}

	if (nouveau_object_new(info->chan, NvImageFromCpu, class,
			       NULL, 0, &info->NvImageFromCpu))
		return FALSE;

	if (!PUSH_SPACE(push, 16))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(IFC, OBJECT), 1);
	PUSH_DATA (push, info->NvImageFromCpu->handle);
	BEGIN_NV04(push, NV01_IFC(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->notify0->handle);
	BEGIN_NV04(push, NV01_IFC(CLIP), 1);
	PUSH_DATA (push, info->NvNull->handle);
	BEGIN_NV04(push, NV01_IFC(PATTERN), 1);
	PUSH_DATA (push, info->NvNull->handle);
	BEGIN_NV04(push, NV01_IFC(ROP), 1);
	PUSH_DATA (push, info->NvNull->handle);
	if (info->arch >= NV_ARCH_10) {
		BEGIN_NV04(push, NV01_IFC(BETA), 1);
		PUSH_DATA (push, info->NvNull->handle);
		BEGIN_NV04(push, NV04_IFC(BETA4), 1);
		PUSH_DATA (push, info->NvNull->handle);
	}
	BEGIN_NV04(push, NV04_IFC(SURFACE), 1);
	PUSH_DATA (push, info->NvContextSurfaces->handle);
	BEGIN_NV04(push, NV01_IFC(OPERATION), 1);
	PUSH_DATA (push, NV01_IFC_OPERATION_SRCCOPY);
	return TRUE;
}

#define INIT_CONTEXT_OBJECT(name) do {                                        \
	ret = NVAccelInit##name(info);                                       \
	if (!ret) {                                                           \
		ALOGE("failed while calling init function %s", name);		      \
		return FALSE;                                                 \
	}                                                                     \
} while(0)



int NVAccelInit2D_NV50(struct nouveau_info *info)
{

  struct nouveau_pushbuf *push = info->pushbuf;
	struct nv04_fifo *fifo = info->chan->data;

	if (nouveau_object_new(info->chan, Nv2D, NV50_2D_CLASS,
			       NULL, 0, &info->Nv2D))
		return FALSE;

	if (!PUSH_SPACE(push, 64))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(2D, OBJECT), 1);
	PUSH_DATA (push, info->Nv2D->handle);
	BEGIN_NV04(push, NV50_2D(DMA_NOTIFY), 3);
	PUSH_DATA (push, info->notify0->handle);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, fifo->vram);

	/* Magics from nv, no clue what they do, but at least some
	 * of them are needed to avoid crashes.
	 */
	BEGIN_NV04(push, SUBC_2D(0x0260), 1);
	PUSH_DATA (push, 1);
	BEGIN_NV04(push, NV50_2D(CLIP_ENABLE), 1);
	PUSH_DATA (push, 1);
	BEGIN_NV04(push, NV50_2D(COLOR_KEY_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, SUBC_2D(0x058c), 1);
	PUSH_DATA (push, 0x111);

	info->currentRop = 0xfffffffa;
	return TRUE;
}

int NVAccelInitM2MF_NV50(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;
	struct nv04_fifo *fifo = info->chan->data;

	if (nouveau_object_new(info->chan, NvMemFormat, NV50_M2MF_CLASS,
			       NULL, 0, &info->NvMemFormat))
		return FALSE;

	if (!PUSH_SPACE(push, 8))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(M2MF, OBJECT), 1);
	PUSH_DATA (push, info->NvMemFormat->handle);
	BEGIN_NV04(push, NV03_M2MF(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->notify0->handle);
	BEGIN_NV04(push, NV03_M2MF(DMA_BUFFER_IN), 2);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, fifo->vram);
	return TRUE;
}


int NVAccelInitNV50TCL(struct nouveau_info *info)
{
	struct nv04_fifo *fifo = info->chan->data;
	struct nouveau_pushbuf *push = info->pushbuf;
	struct nv04_notify ntfy = { .length = 32 };
	unsigned class;
	int i;

	switch (info->dev->chipset & 0xf0) {
	case 0x50:
		class = NV50_3D_CLASS;
		break;
	case 0x80:
	case 0x90:
		class = NV84_3D_CLASS;
		break;
	case 0xa0:
		switch (info->dev->chipset) {
		case 0xa0:
		case 0xaa:
		case 0xac:
			class = NVA0_3D_CLASS;
			break;
		case 0xaf:
			class = NVAF_3D_CLASS;
			break;
		default:
			class = NVA3_3D_CLASS;
			break;
		}
		break;
	default:
		return FALSE;
	}

	if (nouveau_object_new(info->chan, Nv3D, class, NULL, 0, &info->Nv3D))
		return FALSE;

	if (nouveau_object_new(info->chan, NvSW, 0x506e, NULL, 0, &info->NvSW)) {
		nouveau_object_del(&info->Nv3D);
		return FALSE;
	}

	if (nouveau_object_new(info->chan, NvVBlankSem, NOUVEAU_NOTIFIER_CLASS,
			       &ntfy, sizeof(ntfy), &info->vblank_sem)) {
		nouveau_object_del(&info->NvSW);
		nouveau_object_del(&info->Nv3D);
		return FALSE;
	}

	if (nouveau_pushbuf_space(push, 512, 0, 0) ||
	    nouveau_pushbuf_refn (push, &(struct nouveau_pushbuf_refn) {
					info->scratch, NOUVEAU_BO_VRAM |
					NOUVEAU_BO_WR }, 1))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(NVSW, OBJECT), 1);
	PUSH_DATA (push, info->NvSW->handle);
	BEGIN_NV04(push, SUBC_NVSW(0x018c), 1);
	PUSH_DATA (push, info->vblank_sem->handle);
	BEGIN_NV04(push, SUBC_NVSW(0x0400), 1);
	PUSH_DATA (push, 0);

	BEGIN_NV04(push, NV01_SUBC(3D, OBJECT), 1);
	PUSH_DATA (push, info->Nv3D->handle);
	BEGIN_NV04(push, NV50_3D(COND_MODE), 1);
	PUSH_DATA (push, NV50_3D_COND_MODE_ALWAYS);
	BEGIN_NV04(push, NV50_3D(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->NvNull->handle);
	BEGIN_NV04(push, NV50_3D(DMA_ZETA), 11);
	for (i = 0; i < 11; i++)
		PUSH_DATA (push, fifo->vram);
	BEGIN_NV04(push, NV50_3D(DMA_COLOR(0)), NV50_3D_DMA_COLOR__LEN);
	for (i = 0; i < NV50_3D_DMA_COLOR__LEN; i++)
		PUSH_DATA (push, fifo->vram);
	BEGIN_NV04(push, NV50_3D(RT_CONTROL), 1);
	PUSH_DATA (push, 1);

	BEGIN_NV04(push, NV50_3D(VIEWPORT_TRANSFORM_EN), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, SUBC_3D(0x0f90), 1);
	PUSH_DATA (push, 1);

	BEGIN_NV04(push, NV50_3D(TIC_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (info->scratch->offset + TIC_OFFSET_NV50) >> 32);
	PUSH_DATA (push, (info->scratch->offset + TIC_OFFSET_NV50));
	PUSH_DATA (push, 0x00000800);
	BEGIN_NV04(push, NV50_3D(TSC_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (info->scratch->offset + TSC_OFFSET_NV50) >> 32);
	PUSH_DATA (push, (info->scratch->offset + TSC_OFFSET_NV50));
	PUSH_DATA (push, 0x00000000);
	BEGIN_NV04(push, NV50_3D(LINKED_TSC), 1);
	PUSH_DATA (push, 1);
	BEGIN_NV04(push, NV50_3D(TEX_LIMITS(2)), 1);
	PUSH_DATA (push, 0x54);

	PUSH_DATAu (push, info->scratch, PVP_OFFSET, 30 * 2);
	PUSH_DATA (push, 0x10000001);
	PUSH_DATA (push, 0x0423c788);
	PUSH_DATA (push, 0x10000205);
	PUSH_DATA (push, 0x0423c788);
	PUSH_DATA (push, 0xc0800401);
	PUSH_DATA (push, 0x00200780);
	PUSH_DATA (push, 0xc0830405);
	PUSH_DATA (push, 0x00200780);
	PUSH_DATA (push, 0xc0860409);
	PUSH_DATA (push, 0x00200780);
	PUSH_DATA (push, 0xe0810601);
	PUSH_DATA (push, 0x00200780);
	PUSH_DATA (push, 0xe0840605);
	PUSH_DATA (push, 0x00204780);
	PUSH_DATA (push, 0xe0870609);
	PUSH_DATA (push, 0x00208780);
	PUSH_DATA (push, 0xb1000001);
	PUSH_DATA (push, 0x00008780);
	PUSH_DATA (push, 0xb1000205);
	PUSH_DATA (push, 0x00014780);
	PUSH_DATA (push, 0xb1000409);
	PUSH_DATA (push, 0x00020780);
	PUSH_DATA (push, 0x90000409);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0xc0020001);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0xc0020205);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0xc0890009);
	PUSH_DATA (push, 0x00000788);
	PUSH_DATA (push, 0xc08a020d);
	PUSH_DATA (push, 0x00000788);
	PUSH_DATA (push, 0xc08b0801);
	PUSH_DATA (push, 0x00200780);
	PUSH_DATA (push, 0xc08e0805);
	PUSH_DATA (push, 0x00200780);
	PUSH_DATA (push, 0xc0910809);
	PUSH_DATA (push, 0x00200780);
	PUSH_DATA (push, 0xe08c0a01);
	PUSH_DATA (push, 0x00200780);
	PUSH_DATA (push, 0xe08f0a05);
	PUSH_DATA (push, 0x00204780);
	PUSH_DATA (push, 0xe0920a09);
	PUSH_DATA (push, 0x00208780);
	PUSH_DATA (push, 0xb1000001);
	PUSH_DATA (push, 0x00034780);
	PUSH_DATA (push, 0xb1000205);
	PUSH_DATA (push, 0x00040780);
	PUSH_DATA (push, 0xb1000409);
	PUSH_DATA (push, 0x0004c780);
	PUSH_DATA (push, 0x90000409);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0xc0020001);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0xc0020205);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0xc0940011);
	PUSH_DATA (push, 0x00000788);
	PUSH_DATA (push, 0xc0950215);
	PUSH_DATA (push, 0x00000789);

	/* fetch only VTX_ATTR[0,8,9].xy */
	BEGIN_NV04(push, NV50_3D(VP_ATTR_EN(0)), 2);
	PUSH_DATA (push, 0x00000003);
	PUSH_DATA (push, 0x00000033);
	BEGIN_NV04(push, NV50_3D(VP_REG_ALLOC_RESULT), 1);
	PUSH_DATA (push, 6);
	BEGIN_NV04(push, NV50_3D(VP_RESULT_MAP_SIZE), 2);
	PUSH_DATA (push, 8);
	PUSH_DATA (push, 4); /* NV50_3D_VP_REG_ALLOC_TEMP */
	BEGIN_NV04(push, NV50_3D(VP_ADDRESS_HIGH), 2);
	PUSH_DATA (push, (info->scratch->offset + PVP_OFFSET) >> 32);
	PUSH_DATA (push, (info->scratch->offset + PVP_OFFSET));
	BEGIN_NV04(push, NV50_3D(CB_DEF_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (info->scratch->offset + PVP_DATA_NV50) >> 32);
	PUSH_DATA (push, (info->scratch->offset + PVP_DATA_NV50));
	PUSH_DATA (push, (CB_PVP << NV50_3D_CB_DEF_SET_BUFFER__SHIFT) | 256);
	BEGIN_NV04(push, NV50_3D(SET_PROGRAM_CB), 1);
	PUSH_DATA (push, 0x00000001 | (CB_PVP << 12));
	BEGIN_NV04(push, NV50_3D(VP_START_ID), 1);
	PUSH_DATA (push, 0);

	PUSH_DATAu(push, info->scratch, PFP_OFFSET + PFP_S_NV50, 6);
	PUSH_DATA (push, 0x80000000);
	PUSH_DATA (push, 0x90000004);
	PUSH_DATA (push, 0x82010200);
	PUSH_DATA (push, 0x82020204);
	PUSH_DATA (push, 0xf6400001);
	PUSH_DATA (push, 0x0000c785);
	PUSH_DATAu(push, info->scratch, PFP_OFFSET + PFP_C_NV50, 16);
	PUSH_DATA (push, 0x80000000);
	PUSH_DATA (push, 0x90000004);
	PUSH_DATA (push, 0x82030210);
	PUSH_DATA (push, 0x82040214);
	PUSH_DATA (push, 0x82010200);
	PUSH_DATA (push, 0x82020204);
	PUSH_DATA (push, 0xf6400001);
	PUSH_DATA (push, 0x0000c784);
	PUSH_DATA (push, 0xf0400211);
	PUSH_DATA (push, 0x00008784);
	PUSH_DATA (push, 0xc0040000);
	PUSH_DATA (push, 0xc0040204);
	PUSH_DATA (push, 0xc0040409);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0xc004060d);
	PUSH_DATA (push, 0x00000781);
	PUSH_DATAu(push, info->scratch, PFP_OFFSET + PFP_CCA_NV50, 16);
	PUSH_DATA (push, 0x80000000);
	PUSH_DATA (push, 0x90000004);
	PUSH_DATA (push, 0x82030210);
	PUSH_DATA (push, 0x82040214);
	PUSH_DATA (push, 0x82010200);
	PUSH_DATA (push, 0x82020204);
	PUSH_DATA (push, 0xf6400001);
	PUSH_DATA (push, 0x0000c784);
	PUSH_DATA (push, 0xf6400211);
	PUSH_DATA (push, 0x0000c784);
	PUSH_DATA (push, 0xc0040000);
	PUSH_DATA (push, 0xc0050204);
	PUSH_DATA (push, 0xc0060409);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0xc007060d);
	PUSH_DATA (push, 0x00000781);
	PUSH_DATAu(push, info->scratch, PFP_OFFSET + PFP_CCASA_NV50, 16);
	PUSH_DATA (push, 0x80000000);
	PUSH_DATA (push, 0x90000004);
	PUSH_DATA (push, 0x82030200);
	PUSH_DATA (push, 0x82040204);
	PUSH_DATA (push, 0x82010210);
	PUSH_DATA (push, 0x82020214);
	PUSH_DATA (push, 0xf6400201);
	PUSH_DATA (push, 0x0000c784);
	PUSH_DATA (push, 0xf0400011);
	PUSH_DATA (push, 0x00008784);
	PUSH_DATA (push, 0xc0040000);
	PUSH_DATA (push, 0xc0040204);
	PUSH_DATA (push, 0xc0040409);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0xc004060d);
	PUSH_DATA (push, 0x00000781);
	PUSH_DATAu(push, info->scratch, PFP_OFFSET + PFP_S_A8_NV50, 10);
	PUSH_DATA (push, 0x80000000);
	PUSH_DATA (push, 0x90000004);
	PUSH_DATA (push, 0x82010200);
	PUSH_DATA (push, 0x82020204);
	PUSH_DATA (push, 0xf0400001);
	PUSH_DATA (push, 0x00008784);
	PUSH_DATA (push, 0x10008004);
	PUSH_DATA (push, 0x10008008);
	PUSH_DATA (push, 0x1000000d);
	PUSH_DATA (push, 0x0403c781);
	PUSH_DATAu(push, info->scratch, PFP_OFFSET + PFP_C_A8_NV50, 16);
	PUSH_DATA (push, 0x80000000);
	PUSH_DATA (push, 0x90000004);
	PUSH_DATA (push, 0x82030208);
	PUSH_DATA (push, 0x8204020c);
	PUSH_DATA (push, 0x82010200);
	PUSH_DATA (push, 0x82020204);
	PUSH_DATA (push, 0xf0400001);
	PUSH_DATA (push, 0x00008784);
	PUSH_DATA (push, 0xf0400209);
	PUSH_DATA (push, 0x00008784);
	PUSH_DATA (push, 0xc002000d);
	PUSH_DATA (push, 0x00000780);
	PUSH_DATA (push, 0x10008600);
	PUSH_DATA (push, 0x10008604);
	PUSH_DATA (push, 0x10000609);
	PUSH_DATA (push, 0x0403c781);
	PUSH_DATAu(push, info->scratch, PFP_OFFSET + PFP_NV12_NV50, 24);
	PUSH_DATA (push, 0x80000008);
	PUSH_DATA (push, 0x90000408);
	PUSH_DATA (push, 0x82010400);
	PUSH_DATA (push, 0x82020404);
	PUSH_DATA (push, 0xf0400001);
	PUSH_DATA (push, 0x00008784);
	PUSH_DATA (push, 0xc0800014);
	PUSH_DATA (push, 0xb0810a0c);
	PUSH_DATA (push, 0xb0820a10);
	PUSH_DATA (push, 0xb0830a14);
	PUSH_DATA (push, 0x82010400);
	PUSH_DATA (push, 0x82020404);
	PUSH_DATA (push, 0xf0400201);
	PUSH_DATA (push, 0x0000c784);
	PUSH_DATA (push, 0xe084000c);
	PUSH_DATA (push, 0xe0850010);
	PUSH_DATA (push, 0xe0860015);
	PUSH_DATA (push, 0x00014780);
	PUSH_DATA (push, 0xe0870201);
	PUSH_DATA (push, 0x0000c780);
	PUSH_DATA (push, 0xe0890209);
	PUSH_DATA (push, 0x00014780);
	PUSH_DATA (push, 0xe0880205);
	PUSH_DATA (push, 0x00010781);

	/* HPOS.xy = ($o0, $o1), HPOS.zw = (0.0, 1.0), then map $o2 - $o5 */
	BEGIN_NV04(push, NV50_3D(VP_RESULT_MAP(0)), 2);
	PUSH_DATA (push, 0x41400100);
	PUSH_DATA (push, 0x05040302);
	BEGIN_NV04(push, NV50_3D(POINT_SPRITE_ENABLE), 1);
	PUSH_DATA (push, 0x00000000);
	BEGIN_NV04(push, NV50_3D(FP_INTERPOLANT_CTRL), 2);
	PUSH_DATA (push, 0x08040404);
	PUSH_DATA (push, 0x00000008); /* NV50_3D_FP_REG_ALLOC_TEMP */
	BEGIN_NV04(push, NV50_3D(FP_ADDRESS_HIGH), 2);
	PUSH_DATA (push, (info->scratch->offset + PFP_OFFSET) >> 32);
	PUSH_DATA (push, (info->scratch->offset + PFP_OFFSET));
	BEGIN_NV04(push, NV50_3D(CB_DEF_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (info->scratch->offset + PFP_DATA_NV50) >> 32);
	PUSH_DATA (push, (info->scratch->offset + PFP_DATA_NV50));
	PUSH_DATA (push, (CB_PFP << NV50_3D_CB_DEF_SET_BUFFER__SHIFT) | 256);
	BEGIN_NV04(push, NV50_3D(SET_PROGRAM_CB), 1);
	PUSH_DATA (push, 0x00000031 | (CB_PFP << 12));

	BEGIN_NV04(push, NV50_3D(SCISSOR_ENABLE(0)), 1);
	PUSH_DATA (push, 1);

	BEGIN_NV04(push, NV50_3D(VIEWPORT_HORIZ(0)), 2);
	PUSH_DATA (push, 8192 << NV50_3D_VIEWPORT_HORIZ_W__SHIFT);
	PUSH_DATA (push, 8192 << NV50_3D_VIEWPORT_VERT_H__SHIFT);
	/* NV50_3D_SCISSOR_VERT_T_SHIFT is wrong, because it was deducted with
	 * origin lying at the bottom left. This will be changed to _MIN_ and _MAX_
	 * later, because it is origin dependent.
	 */
	BEGIN_NV04(push, NV50_3D(SCISSOR_HORIZ(0)), 2);
	PUSH_DATA (push, 8192 << NV50_3D_SCISSOR_HORIZ_MAX__SHIFT);
	PUSH_DATA (push, 8192 << NV50_3D_SCISSOR_VERT_MAX__SHIFT);
	BEGIN_NV04(push, NV50_3D(SCREEN_SCISSOR_HORIZ), 2);
	PUSH_DATA (push, 8192 << NV50_3D_SCREEN_SCISSOR_HORIZ_W__SHIFT);
	PUSH_DATA (push, 8192 << NV50_3D_SCREEN_SCISSOR_VERT_H__SHIFT);

	return TRUE;
}



int NVAccelInit2D_NVC0(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;
	int ret;

	ret = nouveau_object_new(info->chan, 0x0000902d, 0x902d,
				 NULL, 0, &info->Nv2D);
	if (ret)
		return FALSE;

	if (!PUSH_SPACE(push, 64))
		return FALSE;

	BEGIN_NVC0(push, NV01_SUBC(2D, OBJECT), 1);
	PUSH_DATA (push, info->Nv2D->handle);

	BEGIN_NVC0(push, NV50_2D(CLIP_ENABLE), 1);
	PUSH_DATA (push, 1);
	BEGIN_NVC0(push, NV50_2D(COLOR_KEY_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NVC0(push, SUBC_2D(0x0884), 1);
	PUSH_DATA (push, 0x3f);
	BEGIN_NVC0(push, SUBC_2D(0x0888), 1);
	PUSH_DATA (push, 1);
	BEGIN_NVC0(push, NV50_2D(ROP), 1);
	PUSH_DATA (push, 0x55);
	BEGIN_NVC0(push, NV50_2D(OPERATION), 1);
	PUSH_DATA (push, NV50_2D_OPERATION_SRCCOPY);

	BEGIN_NVC0(push, NV50_2D(BLIT_DU_DX_FRACT), 4);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 1);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 1);
	BEGIN_NVC0(push, NV50_2D(DRAW_SHAPE), 2);
	PUSH_DATA (push, 4);
	PUSH_DATA (push, NV50_SURFACE_FORMAT_B5G6R5_UNORM);
	BEGIN_NVC0(push, NV50_2D(PATTERN_COLOR_FORMAT), 2);
	PUSH_DATA (push, 2);
	PUSH_DATA (push, 1);

	info->currentRop = 0xfffffffa;
	return TRUE;
}

int NVAccelInitM2MF_NVC0(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;
	int ret;

	ret = nouveau_object_new(info->chan, 0x00009039, 0x9039,
				 NULL, 0, &info->NvMemFormat);
	if (ret)
		return FALSE;

	BEGIN_NVC0(push, NV01_SUBC(M2MF, OBJECT), 1);
	PUSH_DATA (push, info->NvMemFormat->handle);
	BEGIN_NVC0(push, NVC0_M2MF(QUERY_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (info->scratch->offset + NTFY_OFFSET) >> 32);
	PUSH_DATA (push, (info->scratch->offset + NTFY_OFFSET));
	PUSH_DATA (push, 0);

	return TRUE;
}

int NVAccelInitP2MF_NVE0(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;
	uint32_t class = (info->dev->chipset < 0xf0) ? 0xa040 : 0xa140;
	int ret;

	ret = nouveau_object_new(info->chan, class, class, NULL, 0,
				&info->NvMemFormat);
	if (ret)
		return FALSE;

	BEGIN_NVC0(push, NV01_SUBC(P2MF, OBJECT), 1);
	PUSH_DATA (push, info->NvMemFormat->handle);
	return TRUE;
}

int NVAccelInitCOPY_NVE0(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;
	int ret;

	ret = nouveau_object_new(info->chan, 0x0000a0b5, 0xa0b5,
				 NULL, 0, &info->NvCOPY);
	if (ret)
		return FALSE;

	BEGIN_NVC0(push, NV01_SUBC(COPY_NVC0, OBJECT), 1);
	PUSH_DATA (push, info->NvCOPY->handle);
	return TRUE;
}

int NVAccelInitNV40TCL(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;
	struct nv04_fifo *fifo = info->chan->data;
	uint32_t class = 0, chipset;
	int i;

	NVXVComputeBicubicFilter(info->scratch, XV_TABLE, XV_TABLE_SIZE);

	chipset = info->dev->chipset;
	if ((chipset & 0xf0) == NV_ARCH_40) {
		chipset &= 0xf;
		if (NV30_3D_CHIPSET_4X_MASK & (1<<chipset))
			class = NV40_3D_CLASS;
		else if (NV44TCL_CHIPSET_4X_MASK & (1<<chipset))
			class = NV44_3D_CLASS;
		else {
			ALOGE("NV40EXA: Unknown chipset NV4%1x\n", chipset);
			return FALSE;
		}
	} else if ( (chipset & 0xf0) == 0x60) {
		class = NV44_3D_CLASS;
	} else
		return TRUE;

	if (nouveau_object_new(info->chan, Nv3D, class, NULL, 0, &info->Nv3D))
		return FALSE;

	if (!PUSH_SPACE(push, 256))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(3D, OBJECT), 1);
	PUSH_DATA (push, info->Nv3D->handle);
	BEGIN_NV04(push, NV30_3D(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->notify0->handle);
	BEGIN_NV04(push, NV30_3D(DMA_TEXTURE0), 2);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, fifo->gart);
	BEGIN_NV04(push, NV30_3D(DMA_COLOR0), 2);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, fifo->vram);

	/* voodoo */
	BEGIN_NV04(push, SUBC_3D(0x1ea4), 3);
	PUSH_DATA (push, 0x00000010);
	PUSH_DATA (push, 0x01000100);
	PUSH_DATA (push, 0xff800006);
	BEGIN_NV04(push, SUBC_3D(0x1fc4), 1);
	PUSH_DATA (push, 0x06144321);
	BEGIN_NV04(push, SUBC_3D(0x1fc8), 2);
	PUSH_DATA (push, 0xedcba987);
	PUSH_DATA (push, 0x00000021);
	BEGIN_NV04(push, SUBC_3D(0x1fd0), 1);
	PUSH_DATA (push, 0x00171615);
	BEGIN_NV04(push, SUBC_3D(0x1fd4), 1);
	PUSH_DATA (push, 0x001b1a19);
	BEGIN_NV04(push, SUBC_3D(0x1ef8), 1);
	PUSH_DATA (push, 0x0020ffff);
	BEGIN_NV04(push, SUBC_3D(0x1d64), 1);
	PUSH_DATA (push, 0x00d30000);
	BEGIN_NV04(push, SUBC_3D(0x1e94), 1);
	PUSH_DATA (push, 0x00000001);

	/* This removes the the stair shaped tearing that i get. */
	/* Verified on one G70 card that it doesn't cause regressions for people without the problem. */
	/* The blob sets this up by default for NV43. */
	BEGIN_NV04(push, SUBC_3D(0x1450), 1);
	PUSH_DATA (push, 0x0000000F);

	BEGIN_NV04(push, NV30_3D(VIEWPORT_TRANSLATE_X), 8);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 0.0);

	/* default 3D state */
	/*XXX: replace with the same state that the DRI emits on startup */
	BEGIN_NV04(push, NV30_3D(STENCIL_ENABLE(0)), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(STENCIL_ENABLE(1)), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(ALPHA_FUNC_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(DEPTH_WRITE_ENABLE), 2);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(COLOR_MASK), 1);
	PUSH_DATA (push, 0x01010101); /* TR,TR,TR,TR */
	BEGIN_NV04(push, NV30_3D(CULL_FACE_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(BLEND_FUNC_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(COLOR_LOGIC_OP_ENABLE), 2);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, NV30_3D_COLOR_LOGIC_OP_OP_COPY);
	BEGIN_NV04(push, NV30_3D(DITHER_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(SHADE_MODEL), 1);
	PUSH_DATA (push, NV30_3D_SHADE_MODEL_SMOOTH);
	BEGIN_NV04(push, NV30_3D(POLYGON_OFFSET_FACTOR),2);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	BEGIN_NV04(push, NV30_3D(POLYGON_MODE_FRONT), 2);
	PUSH_DATA (push, NV30_3D_POLYGON_MODE_FRONT_FILL);
	PUSH_DATA (push, NV30_3D_POLYGON_MODE_BACK_FILL);
	BEGIN_NV04(push, NV30_3D(POLYGON_STIPPLE_PATTERN(0)), 0x20);
	for (i=0;i<0x20;i++)
		PUSH_DATA (push, 0xFFFFFFFF);
	for (i=0;i<16;i++) {
		BEGIN_NV04(push, NV30_3D(TEX_ENABLE(i)), 1);
		PUSH_DATA (push, 0);
	}

	BEGIN_NV04(push, SUBC_3D(0x1d78), 1);
	PUSH_DATA (push, 0x110);

	BEGIN_NV04(push, NV30_3D(RT_ENABLE), 1);
	PUSH_DATA (push, NV30_3D_RT_ENABLE_COLOR0);

	BEGIN_NV04(push, NV30_3D(RT_HORIZ), 2);
	PUSH_DATA (push, (4096 << 16));
	PUSH_DATA (push, (4096 << 16));
	BEGIN_NV04(push, NV30_3D(SCISSOR_HORIZ), 2);
	PUSH_DATA (push, (4096 << 16));
	PUSH_DATA (push, (4096 << 16));
	BEGIN_NV04(push, NV30_3D(VIEWPORT_HORIZ), 2);
	PUSH_DATA (push, (4096 << 16));
	PUSH_DATA (push, (4096 << 16));
	BEGIN_NV04(push, NV30_3D(VIEWPORT_CLIP_HORIZ(0)), 2);
	PUSH_DATA (push, (4095 << 16));
	PUSH_DATA (push, (4095 << 16));

	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_FROM_ID), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x401f9c6c); /* mov o[hpos], a[0] */
	PUSH_DATA (push, 0x0040000d);
	PUSH_DATA (push, 0x8106c083);
	PUSH_DATA (push, 0x6041ef80);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x00001c6c); /* mov r0.xyw, a[8].xyww */
	PUSH_DATA (push, 0x0040080f);
	PUSH_DATA (push, 0x8106c083);
	PUSH_DATA (push, 0x6041affc);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x00009c6c); /* dp3 r1.x, r0.xyw, c[0].xyz */
	PUSH_DATA (push, 0x0140000f);
	PUSH_DATA (push, 0x808680c3);
	PUSH_DATA (push, 0x60410ffc);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x00009c6c); /* dp3 r1.y, r0.xyw, c[1].xyz */
	PUSH_DATA (push, 0x0140100f);
	PUSH_DATA (push, 0x808680c3);
	PUSH_DATA (push, 0x60408ffc);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x00009c6c); /* dp3 r1.w, r0.xyw, c[2].xyz */
	PUSH_DATA (push, 0x0140200f);
	PUSH_DATA (push, 0x808680c3);
	PUSH_DATA (push, 0x60402ffc);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x401f9c6c); /* mul o[tex0].xyw, r1, c[3] */
	PUSH_DATA (push, 0x0080300d);
	PUSH_DATA (push, 0x8286c0c3);
	PUSH_DATA (push, 0x6041af9c);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x00001c6c); /* mov r0.xyw, a[9].xyww */
	PUSH_DATA (push, 0x0040090f);
	PUSH_DATA (push, 0x8106c083);
	PUSH_DATA (push, 0x6041affc);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x00009c6c); /* dp3 r1.x, r0.xyw, c[4].xyz */
	PUSH_DATA (push, 0x0140400f);
	PUSH_DATA (push, 0x808680c3);
	PUSH_DATA (push, 0x60410ffc);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x00009c6c); /* dp3 r1.y, r0.xyw, c[5].xyz */
	PUSH_DATA (push, 0x0140500f);
	PUSH_DATA (push, 0x808680c3);
	PUSH_DATA (push, 0x60408ffc);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x00009c6c); /* dp3 r1.w, r0.xyw, c[6].xyz */
	PUSH_DATA (push, 0x0140600f);
	PUSH_DATA (push, 0x808680c3);
	PUSH_DATA (push, 0x60402ffc);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x401f9c6c); /* exit mul o[tex1].xyw, r1, c[4] */
	PUSH_DATA (push, 0x0080700d);
	PUSH_DATA (push, 0x8286c0c3);
	PUSH_DATA (push, 0x6041afa1);
	BEGIN_NV04(push, NV30_3D(VP_UPLOAD_INST(0)), 4);
	PUSH_DATA (push, 0x00000000); /* exit */
	PUSH_DATA (push, 0x00000000);
	PUSH_DATA (push, 0x00000000);
	PUSH_DATA (push, 0x00000001);
	BEGIN_NV04(push, NV30_3D(VP_START_FROM_ID), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV40_3D(VP_ATTRIB_EN), 2);
	PUSH_DATA (push, 0x00000309);
	PUSH_DATA (push, 0x0000c001);

	PUSH_DATAu(push, info->scratch, PFP_PASS, 1 * 4);
	PUSH_DATAs(push, 0x01403e81); /* mov r0, a[col0] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);

	PUSH_DATAu(push, info->scratch, PFP_S_NV04, 2 * 4);
	PUSH_DATAs(push, 0x18009e00); /* txp r0, a[tex0], t[0] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x01401e81); /* mov r0, r0 */
	PUSH_DATAs(push, 0x1c9dc800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);

	PUSH_DATAu(push, info->scratch, PFP_S_A8_NV04, 2 * 4);
	PUSH_DATAs(push, 0x18009000); /* txp r0.w, a[tex0], t[0] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x01401e81); /* mov r0, r0.w */
	PUSH_DATAs(push, 0x1c9dfe00);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);

	PUSH_DATAu(push, info->scratch, PFP_C_NV04, 3 * 4);
	PUSH_DATAs(push, 0x1802b102); /* txpc0 r1.w, a[tex1], t[1] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x18009e00); /* txp r0 (ne0.w), a[tex0], t[0] */
	PUSH_DATAs(push, 0x1ff5c801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x02001e81); /* mul r0, r0, r1.w */
	PUSH_DATAs(push, 0x1c9dc800);
	PUSH_DATAs(push, 0x0001fe04);
	PUSH_DATAs(push, 0x0001c800);

	PUSH_DATAu(push, info->scratch, PFP_C_A8_NV04, 3 * 4);
	PUSH_DATAs(push, 0x1802b102); /* txpc0 r1.w, a[tex1], t[1] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x18009000); /* txp r0.w (ne0.w), a[tex0], t[0] */
	PUSH_DATAs(push, 0x1ff5c801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x02001e81); /* mul r0, r0.w, r1.w */
	PUSH_DATAs(push, 0x1c9dfe00);
	PUSH_DATAs(push, 0x0001fe04);
	PUSH_DATAs(push, 0x0001c800);

	PUSH_DATAu(push, info->scratch, PFP_CCA_NV04, 3 * 4);
	PUSH_DATAs(push, 0x18009f00); /* txpc0 r0, a[tex0], t[0] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x1802be02); /* txp r1 (ne0), a[tex1], t[1] */
	PUSH_DATAs(push, 0x1c95c801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x02001e81); /* mul r0, r0, r1 */
	PUSH_DATAs(push, 0x1c9dc800);
	PUSH_DATAs(push, 0x0001c804);
	PUSH_DATAs(push, 0x0001c800);

	PUSH_DATAu(push, info->scratch, PFP_CCASA_NV04, 3 * 4);
	PUSH_DATAs(push, 0x18009102); /* txpc0 r1.w, a[tex0], t[0] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x1802be00); /* txp r0 (ne0.w), a[tex1], t[1] */
	PUSH_DATAs(push, 0x1ff5c801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x02001e81); /* mul r0, r1.w, r0 */
	PUSH_DATAs(push, 0x1c9dfe04);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);

	PUSH_DATAu(push, info->scratch, PFP_NV12_BILINEAR, 8 * 4);
	PUSH_DATAs(push, 0x17028200); /* texr r0.x, a[tex0], t[1] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x04000e02); /* madr r1.xyz, r0.x, imm.x, imm.yzww */
	PUSH_DATAs(push, 0x1c9c0000);
	PUSH_DATAs(push, 0x00000002);
	PUSH_DATAs(push, 0x0001f202);
	PUSH_DATAs(push, 0x3f9507c8); /* { 1.16, -0.87, 0.53, -1.08 } */
	PUSH_DATAs(push, 0xbf5ee393);
	PUSH_DATAs(push, 0x3f078fef);
	PUSH_DATAs(push, 0xbf8a6762);
	PUSH_DATAs(push, 0x1704ac80); /* texr r0.yz, a[tex1], t[2] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x04000e02); /* madr r1.xyz, r0.y, imm, r1 */
	PUSH_DATAs(push, 0x1c9cab00);
	PUSH_DATAs(push, 0x0001c802);
	PUSH_DATAs(push, 0x0001c804);
	PUSH_DATAs(push, 0x00000000); /* { 0.00, -0.39, 2.02, 0.00 } */
	PUSH_DATAs(push, 0xbec890d6);
	PUSH_DATAs(push, 0x40011687);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x04000e81); /* madr r0.xyz, r0.z, imm, r1 */
	PUSH_DATAs(push, 0x1c9d5500);
	PUSH_DATAs(push, 0x0001c802);
	PUSH_DATAs(push, 0x0001c804);
	PUSH_DATAs(push, 0x3fcc432d); /* { 1.60, -0.81, 0.00, 0.00 } */
	PUSH_DATAs(push, 0xbf501a37);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);


	PUSH_DATAu(push, info->scratch, PFP_NV12_BICUBIC, 29 * 4);
	PUSH_DATAs(push, 0x01008600); /* movr r0.xy, a[tex0] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x03000800); /* addr r0.z, r0.y, imm.x */
	PUSH_DATAs(push, 0x1c9caa00);
	PUSH_DATAs(push, 0x00000002);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3f000000); /* { 0.50, 0.00, 0.00, 0.00 } */
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x03000202); /* addr r1.x, r0, imm.x */
	PUSH_DATAs(push, 0x1c9dc800);
	PUSH_DATAs(push, 0x00000002);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3f000000); /* { 0.50, 0.00, 0.00, 0.00 } */
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x17000f82); /* texrc0 r1.xyz, r0.z, t[0] */
	PUSH_DATAs(push, 0x1c9d5400);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x02001404); /* mulr r2.yw, r1.xxyy, imm.xxyy */
	PUSH_DATAs(push, 0x1c9ca104);
	PUSH_DATAs(push, 0x0000a002);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0xbf800000); /* { -1.00, 1.00, 0.00, 0.00 } */
	PUSH_DATAs(push, 0x3f800000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x17000e86); /* texr r3.xyz, r1, t[0] */
	PUSH_DATAs(push, 0x1c9dc804);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x02000a04); /* mulr r2.xz, r3.xxyy, imm.xxyy */
	PUSH_DATAs(push, 0x1c9ca10c);
	PUSH_DATAs(push, 0x0000a002);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0xbf800000); /* { -1.00, 1.00, 0.00, 0.00 } */
	PUSH_DATAs(push, 0x3f800000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x03001e04); /* addr r2, r0.xyxy, r2 */
	PUSH_DATAs(push, 0x1c9c8800);
	PUSH_DATAs(push, 0x0001c808);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x17020402); /* texr r1.y, r2.zwzz, -t[1] */
	PUSH_DATAs(push, 0x1c9d5c08);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x04400282); /* madh r1.x, -r1.z, r1.y, r1.y */
	PUSH_DATAs(push, 0x1c9f5504);
	PUSH_DATAs(push, 0x0000aa04);
	PUSH_DATAs(push, 0x0000aa04);
	PUSH_DATAs(push, 0x17020400); /* texr r0.y, r2.xwxw, -t[1] */
	PUSH_DATAs(push, 0x1c9d9808);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x04401080); /* madh r0.w, -r1.z, r0.y, r0.y */
	PUSH_DATAs(push, 0x1c9f5504);
	PUSH_DATAs(push, 0x0000aa00);
	PUSH_DATAs(push, 0x0000aa00);
	PUSH_DATAs(push, 0x17020200); /* texr r0.x, r2.zyxy, t[1] */
	PUSH_DATAs(push, 0x1c9c8c08);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x04400282); /* madh r1.x, r1.z, r0, r1 */
	PUSH_DATAs(push, 0x1c9d5504);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c904);
	PUSH_DATAs(push, 0x17020200); /* texr r0.x (NE0.z), r2, t[1] */
	PUSH_DATAs(push, 0x1555c808);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x04400280); /* madh r0.x, r1.z, r0, r0.w */
	PUSH_DATAs(push, 0x1c9d5504);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001ff00);
	PUSH_DATAs(push, 0x04401080); /* madh r0.w, -r3.z, r1.x, r1.x */
	PUSH_DATAs(push, 0x1c9f550c);
	PUSH_DATAs(push, 0x00000104);
	PUSH_DATAs(push, 0x00000104);
	PUSH_DATAs(push, 0x1704ac80); /* texr r0.yz, a[tex1], t[2] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x04400280); /* madh r0.x, r3.z, r0, r0.w */
	PUSH_DATAs(push, 0x1c9d550c);
	PUSH_DATAs(push, 0x0001c900);
	PUSH_DATAs(push, 0x0001ff00);
	PUSH_DATAs(push, 0x04400e82); /* madh r1.xyz, r0.x, imm.x, imm.yzww */
	PUSH_DATAs(push, 0x1c9c0100);
	PUSH_DATAs(push, 0x00000002);
	PUSH_DATAs(push, 0x0001f202);
	PUSH_DATAs(push, 0x3f9507c8); /* { 1.16, -0.87, 0.53, -1.08 } */
	PUSH_DATAs(push, 0xbf5ee393);
	PUSH_DATAs(push, 0x3f078fef);
	PUSH_DATAs(push, 0xbf8a6762);
	PUSH_DATAs(push, 0x04400e82); /* madh r1.xyz, r0.y, imm, r1 */
	PUSH_DATAs(push, 0x1c9cab00);
	PUSH_DATAs(push, 0x0001c802);
	PUSH_DATAs(push, 0x0001c904);
	PUSH_DATAs(push, 0x00000000); /* { 0.00, -0.39, 2.02, 0.00 } */
	PUSH_DATAs(push, 0xbec890d6);
	PUSH_DATAs(push, 0x40011687);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x04400e81); /* madh r0.xyz, r0.z, imm, r1 */
	PUSH_DATAs(push, 0x1c9d5500);
	PUSH_DATAs(push, 0x0001c802);
	PUSH_DATAs(push, 0x0001c904);
	PUSH_DATAs(push, 0x3fcc432d); /* { 1.60, -0.81, 0.00, 0.00 } */
	PUSH_DATAs(push, 0xbf501a37);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	return TRUE;
}

int NVAccelInitNV30TCL(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;
	struct nv04_fifo *fifo = info->chan->data;
	uint32_t class = 0, chipset;
	int i;

	NVXVComputeBicubicFilter(info->scratch, XV_TABLE, XV_TABLE_SIZE);

#define NV30TCL_CHIPSET_3X_MASK 0x00000003
#define NV35TCL_CHIPSET_3X_MASK 0x000001e0
#define NV30_3D_CHIPSET_3X_MASK 0x00000010

	chipset = info->dev->chipset;
	if ((chipset & 0xf0) != NV_ARCH_30)
		return TRUE;
	chipset &= 0xf;

	if (NV30TCL_CHIPSET_3X_MASK & (1<<chipset))
		class = NV30_3D_CLASS;
	else if (NV35TCL_CHIPSET_3X_MASK & (1<<chipset))
		class = NV35_3D_CLASS;
	else if (NV30_3D_CHIPSET_3X_MASK & (1<<chipset))
		class = NV34_3D_CLASS;
	else {
		ALOGE("NV30EXA: Unknown chipset NV3%1x\n", chipset);
		return FALSE;
	}

	if (nouveau_object_new(info->chan, Nv3D, class, NULL, 0, &info->Nv3D))
		return FALSE;

	if (!PUSH_SPACE(push, 256))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(3D, OBJECT), 1);
	PUSH_DATA (push, info->Nv3D->handle);
	BEGIN_NV04(push, NV30_3D(DMA_TEXTURE0), 3);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, fifo->gart);
	PUSH_DATA (push, fifo->vram);
	BEGIN_NV04(push, NV30_3D(DMA_UNK1AC), 1);
	PUSH_DATA (push, fifo->vram);
	BEGIN_NV04(push, NV30_3D(DMA_COLOR0), 2);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, fifo->vram);
	BEGIN_NV04(push, NV30_3D(DMA_UNK1B0), 1);
	PUSH_DATA (push, fifo->vram);

	for (i=1; i<8; i++) {
		BEGIN_NV04(push, NV30_3D(VIEWPORT_CLIP_HORIZ(i)), 2);
		PUSH_DATA (push, 0);
		PUSH_DATA (push, 0);
	}

	BEGIN_NV04(push, SUBC_3D(0x220), 1);
	PUSH_DATA (push, 1);

	BEGIN_NV04(push, SUBC_3D(0x03b0), 1);
	PUSH_DATA (push, 0x00100000);
	BEGIN_NV04(push, SUBC_3D(0x1454), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, SUBC_3D(0x1d80), 1);
	PUSH_DATA (push, 3);
	BEGIN_NV04(push, SUBC_3D(0x1450), 1);
	PUSH_DATA (push, 0x00030004);

	/* NEW */
	BEGIN_NV04(push, SUBC_3D(0x1e98), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, SUBC_3D(0x17e0), 3);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0x3f800000);
	BEGIN_NV04(push, SUBC_3D(0x1f80), 16);
	PUSH_DATA (push, 0); PUSH_DATA (push, 0); PUSH_DATA (push, 0); PUSH_DATA (push, 0);
	PUSH_DATA (push, 0); PUSH_DATA (push, 0); PUSH_DATA (push, 0); PUSH_DATA (push, 0);
	PUSH_DATA (push, 0x0000ffff);
	PUSH_DATA (push, 0); PUSH_DATA (push, 0); PUSH_DATA (push, 0); PUSH_DATA (push, 0);
	PUSH_DATA (push, 0); PUSH_DATA (push, 0); PUSH_DATA (push, 0);

	BEGIN_NV04(push, SUBC_3D(0x120), 3);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 1);
	PUSH_DATA (push, 2);

	BEGIN_NV04(push, SUBC_BLIT(0x120), 3);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 1);
	PUSH_DATA (push, 2);

	BEGIN_NV04(push, SUBC_3D(0x1d88), 1);
	PUSH_DATA (push, 0x00001200);

	BEGIN_NV04(push, NV30_3D(MULTISAMPLE_CONTROL), 1);
	PUSH_DATA (push, 0xffff0000);

	/* Attempt to setup a known state.. Probably missing a heap of
	 * stuff here..
	 */
	BEGIN_NV04(push, NV30_3D(STENCIL_ENABLE(0)), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(STENCIL_ENABLE(1)), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(ALPHA_FUNC_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(DEPTH_WRITE_ENABLE), 2);
	PUSH_DATA (push, 0); /* wr disable */
	PUSH_DATA (push, 0); /* test disable */
	BEGIN_NV04(push, NV30_3D(COLOR_MASK), 1);
	PUSH_DATA (push, 0x01010101); /* TR,TR,TR,TR */
	BEGIN_NV04(push, NV30_3D(CULL_FACE_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV30_3D(BLEND_FUNC_ENABLE), 5);
	PUSH_DATA (push, 0);				/* Blend enable */
	PUSH_DATA (push, 0);				/* Blend src */
	PUSH_DATA (push, 0);				/* Blend dst */
	PUSH_DATA (push, 0x00000000);			/* Blend colour */
	PUSH_DATA (push, 0x8006);			/* FUNC_ADD */
	BEGIN_NV04(push, NV30_3D(COLOR_LOGIC_OP_ENABLE), 2);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0x1503 /*GL_COPY*/);
	BEGIN_NV04(push, NV30_3D(DITHER_ENABLE), 1);
	PUSH_DATA (push, 1);
	BEGIN_NV04(push, NV30_3D(SHADE_MODEL), 1);
	PUSH_DATA (push, 0x1d01 /*GL_SMOOTH*/);
	BEGIN_NV04(push, NV30_3D(POLYGON_OFFSET_FACTOR),2);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	BEGIN_NV04(push, NV30_3D(POLYGON_MODE_FRONT), 2);
	PUSH_DATA (push, 0x1b02 /*GL_FILL*/);
	PUSH_DATA (push, 0x1b02 /*GL_FILL*/);
	/* - Disable texture units
	 * - Set fragprog to MOVR result.color, fragment.color */
	for (i=0;i<4;i++) {
		BEGIN_NV04(push, NV30_3D(TEX_ENABLE(i)), 1);
		PUSH_DATA (push, 0);
	}
	/* Polygon stipple */
	BEGIN_NV04(push, NV30_3D(POLYGON_STIPPLE_PATTERN(0)), 0x20);
	for (i=0;i<0x20;i++)
		PUSH_DATA (push, 0xFFFFFFFF);

	BEGIN_NV04(push, NV30_3D(DEPTH_RANGE_NEAR), 2);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);

	/* Ok.  If you start X with the nvidia driver, kill it, and then
	 * start X with nouveau you will get black rendering instead of
	 * what you'd expect.  This fixes the problem, and it seems that
	 * it's not needed between nouveau restarts - which suggests that
	 * the 3D context (wherever it's stored?) survives somehow.
	 */
	//BEGIN_NV04(push, SUBC_3D(0x1d60),1);
	//PUSH_DATA (push, 0x03008000);

	int w=4096;
	int h=4096;
	int pitch=4096*4;
	BEGIN_NV04(push, NV30_3D(RT_HORIZ), 5);
	PUSH_DATA (push, w<<16);
	PUSH_DATA (push, h<<16);
	PUSH_DATA (push, 0x148); /* format */
	PUSH_DATA (push, pitch << 16 | pitch);
	PUSH_DATA (push, 0x0);
	BEGIN_NV04(push, NV30_3D(VIEWPORT_TX_ORIGIN), 1);
	PUSH_DATA (push, 0);
        BEGIN_NV04(push, SUBC_3D(0x0a00), 2);
        PUSH_DATA (push, (w<<16) | 0);
        PUSH_DATA (push, (h<<16) | 0);
	BEGIN_NV04(push, NV30_3D(VIEWPORT_CLIP_HORIZ(0)), 2);
	PUSH_DATA (push, (w-1)<<16);
	PUSH_DATA (push, (h-1)<<16);
	BEGIN_NV04(push, NV30_3D(SCISSOR_HORIZ), 2);
	PUSH_DATA (push, w<<16);
	PUSH_DATA (push, h<<16);
	BEGIN_NV04(push, NV30_3D(VIEWPORT_HORIZ), 2);
	PUSH_DATA (push, w<<16);
	PUSH_DATA (push, h<<16);

	BEGIN_NV04(push, NV30_3D(VIEWPORT_TRANSLATE_X), 8);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 0.0);

	BEGIN_NV04(push, NV30_3D(MODELVIEW_MATRIX(0)), 16);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);

	BEGIN_NV04(push, NV30_3D(PROJECTION_MATRIX(0)), 16);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);

	BEGIN_NV04(push, NV30_3D(SCISSOR_HORIZ), 2);
	PUSH_DATA (push, 4096<<16);
	PUSH_DATA (push, 4096<<16);

	PUSH_DATAu(push, info->scratch, PFP_PASS, 2 * 4);
	PUSH_DATAs(push, 0x18009e80); /* txph r0, a[tex0], t[0] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x1802be83); /* txph r1, a[tex1], t[1] */
	PUSH_DATAs(push, 0x1c9dc801); /* exit */
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);

	PUSH_DATAu(push, info->scratch, PFP_NV12_BILINEAR, 8 * 4);
	PUSH_DATAs(push, 0x17028200); /* texr r0.x, a[tex0], t[1] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x04000e02); /* madr r1.xyz, r0.x, imm.x, imm.yzww */
	PUSH_DATAs(push, 0x1c9c0000);
	PUSH_DATAs(push, 0x00000002);
	PUSH_DATAs(push, 0x0001f202);
	PUSH_DATAs(push, 0x3f9507c8); /* { 1.16, -0.87, 0.53, -1.08 } */
	PUSH_DATAs(push, 0xbf5ee393);
	PUSH_DATAs(push, 0x3f078fef);
	PUSH_DATAs(push, 0xbf8a6762);
	PUSH_DATAs(push, 0x1704ac80); /* texr r0.yz, a[tex1], t[2] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3fe1c800);
	PUSH_DATAs(push, 0x04000e02); /* madr r1.xyz, r0.y, imm, r1 */
	PUSH_DATAs(push, 0x1c9cab00);
	PUSH_DATAs(push, 0x0001c802);
	PUSH_DATAs(push, 0x0001c804);
	PUSH_DATAs(push, 0x00000000); /* { 0.00, -0.39, 2.02, 0.00 } */
	PUSH_DATAs(push, 0xbec890d6);
	PUSH_DATAs(push, 0x40011687);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x04000e81); /* madr r0.xyz, r0.z, imm, r1 */
	PUSH_DATAs(push, 0x1c9d5500);
	PUSH_DATAs(push, 0x0001c802);
	PUSH_DATAs(push, 0x0001c804);
	PUSH_DATAs(push, 0x3fcc432d); /* { 1.60, -0.81, 0.00, 0.00 } */
	PUSH_DATAs(push, 0xbf501a37);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);

	PUSH_DATAu(push, info->scratch, PFP_NV12_BICUBIC, 24 * 4);
	PUSH_DATAs(push, 0x01008604); /* movr r2.xy, a[tex0] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x03000600); /* addr r0.xy, r2, imm.x */
	PUSH_DATAs(push, 0x1c9dc808);
	PUSH_DATAs(push, 0x00000002);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x3f000000); /* { 0.50, 0.00, 0.00, 0.00 } */
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x17000e06); /* texr r3.xyz, r0, t[0] */
	PUSH_DATAs(push, 0x1c9dc800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x17000e00); /* texr r0.xyz, r0.y, t[0] */
	PUSH_DATAs(push, 0x1c9caa00);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x02000a02); /* mulr r1.xz, r3.xxyy, imm.xxyy */
	PUSH_DATAs(push, 0x1c9ca00c);
	PUSH_DATAs(push, 0x0000a002);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0xbf800000); /* { -1.00, 1.00, 0.00, 0.00 } */
	PUSH_DATAs(push, 0x3f800000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x02001402); /* mulr r1.yw, r0.xxyy, imm.xxyy */
	PUSH_DATAs(push, 0x1c9ca000);
	PUSH_DATAs(push, 0x0000a002);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0xbf800000); /* { -1.00, 1.00, 0.00, 0.00 } */
	PUSH_DATAs(push, 0x3f800000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x03001e04); /* addr r2, r2.xyxy, r1 */
	PUSH_DATAs(push, 0x1c9c8808);
	PUSH_DATAs(push, 0x0001c804);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x17020200); /* texr r0.x, r2, t[1] */
	PUSH_DATAs(push, 0x1c9dc808);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x17020402); /* texr r1.y, r2.xwxw, t[1] */
	PUSH_DATAs(push, 0x1c9d9808);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x17020202); /* texr r1.x, r2.zyxy, t[1] */
	PUSH_DATAs(push, 0x1c9c8c08);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x1f400280); /* lrph r0.x, r0.z, r0, r1.y */
	PUSH_DATAs(push, 0x1c9d5400);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0000aa04);
	PUSH_DATAs(push, 0x17020400); /* texr r0.y, r2.zwzz, t[1] */
	PUSH_DATAs(push, 0x1c9d5c08);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x1f400480); /* lrph r0.y, r0.z, r1.x, r0 */
	PUSH_DATAs(push, 0x1c9d5400);
	PUSH_DATAs(push, 0x00000004);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x1f400280); /* lrph r0.x, r3.z, r0, r0.y */
	PUSH_DATAs(push, 0x1c9d540c);
	PUSH_DATAs(push, 0x0001c900);
	PUSH_DATAs(push, 0x0000ab00);
	PUSH_DATAs(push, 0x04400e80); /* madh r0.xyz, r0.x, imm.x, imm.yzww */
	PUSH_DATAs(push, 0x1c9c0100);
	PUSH_DATAs(push, 0x00000002);
	PUSH_DATAs(push, 0x0001f202);
	PUSH_DATAs(push, 0x3f9507c8); /* { 1.16, -0.87, 0.53, -1.08 } */
	PUSH_DATAs(push, 0xbf5ee393);
	PUSH_DATAs(push, 0x3f078fef);
	PUSH_DATAs(push, 0xbf8a6762);
	PUSH_DATAs(push, 0x1704ac02); /* texr r1.yz, a[tex1], t[2] */
	PUSH_DATAs(push, 0x1c9dc801);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x0001c800);
	PUSH_DATAs(push, 0x04400e80); /* madh r0.xyz, r1.y, imm, r0 */
	PUSH_DATAs(push, 0x1c9caa04);
	PUSH_DATAs(push, 0x0001c802);
	PUSH_DATAs(push, 0x0001c900);
	PUSH_DATAs(push, 0x00000000); /* { 0.00, -0.39, 2.02, 0.00 } */
	PUSH_DATAs(push, 0xbec890d6);
	PUSH_DATAs(push, 0x40011687);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x04400e81); /* madh r0.xyz, r1.z, imm, r0 */
	PUSH_DATAs(push, 0x1c9d5404);
	PUSH_DATAs(push, 0x0001c802);
	PUSH_DATAs(push, 0x0001c900);
	PUSH_DATAs(push, 0x3fcc432d); /* { 1.60, -0.81, 0.00, 0.00 } */
	PUSH_DATAs(push, 0xbf501a37);
	PUSH_DATAs(push, 0x00000000);
	PUSH_DATAs(push, 0x00000000);

	return TRUE;
}

int NVAccelInitNV10TCL(struct nouveau_info *info)
{
	struct nouveau_pushbuf *push = info->pushbuf;
	struct nv04_fifo *fifo = info->chan->data;
	uint32_t class = 0;
	int i;

	if (((info->dev->chipset & 0xf0) != NV_ARCH_10) &&
	    ((info->dev->chipset & 0xf0) != NV_ARCH_20))
		return FALSE;

	if (info->dev->chipset >= 0x20 || info->dev->chipset == 0x1a)
		class = NV15_3D_CLASS;
	else if (info->dev->chipset >= 0x17)
		class = NV17_3D_CLASS;
	else if (info->dev->chipset >= 0x11)
		class = NV15_3D_CLASS;
	else
		class = NV10_3D_CLASS;

	if (nouveau_object_new(info->chan, Nv3D, class, NULL, 0, &info->Nv3D))
		return FALSE;

	if (!PUSH_SPACE(push, 256))
		return FALSE;

	BEGIN_NV04(push, NV01_SUBC(3D, OBJECT), 1);
	PUSH_DATA (push, info->Nv3D->handle);
	BEGIN_NV04(push, NV10_3D(DMA_NOTIFY), 1);
	PUSH_DATA (push, info->NvNull->handle);

	BEGIN_NV04(push, NV10_3D(DMA_TEXTURE0), 2);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, fifo->gart);

	BEGIN_NV04(push, NV10_3D(DMA_COLOR), 2);
	PUSH_DATA (push, fifo->vram);
	PUSH_DATA (push, fifo->vram);

	BEGIN_NV04(push, NV04_GRAPH(3D, NOP), 1);
	PUSH_DATA (push, 0);

	BEGIN_NV04(push, NV10_3D(RT_HORIZ), 2);
	PUSH_DATA (push, 2048 << 16 | 0);
	PUSH_DATA (push, 2048 << 16 | 0);

	BEGIN_NV04(push, NV10_3D(ZETA_OFFSET), 1);
	PUSH_DATA (push, 0);

	BEGIN_NV04(push, NV10_3D(VIEWPORT_CLIP_MODE), 1);
	PUSH_DATA (push, 0);

	BEGIN_NV04(push, NV10_3D(VIEWPORT_CLIP_HORIZ(0)), 1);
	PUSH_DATA (push, 0x7ff << 16 | 0x800);
	BEGIN_NV04(push, NV10_3D(VIEWPORT_CLIP_VERT(0)), 1);
	PUSH_DATA (push, 0x7ff << 16 | 0x800);

	for (i = 1; i < 8; i++) {
		BEGIN_NV04(push, NV10_3D(VIEWPORT_CLIP_HORIZ(i)), 1);
		PUSH_DATA (push, 0);
		BEGIN_NV04(push, NV10_3D(VIEWPORT_CLIP_VERT(i)), 1);
		PUSH_DATA (push, 0);
	}

	BEGIN_NV04(push, SUBC_3D(0x290), 1);
	PUSH_DATA (push, (0x10<<16)|1);
	BEGIN_NV04(push, SUBC_3D(0x3f4), 1);
	PUSH_DATA (push, 0);

	BEGIN_NV04(push, NV04_GRAPH(3D, NOP), 1);
	PUSH_DATA (push, 0);

	if (class != NV10_3D_CLASS) {
		/* For nv11, nv17 */
		BEGIN_NV04(push, SUBC_3D(0x120), 3);
		PUSH_DATA (push, 0);
		PUSH_DATA (push, 1);
		PUSH_DATA (push, 2);

		BEGIN_NV04(push, SUBC_BLIT(0x120), 3);
		PUSH_DATA (push, 0);
		PUSH_DATA (push, 1);
		PUSH_DATA (push, 2);

		BEGIN_NV04(push, NV04_GRAPH(3D, NOP), 1);
		PUSH_DATA (push, 0);
	}

	BEGIN_NV04(push, NV04_GRAPH(3D, NOP), 1);
	PUSH_DATA (push, 0);

	/* Set state */
	BEGIN_NV04(push, NV10_3D(FOG_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(ALPHA_FUNC_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(ALPHA_FUNC_FUNC), 2);
	PUSH_DATA (push, 0x207);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(TEX_ENABLE(0)), 2);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(RC_IN_ALPHA(0)), 6);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(RC_OUT_ALPHA(0)), 6);
	PUSH_DATA (push, 0x00000c00);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0x00000c00);
	PUSH_DATA (push, 0x18000000);
	PUSH_DATA (push, 0x300c0000);
	PUSH_DATA (push, 0x00001c80);
	BEGIN_NV04(push, NV10_3D(BLEND_FUNC_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(DITHER_ENABLE), 2);
	PUSH_DATA (push, 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(LINE_SMOOTH_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(VERTEX_WEIGHT_ENABLE), 2);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(BLEND_FUNC_SRC), 4);
	PUSH_DATA (push, 1);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0x8006);
	BEGIN_NV04(push, NV10_3D(STENCIL_MASK), 8);
	PUSH_DATA (push, 0xff);
	PUSH_DATA (push, 0x207);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0xff);
	PUSH_DATA (push, 0x1e00);
	PUSH_DATA (push, 0x1e00);
	PUSH_DATA (push, 0x1e00);
	PUSH_DATA (push, 0x1d01);
	BEGIN_NV04(push, NV10_3D(NORMALIZE_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(FOG_ENABLE), 2);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(LIGHT_MODEL), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(SEPARATE_SPECULAR_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(ENABLED_LIGHTS), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(POLYGON_OFFSET_POINT_ENABLE), 3);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(DEPTH_FUNC), 1);
	PUSH_DATA (push, 0x201);
	BEGIN_NV04(push, NV10_3D(DEPTH_WRITE_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(DEPTH_TEST_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(POLYGON_OFFSET_FACTOR), 2);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(POINT_SIZE), 1);
	PUSH_DATA (push, 8);
	BEGIN_NV04(push, NV10_3D(POINT_PARAMETERS_ENABLE), 2);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(LINE_WIDTH), 1);
	PUSH_DATA (push, 8);
	BEGIN_NV04(push, NV10_3D(LINE_SMOOTH_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(POLYGON_MODE_FRONT), 2);
	PUSH_DATA (push, 0x1b02);
	PUSH_DATA (push, 0x1b02);
	BEGIN_NV04(push, NV10_3D(CULL_FACE), 2);
	PUSH_DATA (push, 0x405);
	PUSH_DATA (push, 0x901);
	BEGIN_NV04(push, NV10_3D(POLYGON_SMOOTH_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(CULL_FACE_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(TEX_GEN_MODE(0, 0)), 8);
	for (i = 0; i < 8; i++)
		PUSH_DATA (push, 0);

	BEGIN_NV04(push, NV10_3D(FOG_COEFF(0)), 3);
	PUSH_DATA (push, 0x3fc00000);	/* -1.50 */
	PUSH_DATA (push, 0xbdb8aa0a);	/* -0.09 */
	PUSH_DATA (push, 0);		/*  0.00 */

	BEGIN_NV04(push, NV04_GRAPH(3D, NOP), 1);
	PUSH_DATA (push, 0);

	BEGIN_NV04(push, NV10_3D(FOG_MODE), 2);
	PUSH_DATA (push, 0x802);
	PUSH_DATA (push, 2);
	/* for some reason VIEW_MATRIX_ENABLE need to be 6 instead of 4 when
	 * using texturing, except when using the texture matrix
	 */
	BEGIN_NV04(push, NV10_3D(VIEW_MATRIX_ENABLE), 1);
	PUSH_DATA (push, 6);
	BEGIN_NV04(push, NV10_3D(COLOR_MASK), 1);
	PUSH_DATA (push, 0x01010101);

	BEGIN_NV04(push, NV10_3D(PROJECTION_MATRIX(0)), 16);
	for(i = 0; i < 16; i++)
		PUSH_DATAf(push, i/4 == i%4 ? 1.0 : 0.0);

	BEGIN_NV04(push, NV10_3D(DEPTH_RANGE_NEAR), 2);
	PUSH_DATA (push, 0);
	PUSH_DATAf(push, 65536.0);

	BEGIN_NV04(push, NV10_3D(VIEWPORT_TRANSLATE_X), 4);
	PUSH_DATAf(push, -2048.0);
	PUSH_DATAf(push, -2048.0);
	PUSH_DATAf(push, 0);
	PUSH_DATA (push, 0);

	/* Set vertex component */
	BEGIN_NV04(push, NV10_3D(VERTEX_COL_4F_R), 4);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 1.0);
	PUSH_DATAf(push, 1.0);
	BEGIN_NV04(push, NV10_3D(VERTEX_COL2_3F_R), 3);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	BEGIN_NV04(push, NV10_3D(VERTEX_NOR_3F_X), 3);
	PUSH_DATA (push, 0);
	PUSH_DATA (push, 0);
	PUSH_DATAf(push, 1.0);
	BEGIN_NV04(push, NV10_3D(VERTEX_TX0_4F_S), 4);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);
	BEGIN_NV04(push, NV10_3D(VERTEX_TX1_4F_S), 4);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 0.0);
	PUSH_DATAf(push, 1.0);
	BEGIN_NV04(push, NV10_3D(VERTEX_FOG_1F), 1);
	PUSH_DATAf(push, 0.0);
	BEGIN_NV04(push, NV10_3D(EDGEFLAG_ENABLE), 1);
	PUSH_DATA (push, 1);

	return TRUE;
}

int NVAccelInit3D_NVC0(struct nouveau_info *info)
{

	struct nouveau_pushbuf *push = info->pushbuf;
	struct nouveau_bo *bo = info->scratch;
	uint32_t class, handle;
	int ret;

	if (info->arch < NV_KEPLER) {
		class  = 0x9097;
		handle = 0x001f906e;
	} else
	if (info->dev->chipset < 0xf0) {
		class  = 0xa097;
		handle = 0x0000906e;
	} else {
		class  = 0xa197;
		handle = 0x0000906e;
	}

	ret = nouveau_object_new(info->chan, class, class,
				 NULL, 0, &info->Nv3D);
	if (ret)
		return FALSE;

	ret = nouveau_object_new(info->chan, handle, 0x906e,
				 NULL, 0, &info->NvSW);
	if (ret) {
		ALOGI("DRM doesn't support sync-to-vblank\n");
	}

	if (nouveau_pushbuf_space(push, 512, 0, 0) ||
	    nouveau_pushbuf_refn (push, &(struct nouveau_pushbuf_refn) {
					info->scratch, NOUVEAU_BO_VRAM |
					NOUVEAU_BO_WR }, 1))
		return FALSE;

	BEGIN_NVC0(push, NV01_SUBC(3D, OBJECT), 1);
	PUSH_DATA (push, info->Nv3D->handle);
	BEGIN_NVC0(push, NVC0_3D(COND_MODE), 1);
	PUSH_DATA (push, NVC0_3D_COND_MODE_ALWAYS);
	BEGIN_NVC0(push, SUBC_3D(NVC0_GRAPH_NOTIFY_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (info->scratch->offset + NTFY_OFFSET) >> 32);
	PUSH_DATA (push, (info->scratch->offset + NTFY_OFFSET));
	PUSH_DATA (push, 0);
	BEGIN_NVC0(push, NVC0_3D(CSAA_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NVC0(push, NVC0_3D(ZETA_ENABLE), 1);
	PUSH_DATA (push, 0);
	BEGIN_NVC0(push, NVC0_3D(RT_SEPARATE_FRAG_DATA), 1);
	PUSH_DATA (push, 0);

	BEGIN_NVC0(push, NVC0_3D(VIEWPORT_HORIZ(0)), 2);
	PUSH_DATA (push, (8192 << 16) | 0);
	PUSH_DATA (push, (8192 << 16) | 0);
	BEGIN_NVC0(push, NVC0_3D(SCREEN_SCISSOR_HORIZ), 2);
	PUSH_DATA (push, (8192 << 16) | 0);
	PUSH_DATA (push, (8192 << 16) | 0);
	BEGIN_NVC0(push, NVC0_3D(SCISSOR_ENABLE(0)), 1);
	PUSH_DATA (push, 1);
	BEGIN_NVC0(push, NVC0_3D(VIEWPORT_TRANSFORM_EN), 1);
	PUSH_DATA (push, 0);
	BEGIN_NVC0(push, NVC0_3D(VIEW_VOLUME_CLIP_CTRL), 1);
	PUSH_DATA (push, 0);

	BEGIN_NVC0(push, NVC0_3D(TIC_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (bo->offset + TIC_OFFSET_NVC0) >> 32);
	PUSH_DATA (push, (bo->offset + TIC_OFFSET_NVC0));
	PUSH_DATA (push, 15);
	BEGIN_NVC0(push, NVC0_3D(TSC_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (bo->offset + TSC_OFFSET_NVC0) >> 32);
	PUSH_DATA (push, (bo->offset + TSC_OFFSET_NVC0));
	PUSH_DATA (push, 0);
	BEGIN_NVC0(push, NVC0_3D(LINKED_TSC), 1);
	PUSH_DATA (push, 1);
	if (info->arch < NV_KEPLER) {
		BEGIN_NVC0(push, NVC0_3D(TEX_LIMITS(4)), 1);
		PUSH_DATA (push, 0x54);
		BEGIN_NIC0(push, NVC0_3D(BIND_TIC(4)), 2);
		PUSH_DATA (push, (0 << 9) | (0 << 1) | NVC0_3D_BIND_TIC_ACTIVE);
		PUSH_DATA (push, (1 << 9) | (1 << 1) | NVC0_3D_BIND_TIC_ACTIVE);
	} else {
		BEGIN_NVC0(push, NVC0_3D(CB_SIZE), 6);
		PUSH_DATA (push, 256);
		PUSH_DATA (push, (bo->offset + TB_OFFSET) >> 32);
		PUSH_DATA (push, (bo->offset + TB_OFFSET));
		PUSH_DATA (push, 0);
		PUSH_DATA (push, 0x00000000);
		PUSH_DATA (push, 0x00000001);
		BEGIN_NVC0(push, NVC0_3D(CB_BIND(4)), 1);
		PUSH_DATA (push, 0x11);
		BEGIN_NVC0(push, SUBC_3D(0x2608), 1);
		PUSH_DATA (push, 1);
	}

	BEGIN_NVC0(push, NVC0_3D(VERTEX_QUARANTINE_ADDRESS_HIGH), 3);
	PUSH_DATA (push, (bo->offset + MISC_OFFSET) >> 32);
	PUSH_DATA (push, (bo->offset + MISC_OFFSET));
	PUSH_DATA (push, 1);

	BEGIN_NVC0(push, NVC0_3D(CODE_ADDRESS_HIGH), 2);
	PUSH_DATA (push, (bo->offset + CODE_OFFSET) >> 32);
	PUSH_DATA (push, (bo->offset + CODE_OFFSET));
	if (info->arch < NV_KEPLER) {
		NVC0PushProgram(info, PVP_PASS_NVC0, NVC0VP_Transform2);
		NVC0PushProgram(info, PFP_S_NVC0, NVC0FP_Source);
		NVC0PushProgram(info, PFP_C_NVC0, NVC0FP_Composite);
		NVC0PushProgram(info, PFP_CCA_NVC0, NVC0FP_CAComposite);
		NVC0PushProgram(info, PFP_CCASA_NVC0, NVC0FP_CACompositeSrcAlpha);
		NVC0PushProgram(info, PFP_S_A8_NVC0, NVC0FP_Source_A8);
		NVC0PushProgram(info, PFP_C_A8_NVC0, NVC0FP_Composite_A8);
		NVC0PushProgram(info, PFP_NV12_NVC0, NVC0FP_NV12);

		BEGIN_NVC0(push, NVC0_3D(MEM_BARRIER), 1);
		PUSH_DATA (push, 0x1111);
	} else
	if (info->dev->chipset < 0xf0) {
		NVC0PushProgram(info, PVP_PASS_NVC0, NVE0VP_Transform2);
		NVC0PushProgram(info, PFP_S_NVC0, NVE0FP_Source);
		NVC0PushProgram(info, PFP_C_NVC0, NVE0FP_Composite);
		NVC0PushProgram(info, PFP_CCA_NVC0, NVE0FP_CAComposite);
		NVC0PushProgram(info, PFP_CCASA_NVC0, NVE0FP_CACompositeSrcAlpha);
		NVC0PushProgram(info, PFP_S_A8_NVC0, NVE0FP_Source_A8);
		NVC0PushProgram(info, PFP_C_A8_NVC0, NVE0FP_Composite_A8);
		NVC0PushProgram(info, PFP_NV12_NVC0, NVE0FP_NV12);
	} else {
		NVC0PushProgram(info, PVP_PASS_NVC0, NVF0VP_Transform2);
		NVC0PushProgram(info, PFP_S_NVC0, NVF0FP_Source);
		NVC0PushProgram(info, PFP_C_NVC0, NVF0FP_Composite);
		NVC0PushProgram(info, PFP_CCA_NVC0, NVF0FP_CAComposite);
		NVC0PushProgram(info, PFP_CCASA_NVC0, NVF0FP_CACompositeSrcAlpha);
		NVC0PushProgram(info, PFP_S_A8_NVC0, NVF0FP_Source_A8);
		NVC0PushProgram(info, PFP_C_A8_NVC0, NVF0FP_Composite_A8);
		NVC0PushProgram(info, PFP_NV12_NVC0, NVF0FP_NV12);
	}

	BEGIN_NVC0(push, NVC0_3D(SP_SELECT(1)), 4);
	PUSH_DATA (push, NVC0_3D_SP_SELECT_PROGRAM_VP_B |
			 NVC0_3D_SP_SELECT_ENABLE);
	PUSH_DATA (push, PVP_PASS_NVC0);
	PUSH_DATA (push, 0x00000000);
	PUSH_DATA (push, 8);
	BEGIN_NVC0(push, NVC0_3D(VERT_COLOR_CLAMP_EN), 1);
	PUSH_DATA (push, 1);
	BEGIN_NVC0(push, NVC0_3D(CB_SIZE), 3);
	PUSH_DATA (push, 256);
	PUSH_DATA (push, (bo->offset + PVP_DATA_NVC0) >> 32);
	PUSH_DATA (push, (bo->offset + PVP_DATA_NVC0));
	BEGIN_NVC0(push, NVC0_3D(CB_BIND(0)), 1);
	PUSH_DATA (push, 0x01);

	BEGIN_NVC0(push, NVC0_3D(SP_SELECT(5)), 4);
	PUSH_DATA (push, NVC0_3D_SP_SELECT_PROGRAM_FP |
			 NVC0_3D_SP_SELECT_ENABLE);
	PUSH_DATA (push, PFP_S_NVC0);
	PUSH_DATA (push, 0x00000000);
	PUSH_DATA (push, 8);
	BEGIN_NVC0(push, NVC0_3D(FRAG_COLOR_CLAMP_EN), 1);
	PUSH_DATA (push, 0x11111111);
	BEGIN_NVC0(push, NVC0_3D(CB_SIZE), 3);
	PUSH_DATA (push, 256);
	PUSH_DATA (push, (bo->offset + PFP_DATA_NVC0) >> 32);
	PUSH_DATA (push, (bo->offset + PFP_DATA_NVC0));
	BEGIN_NVC0(push, NVC0_3D(CB_BIND(4)), 1);
	PUSH_DATA (push, 0x01);

	return TRUE;
}

static void nouveau_destroy(struct gralloc_drm_drv_t *drv)
{
	struct nouveau_info *info = (struct nouveau_info *) drv;

	if (info->chan) {
		/*nouveau_accel_free(info);*/
		nouveau_takedown_dma(info);
		info->chan = NULL;
	}

	
	nouveau_device_del(&info->dev);
	free(info);
}

static int nouveau_init(struct nouveau_info *info)
{
	int err = 0;

	switch (info->dev->chipset & 0xf0) {
	case 0x00:
		info->arch = NV_ARCH_04;
		break;
	case 0x10:
		info->arch = NV_ARCH_10;
		break;
	case 0x20:
		info->arch = NV_ARCH_20;
		break;
	case 0x30:
		info->arch = NV_ARCH_30;
		break;
	case 0x40:
	case 0x60:
		info->arch = NV_ARCH_40;
		break;
	case 0x50:
	case 0x80:
	case 0x90:
	case 0xa0:
		info->arch = NV_TESLA;
		break;
	case 0xc0:
	case 0xd0:
		info->arch = NV_FERMI;
		break;
	case 0xe0:
	case 0xf0:
		info->arch = NV_KEPLER;
		break;
	case 0x110:
		info->arch = NV_MAXWELL;
		break;
		
	default:
		ALOGE("unknown nouveau chipset 0x%x", info->dev->chipset);
		err = -EINVAL;
		break;
	}

	info->tiled_scanout = (info->chan != NULL);

	return err;
}


struct gralloc_drm_drv_t *gralloc_drm_drv_create_for_nouveau(int fd)
{
	struct nouveau_info *info;
	int err;

	/* channel variables */
	struct nv04_fifo nv04_data = {	.vram = NvDmaFB,
					.gart = NvDmaTT };
	struct nvc0_fifo nvc0_data = { };
	struct nouveau_object *device;
	struct nouveau_fifo *fifo;
	int size;	
	void *data;

	info = calloc(1, sizeof(*info));
	if (!info)
		return NULL;

	info->fd = fd;
	err = nouveau_device_wrap(info->fd, 0,  &info->dev);
	if (err) {
		ALOGE("failed to create nouveau device");
		free(info);
		return NULL;
	}
	else
	{
		ALOGI("DEBUG PST - nouveau device created");
	}
	
	err = nouveau_init(info);
	if (err) {
		if (info->chan) {
			/*nouveau_accel_free(info);*/
			nouveau_takedown_dma(info);
			info->chan = NULL;
		}
		
		ALOGE("DEBUG PST - nouveau_init failed");
		nouveau_device_del(&info->dev);
		free(info);
		return NULL;
	}
	

	/*
	*err = nouveau_channel_alloc(info->dev, NvDmaFB, NvDmaTT,
	*		24 * 1024, &info->chan);
	*if (err) {
	*	 make it non-fatal temporarily as it may require firmwares 
	*	ALOGW("failed to create nouveau channel");
	*	info->chan = NULL;
	*}
	*/

	/* pstglia NOTE (2014-06-08): Started to copy/transcript NVInitDma from xf86-video-nouveau
	* But the question is: Ok, I'm allocating dma channels and buffers to read/write
	* data, but how gralloc will use them? nouveau_client, channel, and the buffers
	* are structures declared outside gralloc_drm_drv_t scope. xf86-video-nouveau
	* have extra functions to read and write these dma buffers. Maybe this need
	* gralloc coding. Well, it's out of my bounds. My limited knowledge tells me it's
	* better trying to work without dma (if this is possible)
	*
	* 
	*
	*/
	// creating a client 
	device = &info->dev->object;
	info->chan = NULL;
	info->ce_channel = NULL;
	info->client = NULL;


	err  = nouveau_client_new(info->dev, &info->client);
	
	if (err) {
		ALOGE("PST DEBUG - Could not define a nouveau client - Forcing channel NULL");
		info->chan = NULL;
		info->ce_channel = NULL;
		info->client = NULL;
	}

	
	if (info->client) {
	
	// Creating the gpu channel (based on xf86-video-nouveau - nv_dma.c)
	// Note1: Putting all this code on a separated function is a good idea. Cleaner code
	// Note2: How to deal with this push buffers?
		ALOGI("PST DEBUG - Before setting data and size for channel");
		if (info->arch < 0xc0) {
			data = &nv04_data;
			size = sizeof(nv04_data);
		}
		else {
			data = &nvc0_data;
			size = sizeof(nvc0_data);
		}
	
	
		ALOGI("PST DEBUG - Before NOUVEAU_FIFO_CHANNEL_CLASS");
		err = nouveau_object_new(device, 0, NOUVEAU_FIFO_CHANNEL_CLASS, 
					data, size, &info->chan);
	
		if (err) {
			ALOGE("PST DEBUG - Error on nouveau_object_new (creating GPU channel) - Forcing channel NULL");
			info->chan = NULL;
			info->ce_channel = NULL;
		}
		else {
			
			ALOGI("PST DEBUG - Before err = nouveau_pushbuf_new");
			fifo = info->chan->data;
			err = nouveau_pushbuf_new(info->client, info->chan, 4, 32 * 1024,
					true, &info->pushbuf);
	
			if (err) {
				ALOGE("PST DEBUG - Error alocating push buffer");
				nouveau_takedown_dma(info);
				info->chan = NULL;
			}
			else {
				ALOGI("PST DEBUG - Before err = nouveau_bufctx_new");
				err = nouveau_bufctx_new(info->client, 1, &info->bufctx);

				if (err) {
					ALOGE("PST DEBUG - Error allocating bufctx");
					nouveau_takedown_dma(info);
					info->chan = NULL;
					
				}
			}
		}
	
	}	

	/* Create and map a scratch buffer */
	if (info->chan) {
		err = nouveau_bo_new(info->dev, NOUVEAU_BO_VRAM | NOUVEAU_BO_MAP,
				     128 * 1024, 128 * 1024, NULL, &info->scratch);
		if (!err) {
			err = nouveau_bo_map(info->scratch, 0, info->client);
			if (err) {
				ALOGE("PST DEBUG - Failed to allocate scratch buffer: %d",err);
				nouveau_takedown_dma(info);
			}
		}
		else {
			ALOGE("PST DEBUG - Failed to map scratch buffer: %d",err);
			nouveau_accel_free(info);
			nouveau_takedown_dma(info);
		}

	}

	
	if (info->chan) {
	    /* General engine objects */
	    if (info->arch < NV_FERMI) {
		    NVAccelInitDmaNotifier0(info);
		    NVAccelInitNull(info);
	    }

	    /* 2D engine */
	    if (info->arch < NV_TESLA) {
		    NVAccelInitContextSurfaces(info);
		    NVAccelInitContextBeta1(info);
		    NVAccelInitContextBeta4(info);
		    NVAccelInitImagePattern(info);
		    NVAccelInitRasterOp(info);
		    NVAccelInitRectangle(info);
		    NVAccelInitImageBlit(info);
		    NVAccelInitScaledImage(info);
		    NVAccelInitClipRectangle(info);
		    NVAccelInitImageFromCpu(info);
	    } else
	    if (info->arch < NV_FERMI) {
		    NVAccelInit2D_NV50(info);
	    } else {
		    NVAccelInit2D_NVC0(info);
	    }

	    if (info->arch < NV_TESLA)
		    NVAccelInitMemFormat(info);
	    else
	    if (info->arch < NV_FERMI)
		    NVAccelInitM2MF_NV50(info);
	    else
	    if (info->arch < NV_KEPLER)
		    NVAccelInitM2MF_NVC0(info);
	    else {
		    NVAccelInitP2MF_NVE0(info);
		    NVAccelInitCOPY_NVE0(info);
	    }

	    /* 3D init */
	    switch (info->arch) {
	    case NV_FERMI:
	    case NV_KEPLER:
		    NVAccelInit3D_NVC0(info);
		    break;
	    case NV_TESLA:
		    NVAccelInitNV50TCL(info);
		    break;
	    case NV_ARCH_40:
		    NVAccelInitNV40TCL(info);
		    break;
	    case NV_ARCH_30:
		    NVAccelInitNV30TCL(info);
		    break;
	    case NV_ARCH_20:
	    case NV_ARCH_10:
		    NVAccelInitNV10TCL(info);
		    break;
	    default:
		    break;
	    }
	}
	
	info->base.destroy = nouveau_destroy;
	info->base.init_kms_features = nouveau_init_kms_features;
	info->base.alloc = nouveau_alloc;
	info->base.free = nouveau_free;
	info->base.map = nouveau_map;
	info->base.unmap = nouveau_unmap;

	return &info->base;
}
