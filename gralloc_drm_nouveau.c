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

#define LOG_TAG "GRALLOC-NOUVEAU"

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
	if (info->arch >= 0x50) {
		if (scanout && !info->tiled_scanout)
			align = 256;
		else
			tiled = 1;
	}

	*pitch = ALIGN(width * cpp, align);

	ALOGI("DEBUG PST - inside function alloc_bo: tiled: %d; scanout: %d; usage: %d", tiled, scanout, usage);
	ALOGI("DEBUG PST - inside function alloc_bo (2): cpp: %d; pitch: %d; width: %d;height: %d", cpp, *pitch, width, height);

	if (tiled) {
		if (info->arch >= 0xc0) {
			ALOGI("DEBUG PST - arch is 0xc0 or higher - setting tile_flags, align, height");
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
		else if (info->arch >= 0x50) {
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
	if (info->arch >= 0xc0) {
		bo_config.nvc0.memtype = tile_flags;
		bo_config.nvc0.tile_mode = tile_mode;
	}
	else if (info->arch >= 0x50) {
		bo_config.nv50.memtype = tile_flags;
		bo_config.nv50.tile_mode = tile_mode;
	}
	else {
		if (sw_indicator) {
			bo_config.nv04.surf_flags = 0x00;
		}
		else {
			if ( cpp == 2 )
				bo_config.nv04.surf_flags |= NV04_BO_16BPP;
			if ( cpp == 4 )
				bo_config.nv04.surf_flags |= NV04_BO_32BPP;
		}
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
		ALOGI("PST DEBUG - bo->map is not NULL after nouveau_bo_new");
		/* Setting created bo map to NULL */
		bo->map = NULL;
	}

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
	struct nouveau_info *info = (struct nouveau_info *) drv;
	struct nouveau_buffer *nb = (struct nouveau_buffer *) bo;
	/* TODO if tiled, unmap the linear bo and copy back */

	ALOGI("DEBUG PST - Inside nouveau_unmap");

	/* Replaced noveau_bo_unmap (not existant in libdrm anymore) by these 2 cmd  */
	/* nouveau_bo_del execute these cmds, but it also release the bo... */
	/* TODO: Confirm if this is the best way for unmapping bo */
	munmap(nb->bo->map, nb->bo->size);
	nb->bo->map = NULL;

}

static void nouveau_init_kms_features(struct gralloc_drm_drv_t *drv,
		struct gralloc_drm_t *drm)
{
	struct nouveau_info *info = (struct nouveau_info *) drv;

	switch (drm->primary.fb_format) {
	case HAL_PIXEL_FORMAT_BGRA_8888:
	/* pstglia: This mode (HAL_PIXEL_FORMAT_RGB_565) was not supported in my tests - so I disabled it*/
	/* please confirm it */
	//case HAL_PIXEL_FORMAT_RGB_565:
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

static void nouveau_destroy(struct gralloc_drm_drv_t *drv)
{
	struct nouveau_info *info = (struct nouveau_info *) drv;

	if (info->chan) {
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
		info->arch = 0x04;
		break;
	case 0x10:
		info->arch = 0x10;
		break;
	case 0x20:
		info->arch = 0x20;
		break;
	case 0x30:
		info->arch = 0x30;
		break;
	case 0x40:
	case 0x60:
		info->arch = 0x40;
		break;
	case 0x50:
	case 0x80:
	case 0x90:
	case 0xa0:
		info->arch = 0x50;
		break;
	case 0xc0:
	case 0xd0:
		info->arch = 0xc0;
		break;
	case 0xe0:
	case 0xf0:
		info->arch = 0xe0;
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

	err = nouveau_init(info);
	if (err) {
		if (info->chan) {
			nouveau_takedown_dma(info);
			info->chan = NULL;
		}
		
		ALOGE("DEBUG PST - nouveau_init failed");
		nouveau_device_del(&info->dev);
		free(info);
		return NULL;
	}

	info->base.destroy = nouveau_destroy;
	info->base.init_kms_features = nouveau_init_kms_features;
	info->base.alloc = nouveau_alloc;
	info->base.free = nouveau_free;
	info->base.map = nouveau_map;
	info->base.unmap = nouveau_unmap;

	return &info->base;
}
