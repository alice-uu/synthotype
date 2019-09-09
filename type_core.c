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

#define INIT_FONT "font"
#define INIT_COLS 15
#define INIT_ROWS 20

#define BYTES_PER_PIXEL 4
#define BITS_PER_PIXEL 32
#define RMASK 0xff000000
#define GMASK 0x00ff0000
#define BMASK 0x0000ff00
#define AMASK 0x000000ff
#define TRANSPARENT_R 0xff
#define TRANSPARENT_G 0xff
#define TRANSPARENT_B 0xff
#define TRANSPARENT_A 0x00
#define TRANSPARENT_RGBA 0xffffffff
#define TEXTURE_FORMAT SDL_PIXELFORMAT_RGBA32

struct palette *new_palette (unsigned char num_colors)
{
	if (!num_colors) {
		fprintf(stderr,
		        "Palette must contain at least one color.\n"
		        "Could not create palette.\n");
		return NULL;
	}

	struct palette *palette = malloc(sizeof(struct palette));
	palette->cmy = malloc(num_colors * sizeof(struct cmy));

	if (!palette || !palette->cmy) {
		if (palette) free(palette);
		fprintf(stderr,
		        "Error allocating memory.\n"
		        "Could not create palette.\n");
		return NULL;
	}

	palette->ref = 1;
	palette->num_colors = num_colors;

	for (unsigned i = 0; i < num_colors; i++) {
		palette->cmy[i].c = 0xff;
		palette->cmy[i].m = 0xff;
		palette->cmy[i].y = 0xff;
	}

	return palette;
};

struct palette *copy_palette (struct palette *palette)
{
	if (palette) palette->ref += 1;
	return palette;
}

void destroy_palette (struct palette **palette_p)
{
	struct palette *palette = *palette_p;
	if (!palette) return;

	*palette_p = NULL;

	palette->ref -= 1;
	if (palette->ref)
		return;
	if (palette->cmy)
		free(palette->cmy);
	free(palette);

	*palette_p = NULL;
}

unsigned char *get_glyph (SDL_Surface *img, SDL_Rect block)
{
	Uint8 *p;
	Uint8 r, g, b;

	unsigned char glyph_found = 0;
	unsigned char *glyph;

	for (unsigned char confirmed = 0; confirmed < 2; confirmed++) {
		for (int y = 0; y < block.h; y++) {
			p = img->pixels +
			    (y + block.y) * img->pitch +
			    block.x * BYTES_PER_PIXEL;
			for (int x = 0; x < block.w; x++) {
				SDL_GetRGB(*((Uint32 *)p),
				           img->format,
				           &r, &g, &b);
				if (r + g + b >= 0xff) {
					if (confirmed)
						glyph[x + y * block.w] = 0;
				} else {
					if (confirmed) {
						glyph[x + y * block.w] = 1;
					} else {
						glyph_found = 1;
						// Short circuit the for loops
						x = block.w;
						y = block.h;
					}
				}
				p += BYTES_PER_PIXEL;
			}
		}

		if (!confirmed) {
			if (!glyph_found)
				return NULL;

			glyph = malloc(block.w * block.h);

			if (!glyph) {
				fprintf(stderr,
				        "Error allocating memory.\n"
				        "Could not load glyph.\n");
				return NULL;
			}
		}
	}

	return glyph;
}

struct font *load_font (unsigned char *path)
{
	if (!path) {
		fprintf(stderr,
		        "Path not given.\n"
		        "Could not load font.\n");
		return NULL;
	}

	SDL_Surface *img = IMG_Load(path);

	if (!img) {
		fprintf(stderr,
		        "Error loading image '%s'.\n"
		        "IMG_Error: %s\n",
		        path,
		        IMG_GetError());
		return NULL;
	}

	struct font *font = malloc(sizeof(struct font));

	if (!font) {
		SDL_FreeSurface(img);
		fprintf(stderr,
		        "Error allocating memory.\n"
		        "Could not load font '%s'.\n",
		        path);
		return NULL;
	}

