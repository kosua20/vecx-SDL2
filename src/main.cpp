#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_opengl.h>


#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_impl_opengl2.h"
#include "imgui/imgui_memory_editor.h"

#include "emu/e6809.h"
#include "emu/e8910.h"
#include "emu/e6522.h"
#include "emu/edac.h"
#include "emu/vecx.h"
#include "ser.h"

#define EMU_TIMER 20 /* the emulators heart beats at 20 milliseconds */

#define DEFAULT_WIDTH 495
#define DEFAULT_HEIGHT 615
#define DEBUG_WIDTH 560
#define DEBUG_POS_X 50

vecx vectrex;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *overlay = NULL;
static SDL_Texture *buffer = NULL;
static SDL_Texture *buffer2 = NULL;

static SDL_Window *windowAux = NULL;
static SDL_GLContext windowAuxContext = NULL;

static int32_t scl_factor;

static void quit(void);

/* command line arguments */
static char *bios_filename = "bios.bin";
static char *cart_filename = NULL;
static char *overlay_filename = NULL;
static char fullscreen = 0;
static bool debugWindow = false;
static bool mute = false;
/* Debug */
static MemoryEditor mem_edit;
static bool paused = false;
static bool step = false;
static int stepSize = 1;
static bool sharpRender = false;
static int remanence = 128;
static int emuSpeed = 1;

static void render(void)
{
	SDL_SetRenderTarget(renderer, buffer);
	{
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, sharpRender ? 255 : remanence);
		SDL_RenderFillRect(renderer, NULL);

		for (size_t v = 0; v < vectrex.vector_draw_cnt; v++)
		{
			Uint8 c = vectrex.vectors[v].color * 256 / VECTREX_COLORS;
			int x0 = vectrex.vectors[v].x0 / scl_factor;
			int y0 = vectrex.vectors[v].y0 / scl_factor;
			int x1 = vectrex.vectors[v].x1 / scl_factor;
			int y1 = vectrex.vectors[v].y1 / scl_factor;

			SDL_SetRenderDrawColor(renderer, 255, 255, 255, c);
			if (x0 == x1 && y0 == y1)
			{
				/* point */
				SDL_RenderDrawPoint(renderer, x0, y0);
				SDL_RenderDrawPoint(renderer, x0 + 1, y0);
				SDL_RenderDrawPoint(renderer, x0, y0 + 1);
				SDL_RenderDrawPoint(renderer, x0 + 1, y0 + 1);
			}
			else
			{
				SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
				SDL_RenderDrawLine(renderer, x0 + 1, y0 + 1, x1 + 1, y1 + 1);
			}
		}
	}

	SDL_SetRenderTarget(renderer, buffer2);
	{
		SDL_RenderCopy(renderer, buffer, NULL, NULL);
	}

	SDL_SetRenderTarget(renderer, NULL);
	{
		SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);
		SDL_RenderClear(renderer);

		SDL_RenderCopy(renderer, buffer, NULL, NULL);

		if(!sharpRender){
			SDL_RenderCopy(renderer, buffer2, NULL, NULL);
		}

		if (overlay)
		{
			SDL_RenderCopy(renderer, overlay, NULL, NULL);
		}
	}
	SDL_RenderPresent(renderer);
}

static void load_bios(void)
{
	FILE *f;
	if (!(f = fopen(bios_filename, "rb")))
	{
		perror(bios_filename);
		quit();
	}
	if (fread(vectrex.rom, 1, sizeof(vectrex.rom), f) != sizeof(vectrex.rom))
	{
		fprintf(stderr, "Invalid bios length\n");
		quit();
	}
	fclose(f);
}

static void load_cart(void)
{
	memset(vectrex.cart, 0, sizeof(vectrex.cart));
	if (cart_filename)
	{
		FILE *f;
		if (!(f = fopen(cart_filename, "rb")))
		{
			perror(cart_filename);
			return;
		}
		fread(vectrex.cart, 1, sizeof(vectrex.cart), f);
		fclose(f);
	}
}

