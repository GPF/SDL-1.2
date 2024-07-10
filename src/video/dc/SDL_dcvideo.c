/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    BERO
    bero@geocities.co.jp

    based on SDL_nullvideo.c by

    Sam Lantinga
    slouken@libsdl.org
*/ 

#include <kos.h> //For 60Hz mode
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "SDL_error.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_sysvideo.h"
#include "SDL_pixels_c.h"
#include "SDL_events_c.h"

#include "SDL_dreamcast.h" //DMA mode

#include "SDL_dcvideo.h"
#include "SDL_dcevents_c.h"
#include "SDL_dcmouse_c.h"

#include <dc/video.h>
#include <dc/pvr.h>

#ifdef SDL_VIDEO_OPENGL
#include "GL/gl.h"
#include "GL/glu.h"
#include "GL/glkos.h"
#endif

//Custom code for 60Hz
static int sdl_dc_no_ask_60hz=0;
static int sdl_dc_default_60hz=0;
#include "60hz.h"

void SDL_DC_ShowAskHz(SDL_bool value)
{
	sdl_dc_no_ask_60hz=!value;
}

void SDL_DC_Default60Hz(SDL_bool value)
{
	sdl_dc_default_60hz=value;
}

unsigned __sdl_dc_mouse_shift=1;


/* Initialization/Query functions */
static int DC_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **DC_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *DC_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static int DC_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors);
static void DC_VideoQuit(_THIS);

/* Hardware surface functions */
static int DC_AllocHWSurface(_THIS, SDL_Surface *surface);
static int DC_LockHWSurface(_THIS, SDL_Surface *surface);
static void DC_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void DC_FreeHWSurface(_THIS, SDL_Surface *surface);
static int DC_FlipHWSurface(_THIS, SDL_Surface *surface);

/* etc. */
static void DC_UpdateRects(_THIS, int numrects, SDL_Rect *rects);

/* OpenGL */
#ifdef SDL_VIDEO_OPENGL
static void *DC_GL_GetProcAddress(_THIS, const char *proc);
static int DC_GL_LoadLibrary(_THIS, const char *path);
static int DC_GL_GetAttribute(_THIS, SDL_GLattr attrib, int* value);
static void DC_GL_SwapBuffers(_THIS);
#endif

/* DC driver bootstrap functions */

static int DC_Available(void)
{
	return 1;
}

static void DC_DeleteDevice(SDL_VideoDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
}

static SDL_DC_driver sdl_dc_video_driver=SDL_DC_DMA_VIDEO;

void SDL_DC_SetVideoDriver(SDL_DC_driver value)
{
	sdl_dc_video_driver=value;
}

SDL_DC_driver SDL_DC_GetVideoDriver(void)
{
    return sdl_dc_video_driver;
}

static SDL_VideoDevice *DC_CreateDevice(int devindex)
{
	SDL_VideoDevice *device;

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
	if ( device ) {
		SDL_memset(device, 0, (sizeof *device));
		device->hidden = (struct SDL_PrivateVideoData *)
				SDL_malloc((sizeof *device->hidden));
	}
	if ( (device == NULL) || (device->hidden == NULL) ) {
		SDL_OutOfMemory();
		if ( device ) {
			SDL_free(device);
		}
		return(0);
	}
	SDL_memset(device->hidden, 0, (sizeof *device->hidden));

	/* Set the function pointers */
	device->VideoInit = DC_VideoInit;
	device->ListModes = DC_ListModes;
	device->SetVideoMode = DC_SetVideoMode;
	device->CreateYUVOverlay = NULL;
	device->SetColors = DC_SetColors;
	device->UpdateRects = DC_UpdateRects;
	device->VideoQuit = DC_VideoQuit;
	device->AllocHWSurface = DC_AllocHWSurface;
	device->CheckHWBlit = NULL;
	device->FillHWRect = NULL;
	device->SetHWColorKey = NULL;
	device->SetHWAlpha = NULL;
	device->LockHWSurface = DC_LockHWSurface;
	device->UnlockHWSurface = DC_UnlockHWSurface;
	device->FlipHWSurface = DC_FlipHWSurface;
	device->FreeHWSurface = DC_FreeHWSurface;
#ifdef SDL_VIDEO_OPENGL
	device->GL_LoadLibrary = DC_GL_LoadLibrary;
	device->GL_GetProcAddress = DC_GL_GetProcAddress;
	device->GL_GetAttribute = DC_GL_GetAttribute;
	device->GL_MakeCurrent = NULL;
	device->GL_SwapBuffers = DC_GL_SwapBuffers;
#endif
	device->SetCaption = NULL;
	device->SetIcon = NULL;
	device->IconifyWindow = NULL;
	device->GrabInput = NULL;
	device->GetWMInfo = NULL;
	device->InitOSKeymap = DC_InitOSKeymap;
	device->PumpEvents = DC_PumpEvents;

	device->free = DC_DeleteDevice;

	return device;
}

