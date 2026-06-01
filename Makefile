CC      = cc
CFLAGS  = -O2 -Wall -Wextra $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs) -lm

build_engine: build_engine.c
	$(CC) $(CFLAGS) build_engine.c -o build_engine $(LDFLAGS)

run: build_engine
	./build_engine

clean:
	rm -f build_engine

.PHONY: run clean