	font->ref = 1;
	font->w = img->w / 16;
	font->h = img->h / 16;

	if (!font->w || !font->h) {
		fprintf(stderr,
		        "Glyphs are too small.\n"
		        "Could not load font '%s'.\n",
		        path);
		SDL_FreeSurface(img);
		free(font);
		return NULL;
	}

	SDL_Rect block = {0, 0, font->w, font->h};

	for (int j = 0; j < 16; j++) {
		for (int i = 0; i < 16; i++) {
			block.x = (i * img->w) / 16;
			block.y = (j * img->h) / 16;
			font->glyph[i + j * 16] = get_glyph(img, block);
		}
	}

	return font;
}

struct font *copy_font (struct font *font)
{
	if (font) font->ref += 1;
	return font;
}

void destroy_font (struct font **font_p)
{
	struct font *font = *font_p;
	if (!font) return;

	*font_p = NULL;

	font->ref -= 1;
	if (font->ref)
		return;
	if (font->glyph) {
		for (unsigned i = 0; i < 256; i++) {
			if (font->glyph[i])
				free(font->glyph[i]);
		}
		free(font->glyph);
	}
	free(font);
}

void destroy_doc (struct doc *doc)
{
	destroy_font(&doc->font);
	destroy_palette(&doc->palette);

	struct stroke *next;

	if (doc->stroke) {
		for (int j = 0; j < doc->rows; j++) {
			for (int i = 0; i < doc->cols; i++) {
				for (struct stroke *stroke =
					doc->stroke[i + j * doc->cols];
				     stroke;
				     stroke = next) {
					next = stroke->next;
				}
			}
		}
		free(doc->stroke);
		doc->stroke = NULL;
	}

	if (doc->surface) {
		if (doc->pixels && doc->pixels[0])
			doc->surface->pixels = doc->pixels[0];
		SDL_FreeSurface(doc->surface);
		doc->surface = NULL;
	}

	if (doc->pixels) {
		for (unsigned i = 1; i < doc->num_blocks; i++) {
			if (doc->pixels[i])
				free(doc->pixels[i]);
		}
		free(doc->pixels);
		doc->pixels = NULL;
	}

	if (doc->texture) {
		SDL_DestroyTexture(doc->texture);
		doc->texture = NULL;
	}

	doc->cols = 0;
	doc->rows = 0;
}

struct doc new_doc (struct font *font, struct palette *palette, int cols, int rows)
{
	struct doc doc;

	if (!font || !palette) {
		fprintf(stderr,
		        "Could not create document.\n");
		return doc;
	}

	doc.font = copy_font(font);
	doc.palette = copy_palette(palette);
	doc.cols = cols;
	doc.rows = rows;

	doc.stroke = calloc(cols * rows, sizeof(struct stroke *));

	if (!doc.stroke) {
		destroy_doc(&doc);
		fprintf(stderr,
		        "Error allocating memory.\n"
		        "Could not create document.\n");
		return doc;
	}

	doc.surface = SDL_CreateRGBSurface(0,
	                                   font->w,
	                                   font->h,
	                                   BITS_PER_PIXEL,
	                                   RMASK,
	                                   GMASK,
	                                   BMASK,
	                                   AMASK);
	SDL_SetColorKey(doc.surface, SDL_TRUE, TRANSPARENT_RGBA);

	if (!doc.surface) {
		destroy_doc(&doc);
		fprintf(stderr,
		        "Error creating SDL_Surface.\n"
		        "SDL_Error: %s\n",
		        "Could not create document.\n",
		        SDL_GetError);
		return doc;
	}

	doc.num_blocks = cols * ((rows + 2) / 2);
	doc.pixels = malloc(doc.num_blocks * sizeof(Uint8 *));

