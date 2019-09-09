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

#define CHBUF_INIT_SIZE 32
#define CHBUF_CHUNK_SIZE 16

#define lock(chbuf) while (chbuf->lock); chbuf->lock = 1;
#define unlock(chbuf) chbuf->lock = 0;

#define MAX_KEYCODE 400
#define HIGH_KEY 1073741824
#define HIGH_KEY_DELTA 1073741654

#define ALT_DOWN 1
#define CTRL_DOWN 2
#define SHIFT_DOWN 4
#define ALTGR_DOWN 8
#define GUI_DOWN 16
#define MOD_KEY_MASK 31

#define CLIP_ON 32

#define lokey(key) (key < HIGH_KEY ? key : key - HIGH_KEY_DELTA)

struct chbuf {
	char lock;
	size_t size;
	size_t len;
	unsigned char *ch;
};

struct binding {
	unsigned force_mode;
	unsigned block_mode;
	unsigned char *str;
	struct binding *next;
};

unsigned mode = 0;
struct binding *keybind[MAX_KEYCODE] = {0};
unsigned char *fifo_in = "/tmp/synthotype-in";
unsigned char *fifo_out = "/tmp/synthotype-out";
struct chbuf *stream;
struct chbuf *return_stream;
pthread_t output_thread;

void bind (unsigned force_mode, unsigned block_mode, long unsigned key, unsigned char *str)
{
	struct binding *binding;

	key = lokey(key);

	for (binding = keybind[key];
	     binding;
	     binding = binding->next) {
		if (binding->force_mode == force_mode &&
		    binding->block_mode == block_mode) {
			if (binding->str[0] == 0)
				free(binding->str);
			binding->str = str;
			return;
		}
	}

	binding = malloc(sizeof(struct binding));

	if (!binding) {
		fprintf(stderr,
		        "Error allocating memory.\n"
		        "Could not bind key.\n");
		return;
	}

	binding->force_mode = force_mode;
	binding->block_mode = block_mode;
	binding->str = str;
	binding->next = keybind[key];
	keybind[key] = binding;
}

struct chbuf *new_chbuf()
{
	struct chbuf *chbuf = malloc(sizeof(struct chbuf));
	if (chbuf)
		chbuf->ch = malloc(CHBUF_INIT_SIZE);

	if (!chbuf || !chbuf->ch) {
		if (chbuf) free(chbuf);
		fprintf(stderr,
		        "Error allocating memory.\n"
		        "Could not create character buffer.\n");
		return NULL;
	}

	chbuf->lock  = 0;
	chbuf->size  = CHBUF_INIT_SIZE;
	chbuf->len   = 0;
	chbuf->ch[0] = 0;

	return chbuf;
}

int chbuf_push (struct chbuf *chbuf, unsigned char ch)
{
	lock(chbuf);

	unsigned char *new_ch;

	if (chbuf->len == chbuf->size - 1) {
		new_ch = malloc(chbuf->size + CHBUF_CHUNK_SIZE);
		if (!new_ch) {
			fprintf(stderr,
			        "Error allocating memory.\n"
			        "Could not update character buffer.\n");
			unlock(chbuf);
			return 0;
		}
		chbuf->size += CHBUF_CHUNK_SIZE;
		for (size_t i = 0; i < chbuf->len; i++)
			new_ch[i] = chbuf->ch[i];
		free(chbuf->ch);
		chbuf->ch = new_ch;
	}

	chbuf->ch[chbuf->len] = ch;
	chbuf->len += 1;
	chbuf->ch[chbuf->len] = 0;

	unlock(chbuf);

	return 1;
}

int chbuf_append (struct chbuf *chbuf, unsigned char *str)
{
	lock(chbuf);

	size_t new_len = chbuf->len + strlen(str);
	size_t new_size = chbuf->size;
	while (new_len > new_size)
		new_size += CHBUF_CHUNK_SIZE;

	unsigned char *new_ch;

	if (new_size > chbuf->size) {
		new_ch = malloc(new_size);
		if (!new_ch) {
			fprintf(stderr,
			        "Error allocating memory.\n"
			        "Could not update character buffer.\n");
			unlock(chbuf);
			return 0;
		}
		for (size_t i = 0; i < chbuf->len; i++)
			new_ch[i] = chbuf->ch[i];
		free(chbuf->ch);
		chbuf->ch = new_ch;
	}

	for (size_t i = chbuf->len; i < new_len; i++)
		chbuf->ch[i] = str[i - chbuf->len];
	chbuf->ch[new_len] = 0;

	chbuf->size = new_size;
	chbuf->len = new_len;

	unlock(chbuf);

	return 1;
}

