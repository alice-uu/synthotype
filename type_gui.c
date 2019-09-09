#include <SDL2/SDL_image.h>
#include <SDL2/SDL.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <ctype.h>
#include <math.h>
#include "type.h"

#define INIT_WIN_NAME "Synthotype"
#define INIT_WIN_WIDTH 640
#define INIT_WIN_HEIGHT 480

#define BACKGROUND_R 0x33
#define BACKGROUND_G 0x33
#define BACKGROUND_B 0x33
#define BACKGROUND_A 0xff

#define DOC_R 0xff
#define DOC_G 0xff
#define DOC_B 0xff
#define DOC_A 0xff

#define TAB_R 0x33
#define TAB_G 0x66
#define TAB_B 0xff
#define TAB_A 0x66

#define MARGIN_R 0x00
#define MARGIN_G 0x00
#define MARGIN_B 0x00
#define MARGIN_A 0xff

#define SELECT_R 0x00
#define SELECT_G 0x33
#define SELECT_B 0x66
#define SELECT_A 0x66

#define BLINK_TIME 500

SDL_Window *window;
SDL_Renderer *renderer;

SDL_Texture *texture;
Uint32 texture_format;
int texture_access;
int texture_w;
int texture_h;

SDL_Rect frame;
SDL_Rect cursor;
SDL_Color cursor_rgb;

int gui_cleanup ()
{
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	SDL_Quit();

	window = NULL;
	renderer = NULL;
}

int gui_init ()
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr,
		        "SDL could not be initialized.\n"
		        "SDL_Error: %s\n",
		        SDL_GetError());
		return 0;
	}

	window = SDL_CreateWindow(INIT_WIN_NAME,
	                          SDL_WINDOWPOS_UNDEFINED,
	                          SDL_WINDOWPOS_UNDEFINED,
	                          INIT_WIN_WIDTH,
	                          INIT_WIN_HEIGHT,
	                          SDL_WINDOW_SHOWN);

	if (!window) {
		fprintf(stderr,
		        "Could not create window.\n"
		        "SDL_Error: %s\n",
		        SDL_GetError());
		return 0;
	}

	renderer = SDL_CreateRenderer(window,
	                              -1,
	                              SDL_RENDERER_ACCELERATED);

	if (!renderer) {
		fprintf(stderr,
		        "Could not create renderer.\n"
		        "SDL_Error: %s\n",
		        SDL_GetError());
		return 0;
	}

	SDL_SetRenderDrawBlendMode(renderer,
	                           SDL_BLENDMODE_BLEND);

	return 1;
}

void gui_draw (unsigned char blink)
{
	SDL_SetRenderDrawColor(renderer,
	                       BACKGROUND_R,
	                       BACKGROUND_G,
	                       BACKGROUND_B,
	                       BACKGROUND_A);
	SDL_RenderClear(renderer);

	if (texture) {
		SDL_SetRenderDrawColor(renderer,
		                       DOC_R,
		                       DOC_G,
		                       DOC_B,
		                       DOC_A);
		SDL_RenderFillRect(renderer, &frame);
		SDL_RenderCopy(renderer, texture, NULL, &frame);

		SDL_SetRenderDrawColor(renderer,
		                       SELECT_R,
		                       SELECT_G,
		                       SELECT_B,
		                       SELECT_A);
		for (struct select *select = curbuf->select;
		     select;
		     select = select->next) {
			SDL_RenderFillRect(renderer, &select->rect);
		}

		if (blink) {
			if (mode & CLIP_ON) {
				SDL_RenderCopy(renderer,
				               clipboard.texture,
				               NULL,
				               &cursor);
			} else {
				SDL_SetRenderDrawColor(renderer,
				                       cursor_rgb.r,
				                       cursor_rgb.g,
				                       cursor_rgb.b,
				                       0xff);
				SDL_RenderFillRect(renderer, &cursor);
			}
		}

		SDL_SetRenderDrawColor(renderer,
		                       MARGIN_R,
		                       MARGIN_G,
		                       MARGIN_B,
		                       MARGIN_A);
		SDL_RenderDrawRect(renderer, &curbuf->margin);

		SDL_SetRenderDrawColor(renderer,
		                       TAB_R,
		                       TAB_G,
		                       TAB_B,
		                       TAB_A);
		for (int i = 0; i < curbuf->doc.rows; i++) {
			if (curbuf->v_tab[i]) {
				SDL_RenderDrawLine(renderer,
				                   frame.x,
				                   curbuf->v_tab[i],
				                   frame.x + frame.w,
				                   curbuf->v_tab[i]);
			}
		}
		for (int i = 0; i < curbuf->doc.cols; i++) {
			if (curbuf->h_tab[i]) {
				SDL_RenderDrawLine(renderer,
				                   curbuf->h_tab[i],
				                   frame.y,
				                   curbuf->h_tab[i],
				                   frame.y + frame.h);
			}
		}
	}

	SDL_RenderPresent(renderer);
}

void gui_loop ()
{
	if (!gui_init()) {
		gui_cleanup();
		return;
	}

	choose_buffer(allbuf);
	zoom_to_fit(allbuf);

	Uint32 ticks = SDL_GetTicks();
	Uint32 newticks;
	unsigned char blink;

	SDL_Event e;

	while (run) {
		newticks = SDL_GetTicks();
		if (newticks > ticks + BLINK_TIME) {
			ticks = newticks;
			blink = 1 - blink;
		}

		gui_draw(blink);

		while (SDL_PollEvent(&e) != 0) {
			if (e.type == SDL_QUIT) {
				run = 0;
			} else if (e.type == SDL_KEYDOWN) {
				handle_keypress(SDL_GetModState(),
				                e.key.keysym.sym);
			}
		}

		control_handle(curbuf);
	}

	gui_cleanup();
}