	if (!doc.pixels) {
		destroy_doc(&doc);
		fprintf(stderr,
		        "Error allocating memory.\n"
		        "Could not create document.\n");
		return doc;
	}

	doc.pixels[0] = doc.surface->pixels;
	SDL_FillRect(doc.surface, NULL, TRANSPARENT_RGBA);

	for (unsigned i = 1; i < doc.num_blocks; i++) {
		doc.pixels[i] = calloc(font->h * doc.surface->pitch,
		                       sizeof(Uint8));
		if (!doc.pixels[i]) {
			destroy_doc(&doc);
			fprintf(stderr,
			        "Could not create document.\n"
			        "SDL_Error: %s.\n",
			        SDL_GetError());
			return doc;
		}

		doc.surface->pixels = doc.pixels[i];
		SDL_FillRect(doc.surface, NULL, TRANSPARENT_RGBA);
	}

	doc.texture = NULL;
	doc.texture_w = 0;
	doc.texture_h = 0;

	return doc;
}

void constrain_cursor (struct buffer *buf)
{
	if (buf->ptr_col >= buf->right_margin)
		buf->ptr_col = buf->right_margin;
	if (buf->ptr_row >= buf->bottom_margin)
		buf->ptr_row = buf->bottom_margin - 1;
	if (buf->ptr_col < buf->left_margin)
		buf->ptr_col = buf->left_margin;
	if (buf->ptr_row < buf->top_margin)
		buf->ptr_row = buf->top_margin;
}

void update_cursor ()
{
	if (!curbuf) return;

	constrain_cursor(curbuf);

	if (!window) return;

	cursor.x = (int) floor(curbuf->cam_z *
	                       (double) (curbuf->ptr_col * curbuf->doc.font->w));
	cursor.y = (int) floor(curbuf->cam_z *
	                       (double) (curbuf->ptr_row * curbuf->doc.font->h / 2));
	if (mode & CLIP_ON) {
		cursor.w = (int) ceil(curbuf->cam_z *
		                      (double) (clipboard.font->w *
		                                clipboard.cols));
		cursor.h = (int) ceil(curbuf->cam_z *
		                      (double) (clipboard.font->h *
		                                (clipboard.rows + 1) / 2));
	} else {
		cursor.w = (int) ceil(curbuf->cam_z * (double) curbuf->doc.font->w);
		cursor.h = (int) ceil(curbuf->cam_z * (double) curbuf->doc.font->h);
	}

	cursor.x += frame.x;
	cursor.y += frame.y;
}

void update_selection (struct select *select)
{
	int lo_col, lo_row, hi_col, hi_row;
	if (select->start_col < select->end_col) {
		lo_col = select->start_col * curbuf->doc.font->w;
		hi_col  = (select->end_col + 1) * curbuf->doc.font->w;
	} else {
		lo_col = select->end_col * curbuf->doc.font->w;
		hi_col  = (select->start_col + 1) * curbuf->doc.font->w;
	}
	if (select->start_row < select->end_row) {
		lo_row = select->start_row * curbuf->doc.font->h / 2;
		hi_row  = (select->end_row + 2) * curbuf->doc.font->h / 2;
	} else {
		lo_row = select->end_row * curbuf->doc.font->h / 2;
		hi_row  = (select->start_row + 2) * curbuf->doc.font->h / 2;
	}
	select->rect.x = (int) floor(curbuf->cam_z * (double) lo_col);
	select->rect.y = (int) floor(curbuf->cam_z * (double) lo_row);
	select->rect.w = (int) ceil(curbuf->cam_z * (double) (hi_col - lo_col));
	select->rect.h = (int) ceil(curbuf->cam_z * (double) (hi_row - lo_row));
	select->rect.x += frame.x;
	select->rect.y += frame.y;
}

