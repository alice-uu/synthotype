all:
	gcc type_internal.c type_ctrl.c type_gui.c type_core.c type.c -lm -lSDL2 -lSDL2_image -lpthread -o type