VideoBootStrap DC_bootstrap = {
	"dcvideo", "Dreamcast Video",
	DC_Available, DC_CreateDevice
};


int DC_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
	/* Determine the screen depth (use default 16-bit depth) */
	/* we change this during the SDL_SetVideoMode implementation... */
	vformat->BitsPerPixel = 16;
	vformat->Rmask = 0x0000f800;
	vformat->Gmask = 0x000007e0;
	vformat->Bmask = 0x0000001f;

	/* We're done! */
	return(0);
}

const static SDL_Rect
	// RECT_800x600 = {0,0,800,600},
	RECT_768x576 = {0,0,768,576},
	RECT_768x480 = {0,0,768,480},
	RECT_640x480 = {0,0,640,480},
	RECT_320x240 = {0,0,320,240},
	RECT_256x256 = {0,0,256,256};

const static SDL_Rect *vid_modes[] = {
	// &RECT_800x600,
	&RECT_768x576,
	&RECT_768x480,
	&RECT_640x480,
	&RECT_320x240,
	&RECT_256x256,
	NULL
};

//Textured modes (an extra variation)
const static SDL_Rect
	RECT_64x64 = {0,0,64,64},
	RECT_64x128 = {0,0,64,128},
	RECT_64x256 = {0,0,64,256},
	RECT_64x512 = {0,0,64,512},
	RECT_64x1024 = {0,0,64,1024};
	
const static SDL_Rect
	RECT_128x64 = {0,0,128,64},
	RECT_128x128 = {0,0,128,128},
	RECT_128x256 = {0,0,128,256},
	RECT_128x512 = {0,0,128,512},
	RECT_128x1024 = {0,0,128,1024};

const static SDL_Rect
	RECT_256x64 = {0,0,256,64},
	RECT_256x128 = {0,0,256,128},
//	RECT_256x256 = {0,0,256,256},
	RECT_256x512 = {0,0,256,512},
	RECT_256x1024 = {0,0,256,1024};

const static SDL_Rect
	RECT_512x64 = {0,0,512,64},
	RECT_512x128 = {0,0,512,128},
	RECT_512x256 = {0,0,512,256},
	RECT_512x512 = {0,0,512,512},
	RECT_512x1024 = {0,0,512,1024};

const static SDL_Rect
	RECT_1024x64 = {0,0,1024,64},
	RECT_1024x128 = {0,0,1024,128},
	RECT_1024x256 = {0,0,1024,256},
	RECT_1024x512 = {0,0,1024,512},
	RECT_1024x1024 = {0,0,1024,1024};


const static SDL_Rect *tex_modes[]={
	&RECT_64x64,
	&RECT_64x128,
	&RECT_64x256,
	&RECT_64x512,
	&RECT_64x1024,

	&RECT_128x64,
	&RECT_128x128,
	&RECT_128x256,
	&RECT_128x512,
	&RECT_128x1024,

	&RECT_256x64,
	&RECT_256x128,
	&RECT_256x256,
	&RECT_256x512,
	&RECT_256x1024,

	&RECT_512x64,
	&RECT_512x128,
	&RECT_512x256,
	&RECT_512x512,
	&RECT_512x1024,

	&RECT_1024x64,
	&RECT_1024x128,
	&RECT_1024x256,
	&RECT_1024x512,
	&RECT_1024x1024,
	NULL
};