static void resize(void)
{
	int sclx, scly;
	int screenx, screeny;
	int width, height;

	SDL_GetWindowSize(window, &screenx, &screeny);

	sclx = DAC_MAX_X / screenx;
	scly = DAC_MAX_Y / screeny;

	scl_factor = sclx > scly ? sclx : scly;
	width = DAC_MAX_X / scl_factor;
	height = DAC_MAX_Y / scl_factor;

	SDL_RenderSetLogicalSize(renderer, width, height);

	SDL_DestroyTexture(buffer);
	SDL_DestroyTexture(buffer2);

	buffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
	buffer2 = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width / 2, height / 2);

	SDL_SetTextureBlendMode(buffer2, SDL_BLENDMODE_BLEND);
	SDL_SetTextureAlphaMod(buffer2, 128);
}

static int readevents(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e))
	{
		// Shared event.
		if(e.type == SDL_KEYUP){
			bool handled = true;
			switch (e.key.keysym.sym) {
				case SDLK_ESCAPE: return 1;
				case SDLK_p:
					paused = !paused;
				break;
				case SDLK_CARET: 
					step = paused;
					stepSize = 1;
				break;
				case SDLK_DOLLAR: 
					step = paused;
					stepSize = 10;
				break;
				default:
				handled = false;
				break;
			}
			if(handled){
				return 0;
			}
		}

		if(e.window.windowID == SDL_GetWindowID( window ))
		{
			switch (e.type)
			{
			case SDL_QUIT:
				return 1;
				break;
			case SDL_WINDOWEVENT:
				if (e.window.event == SDL_WINDOWEVENT_RESIZED)
					resize();
				break;
			case SDL_DROPFILE:
				cart_filename = e.drop.file;
				load_cart();
				vecx_reset(&vectrex);
				break;
			case SDL_KEYDOWN:
				switch (e.key.keysym.sym)
				{
				case SDLK_ESCAPE: return 1;
				// French keyboard version. a->q
				case SDLK_q: vecx_input(&vectrex, VECTREX_PAD1_BUTTON1, 1); break;
				case SDLK_s: vecx_input(&vectrex, VECTREX_PAD1_BUTTON2, 1); break;
				case SDLK_d: vecx_input(&vectrex, VECTREX_PAD1_BUTTON3, 1); break;
				case SDLK_f: vecx_input(&vectrex, VECTREX_PAD1_BUTTON4, 1); break;
				case SDLK_LEFT: vecx_input(&vectrex, VECTREX_PAD1_X, 0x00); break;
				case SDLK_RIGHT: vecx_input(&vectrex, VECTREX_PAD1_X, 0xff); break;
				case SDLK_UP: vecx_input(&vectrex, VECTREX_PAD1_Y, 0xff); break;
				case SDLK_DOWN: vecx_input(&vectrex, VECTREX_PAD1_Y, 0x00); break;
				}
				break;
			case SDL_KEYUP:
				switch (e.key.keysym.sym)
				{
				case SDLK_F1: vecx_load(&vectrex, "q.save"); break;
				case SDLK_F2: vecx_save(&vectrex, "q.save"); break;
				case SDLK_r: load_cart(); vecx_reset(&vectrex); break;
				// French keyboard version. a->q
				case SDLK_q: vecx_input(&vectrex, VECTREX_PAD1_BUTTON1, 0); break;
				case SDLK_s: vecx_input(&vectrex, VECTREX_PAD1_BUTTON2, 0); break;
				case SDLK_d: vecx_input(&vectrex, VECTREX_PAD1_BUTTON3, 0); break;
				case SDLK_f: vecx_input(&vectrex, VECTREX_PAD1_BUTTON4, 0); break;
				case SDLK_LEFT: vecx_input(&vectrex, VECTREX_PAD1_X, 0x80); break;
				case SDLK_RIGHT: vecx_input(&vectrex, VECTREX_PAD1_X, 0x80); break;
				case SDLK_UP: vecx_input(&vectrex, VECTREX_PAD1_Y, 0x80); break;
				case SDLK_DOWN: vecx_input(&vectrex, VECTREX_PAD1_Y, 0x80); break;
				}
				break;
			}
		} 
		else if(windowAux && (e.window.windowID == SDL_GetWindowID( windowAux )))
		{
			ImGui_ImplSDL2_ProcessEvent(&e);

		}
	}
	return 0;
}

