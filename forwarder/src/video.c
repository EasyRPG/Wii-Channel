#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "images_scf_tpl.h"
#include "images_scf.h"

#define DEFAULT_FIFO_SIZE (256 * 1024)
static void *xfb[2] = { NULL, NULL };
static int whichfb = 0;
static GXRModeObj *vmode;
static unsigned char gp_fifo[DEFAULT_FIFO_SIZE] ATTRIBUTE_ALIGN(32);
static Mtx GXmodelView2D;
#define VIDEOSTACK (1024 * 8)
lwp_t vthread = LWP_THREAD_NULL;
u8 vstack[VIDEOSTACK];
static s8 fade_mode = 0; // + fade in, - fade out
static u8 progress = 0;
static bool render_stop = false;

#define CLAMP(x, low, high) (((x)>=(high)) ? (high) : (((x)<=(low)) ? (low) : (x)))
#define RGBA(r, g, b, a) ((u32)((((u32)(r))<<24) | ((((u32)(g))&0xFF)<<16) | \
						((((u32)(b))&0xFF)<<8) | (((u32)(a))&0xFF)))

static void RenderGX() {
	GX_DrawDone();
	whichfb ^= 1;
	GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);
	GX_CopyDisp(xfb[whichfb], GX_TRUE);
	VIDEO_SetNextFramebuffer(xfb[whichfb]);
	VIDEO_Flush();
	VIDEO_WaitVSync();
}

static void *RenderThread(void *) {
	u8 bar_percent = 0;
	s16 fade_alpha = 0;
	f32 logo_rotation = 0.0f;
	u32 color;
	s8 old_progress = 0;
	s8 progress_step = 1;

	// setup tex
	GXTexObj texObj;
	TPLFile imagesTPL;
	TPL_OpenTPLFromMemory(&imagesTPL, (void *)images_scf_tpl, images_scf_tpl_size);
	u32 tex_fmt; // unused
	u16 tex_width, tex_height;
	TPL_GetTextureInfo(&imagesTPL, logo, &tex_fmt, &tex_width, &tex_height);
	TPL_GetTexture(&imagesTPL, logo, &texObj);
	GX_LoadTexObj(&texObj, GX_TEXMAP0);
	GX_InvalidateTexAll();

	// centered in upper part
	Mtx m, mv;
	guVector axis = { 0, 0, 1 };
	f32 logo_x = (vmode->fbWidth - tex_width) / 2;
	f32 logo_y = vmode->xfbHeight / 2 - tex_height;
	f32 logo_width = tex_width / 2;
	f32 logo_height = tex_height / 2;

	// setup bar position centered at bottom
	f32 bar_x = 60;
	f32 bar_y = vmode->xfbHeight - 40;
	f32 bar_width = vmode->fbWidth - 120;
	f32 bar_height = 20;
	f32 bar_x2, bar_y2 = bar_y + bar_height;

	while (!render_stop) {
		// check fading
		if (fade_mode > 0) fade_alpha+=4;
		if (fade_mode < 0) fade_alpha-=4;
		fade_alpha = CLAMP(fade_alpha, 0, 255);

		// check bar position
		if (progress != old_progress) {
			progress_step = (progress - bar_percent) / 5;
			old_progress = progress;
		}
		if (progress > bar_percent) bar_percent += progress_step;
		if (progress < bar_percent) bar_percent = progress;
		bar_percent = CLAMP(bar_percent, 0, 100);

		// rotate logo
		if (logo_rotation < 360.0f) logo_rotation += 3.0f;
		if (logo_rotation >= 360.0f) logo_rotation = 0.0f;

		// very verbose
		//printf("mode: %d, alpha: %u, percent: %u, rotation: %f\n", fade_mode, fade_alpha, bar_percent, logo_rotation);

		// draw logo
		color = RGBA(255, 255, 255, fade_alpha); // white
		GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
		GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
		guMtxIdentity(m);
		guMtxRotAxisDeg(m, &axis, logo_rotation);
		guMtxTransApply(m, m, logo_x + logo_width + 0.5f, logo_y + logo_height + 0.5f, 0.0f);
		guMtxConcat(GXmodelView2D, m, mv);
		GX_LoadPosMtxImm(mv, GX_PNMTX0);
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			GX_Position3f32(-logo_width, -logo_height, 0.0f);
			GX_Color1u32(color);
			GX_TexCoord2f32(0.0f, 0.0f);
			GX_Position3f32(logo_width, -logo_height, 0.0f);
			GX_Color1u32(color);
			GX_TexCoord2f32(1.0f, 0.0f);
			GX_Position3f32(logo_width, logo_height, 0.0f);
			GX_Color1u32(color);
			GX_TexCoord2f32(1.0f, 1.0f);
			GX_Position3f32(-logo_width, logo_height, 0.0f);
			GX_Color1u32(color);
			GX_TexCoord2f32(0.0f, 1.0f);
		GX_End();
		GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);
		GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
		GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);

		// draw bar outer box
		color = RGBA(128, 128, 128, fade_alpha); // gray
		bar_x2 = bar_x + bar_width;
		GX_Begin(GX_LINESTRIP, GX_VTXFMT0, 5);
			GX_Position3f32(bar_x, bar_y, 0.0f);
			GX_Color1u32(color);
			GX_Position3f32(bar_x2, bar_y, 0.0f);
			GX_Color1u32(color);
			GX_Position3f32(bar_x2, bar_y2, 0.0f);
			GX_Color1u32(color);
			GX_Position3f32(bar_x, bar_y2, 0.0f);
			GX_Color1u32(color);
			GX_Position3f32(bar_x, bar_y, 0.0f);
			GX_Color1u32(color);
		GX_End();

		if (bar_percent > 0) {
			// inner bar
			bar_x2 = bar_x + (bar_width / 100 * bar_percent);
			GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
				color = RGBA(0, 255, 0, fade_alpha); // green
				GX_Position3f32(bar_x, bar_y, 0.0f);
				GX_Color1u32(color);
				GX_Position3f32(bar_x2, bar_y, 0.0f);
				GX_Color1u32(color);
				color = RGBA(0, 80, 0, fade_alpha); // dark green
				GX_Position3f32(bar_x2, bar_y2, 0.0f);
				GX_Color1u32(color);
				GX_Position3f32(bar_x, bar_y2, 0.0f);
				GX_Color1u32(color);
			GX_End();
		}

		// finally render
		RenderGX();

		// stop fadein
		if (fade_alpha == 255 && fade_mode == 1)
			fade_mode = 0;
		// stop fadeout and exit
		if (fade_alpha == 0 && fade_mode == -1) {
			fade_mode = 0;
			render_stop = true;
		}
	}

	return NULL;
}