SDL_Rect **DC_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
#ifndef SDL_VIDEO_OPENGL
	if ((!(flags & SDL_FULLSCREEN))&&(SDL_DC_GetVideoDriver()==SDL_DC_TEXTURED_VIDEO))
	{
		switch(format->BitsPerPixel)
		{
			case 16:
				SDL_Rect **thetex_modes = (SDL_Rect **)&tex_modes;
				return thetex_modes;
			default:
				return NULL;	
		}
	}
	else
#endif
		switch(format->BitsPerPixel)
		{
			case 15:
			case 16:
				return (SDL_Rect **)&vid_modes;
			case 32:
				if (!(flags & SDL_OPENGL))
					return (SDL_Rect **)&vid_modes;
			default:
				return NULL;
		}
	return((SDL_Rect **)-1);
}

static int sdl_dc_pvr_inited=0;
static int sdl_dc_wait_vblank=0;

void SDL_DC_VerticalWait(SDL_bool value)
{
	sdl_dc_wait_vblank=value;
}

#ifndef SDL_VIDEO_OPENGL
static int sdl_dc_textured=0;
static int sdl_dc_width=0;
static int sdl_dc_height=0;
static int sdl_dc_bpp=0;
static int sdl_dc_wtex=0;
static int sdl_dc_htex=0;
static pvr_ptr_t sdl_dc_memtex;
static unsigned short *sdl_dc_buftex;
static void *sdl_dc_memfreed;
#endif

static void *sdl_dc_dblfreed=NULL;
static void *sdl_dc_dblmem=NULL;
static unsigned sdl_dc_dblsize=0;

int __sdl_dc_is_60hz=0;