void update_margins ()
{
	if (!curbuf) return;

	for (int i = 0; i < curbuf->doc.rows; i++) {
		if (curbuf->v_tab[i])
			curbuf->v_tab[i] = frame.y +
				(int) floor(curbuf->cam_z *
				            (double) (i * curbuf->doc.font->h) /
					    2.0);
	}
	for (int i = 0; i < curbuf->doc.cols; i++) {
		if (curbuf->h_tab[i])
			curbuf->h_tab[i] = frame.x +
				(int) floor(curbuf->cam_z *
				            (double) (i * curbuf->doc.font->w));
	}

	curbuf->margin.x = frame.x +
	                   (int) floor(curbuf->cam_z *
	                               (double) (curbuf->left_margin *
	                                         curbuf->doc.font->w));
	curbuf->margin.y = frame.y +
	                   (int) floor(curbuf->cam_z *
	                               (double) (curbuf->top_margin *
	                                         curbuf->doc.font->h) / 2.0);
	curbuf->margin.w = (int) ceil(curbuf->cam_z *
	                              (double) ((curbuf->right_margin -
	                                         curbuf->left_margin + 1) *
	                                        curbuf->doc.font->w));
	curbuf->margin.h = (int) ceil(curbuf->cam_z *
	                              (double) ((curbuf->bottom_margin -
	                                         curbuf->top_margin + 1) *
	                                        curbuf->doc.font->h) / 2.0);
}

void update_frame ()
{
	if (!window || !curbuf)
		return;

	SDL_Surface *screen = SDL_GetWindowSurface(window);

	int center_x = screen->w / 2;
	int center_y = screen->h / 2;

	frame.x = center_x - (int) round(curbuf->cam_z * curbuf->cam_x);
	frame.y = center_y - (int) round(curbuf->cam_z * curbuf->cam_y);
	frame.w = (int) round(curbuf->cam_z * curbuf->doc.texture_w);
	frame.h = (int) round(curbuf->cam_z * curbuf->doc.texture_h);

	update_cursor();
	update_margins();

	for (struct select *select = curbuf->select;
	     select;
	     select = select->next) {
		update_selection(select);
	}
}

void update_cursor_rgb ()
{
	if (!curbuf) return;

	struct cmy cmy = curbuf->doc.palette->cmy[curbuf->color];
	cursor_rgb.r = 0xff - cmy.c;
	cursor_rgb.g = 0xff - cmy.m;
	cursor_rgb.b = 0xff - cmy.y;
}

void zoom_to_fit (struct buffer *buf)
{
	if (!window || !buf)
		return;

	SDL_Surface *screen = SDL_GetWindowSurface(window);

	double zoom_x = (double) screen->w / (double) buf->doc.texture_w;
	double zoom_y = (double) screen->h / (double) buf->doc.texture_h;

	buf->cam_z = (zoom_x < zoom_y) ? zoom_x : zoom_y;
	buf->cam_x = (double) buf->doc.texture_w / 2.0;
	buf->cam_y = (double) buf->doc.texture_h / 2.0;

	update_frame();
}

void render_doc (struct doc *doc)
{
	doc->texture_w = doc->font->w * doc->cols;
	doc->texture_h = doc->font->h * (doc->rows + 1) / 2;
	doc->texture = SDL_CreateTexture(renderer,
	                                     TEXTURE_FORMAT,
	                                     SDL_TEXTUREACCESS_TARGET,
	                                     doc->texture_w,
	                                     doc->texture_h);
	SDL_SetTextureBlendMode(doc->texture, SDL_BLENDMODE_BLEND);
	if (!doc->texture) {
		fprintf(stderr,
		        "Could not create texture.\n"
		        "SDL_Error: %s\n",
		        "Unable to display buffer.\n",
			SDL_GetError());
		return;
	}

	SDL_SetRenderTarget(renderer, doc->texture);
	SDL_SetRenderDrawColor(renderer,
	                       TRANSPARENT_R,
	                       TRANSPARENT_G,
	                       TRANSPARENT_B,
	                       TRANSPARENT_A);
	SDL_RenderClear(renderer);
	SDL_SetRenderTarget(renderer, NULL);

	draw_doc(*doc);
}

