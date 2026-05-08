#!/bin/sh

cc -Wall -Wextra -Wpedantic -o BlackHole \
   main.c \
   $(pkg-config --cflags --libs sdl3 sdl3-shadercross)