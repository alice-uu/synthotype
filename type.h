#ifndef SYNTHOTYPE_H
#define SYNTHOTYPE_H

#define CLIP_ON 32

struct palette {
	unsigned ref;
	unsigned char num_colors;
	struct cmy {
		unsigned char c;
		unsigned char m;
		unsigned char y;
	} *cmy;
};

struct font {
	unsigned ref;
	int w;
	int h;
	unsigned char *glyph[256];
};

struct stroke {
	unsigned char color;
	unsigned char glyph;
	struct stroke *next;
};

struct doc {
	struct font *font;
	struct palette *palette;
	int cols;
	int rows;
	struct stroke **stroke;
	SDL_Surface *surface;
	int num_blocks;
	Uint8 **pixels;
	SDL_Texture *texture;
	int texture_w;
	int texture_h;
};

struct select {
	unsigned char active;
	int start_col;
	int start_row;
	int end_col;
	int end_row;
	SDL_Rect rect;
	struct select *next;
};

struct buffer {
	struct doc doc;

	int *h_tab;
	int *v_tab;

	int top_margin;
	int bottom_margin;
	int left_margin;
	int right_margin;
	SDL_Rect margin;

	int ptr_col;
	int ptr_row;
	unsigned char color;

	double cam_x;
	double cam_y;
	double cam_z;

	SDL_Rect frame;
	SDL_Rect cursor;
	SDL_Color cursor_rgb;

	struct select *select;

	struct buffer *next;
};

extern char run;
extern unsigned mode;

extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern SDL_Texture *texture;

extern SDL_Rect frame;
extern SDL_Rect cursor;
extern SDL_Color cursor_rgb;

extern struct buffer *allbuf;
extern struct buffer *curbuf;
extern struct doc clipboard;

extern unsigned char *fifo_in;
extern unsigned char *fifo_out;

void init_control ();
void cleanup_control ();
void handle_keypress (SDL_Keymod mod, long unsigned key);
void control_handle (struct buffer *buf);
void *control_loop (void *params);

void gui_loop();

struct buffer *default_buffer ();
void cleanup_buffers ();
void zoom_to_fit (struct buffer *buf);
void render_doc (struct doc *doc);
void choose_buffer (struct buffer *buf);
void draw_doc (struct doc doc);
struct stroke *add_stroke (struct doc doc, int col, int row, unsigned char color, unsigned char glyph);
struct stroke *del_stroke (struct doc doc, int col, int row);
void update_cursor ();
void update_selection (struct select *select);
void update_margins ();
void update_frame ();
void update_cursor_rgb ();
void constrain_cursor (struct buffer *buf);
int save_buffer (struct buffer *buf, FILE *f);
struct buffer *load_buffer (FILE *f);
void add_selection (struct buffer *buf, int start_col, int start_row, int end_col, int end_row);
void copy_selection (struct buffer *buf);
void paste_doc (struct doc dest, struct doc src, int at_col, int at_row);
void clear_selection (struct buffer *buf);

void do_quit ();
void do_absmove (struct buffer *buf, int col, int row);
void do_type (struct buffer *buf, unsigned char glyph);
void do_test (struct buffer *buf, int a, int b, int c);

#endif
