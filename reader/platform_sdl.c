/*
 * Backend: a window on the Mac that stands in for the Nook's screen.
 * Arrow keys, space and page up/down turn pages; the left mouse
 * button is the finger (click to tap, drag to scroll); the wheel or
 * trackpad scrolls; Esc, Home or h is the home button; l is the
 * power button (tap to lock, hold for the power-off menu); q or
 * closing the window quits. The battery is the Mac's own.
 */
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include "platform.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

/* One 32-bit pixel per screen pixel: the format the texture takes. */
static Uint32 *argb;

static int sdl_init(int *width, int *height)
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return -1;
	}

	/*
	 * 1024 pixels is taller than most laptop screens, so shrink the
	 * window to fit. The reader still sees a 600x1024 screen: the
	 * logical size set below scales drawing down and clicks back up.
	 */
	int win_w = MR_NOOK_W, win_h = MR_NOOK_H;
	SDL_Rect usable;
	if (SDL_GetDisplayUsableBounds(0, &usable) == 0 &&
	    usable.h - 40 < MR_NOOK_H) {
		/* 40 leaves room for the title bar */
		win_h = usable.h - 40;
		win_w = MR_NOOK_W * win_h / MR_NOOK_H;
	}

	window = SDL_CreateWindow("MyReader",
				  SDL_WINDOWPOS_CENTERED,
				  SDL_WINDOWPOS_CENTERED,
				  win_w, win_h, SDL_WINDOW_RESIZABLE);
	if (window)
		renderer = SDL_CreateRenderer(window, -1, 0);
	if (renderer)
		texture = SDL_CreateTexture(renderer,
					    SDL_PIXELFORMAT_ARGB8888,
					    SDL_TEXTUREACCESS_STREAMING,
					    MR_NOOK_W, MR_NOOK_H);
	argb = malloc(sizeof(Uint32) * MR_NOOK_W * MR_NOOK_H);
	if (!texture || !argb) {
		fprintf(stderr, "SDL setup: %s\n", SDL_GetError());
		return -1;
	}
	SDL_RenderSetLogicalSize(renderer, MR_NOOK_W, MR_NOOK_H);

	*width = MR_NOOK_W;
	*height = MR_NOOK_H;
	return 0;
}

static void redraw(void)
{
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

static void sdl_present(const unsigned char *rgb, int stride)
{
	/* RGB bytes to ARGB words, alpha fully opaque */
	for (int y = 0; y < MR_NOOK_H; y++) {
		const unsigned char *row = rgb + y * stride;
		Uint32 *out = argb + y * MR_NOOK_W;
		for (int x = 0; x < MR_NOOK_W; x++) {
			const unsigned char *p = row + x * 3;
			out[x] = 0xFF000000u | (Uint32)p[0] << 16 |
				 (Uint32)p[1] << 8 | p[2];
		}
	}
	SDL_UpdateTexture(texture, NULL, argb, MR_NOOK_W * sizeof(Uint32));
	redraw();
}

static void sdl_next_event(mr_event *ev, int timeout_ms)
{
	SDL_Event e, next;
	Uint32 deadline = SDL_GetTicks() + (Uint32)timeout_ms;

	/* Loop until something happens that the reader cares about. */
	for (;;) {
		if (timeout_ms < 0) {
			if (!SDL_WaitEvent(&e)) {
				ev->type = MR_EV_QUIT;
				return;
			}
		} else {
			/* events the reader ignores don't reset the clock */
			int left = (int)(deadline - SDL_GetTicks());
			if (left < 0 ||
			    !SDL_WaitEventTimeout(&e, left)) {
				ev->type = MR_EV_TIMEOUT;
				return;
			}
		}
		switch (e.type) {
		case SDL_QUIT:
			ev->type = MR_EV_QUIT;
			return;

		case SDL_KEYUP:
			if (e.key.keysym.sym == SDLK_l) {
				ev->type = MR_EV_POWER_UP;
				return;
			}
			break;

		case SDL_KEYDOWN:
			if (e.key.repeat)	/* held keys repeat */
				break;
			switch (e.key.keysym.sym) {
			case SDLK_l:
				ev->type = MR_EV_POWER;
				return;
			case SDLK_RIGHT:
			case SDLK_DOWN:
			case SDLK_SPACE:
			case SDLK_PAGEDOWN:
				ev->type = MR_EV_NEXT;
				return;
			case SDLK_LEFT:
			case SDLK_UP:
			case SDLK_PAGEUP:
			case SDLK_BACKSPACE:
				ev->type = MR_EV_PREV;
				return;
			case SDLK_ESCAPE:
			case SDLK_HOME:
			case SDLK_h:
				ev->type = MR_EV_HOME;
				return;
			case SDLK_q:
				ev->type = MR_EV_QUIT;
				return;
			}
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			/* the left button plays the finger */
			if (e.button.button != SDL_BUTTON_LEFT)
				break;
			ev->type = e.type == SDL_MOUSEBUTTONDOWN ?
				   MR_EV_DOWN : MR_EV_UP;
			ev->x = e.button.x;
			ev->y = e.button.y;
			return;

		case SDL_MOUSEMOTION:
			if (!(e.motion.state & SDL_BUTTON_LMASK))
				break;
			/*
			 * A drag sends a flood of motion events. Skip to
			 * the newest one, so a slow redraw never falls
			 * behind the finger.
			 */
			while (SDL_PeepEvents(&next, 1, SDL_GETEVENT,
					      SDL_MOUSEMOTION,
					      SDL_MOUSEMOTION) == 1)
				e = next;
			ev->type = MR_EV_MOVE;
			ev->x = e.motion.x;
			ev->y = e.motion.y;
			return;

		case SDL_MOUSEWHEEL:
			/* wheel up (y > 0) moves toward the top */
			ev->type = MR_EV_SCROLL;
			ev->x = 0;
			ev->y = (int)(-e.wheel.preciseY * 40);
			if (ev->y != 0)
				return;
			break;

		case SDL_WINDOWEVENT:
			/*
			 * Window uncovered or resized: repaint from the
			 * texture, which still holds the last page. The
			 * reader never hears about it; the Nook has no
			 * windows, so this is the backend's problem.
			 */
			if (e.window.event == SDL_WINDOWEVENT_EXPOSED ||
			    e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
				redraw();
			break;
		}
	}
}

static void sdl_shutdown(void)
{
	free(argb);
	if (texture)
		SDL_DestroyTexture(texture);
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	SDL_Quit();
}

static int sdl_battery(int *charging)
{
	int pct = -1;
	SDL_PowerState s = SDL_GetPowerInfo(NULL, &pct);
	*charging = s == SDL_POWERSTATE_CHARGING ||
		    s == SDL_POWERSTATE_CHARGED;
	return pct;
}

/* "Asleep" on the Mac: the window goes black, like the Nook's screen. */
static void sdl_sleep(int on)
{
	if (!on) {
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		SDL_RenderPresent(renderer);
	}
}

const mr_platform mr_platform_sdl = {
	.name = "sdl",
	.init = sdl_init,
	.present = sdl_present,
	.next_event = sdl_next_event,
	.shutdown = sdl_shutdown,
	.battery = sdl_battery,
	.sleep = sdl_sleep,
};