void choose_buffer (struct buffer *buf)
{
	if (!buf)
		return;

	if (window && !buf->doc.texture)
		render_doc(&buf->doc);

	curbuf = buf;

	if (window) {
		texture = buf->doc.texture;
		update_frame();
		update_cursor_rgb();
	}
}

void destroy_buffer (struct buffer **buf_p)
{
	struct buffer *buf = *buf_p;
	if (!buf) return;

	struct buffer *prev = NULL;
	struct buffer *trace;

	for (trace = allbuf;
	     trace != buf;
	     trace = trace->next) {
		prev = trace;
	}

	if (trace) {
		if (prev) {
			prev->next = trace->next;
		} else allbuf = allbuf->next;
	}

	if (curbuf == buf) {
		if (curbuf->next) {
			choose_buffer(curbuf->next);
		} else choose_buffer(allbuf);
	}

	destroy_doc(&buf->doc);

	*buf_p = NULL;
}

struct buffer *new_buffer (struct font *font, struct palette *palette,
                           int cols, int rows)
{
	struct buffer *buf = malloc(sizeof(struct buffer));
	buf->h_tab = calloc(cols, sizeof(int));
	buf->v_tab = calloc(rows, sizeof(int));

	if (!buf || !buf->h_tab || !buf->v_tab) {
		if (buf) {
			if (buf->h_tab) free(buf->h_tab);
			if (buf->v_tab) free(buf->v_tab);
			free(buf);
		}
		fprintf(stderr,
		        "Error allocating memory.\n"
		        "Could not add buffer.\n");
		return NULL;
	}

	buf->doc = new_doc(font, palette, cols, rows);

	if (!buf->doc.font) {
		free(buf);
		return NULL;
	}

	buf->top_margin = 0;
	buf->bottom_margin = rows;
	buf->left_margin = 0;
	buf->right_margin = cols - 1;
	buf->margin.x = 0;
	buf->margin.y = 0;
	buf->margin.w = 0;
	buf->margin.h = 0;

	buf->ptr_col = 0;
	buf->ptr_row = 0;
	buf->color = 0;

	buf->cam_x = (double) buf->doc.surface->w / 2.0;
	buf->cam_y = (double) buf->doc.surface->h / 2.0;
	buf->cam_z = 1.0;

	buf->select = NULL;

	buf->next = allbuf;
	allbuf = buf;

	return buf;
}

struct palette *default_palette ()
{
	struct palette *palette = new_palette(2);

	if (!palette) return NULL;

	palette->cmy[1].c = 0x00;

	return palette;
}

struct buffer *default_buffer ()
{
	return new_buffer(load_font(INIT_FONT),
	                  default_palette(),
	                  INIT_COLS,
	                  INIT_ROWS);
}

void cleanup_buffers ()
{
	while (allbuf)
		destroy_buffer(&allbuf);
}

void render_block (struct doc doc, int col, int row)
{
	SDL_Surface *block = doc.surface;
	block->pixels = doc.pixels[col + (row / 2) * doc.cols];
	SDL_Rect block_rect = {col * doc.font->w,
	                       row * doc.font->h / 2,
	                       doc.font->w,
	                       doc.font->h};
	SDL_Texture *block_texture = SDL_CreateTextureFromSurface(renderer,
	                                                          block);
	SDL_SetTextureBlendMode(block_texture, 0);
	SDL_SetRenderTarget(renderer, doc.texture);
	SDL_SetRenderDrawColor(renderer,
	                       TRANSPARENT_R,
	                       TRANSPARENT_G,
	                       TRANSPARENT_B,
	                       TRANSPARENT_A);
	SDL_RenderFillRect(renderer, &block_rect);
	SDL_RenderCopy(renderer, block_texture, NULL, &block_rect);
	SDL_SetRenderTarget(renderer, NULL);
	SDL_DestroyTexture(block_texture);
}

