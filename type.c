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

char run = 1;

struct buffer *allbuf;
struct buffer *curbuf;
struct doc clipboard;

pthread_t ctrl_thread;

int init_all (int argc, char **args)
{
	init_control();

	allbuf = default_buffer();
}

int cleanup_all ()
{
	cleanup_control();
	cleanup_buffers();
}

int main (int argc, char **args)
{
	init_all(argc, args);

	if (pthread_create(&ctrl_thread, NULL, control_loop, NULL)) {
		fprintf(stderr,
		        "Error creating thread.\n"
		        "Could not create control pipe.\n");
		gui_loop();
		pthread_cancel(ctrl_thread);
	} else gui_loop();

	cleanup_all();

	return 0;
}
