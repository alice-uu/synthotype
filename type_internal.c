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

void do_quit ()
{
	run = 0;
}

void do_absmove (struct buffer *buf, int col, int row)
{
	printf("%i %i\n", col, row);
	if (buf->select)
		buf->select->active = 0;

	while (col < 0) col += buf->doc.cols;
	while (row < 0) row += buf->doc.rows;

	buf->ptr_col = col;
	buf->ptr_row = row;

	if (curbuf == buf) {
		update_cursor();
	} else constrain_cursor(buf);
}

void do_type (struct buffer *buf, unsigned char glyph)
{
	add_stroke(buf->doc, buf->ptr_col, buf->ptr_row, buf->color, glyph);
	do_absmove(buf, buf->ptr_col + 1, buf->ptr_row);
}

void do_test (struct buffer *buf, int a, int b, int c)
{
	printf("%i %i %i\n", a, b, c);
}
