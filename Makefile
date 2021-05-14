
CXXFLAGS := -O3 -Wall -Wextra -Wfatal-errors $(shell sdl2-config --cflags)
LIBS := $(shell sdl2-config --libs) -lSDL2_image -framework OpenGl
IMGUI_OBJECTS := src/imgui/imgui.o src/imgui/imgui_demo.o src/imgui/imgui_draw.o src/imgui/imgui_impl_opengl2.o src/imgui/imgui_impl_sdl.o src/imgui/imgui_tables.o src/imgui/imgui_widgets.o
OBJECTS := src/emu/e6809.o src/emu/e8910.o src/emu/e6522.o src/emu/edac.o src/emu/vecx.o $(IMGUI_OBJECTS) src/ser.o src/main.o 
TARGET := vecx
CLEANFILES := $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) $(CLEANFILES)

