CXX      = c++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra $(shell sdl2-config --cflags) $(EXTRA)
LDFLAGS  = $(shell sdl2-config --libs)
SRCS     = main.cpp Map.cpp Renderer.cpp Player.cpp Texture.cpp GrpExtract.cpp stb_image_impl.cpp
HDRS     = Vec2.h Camera.h Map.h Renderer.h Player.h Texture.h GrpExtract.h stb_image.h stb_image_write.h

# Default build: includes the in-engine editor + pick buffer (Tab to toggle).
portal_engine: $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o portal_engine $(LDFLAGS)

run: portal_engine
	./portal_engine

# Stripped "play" build: no editor, no per-pixel pick buffer (-DEDITOR=0).
# Cleans first so the macro change always takes effect. Run `make clean` (or
# plain `make`) afterwards to get the editor build back.
play:
	$(MAKE) clean
	$(MAKE) EXTRA="-DEDITOR=0"
	./portal_engine

clean:
	rm -f portal_engine

.PHONY: run play clean