SDL_Surface *DC_SetVideoMode(_THIS, SDL_Surface *current,
				int width, int height, int bpp, Uint32 flags)
{
	int disp_mode,pixel_mode,pitch;
	Uint32 Rmask, Gmask, Bmask;

	
#ifndef SDL_VIDEO_OPENGL
	if ((!(flags&SDL_FULLSCREEN))&&(SDL_DC_GetVideoDriver()==SDL_DC_TEXTURED_VIDEO))
	{
		// fprintf(stderr, "\nWelcome to SDLdc = SDL_DC_TEXTURED_VIDEO");
		for(sdl_dc_wtex=64;sdl_dc_wtex<width;sdl_dc_wtex<<=1);
		for(sdl_dc_htex=64;sdl_dc_htex<height;sdl_dc_htex<<=1);
		if (sdl_dc_wtex!=width || sdl_dc_htex!=height || bpp !=16) 
		{
			sdl_dc_textured=0;
			SDL_SetError("Couldn't find requested mode in list");
			return(NULL);
		}
		sdl_dc_width=width;
		sdl_dc_height=height;
		sdl_dc_bpp=bpp;
		width=640;
		height=480;
		bpp=16;
		sdl_dc_textured=-1;
		// fprintf(stderr, "\nBye to SDLdc = SDL_DC_TEXTURED_VIDEO - sdl_dc_width %d ,sdl_dc_height %d ,sdl_dc_bpp %d",sdl_dc_width,sdl_dc_height,sdl_dc_bpp);
	}
	else
		sdl_dc_textured=0;
#endif

	if (!vid_check_cable())
	{
		__sdl_dc_is_60hz=1;
		if (width==320 && height==240) disp_mode=DM_320x240_VGA;
		else if (width==640 && height==480) disp_mode=DM_640x480_VGA;
		// else if (width==800 && height==600) disp_mode=DM_800x608_VGA;
		else if (width==768 && height==480) disp_mode=DM_768x480_PAL_IL;
		else if (width==768 && height==576) disp_mode=DM_768x576_PAL_IL;
		else if (width==256 && height==256) disp_mode=DM_256x256_PAL_IL;
		else {
			SDL_SetError("Couldn't find requested mode in list");
			return(NULL);
		}
	}
	else
	if (flashrom_get_region()!=FLASHROM_REGION_US && !sdl_dc_ask_60hz())
	{
		__sdl_dc_is_60hz=0;
		if (width==320 && height==240) disp_mode=DM_320x240_PAL;
		else if (width==640 && height==480) disp_mode=DM_640x480_PAL_IL;
		// else if (width==800 && height==600) disp_mode=DM_800x608;
		else if (width==768 && height==480) disp_mode=DM_768x480_PAL_IL;
		else if (width==768 && height==576) disp_mode=DM_768x576_PAL_IL;
		else if (width==256 && height==256) disp_mode=DM_256x256_PAL_IL;
		else {
			SDL_SetError("Couldn't find requested mode in list");
			return(NULL);
		}
	}
	else
	{
		__sdl_dc_is_60hz=1;
		if (width==320 && height==240) disp_mode=DM_320x240;
		else if (width==640 && height==480) disp_mode=DM_640x480;
		// else if (width==800 && height==600) disp_mode=DM_800x608;
		else if (width==768 && height==480) disp_mode=DM_768x480;
		else if (width==768 && height==576) disp_mode=DM_768x576_PAL_IL;
		else if (width==256 && height==256) disp_mode=DM_256x256_PAL_IL;
		else {
			SDL_SetError("Couldn't find requested mode in list");
			return(NULL);
		}
	}

	switch(bpp) {
	case 15: pixel_mode = PM_RGB555; pitch = width*2;
		/* 5-5-5 */
		Rmask = 0x00007c00;
		Gmask = 0x000003e0;
		Bmask = 0x0000001f;
		break;
	case 16: pixel_mode = PM_RGB565; pitch = width*2;
		/* 5-6-5 */
		Rmask = 0x0000f800;
		Gmask = 0x000007e0;
		Bmask = 0x0000001f;
		break;
	case 24: bpp = 32;
	case 32: pixel_mode = PM_RGB888; pitch = width*4;
		Rmask = 0x00ff0000;
		Gmask = 0x0000ff00;
		Bmask = 0x000000ff;
#ifdef	SDL_VIDEO_OPENGL
		if (!(flags & SDL_OPENGL))
#endif
		break;
	default:
		SDL_SetError("Couldn't find requested mode in list");
		return(NULL);
	}


	if ( bpp != current->format->BitsPerPixel )
		if ( ! SDL_ReallocFormat(current, bpp, Rmask, Gmask, Bmask, 0) )
			return(NULL);


	/* Set up the new mode framebuffer */
#ifndef SDL_VIDEO_OPENGL
	if (sdl_dc_textured)
	{
		// fprintf(stdout, "\nWelcome to SDLdc3! -sdl_dc_width :%d , sdl_dc_height %d \n",sdl_dc_width,sdl_dc_height);
		current->w=sdl_dc_width;
		current->h=sdl_dc_height;
		current->pitch = sdl_dc_width*2; //(bpp>>3);
		__sdl_dc_mouse_shift=640/sdl_dc_width;
	}
	else
#endif
	{
		current->flags = (SDL_FULLSCREEN|SDL_HWSURFACE);
		current->w = width;
		current->h = height;
		current->pitch = pitch;
		__sdl_dc_mouse_shift=640/width;
	}

	if (sdl_dc_pvr_inited) 
		DC_VideoQuit(NULL);

	vid_set_mode(disp_mode,pixel_mode);
	current->pixels = vram_l; //vram_s;

#ifndef SDL_VIDEO_OPENGL
	if (sdl_dc_textured)
	{
		pvr_init_defaults();
		pvr_dma_init();
		sdl_dc_pvr_inited = 1;
		sdl_dc_memtex = pvr_mem_malloc(sdl_dc_wtex*sdl_dc_htex*2);
		if (flags & SDL_DOUBLEBUF)
		{
			current->flags |= SDL_DOUBLEBUF;
			sdl_dc_memfreed = calloc(64+(sdl_dc_wtex*sdl_dc_htex*(sdl_dc_bpp>>3)),1);
			sdl_dc_buftex = (unsigned short *)(((((unsigned)sdl_dc_memfreed)+32)/32)*32);
			current->pixels = sdl_dc_buftex;
		}
		else
		{
			sdl_dc_buftex = 0;
			current->pixels = sdl_dc_memtex;
		}
	}
	else
#else
	if (flags & SDL_OPENGL) {
		this->gl_config.driver_loaded = 1;
		current->flags = SDL_FULLSCREEN | SDL_OPENGL;
		current->pixels = NULL;
		sdl_dc_pvr_inited = 1;
		glKosInit();
	} else
#endif
	if (flags & SDL_DOUBLEBUF) {
		current->flags |= SDL_DOUBLEBUF;
#ifndef SDL_VIDEO_OPENGL
		if (SDL_DC_GetVideoDriver()!=SDL_DC_DIRECT_VIDEO)
		{
			pvr_dma_init();
			sdl_dc_pvr_inited = 1;
		}
#endif
		sdl_dc_dblsize=(width*height*(bpp>>3));
		sdl_dc_dblfreed = calloc(128+sdl_dc_dblsize,1);
		sdl_dc_dblmem = (unsigned short *)(((((unsigned)sdl_dc_dblfreed)+64)/64)*64);
		current->pixels = sdl_dc_dblmem;
	}

	/* We're done */
	return(current);
}

