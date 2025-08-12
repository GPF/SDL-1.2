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

    based on SDL_nullevents.c by

    Sam Lantinga
    slouken@libsdl.org
	
	Modified by Lawrence Sebald <bluecrab2887@netscape.net>
*/
#include "SDL_config.h"

#include "SDL.h"
#include "SDL_sysevents.h"
#include "SDL_events_c.h"
#include "SDL_dcvideo.h"
#include "SDL_dcevents_c.h"

#include <dc/maple.h>
#include <dc/maple/mouse.h>
#include <dc/maple/keyboard.h>
#include <stdio.h>
#include <arch/arch.h>
#include <arch/timer.h>
#include <arch/irq.h>

#define MIN_FRAME_UPDATE 16

extern unsigned __sdl_dc_mouse_shift;

const static unsigned short sdl_key[] = {
    /* 0x00 */ 0, 0, 0, 0,
    /* 0x04 */ SDLK_a, SDLK_b, SDLK_c, SDLK_d, SDLK_e, SDLK_f, SDLK_g, SDLK_h, SDLK_i,
    /* 0x0d */ SDLK_j, SDLK_k, SDLK_l, SDLK_m, SDLK_n, SDLK_o, SDLK_p, SDLK_q, SDLK_r,
    /* 0x16 */ SDLK_s, SDLK_t, SDLK_u, SDLK_v, SDLK_w, SDLK_x, SDLK_y, SDLK_z,
    /* 0x1e */ SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9, SDLK_0,
    /* 0x28 */ SDLK_RETURN, SDLK_ESCAPE, SDLK_BACKSPACE, SDLK_TAB, SDLK_SPACE,
    /* 0x2d */ SDLK_MINUS, SDLK_EQUALS, SDLK_LEFTBRACKET, SDLK_RIGHTBRACKET, SDLK_BACKSLASH,
    /* 0x32 */ 0, SDLK_SEMICOLON, SDLK_QUOTE, SDLK_BACKQUOTE,
    /* 0x36 */ SDLK_COMMA, SDLK_PERIOD, SDLK_SLASH, SDLK_CAPSLOCK,
    /* 0x3a */ SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6,
    /* 0x40 */ SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12,
    /* 0x46 */ SDLK_PRINT, SDLK_SCROLLOCK, SDLK_PAUSE,
    /* 0x49 */ SDLK_INSERT, SDLK_HOME, SDLK_PAGEUP,
    /* 0x4c */ SDLK_DELETE, SDLK_END, SDLK_PAGEDOWN,
    /* 0x4f */ SDLK_RIGHT, SDLK_LEFT, SDLK_DOWN, SDLK_UP,
    /* 0x53 */ SDLK_NUMLOCK,
    /* 0x54 */ SDLK_KP_DIVIDE, SDLK_KP_MULTIPLY, SDLK_KP_MINUS, SDLK_KP_PLUS,
    /* 0x58 */ SDLK_KP_ENTER,
    /* 0x59 */ SDLK_KP1, SDLK_KP2, SDLK_KP3, SDLK_KP4, SDLK_KP5, SDLK_KP6,
    /* 0x5f */ SDLK_KP7, SDLK_KP8, SDLK_KP9, SDLK_KP0, SDLK_KP_PERIOD,
    /* 0x64 */ 0 /* S3 */
};
// const static unsigned short sdl_shift[] = {
// 	SDLK_LCTRL,SDLK_LSHIFT,SDLK_LALT,0 /* S1 */,
// 	SDLK_RCTRL,SDLK_RSHIFT,SDLK_RALT,0 /* S2 */,
// };

#define	MOUSE_WHEELUP 	(1<<4)
#define	MOUSE_WHEELDOWN	(1<<5)

const static char sdl_mousebtn[] = {
	MOUSE_LEFTBUTTON,
	MOUSE_RIGHTBUTTON,
	MOUSE_SIDEBUTTON,
	MOUSE_WHEELUP,
	MOUSE_WHEELDOWN
};

