CC = cc

PKGS = sdl3 sdl3-shadercross
CFLAGS = -g -O0 -Wall -Wextra -Wpedantic $(shell pkg-config --cflags $(PKGS))
LDLIBS = $(shell pkg-config --libs $(PKGS))

BlackHole: Source/BlackHole.c
	$(CC) $(CFLAGS) -o BlackHole Source/BlackHole.c $(LDLIBS)