/* We don't actually allow hardware surfaces other than the main one */
static int DC_AllocHWSurface(_THIS, SDL_Surface *surface)
{
	return(-1);
}
static void DC_FreeHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

/* We need to wait for vertical retrace on page flipped displays */
static int DC_LockHWSurface(_THIS, SDL_Surface *surface)
{
	return(0);
}

static void DC_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

#ifndef SDL_VIDEO_OPENGL

static float sdl_dc_u1=0.3f;
static float sdl_dc_u2=0.3f;
static float sdl_dc_v1=0.9f;
static float sdl_dc_v2=0.6f;

#endif

void SDL_DC_SetWindow(int width, int height)
{
#ifndef SDL_VIDEO_OPENGL
	// fprintf(stdout, "\nWelcome to SDL_DC_SetWindow width: %d , height : %d, sdl_dc_width: %d , sdl_dc_height: %d \n", width, height, sdl_dc_width, sdl_dc_height);
	sdl_dc_width=width;
	sdl_dc_height=height;
	sdl_dc_u1=0.3f*(1.0f/((float)sdl_dc_wtex));
	sdl_dc_v1=0.3f*(1.0f/((float)sdl_dc_wtex));
	sdl_dc_u2=(((float)sdl_dc_width)+0.7f)*(1.0f/((float)sdl_dc_wtex));
	sdl_dc_v2=(((float)sdl_dc_height)+0.7f)*(1.0f/((float)sdl_dc_htex));
#endif
}

#ifndef SDL_VIDEO_OPENGL
static void sdl_dc_blit_textured(void)
{
#define DX1 0.0f
#define DY1 0.0f
#define DZ1 1.0f
#define DWI 640.0f
#define DHE 480.0f

	pvr_poly_hdr_t hdr;
	pvr_vertex_t vert;
	pvr_poly_cxt_t cxt;

	if (sdl_dc_wait_vblank)
		pvr_wait_ready();
	pvr_scene_begin();
	pvr_list_begin(PVR_LIST_OP_POLY);
	if (sdl_dc_buftex)
	{
		dcache_flush_range((unsigned)sdl_dc_buftex,sdl_dc_wtex*sdl_dc_htex*2);
		while (!pvr_dma_ready());
		pvr_txr_load_dma(sdl_dc_buftex, sdl_dc_memtex, sdl_dc_wtex*sdl_dc_htex*2,-1,NULL,0);
//		pvr_txr_load(sdl_dc_buftex, sdl_dc_memtex, sdl_dc_wtex*sdl_dc_htex*2);
	}
	pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565|PVR_TXRFMT_NONTWIDDLED,sdl_dc_wtex, sdl_dc_htex, sdl_dc_memtex, PVR_FILTER_NEAREST);
	pvr_poly_compile(&hdr, &cxt);
	pvr_prim(&hdr, sizeof(hdr));
	vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
	vert.oargb = 0;
	vert.flags = PVR_CMD_VERTEX;
	vert.x = DX1; vert.y = DY1; vert.z = DZ1; vert.u = sdl_dc_u1; vert.v = sdl_dc_v1;
	pvr_prim(&vert, sizeof(vert));
	vert.x = DX1+DWI; vert.y = DY1; vert.z = DZ1; vert.u = sdl_dc_u2; vert.v = sdl_dc_v1;
	pvr_prim(&vert, sizeof(vert));
	vert.x = DX1; vert.y = DY1+DHE; vert.z = DZ1; vert.u = sdl_dc_u1; vert.v = sdl_dc_v2;
	pvr_prim(&vert, sizeof(vert));
	vert.x = DX1+DWI; vert.y = DY1+DHE; vert.z = DZ1; vert.u = sdl_dc_u2; vert.v = sdl_dc_v2;
	vert.flags = PVR_CMD_VERTEX_EOL;
	pvr_prim(&vert, sizeof(vert));
	pvr_list_finish();
	pvr_scene_finish();