void blit_cmy (SDL_Surface *dest, unsigned char *src, struct cmy cmy, int w, int h, int offset)
{
	int at_y = 0;

	if (offset < 0) {
		src -= w * offset;
		h += offset;
	} else if (offset > 0) {
		at_y = offset;
		h -= offset;
	}

	int pitch = dest->pitch;
	Uint8 *p;
	Uint8 r, g, b;

	for (int y = at_y; y < at_y + h; y++) {
		p = dest->pixels + y * pitch;
		for (int x = 0; x < w; x++) {
			if (*src) {
				SDL_GetRGB(*((Uint32 *)p),
				           dest->format,
				           &r, &g, &b);
				if (r > cmy.c) r -= cmy.c;
				else           r  = 0;
				if (g > cmy.m) g -= cmy.m;
				else           g  = 0;
				if (b > cmy.y) b -= cmy.y;
				else           b  = 0;
				*((Uint32 *) p) = SDL_MapRGBA(dest->format,
				                              r, g, b, 0xff);
			}
			src++;
			p += BYTES_PER_PIXEL;
		}
	}
}

void blit_to_block (struct doc doc, int col, int row, int offset,
                    unsigned char color, unsigned char glyph)
{
	SDL_Surface *block = doc.surface;
	block->pixels = doc.pixels[col + (row / 2) * doc.cols];
	blit_cmy(block,
	         doc.font->glyph[glyph],
	         doc.palette->cmy[color],
	         doc.font->w,
	         doc.font->h,
	         offset);
}

void draw_stroke (struct doc doc, int col, int row,
                  unsigned char color, unsigned char glyph)
{
	SDL_Surface *dest = doc.surface;
	SDL_Texture *dest_texture;
	SDL_Rect dest_rect = {0, 0, doc.font->w, doc.font->h};

	if (row & 1) {
		blit_to_block(doc,
		              col,
		              row - 1,
		              doc.font->h / 2,
		              color,
		              glyph);
		blit_to_block(doc,
		              col,
		              row + 1,
		              -(doc.font->h / 2),
		              color,
		              glyph);
		if (doc.texture) {
			render_block(doc, col, row - 1);
			render_block(doc, col, row + 1);
		}
	} else {
		blit_to_block(doc, col, row, 0, color, glyph);
		if (doc.texture)
			render_block(doc, col, row);
	}
}

void draw_all_strokes (struct doc doc, int col, int doc_row, int block_row)
{
	int offset = 0;
	if (block_row < doc_row) {
		offset = doc.font->h / 2;
	} else if (block_row > doc_row) {
		offset = -(doc.font->h / 2);
	}

	for (struct stroke *stroke = doc.stroke[col + doc_row * doc.cols];
	     stroke;
	     stroke = stroke->next) {
		blit_to_block(doc,
		              col,
		              block_row,
		              offset,
		              stroke->color,
		              stroke->glyph);
	}
}

void draw_pos (struct doc doc, int col, int row)
{
	SDL_Surface *block = doc.surface;

	if (row & 1) {
		draw_pos(doc, col, row - 1);
		draw_pos(doc, col, row + 1);
	} else {
		block->pixels = doc.pixels[col + (row / 2) * doc.cols];
		SDL_FillRect(block, NULL, TRANSPARENT_RGBA);
		draw_all_strokes(doc, col, row, row);
		if (row > 0)
			draw_all_strokes(doc, col, row - 1, row);
		if (row < doc.rows - 1)
			draw_all_strokes(doc, col, row + 1, row);
		if (doc.texture)
			render_block(doc, col, row);
	}
}

void draw_doc (struct doc doc)
{
	for (int row = 0; row < doc.rows; row += 2) {
		for (int col = 0; col < doc.cols; col++) {
			draw_pos(doc, col, row);
		}
	}
}