void destroy_chbuf (struct chbuf **chbuf_p)
{
	struct chbuf *chbuf = *chbuf_p;
	free(chbuf->ch);
	free(chbuf);
	*chbuf_p = NULL;
}

#define capswitch(a, b) ((mod & KMOD_SHIFT) ? b : a)

int append_keypress (struct chbuf *chbuf, SDL_Keymod mod, long unsigned key)
{
	mode = (mode & ~MOD_KEY_MASK) |
	       ((mod & KMOD_ALT)   ? ALT_DOWN   : 0) |
	       ((mod & KMOD_CTRL)  ? CTRL_DOWN  : 0) |
	       ((mod & KMOD_SHIFT) ? SHIFT_DOWN : 0) |
	       ((mod & KMOD_MODE)  ? ALTGR_DOWN : 0) |
	       ((mod & KMOD_GUI)   ? GUI_DOWN   : 0);

	unsigned char bound = 0;

	for (struct binding *binding = keybind[lokey(key)];
	     binding;
	     binding = binding->next) {
		if ((mode & binding->force_mode) == binding->force_mode &&
		    !(mode & binding->block_mode) &&
		    binding->str) {
			if (binding->str[0] == '0') {
				if (chbuf_append(chbuf,
				                 binding->str + 1))
					bound = 1;
			} else if (chbuf_append(chbuf, binding->str))
				bound = 1;
		}
	}

	if (bound) return 1;

	unsigned char ch = 0;

	if (key >= SDLK_a && key <= SDLK_z) {
		if (mod & (KMOD_SHIFT | KMOD_CAPS)) {
			ch = key - SDLK_a + 'A';
		} else ch = key - SDLK_a + 'a';
	} else switch (key) {
		case SDLK_0:
			ch = capswitch('0', ')'); break;
		case SDLK_1:
			ch = capswitch('1', '!'); break;
		case SDLK_2:
			ch = capswitch('2', '@'); break;
		case SDLK_3:
			ch = capswitch('3', '#'); break;
		case SDLK_4:
			ch = capswitch('4', '$'); break;
		case SDLK_5:
			ch = capswitch('5', '%'); break;
		case SDLK_6:
			ch = capswitch('6', '^'); break;
		case SDLK_7:
			ch = capswitch('7', '&'); break;
		case SDLK_8:
			ch = capswitch('8', '*'); break;
		case SDLK_9:
			ch = capswitch('9', '('); break;
		case SDLK_KP_000:
			return chbuf_append(chbuf, "000");
		case SDLK_KP_00:
			return chbuf_append(chbuf, "00");
		case SDLK_KP_0:
			ch = '0'; break;
		case SDLK_KP_1:
			ch = '1'; break;
		case SDLK_KP_2:
			ch = '2'; break;
		case SDLK_KP_3:
			ch = '3'; break;
		case SDLK_KP_4:
			ch = '4'; break;
		case SDLK_KP_5:
			ch = '5'; break;
		case SDLK_KP_6:
			ch = '6'; break;
		case SDLK_KP_7:
			ch = '7'; break;
		case SDLK_KP_8:
			ch = '8'; break;
		case SDLK_KP_9:
			ch = '9'; break;
		case SDLK_BACKSLASH:
		
			ch = capswitch('\\', '|'); break;
		case SDLK_COMMA:
			ch = capswitch(',', '<'); break;
		case SDLK_EQUALS:
			ch = capswitch('=', '+'); break;
		case SDLK_BACKQUOTE:
			ch = capswitch('`', '~'); break;
		case SDLK_KP_A:
			ch = 'A'; break;
		case SDLK_KP_B:
			ch = 'B'; break;
		case SDLK_KP_C:
			ch = 'C'; break;
		case SDLK_KP_D:
			ch = 'D'; break;
		case SDLK_KP_E:
			ch = 'E'; break;
		case SDLK_KP_DBLAMPERSAND:
			return chbuf_append(chbuf, "&&");
		case SDLK_KP_AMPERSAND:
			ch = '&'; break;
		case SDLK_KP_AT:
			ch = '@'; break;
		case SDLK_KP_COLON:
			ch = ':'; break;
		case SDLK_KP_COMMA:
			ch = ','; break;
		case SDLK_KP_DBLVERTICALBAR:
			return chbuf_append(chbuf, "||");
		case SDLK_KP_VERTICALBAR:
			ch = '|'; break;
		case SDLK_KP_DECIMAL:
		case SDLK_KP_PERIOD:
			ch = '.'; break;
		case SDLK_KP_DIVIDE:
			ch = '/'; break;
		case SDLK_KP_EQUALS:
		case SDLK_KP_EQUALSAS400:
			ch = '='; break;
		case SDLK_KP_EXCLAM:
			ch = '!'; break;
		case SDLK_KP_GREATER:
			ch = '>'; break;
		case SDLK_KP_HASH:
			ch = '#'; break;
		case SDLK_KP_LEFTBRACE:
			ch = '{'; break;
		case SDLK_KP_LEFTPAREN:
			ch = '('; break;
		case SDLK_KP_LESS:
			ch = '<'; break;
		case SDLK_KP_PLUSMINUS:
			return chbuf_append(chbuf, "+-");
		case SDLK_KP_MINUS:
			ch = '-'; break;
		case SDLK_KP_MULTIPLY:
			ch = '*'; break;
		case SDLK_KP_PERCENT:
			ch = '%'; break;
		case SDLK_KP_PLUS:
			ch = '+'; break;
		case SDLK_KP_POWER:
			ch = '^'; break;
		case SDLK_KP_RIGHTBRACE:
			ch = '}'; break;
		case SDLK_KP_RIGHTPAREN:
			ch = ')'; break;
		case SDLK_KP_SPACE:
		case SDLK_SPACE:
			ch = ' '; break;
		case SDLK_LEFTBRACKET:
			ch = capswitch('[', '{'); break;
		case SDLK_MINUS:
			ch = capswitch('-', '_'); break;
		case SDLK_PERIOD:
			ch = capswitch('.', '>'); break;
		case SDLK_QUOTE:
			ch = capswitch('\'', '"'); break;
		case SDLK_RIGHTBRACKET:
			ch = capswitch(']', '}'); break;
		case SDLK_SEMICOLON:
			ch = capswitch(';', ':'); break;
		case SDLK_SLASH:
			ch = capswitch('/', '?'); break;
		case SDLK_WWW:
			return chbuf_append(chbuf, "www");
		case SDLK_AMPERSAND:
			ch = '&'; break;
		case SDLK_ASTERISK:
			ch = '*'; break;
		case SDLK_AT:
			ch = '@'; break;
		case SDLK_CARET:
			ch = '^'; break;
		case SDLK_COLON:
			ch = ':'; break;
		case SDLK_DOLLAR:
			ch = '$'; break;
		case SDLK_EXCLAIM:
			ch = '!'; break;
		case SDLK_GREATER:
			ch = '>'; break;
		case SDLK_HASH:
			ch = '#'; break;
		case SDLK_LEFTPAREN:
			ch = '('; break;
		case SDLK_LESS:
			ch = '<'; break;
		case SDLK_PERCENT:
			ch = '%'; break;
		case SDLK_PLUS:
			ch = '+'; break;
		case SDLK_QUESTION:
			ch = '?'; break;
		case SDLK_QUOTEDBL:
			ch = '"'; break;
		case SDLK_RIGHTPAREN:
			ch = ')'; break;
		case SDLK_UNDERSCORE:
			ch = '_'; break;
		case SDLK_RETURN:
		case SDLK_RETURN2:
		case SDLK_KP_ENTER:
			ch = '\n'; break;
	}

	if (!ch) return 0;

	return chbuf_push(chbuf, ch);
}

