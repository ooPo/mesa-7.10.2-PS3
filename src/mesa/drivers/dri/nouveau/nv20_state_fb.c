/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "nouveau_driver.h"
#include "nouveau_context.h"
#include "nouveau_fbo.h"
#include "nouveau_gldefs.h"
#include "nouveau_util.h"
#include "nv20_3d.xml.h"
#include "nv20_driver.h"

static inline unsigned
get_rt_format(gl_format format)
{
	switch (format) {
	case MESA_FORMAT_XRGB8888:
		return NV20_3D_RT_FORMAT_COLOR_X8R8G8B8;
	case MESA_FORMAT_ARGB8888:
		return NV20_3D_RT_FORMAT_COLOR_A8R8G8B8;
	case MESA_FORMAT_RGB565:
		return NV20_3D_RT_FORMAT_COLOR_R5G6B5;
	case MESA_FORMAT_Z16:
		return NV20_3D_RT_FORMAT_DEPTH_Z16;
	case MESA_FORMAT_Z24_S8:
		return NV20_3D_RT_FORMAT_DEPTH_Z24S8;
	default:
		assert(0);
	}
}

static void
setup_hierz_buffer(struct gl_context *ctx)
{
	struct nouveau_channel *chan = context_chan(ctx);
	struct nouveau_grobj *kelvin = context_eng3d(ctx);
	struct nouveau_bo_context *bctx = context_bctx(ctx, HIERZ);
	struct gl_framebuffer *fb = ctx->DrawBuffer;
	struct nouveau_framebuffer *nfb = to_nouveau_framebuffer(fb);
	unsigned pitch = align(fb->Width, 128),
		height = align(fb->Height, 2),
		size = pitch * height;

	if (!nfb->hierz.bo || nfb->hierz.bo->size != size) {
		nouveau_bo_ref(NULL, &nfb->hierz.bo);
		nouveau_bo_new(context_dev(ctx), NOUVEAU_BO_VRAM, 0, size,
			       &nfb->hierz.bo);
	}

	BEGIN_RING(chan, kelvin, NV25_3D_HIERZ_PITCH, 1);
	OUT_RING(chan, pitch);

	nouveau_bo_markl(bctx, kelvin, NV25_3D_HIERZ_OFFSET, nfb->hierz.bo,
			 0, NOUVEAU_BO_VRAM | NOUVEAU_BO_RDWR);
}

void
nv20_emit_framebuffer(struct gl_context *ctx, int emit)
{
	struct nouveau_channel *chan = context_chan(ctx);
	struct nouveau_grobj *kelvin = context_eng3d(ctx);
	struct nouveau_bo_context *bctx = context_bctx(ctx, FRAMEBUFFER);
	struct gl_framebuffer *fb = ctx->DrawBuffer;
	struct nouveau_surface *s;
	unsigned rt_format = NV20_3D_RT_FORMAT_TYPE_LINEAR;
	unsigned rt_pitch = 0, zeta_pitch = 0;
	unsigned bo_flags = NOUVEAU_BO_VRAM | NOUVEAU_BO_RDWR;

	if (fb->_Status != GL_FRAMEBUFFER_COMPLETE_EXT)
		return;

	/* Render target */
	if (fb->_ColorDrawBuffers[0]) {
		s = &to_nouveau_renderbuffer(
			fb->_ColorDrawBuffers[0])->surface;

		rt_format |= get_rt_format(s->format);
		rt_pitch = s->pitch;

		nouveau_bo_markl(bctx, kelvin, NV20_3D_COLOR_OFFSET,
				 s->bo, 0, bo_flags);
	}

	/* depth/stencil */
	if (fb->_DepthBuffer) {
		s = &to_nouveau_renderbuffer(
			fb->_DepthBuffer->Wrapped)->surface;

		rt_format |= get_rt_format(s->format);
		zeta_pitch = s->pitch;

		nouveau_bo_markl(bctx, kelvin, NV20_3D_ZETA_OFFSET,
				 s->bo, 0, bo_flags);

		if (context_chipset(ctx) >= 0x25)
			setup_hierz_buffer(ctx);
	} else {
		rt_format |= get_rt_format(MESA_FORMAT_Z24_S8);
		zeta_pitch = rt_pitch;
	}

	BEGIN_RING(chan, kelvin, NV20_3D_RT_FORMAT, 2);
	OUT_RING(chan, rt_format);
	OUT_RING(chan, zeta_pitch << 16 | rt_pitch);

	/* Recompute the viewport/scissor state. */
	context_dirty(ctx, VIEWPORT);
	context_dirty(ctx, SCISSOR);
}

void
nv20_emit_viewport(struct gl_context *ctx, int emit)
{
	struct nouveau_channel *chan = context_chan(ctx);
	struct nouveau_grobj *kelvin = context_eng3d(ctx);
	struct gl_framebuffer *fb = ctx->DrawBuffer;
	float a[4] = {};

	get_viewport_translate(ctx, a);

	BEGIN_RING(chan, kelvin, NV20_3D_VIEWPORT_TRANSLATE_X, 4);
	OUT_RINGp(chan, a, 4);

	BEGIN_RING(chan, kelvin, NV20_3D_VIEWPORT_CLIP_HORIZ(0), 1);
	OUT_RING(chan, (fb->Width - 1) << 16);
	BEGIN_RING(chan, kelvin, NV20_3D_VIEWPORT_CLIP_VERT(0), 1);
	OUT_RING(chan, (fb->Height - 1) << 16);

	context_dirty(ctx, PROJECTION);
}