int raise_stroke (struct doc doc, int col, int row, unsigned char color, unsigned char glyph)
{
	struct stroke *prev = NULL;

	for (struct stroke *stroke = doc.stroke[col + row * doc.cols];
	     stroke;
	     stroke = stroke->next) {
		if (stroke->color == color && stroke->glyph == glyph) {
			if (!prev) return 1;
			prev->next = stroke->next;
			stroke->next = doc.stroke[col + row * doc.cols];
			doc.stroke[col + row * doc.cols] = stroke;
			return 1;
		}
		prev = stroke;
	}

	return 0;
}

struct stroke *add_stroke (struct doc doc, int col, int row,
                           unsigned char color, unsigned char glyph)
{
	if (col < 0 || row < 0 || col >= doc.cols || row >= doc.rows)
		return NULL;

	if (!doc.font->glyph[glyph])
		return NULL;

	if (raise_stroke(doc, col, row, color, glyph))
		return doc.stroke[col + row * doc.cols];

	struct stroke *stroke = malloc(sizeof(struct stroke));

	if (!stroke) {
		fprintf(stderr,
		        "Error allocating memory.\n"
		        "Could not add stroke.\n");
		return NULL;
	}

	stroke->color = color;
	stroke->glyph = glyph;
	stroke->next = doc.stroke[col + row * doc.cols];
	doc.stroke[col + row * doc.cols] = stroke;
	draw_stroke(doc, col, row, color, glyph);

	return stroke;
}

struct stroke *del_stroke (struct doc doc, int col, int row)
{
	struct stroke *stroke = doc.stroke[col + row * doc.cols];

	if (!stroke) return NULL;

	doc.stroke[col + row * doc.cols] = stroke->next;
	free(stroke);
	draw_pos(doc, col, row);

	return doc.stroke[col + row * doc.cols];
}

int save_buffer (struct buffer *buf, FILE *f)
{
	Uint16 doc_cols = buf->doc.cols;
	Uint16 doc_rows = buf->doc.rows;
	unsigned char color;
	unsigned char glyph;

	fwrite("SYN", 1, 3, f);
	fwrite("\0", 1, 1, f);
	fwrite(&doc_cols, sizeof(Uint16), 1, f);
	fwrite(&doc_rows, sizeof(Uint16), 1, f);

	for (int row = 0; row < buf->doc.rows; row++) {
		for (int col = 0; col < buf->doc.cols; col++) {
			for (struct stroke *stroke = buf->doc.stroke[col +
			                                             row *
			                                             buf->doc.cols];
			     stroke;
			     stroke = stroke->next) {
				glyph = stroke->glyph;
				color = stroke->color;
				fwrite(&glyph, 1, 1, f);
				fwrite(&color, 1, 1, f);
			}
			fwrite("\0", 1, 1, f);
		}
	}

	return 1;
}

struct buffer *load_buffer (FILE *f)
{
	unsigned char magic_num[4] = {0, 0, 0, 0};
	unsigned char version;
	Uint16 doc_cols = 0;
	Uint16 doc_rows = 0;

	fread(magic_num, 1, 3, f);

	if (strcmp(magic_num, "SYN")) {
		fprintf(stderr,
		        "File is not a valid Synthotype document.\n"
			"Could not load document.\n");
		return NULL;
	}

	fread(&version, 1, 1, f);

	struct buffer *buf = NULL;
	int row = 0;
	int col = 0;
	unsigned char color;
	unsigned char glyph;