static void mouse_update(void) {
    maple_device_t *dev;
    mouse_state_t *state;
	
	static int prev_buttons;
	int buttons,changed;
	int i,dx,dy;
	
	//DC: Check if any mouse is connected
	if(!(dev = maple_enum_type(0, MAPLE_FUNC_MOUSE)) ||
       !(state = maple_dev_status(dev)))
        return;
	
	buttons = state->buttons^0xff;
	if (state->dz<0) buttons|=MOUSE_WHEELUP;
	if (state->dz>0) buttons|=MOUSE_WHEELDOWN;

	dx=state->dx>>__sdl_dc_mouse_shift;
	dy=state->dy>>__sdl_dc_mouse_shift;
	if (dx||dy)
		SDL_PrivateMouseMotion(0,1,dx,dy);

	changed = buttons^prev_buttons;
	for(i=0;i<sizeof(sdl_mousebtn);i++) {
		if (changed & sdl_mousebtn[i]) {
			//Do not flip state.
			SDL_PrivateMouseButton((buttons & sdl_mousebtn[i])?SDL_PRESSED:SDL_RELEASED,i,0,0);
		}
	}
	prev_buttons = buttons;
}

static void keyboard_update(void) {
    static kbd_mods_t last_mods = {0};
    maple_device_t *dev;
    SDL_keysym keysym = {0};

    if(!(dev = maple_enum_type(0, MAPLE_FUNC_KEYBOARD)))
        return;

    while(1) {
        int raw = kbd_queue_pop(dev, 0);
        if(raw == KBD_QUEUE_END) break;

        // Decode the raw keyboard event
        kbd_key_t key = (kbd_key_t)(raw & 0xFF);
        kbd_mods_t mods = { .raw = (raw >> 8) & 0xFF };
        uint8_t key_state = (raw >> 16) & 0xFF; // Extract key state (press/release)
        
        // Calculate modifier changes
        uint8_t mod_diff = mods.raw ^ last_mods.raw;
        
        // Handle modifier keys first
        if(mod_diff) {
            if(mod_diff & KBD_MOD_LSHIFT) {
                keysym.sym = SDLK_LSHIFT;
                SDL_PrivateKeyboard(mods.lshift ? SDL_PRESSED : SDL_RELEASED, &keysym);
            }
            if(mod_diff & KBD_MOD_RSHIFT) {
                keysym.sym = SDLK_RSHIFT;
                SDL_PrivateKeyboard(mods.rshift ? SDL_PRESSED : SDL_RELEASED, &keysym);
            }
            if(mod_diff & KBD_MOD_LCTRL) {
                keysym.sym = SDLK_LCTRL;
                SDL_PrivateKeyboard(mods.lctrl ? SDL_PRESSED : SDL_RELEASED, &keysym);
            }
            if(mod_diff & KBD_MOD_RCTRL) {
                keysym.sym = SDLK_RCTRL;
                SDL_PrivateKeyboard(mods.rctrl ? SDL_PRESSED : SDL_RELEASED, &keysym);
            }
            if(mod_diff & KBD_MOD_LALT) {
                keysym.sym = SDLK_LALT;
                SDL_PrivateKeyboard(mods.lalt ? SDL_PRESSED : SDL_RELEASED, &keysym);
            }
            if(mod_diff & KBD_MOD_RALT) {
                keysym.sym = SDLK_RALT;
                SDL_PrivateKeyboard(mods.ralt ? SDL_PRESSED : SDL_RELEASED, &keysym);
            }
            last_mods = mods;
        }

        // Skip if no key pressed/released
        if(key == KBD_KEY_NONE)
            continue;

        // Map KOS key to SDL key
        if(key >= 0 && key < (int)(sizeof(sdl_key)/sizeof(sdl_key[0]))) {
            if(sdl_key[key] == 0) continue;
            
            keysym.sym = sdl_key[key];
            
            // Determine if this is a press or release event
            // Bit 0 of key_state indicates pressed (1) or released (0)
            SDL_PrivateKeyboard((key_state & 0x1) ? SDL_PRESSED : SDL_RELEASED, &keysym);
        }
    }
}

static __inline__ Uint32 myGetTicks(void)
{
	return ((timer_us_gettime64()>>10));
}

void DC_PumpEvents(_THIS)
{
	static Uint32 last_time=0;
	Uint32 now=myGetTicks();
	if (now-last_time>MIN_FRAME_UPDATE)
	{
		keyboard_update();
		mouse_update();
		last_time=now;
	}
}

void DC_InitOSKeymap(_THIS)
{
	/* do nothing. */
}

/* end of SDL_dcevents.c ... */