#undef DX1
#undef DY1
#undef DZ1
#undef DWI
#undef DHE
}
#endif

static int DC_FlipHWSurface(_THIS, SDL_Surface *surface)
{

#ifndef SDL_VIDEO_OPENGL
	if (sdl_dc_textured)
		sdl_dc_blit_textured();
	else
	if (surface->flags & SDL_DOUBLEBUF) {
		if (sdl_dc_wait_vblank)
			vid_waitvbl();
		if (SDL_DC_GetVideoDriver()!=SDL_DC_DIRECT_VIDEO)
		{
			dcache_flush_range(sdl_dc_dblmem,sdl_dc_dblsize);
			while (!pvr_dma_ready());
			pvr_dma_transfer(sdl_dc_dblmem, vram_l, sdl_dc_dblsize,PVR_DMA_VRAM32,-1,NULL,NULL);
		}
		else
			sq_cpy(vram_l,sdl_dc_dblmem,sdl_dc_dblsize);
	}
	else
#endif
		if (sdl_dc_wait_vblank)
			vid_waitvbl();
	return(0);
}

static void DC_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
#ifndef SDL_VIDEO_OPENGL
	if (sdl_dc_textured){				
			sdl_dc_blit_textured();
			}
			// bfont_draw_str(vram_l+0 * 640+10, 640,0,"Q W E R T Y U I O P");
			// bfont_draw_str(vram_l+10 * 640+10, 640,0,"A S D F G H J K L : ' <RET>");
			// bfont_draw_str(vram_l+20 * 640+10, 640,0,"Z X C V B N M , . /");		

#endif
}

static int DC_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors)
{
	/* do nothing of note. */
	return(1);
}

/* Note:  If we are terminated, this could be called in the middle of
   another SDL video routine -- notably UpdateRects.
*/
static void DC_VideoQuit(_THIS)
{
	if (sdl_dc_pvr_inited) {
#ifdef HAVE_DCDMA
		while (!pvr_dma_ready());
#endif
		if (sdl_dc_dblmem)
			pvr_init_defaults();
//		pvr_dma_shutdown();
#ifdef HAVE_DCTEXTURED
		if (sdl_dc_buftex)
			free(sdl_dc_memfreed);
		sdl_dc_buftex=0;
		if (sdl_dc_memtex)
		{
			pvr_mem_free(sdl_dc_memtex);
			pvr_mem_reset();
		}
		sdl_dc_memtex=0;
#endif
		if (sdl_dc_dblmem)
			free(sdl_dc_dblfreed);
		sdl_dc_dblmem=NULL;
		pvr_shutdown();
		sdl_dc_pvr_inited = 0;
	}
	else
	{
		if (sdl_dc_dblmem)
			free(sdl_dc_dblfreed);
		sdl_dc_dblmem=NULL;
	}
}

#ifdef SDL_VIDEO_OPENGL

void dmyfunc(void) {}