	switch (version) {
		case 0:
			fread(&doc_cols, sizeof(Uint16), 1, f);
			fread(&doc_rows, sizeof(Uint16), 1, f);

			if (doc_cols < 1 || doc_rows < 1) {
				fprintf(stderr,
				        "Invalid document size encountered.\n"
				        "Could not load document.\n");
				return NULL;
			}

			if (curbuf) {
				buf = new_buffer(copy_font(curbuf->doc.font),
				                 copy_palette(curbuf->doc.palette),
				                 doc_cols,
				                 doc_rows);
			} else if (allbuf) {
				buf = new_buffer(copy_font(allbuf->doc.font),
				                 copy_palette(allbuf->doc.palette),
				                 doc_cols,
				                 doc_rows);
			} else {
				buf = new_buffer(load_font(INIT_FONT),
				                 default_palette(),
				                 doc_cols,
				                 doc_rows);
			}

			if (!buf) {
				fprintf(stderr,
				        "Could not load document.\n");
				return NULL;
			}

			while (!feof(f) && row <= buf->doc.rows) {
				glyph = fgetc(f);
				while (glyph && !feof(f)) {
					color = fgetc(f);
					add_stroke(buf->doc,
					           col,
					           row,
					           color,
					           glyph);
					glyph = fgetc(f);
				}
				col++;
				if (col >= buf->doc.cols) {
					col = 0;
					row++;
				}
			}

			break;
		default:
			fprintf(stderr,
			        "Invalid version detected.\n"
			        "Could not load document.\n");
			return NULL;
	}

	return buf;
}

void add_selection (struct buffer *buf, int start_col, int start_row, int end_col, int end_row)
{
	struct select *select = malloc(sizeof(struct select));

	if (!select) {
		fprintf(stderr,
		        "Error allocating memory.\n"
		        "Could not create select box.\n");
		return;
	}

	select->active = 1;
	select->start_col = start_col;
	select->start_row = start_row;
	select->end_col = end_col;
	select->end_row = end_row;
	select->next = buf->select;
	if (window && curbuf == buf)
		update_selection(select);
	buf->select = select;
}

void copy_selection (struct buffer *buf)
{
	int lo_col = buf->doc.cols;
	int lo_row = buf->doc.rows;
	int hi_col = 0;
	int hi_row = 0;

	for (struct select *select = buf->select;
	     select;
	     select = select->next) {
		if (select->start_col < lo_col) {
			lo_col = select->start_col;
		} else if(select->start_col > hi_col)
			hi_col = select->start_col;
		if (select->end_col < lo_col) {
			lo_col = select->end_col;
		} else if(select->end_col > hi_col)
			hi_col = select->end_col;
		if (select->start_row < lo_row) {
			lo_row = select->start_row;
		} else if(select->start_row > hi_row)
			hi_row = select->start_row;
		if (select->end_row < lo_row) {
			lo_row = select->end_row;
		} else if(select->end_row > hi_row)
			hi_row = select->end_row;
	}

	int w = hi_col - lo_col + 1;
	int h = hi_row - lo_row + 1;

	if (hi_col < lo_col || hi_row < lo_row)
		return;

	destroy_doc(&clipboard);

	clipboard = new_doc(copy_font(buf->doc.font),
	                    copy_palette(buf->doc.palette),
	                    w,
	                    h);

	struct stroke *new_stroke;

	for (int row = lo_row; row <= hi_row; row++) {
		for (int col = lo_col; col <= hi_col; col++) {
			for (struct stroke *stroke = buf->doc.stroke[col + row * buf->doc.cols];
			     stroke;
			     stroke = stroke->next) {
				add_stroke(clipboard,
				           col - lo_col,
				           row - lo_row,
				           stroke->color,
				           stroke->glyph);
			}
		}
	}
}

void clear_selection (struct buffer *buf)
{
	struct select *next;
	for (struct select *select = buf->select;
	     select;
	     select = next) {
		next = select->next;
		free(select);
	}
	buf->select = NULL;
}

void paste_doc (struct doc dest, struct doc src, int at_col, int at_row)
{
	for (int row = 0; row < src.rows; row++) {
		for (int col = 0; col < src.cols; col++) {
			for (struct stroke *stroke = src.stroke[col + row * src.cols];
			     stroke;
			     stroke = stroke->next) {
				add_stroke(dest,
				           at_col + col,
				           at_row + row,
				           stroke->color,
				           stroke->glyph);
			}
		}
	}
}
