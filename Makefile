CXX      = c++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra $(shell sdl2-config --cflags) $(EXTRA)
LDFLAGS  = $(shell sdl2-config --libs)
SRCS     = main.cpp Map.cpp Renderer.cpp Player.cpp
HDRS     = Vec2.h Camera.h Map.h Renderer.h Player.h

# Default build: includes the in-engine editor + pick buffer (Tab to toggle).
build_engine: $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o build_engine $(LDFLAGS)

run: build_engine
	./build_engine

# Stripped "play" build: no editor, no per-pixel pick buffer (-DEDITOR=0).
# Cleans first so the macro change always takes effect. Run `make clean` (or
# plain `make`) afterwards to get the editor build back.
play:
	$(MAKE) clean
	$(MAKE) EXTRA="-DEDITOR=0"
	./build_engine

clean:
	rm -f build_engine

.PHONY: run play clean