void handle_keypress (SDL_Keymod mod, long unsigned key)
{
	append_keypress(stream, mod, lokey(key));
}

size_t find_csi_end (struct chbuf *chbuf, size_t *i_p)
{
	size_t i = *i_p;
	while (chbuf->ch[i] && chbuf->ch[i] != '\007')
		i++;
	*i_p = i;
}

int grab_int (struct chbuf *chbuf, size_t *i_p, int def)
{
	unsigned char neg = 0;
	int n = 0;
	size_t i = *i_p;

	if (chbuf->ch[i] == '-') {
		neg = 1;
		i++;
	}

	if (chbuf->ch[i] < '0' || chbuf->ch[i] > '9') {
		return def;
	} else while (chbuf->ch[i] >= '0' && chbuf->ch[i] <= '9') {
		n = n * 10 + chbuf->ch[i] - '0';
		i++;
	}
	if (chbuf->ch[i] == ';')
		i++;

	*i_p = i;

	return (neg ? -n : n);
}

int csi_handle (struct buffer *buf, struct chbuf *chbuf, size_t *i_p)
{
	size_t i = *i_p;
	size_t j;
	double ratio;
	unsigned char return_buffer[20];

	int a, b, c;

	if (chbuf->ch[i++] != '\033')
		return 0;

	switch (chbuf->ch[i++]) {
		case 'A':
			a = grab_int(chbuf, &i, buf->ptr_col);
			b = grab_int(chbuf, &i, buf->ptr_row);
			do_absmove(curbuf, a, b);
			break;
		case 'Q':
			do_quit();
			break;
		case 'Z':
			a = grab_int(chbuf, &i, 0);
			b = grab_int(chbuf, &i, 0);
			c = grab_int(chbuf, &i, 0);
			do_test(curbuf, a, b, c);
			break;
		default:
			break;
	}
	find_csi_end(chbuf, &i);

	*i_p = i;

	return 1;
}

