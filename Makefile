SDLFLAGS = $(shell sdl2-config --libs --cflags)

chip: main.c
	clang \
	  main.c \
	  -o chip \
	  $(SDLFLAGS) \
	  -framework OpenGL

debug: main.c
	clang \
	  -g \
	  main.c \
	  -o chip \
	  $(SDLFLAGS) \
	  -framework OpenGL

