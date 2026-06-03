CXX      = c++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra $(shell sdl2-config --cflags)
LDFLAGS  = $(shell sdl2-config --libs)
SRCS     = main.cpp Map.cpp Renderer.cpp Player.cpp
HDRS     = Vec2.h Camera.h Map.h Renderer.h Player.h

build_engine: $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o build_engine $(LDFLAGS)

run: build_engine
	./build_engine

clean:
	rm -f build_engine

.PHONY: run clean