void chbuf_handle (struct buffer *buf, struct chbuf *chbuf)
{
	lock(chbuf);

	for (size_t i = 0; i < chbuf->len; i++) {
		if (csi_handle(buf, chbuf, &i)) {
		} else if (chbuf->ch[i] == '\n') {
			do_absmove(buf,
			           0,
			           buf->ptr_row + grab_int(chbuf, &i, 2));
		} else {
			do_type(buf, chbuf->ch[i]);
		}
	}

	chbuf->len = 0;

	unlock(chbuf);
}

void control_handle (struct buffer *buf)
{
	if (stream->len)
		chbuf_handle(buf, stream);
}

void cleanup_control ()
{
	destroy_chbuf(&stream);
	destroy_chbuf(&return_stream);

	struct binding *next_binding;

	for (int i = 0; i < MAX_KEYCODE + 1; i++) {
		for (struct binding *binding = keybind[i];
		     binding;
		     binding = next_binding) {
			next_binding = binding->next;
			if (binding->str && !binding->str[0])
				free(binding->str);
			free(binding);
		}
	}
}

void *output_loop (void *params)
{
	int fd;

	mkfifo(fifo_out, 0666);

	while (1) {
		if (return_stream->len) {
			lock(return_stream);

			fd = open(fifo_out, O_WRONLY);
			write(fd, return_stream->ch, return_stream->len);
			close(fd);
			return_stream->len = 0;

			unlock(return_stream);
		}
	}

	return NULL;
}

void cleanup_control_loop (void *params)
{
	pthread_cancel(output_thread);

	struct chbuf *chbuf = params;
	destroy_chbuf(&chbuf);
}

void *control_loop (void *params)
{
	struct chbuf *chbuf = new_chbuf();

	pthread_cleanup_push(cleanup_control_loop, chbuf);

//	if (pthread_create(&output_thread, NULL, output_loop, NULL))
//		fprintf(stderr,
//		        "Error creating thread.\n"
//		        "Could not create output pipe.\n");

	mkfifo(fifo_in, 0666);

	int fd;
	unsigned char ch;

	while (1) {
		chbuf->len = 0;

		fd = open(fifo_in, O_RDONLY);
		while (read(fd, &ch, 1))
			chbuf_push(chbuf, ch);
		close(fd);

		if (chbuf->len)
			chbuf_append(stream, chbuf->ch);
	}

	pthread_cleanup_pop(0);

	return NULL;
}

unsigned char *CSI_QUIT = "\033Q\007";

unsigned char *CSI_PTR_LEFT = "\033A0;0\007";
unsigned char *CSI_PTR_RIGHT = "\033A1;0\007";
unsigned char *CSI_PTR_UP = "\033A2;0\007";
unsigned char *CSI_PTR_DOWN = "\033A;\007";
unsigned char *CSI_TEST = "\033Z12;34;56;78\007";

void init_control ()
{
	stream = new_chbuf();
	return_stream = new_chbuf();

	bind(ALT_DOWN, 0, SDLK_F4, CSI_QUIT);

	bind(0, 0, SDLK_UP, CSI_PTR_UP);
	bind(0, 0, SDLK_DOWN, CSI_PTR_DOWN);
	bind(0, 0, SDLK_LEFT, CSI_PTR_LEFT);
	bind(0, 0, SDLK_RIGHT, CSI_PTR_RIGHT);

	bind(0, 0, SDLK_RETURN, CSI_TEST);
}