void StartRenderThread(bool wait) {
#if DEBUG
	printf("Starting render thread...\n");
#endif
	// gray background
	GX_SetCopyClear((GXColor){ 16, 16, 16, 255 }, GX_MAX_Z24);

	LWP_CreateThread(&vthread, RenderThread, NULL, vstack, VIDEOSTACK, 66);

	fade_mode = 1;

	// wait until faded in
	if (wait)
		while(fade_mode == 1)
			VIDEO_WaitVSync();
#if DEBUG
	printf("Render thread started%s.\n", wait ? " (waited for fade-in)" : "");
#endif
}

void StopRenderThread(bool wait) {
#if DEBUG
	printf("stopping render thread...\n");
#endif
	fade_mode = -1;

	// no wait until faded out
	if (!wait) render_stop = true;

	// wait until thread exits
	LWP_JoinThread(vthread, NULL);
	vthread = LWP_THREAD_NULL;

	// fill with black
	GX_SetCopyClear((GXColor){ 0, 0, 0, 255 }, GX_MAX_Z24);
	f32 x = -40.0f;
	f32 y = -40.0f;
	f32 x2 = x + vmode->fbWidth + 80.0f;
	f32 y2 = y + vmode->xfbHeight + 80.0f;
	u32 color = RGBA(0, 0, 0, 255);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		GX_Position3f32(x, y, 0.0f);
		GX_Color1u32(color);
		GX_Position3f32(x2, y, 0.0f);
		GX_Color1u32(color);
		GX_Position3f32(x2, y2, 0.0f);
		GX_Color1u32(color);
		GX_Position3f32(x, y2, 0.0f);
		GX_Color1u32(color);
	GX_End();
	RenderGX();
#if DEBUG
	printf("Render thread stopped%s.\n", wait ? " (waited for fade-out)" : "");
#endif
}

void SetProgress(int percent) {
	progress = CLAMP(percent, 0, 100);
}

void InitVideo () {
	VIDEO_Init();
	vmode = VIDEO_GetPreferredMode(NULL);

	if (CONF_GetAspectRatio() == CONF_ASPECT_16_9) {
		vmode->viWidth = 678;
		vmode->viXOrigin = (VI_MAX_WIDTH_NTSC - 678) / 2;
	}
	
	VIDEO_Configure(vmode);

	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));

	VIDEO_ClearFrameBuffer(vmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(vmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb[0]);

	memset(&gp_fifo, 0, DEFAULT_FIFO_SIZE);
	GX_Init(&gp_fifo, DEFAULT_FIFO_SIZE);
	// black background
	GX_SetCopyClear((GXColor){ 0, 0, 0, 255 }, GX_MAX_Z24);

	GX_SetDispCopyGamma(GX_GM_1_0);
	GX_SetCullMode(GX_CULL_NONE);

	Mtx44 p;
	f32 yscale;
	u32 xfbHeight;

	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (vmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
	else
		while (VIDEO_GetNextField())
			VIDEO_WaitVSync();

	// GX setup
	yscale = GX_GetYScaleFactor(vmode->efbHeight, vmode->xfbHeight);
	xfbHeight = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0,0,vmode->fbWidth, vmode->efbHeight);
	GX_SetDispCopySrc(0,0,vmode->fbWidth, vmode->efbHeight);
	GX_SetDispCopyDst(vmode->fbWidth, xfbHeight);
	GX_SetCopyFilter(vmode->aa, vmode->sample_pattern, GX_TRUE, vmode->vfilter);
	GX_SetFieldMode(vmode->field_rendering, ((vmode->viHeight == 2 * vmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

	if (vmode->aa)
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	else
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	GX_ClearVtxDesc();
	GX_InvVtxCache();
	GX_InvalidateTexAll();

	GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);

	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_TRUE);

	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	guMtxIdentity(GXmodelView2D);
	guMtxTransApply(GXmodelView2D, GXmodelView2D, 0.0F, 0.0F, -200.0F);
	GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);

	guOrtho(p, 0, 479, 0, 639, 0, 300);
	GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);

	GX_SetViewport(0, 0, vmode->fbWidth, vmode->efbHeight, 0, 1);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaUpdate(GX_TRUE);
}

void DeinitVideo() {
	GX_AbortFrame();
	GX_Flush();

	VIDEO_SetBlack(true);
	VIDEO_Flush();
	VIDEO_WaitVSync();
}

void EnableConsole() {
	// centered at bottom
	CON_InitEx(vmode, 72, 280, vmode->fbWidth - 72 * 2, vmode->xfbHeight - 280 - 60);
}