static void showTextAndTooltip(const char* name, uint16_t value){
	ImGui::Text("%s: %#x", name, value);
	if(ImGui::IsItemHovered()){
		ImGui::SetTooltip("%huu, %hds", value, value);
	}
}

static void renderDebug(void){
	int w, h;
	SDL_GetWindowSize(windowAux, &w, &h);

	ImGui::SetNextWindowPos(ImVec2(0,0));
	ImGui::SetNextWindowSize(ImVec2(w,h));

	ImGui::Begin("Debug Vecx", NULL, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);
	
	ImGui::Checkbox("Pause", &paused); ImGui::SameLine();
	ImGui::Checkbox("Sharp", &sharpRender); ImGui::SameLine();
	ImGui::PushItemWidth(140);
	ImGui::SliderInt("Remanence", &remanence, 0, 255); 
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::PushItemWidth(80);
	ImGui::InputInt("Emu. speed", &emuSpeed, 1, 10); 
	ImGui::PopItemWidth();

	ImGui::Separator();

	const uint lineStep = 140;
	showTextAndTooltip("Reg. X", vectrex.CPU.reg_x); ImGui::SameLine(lineStep); /* index registers */
	showTextAndTooltip("Reg. Y", vectrex.CPU.reg_y); ImGui::SameLine(2*lineStep);
	showTextAndTooltip("Reg. U", vectrex.CPU.reg_u); ImGui::SameLine(3*lineStep); /* user stack pointer */
	showTextAndTooltip("Reg. S", vectrex.CPU.reg_s); /* hardware stack pointer */
	
	showTextAndTooltip("Prog. count", vectrex.CPU.reg_pc); ImGui::SameLine(lineStep); /* program counter */
	showTextAndTooltip("Acc. A", vectrex.CPU.reg_a); ImGui::SameLine(2*lineStep);
	showTextAndTooltip("Acc. B", vectrex.CPU.reg_b); ImGui::SameLine(3*lineStep); 
	showTextAndTooltip("Direct pg.", vectrex.CPU.reg_dp); /* direct page register */

	showTextAndTooltip("Cond. code", vectrex.CPU.reg_cc); ImGui::SameLine(lineStep); /* condition codes */
	showTextAndTooltip("IRQ stat.", vectrex.CPU.irq_status); ImGui::SameLine(2*lineStep); /* flag to see if interrupts should be handled (sync/cwai). */

	showTextAndTooltip("0", *vectrex.CPU.rptr_xyus[0]); ImGui::SameLine(); 
	showTextAndTooltip("1", *vectrex.CPU.rptr_xyus[1]); ImGui::SameLine(); 
	showTextAndTooltip("2", *vectrex.CPU.rptr_xyus[2]); ImGui::SameLine(); 
	showTextAndTooltip("3", *vectrex.CPU.rptr_xyus[3]);
	

	if (ImGui::BeginTabBar("Memory", 0)){
		if (ImGui::BeginTabItem("RAM")) {
			mem_edit.DrawContents(vectrex.ram, 1024, 0);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("ROM")) {
			mem_edit.DrawContents(vectrex.rom, 8192, 0);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Cart")) {
			mem_edit.DrawContents(vectrex.cart, 32768, 0);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
		
	}
	

	ImGui::End();
}

static void emuloop(void)
{
	Uint32 next_time = SDL_GetTicks() + EMU_TIMER;
	vecx_reset(&vectrex);
	for (;;)
	{
		if(paused && step){
			vecx_emu(&vectrex, stepSize);
			step = false;
			
		} else if(!paused){
			vecx_emu(&vectrex, (VECTREX_MHZ / 1000) * emuSpeed * EMU_TIMER);
		}

		if (readevents()) break;

		 // Start the Dear ImGui frame
		if(windowAux){	

			ImGui_ImplOpenGL2_NewFrame();
			ImGui_ImplSDL2_NewFrame(windowAux);
			ImGui::NewFrame();

			renderDebug();

			ImGui::Render();
			ImGuiIO& io = ImGui::GetIO(); 
			glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
			SDL_GL_SwapWindow(windowAux);
		}

		{
			Uint32 now = SDL_GetTicks();
			if (now < next_time)
				SDL_Delay(next_time - now);
			else
				next_time = now;
			next_time += EMU_TIMER;
		}
	}
}

static void load_overlay()
{
	if (overlay_filename)
	{
		SDL_Texture *image = IMG_LoadTexture(renderer, overlay_filename);
		if (image)
		{
			overlay = image;
			SDL_SetTextureBlendMode(image, SDL_BLENDMODE_BLEND);
			SDL_SetTextureAlphaMod(image, 128);
		}
		else
		{
			fprintf(stderr, "IMG_Load: %s\n", IMG_GetError());
		}
	}
}

static int init(void)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
	{
		fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
		return 0;
	}
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

	int windowPosX = debugWindow ? (DEBUG_WIDTH + DEBUG_POS_X) : SDL_WINDOWPOS_CENTERED;
	window = SDL_CreateWindow("Vecx", windowPosX, SDL_WINDOWPOS_CENTERED, DEFAULT_WIDTH, DEFAULT_HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_INPUT_FOCUS);
	if (!window)
	{
		fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
		return 0;
	}

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
	if (!renderer)
	{
		fprintf(stderr, "Failed to create renderer: %s\n", SDL_GetError());
		return 0;
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

	if (fullscreen)
		SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);

	// Debug window for ImGui.
	if(debugWindow){

		windowAux = SDL_CreateWindow("Vecx debug", DEBUG_POS_X, SDL_WINDOWPOS_CENTERED, DEBUG_WIDTH, DEFAULT_HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
		if (!windowAux)
		{
			fprintf(stderr, "Failed to create debug window: %s\n", SDL_GetError());
			return 0;
		}

		windowAuxContext = SDL_GL_CreateContext(windowAux);
		SDL_GL_MakeCurrent(windowAux, windowAuxContext);

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
   
		// Setup Dear ImGui style
		ImGui::StyleColorsDark();

		// Setup Platform/Renderer backends
		ImGui_ImplSDL2_InitForOpenGL(windowAux, windowAuxContext);
		ImGui_ImplOpenGL2_Init();

	}

	SDL_RaiseWindow(window);


	return 1;
}

static void quit(void)
{	
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	if (windowAux){
		ImGui_ImplOpenGL2_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
		SDL_GL_DeleteContext(windowAuxContext);
		SDL_DestroyWindow(windowAux);
	}
	SDL_Quit();

	exit(0);
}

void parse_args(int argc, char* argv[])
{
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
			puts("Usage: vecx [options] [cart_file]");
			puts("Options:");
			puts("  --help            Display this help message");
			puts("  --bios <file>     Load bios file");
			puts("  --overlay <file>  Load overlay file");
			puts("  --fullscreen      Launch in fullscreen mode");
			puts("  --debug      	  Enable debug view");
			puts("  --mute      	  Mute sound");
			exit(0);
		}
		else if (strcmp(argv[i], "--bios") == 0 || strcmp(argv[i], "-b") == 0)
		{
			bios_filename = argv[++i];
		}
		else if (strcmp(argv[i], "--overlay") == 0 || strcmp(argv[i], "-o") == 0)
		{
			overlay_filename = argv[++i];
		}
		else if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0)
		{
			fullscreen = 1;
		}
		else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0)
		{
			debugWindow = 1;
		}
		else if (strcmp(argv[i], "--mute") == 0 || strcmp(argv[i], "-m") == 0)
		{
			mute = 1;
		}
		else if (i == argc - 1)
		{
			cart_filename = argv[i];
		}
		else
		{
			printf("Unkown flag: %s\n", argv[i]);
			exit(0);
		}
	}
}

int main(int argc, char *argv[])
{
	parse_args(argc, argv);

	if (!init())
		quit();

	resize();
	load_bios();
	load_cart();
	load_overlay();
	e8910_init(&vectrex.PSG);
	e8910_mute(&vectrex.PSG, (char)mute);

	vectrex.render = render;

	emuloop();

	e8910_done(&vectrex.PSG);

	quit();

	return 0;
}