typedef void (*funcptr)();
const static struct {
	char *name;
	funcptr addr;
} glfuncs[] = {
#define	DEF(func)	{#func,(void *)&func}
	DEF(glBegin),
	DEF(glBindTexture),
	DEF(glBlendFunc),
	DEF(glClear),
	DEF(glClearColor),
	DEF(glCullFace),
//	DEF(glClearDepth),
	DEF(glDepthFunc),
	DEF(glDepthMask),
	DEF(glScissor),
	DEF(glColor3f),
	DEF(glColor3fv),
	DEF(glColor4f),
	DEF(glColor4fv),
	DEF(glColor4ub),
	DEF(glNormal3f),
//	DEF(glCopyImageID),
	DEF(glDisable),
	DEF(glEnable),
	DEF(glFrontFace),
	DEF(glEnd),
	DEF(glFlush),
	DEF(glHint),
	DEF(glGenTextures),
	DEF(glGetString),
	DEF(glLoadIdentity),
	DEF(glLoadMatrixf),
	DEF(glLoadTransposeMatrixf),
	DEF(glMatrixMode),
	DEF(glMultMatrixf),
	DEF(glMultTransposeMatrixf),
	DEF(glOrtho),
	DEF(glPixelStorei),
//	DEF(glPopAttrib),
//	DEF(glPopClientAttrib),
	{"glPopAttrib",&dmyfunc},
	{"glPopClientAttrib",&dmyfunc},
	DEF(glPopMatrix),
//	DEF(glPushAttrib),
//	DEF(glPushClientAttrib),
	{"glPushAttrib",&dmyfunc},
	{"glPushClientAttrib",&dmyfunc},
	DEF(glPushMatrix),
	DEF(glRotatef),
	DEF(glScalef),
	DEF(glTranslatef),
	DEF(glTexCoord2f),
	DEF(glTexCoord2fv),
	DEF(glTexEnvi),
	DEF(glTexImage2D),
	DEF(glTexParameteri),
	DEF(glTexSubImage2D),
	DEF(glViewport),
	DEF(glShadeModel),
	DEF(glTexEnvf),
	DEF(glVertex2i)
#undef	DEF
};

static void *DC_GL_GetProcAddress(_THIS, const char *proc)
{
	int i;
/*
	void *ret;
	ret = glKosGetProcAddress(proc);
	if (ret) return ret;
*/

	for(i=0;i<sizeof(glfuncs)/sizeof(glfuncs[0]);i++) {
		if (SDL_strcmp(proc,glfuncs[i].name)==0) return glfuncs[i].addr;
	}

	return NULL;
}

static int DC_GL_LoadLibrary(_THIS, const char *path)
{
	this->gl_config.driver_loaded = 1;

	return 0;
}

static int DC_GL_GetAttribute(_THIS, SDL_GLattr attrib, int* value)
{
	int val;

	switch(attrib) {
	case SDL_GL_RED_SIZE:
		val = 5;
		break;
	case SDL_GL_GREEN_SIZE:
		val = 6;
		break;
	case SDL_GL_BLUE_SIZE:
		val = 5;
		break;
	case SDL_GL_ALPHA_SIZE:
		val = 0;
		break;
	case SDL_GL_DOUBLEBUFFER:
		val = 1;
		break;
	case SDL_GL_DEPTH_SIZE:
		val = 16; /* or 32? */
		break;
	case SDL_GL_STENCIL_SIZE:
		val = 0;
		break;
	case SDL_GL_ACCUM_RED_SIZE:
		val = 0;
		break;
	case SDL_GL_ACCUM_GREEN_SIZE:
		val = 0;
	case SDL_GL_ACCUM_BLUE_SIZE:
		val = 0;
		break;
	case SDL_GL_ACCUM_ALPHA_SIZE:
		val = 0;
		break;
	default :
		return -1;
	}
	*value = val;
	return 0;
}

static void DC_GL_SwapBuffers(_THIS)
{
	    // swap buffers to display, since we're double buffered.
    glKosSwapBuffers();
//	glutSwapBuffers();
}
#endif
