#include <gui/Gui.h>

#include <gui/DebugUI.h>
#include <gui/FontLoader.h>
#include <rendering/TextureMap.h>
#include <rendering/VertexFmt.h>

#include <stdarg.h>

#include <vec/vec.h>

typedef enum { GuiCmd_PushQuad, GuiCmd_PushIcon } GuiCmdType;

typedef struct {
	GuiCmdType type;
	int depth;
	C3D_Tex* texture;
	union {
		struct {
			int16_t x0, y0, x1, y1;
			int16_t u0, v0, u1, v1;
			int16_t color;
		} pushQuad;
		struct {
			Block block;
			int x, y, size;
		} pushIcon;
	};
} GuiCmd;

static vec_t(GuiCmd) cmdList;
static C3D_Tex* currentTexture = NULL;
static Vertex* vertexList[2];
static int projUniform;

static Font* font;
static C3D_Tex whiteTex;
static C3D_Tex widgetsTex;
static C3D_Tex iconsTex;

static InputData oldInput;
static InputData input;

static int guiScale = 2;

void Gui_Init(int projUniform_) {
	vec_init(&cmdList);

	vertexList[0] = linearAlloc(sizeof(Vertex) * 256);
	vertexList[1] = linearAlloc(sizeof(Vertex) * 4096);

	projUniform = projUniform_;

	font = (Font*)malloc(sizeof(Font));
	FontLoader_Init(font, "romfs:/textures/font/ascii.png");
	Texture_Load(&widgetsTex, "romfs:/textures/gui/widgets.png");

	uint8_t data[16 * 16];
	memset(data, 0xff, 16 * 16 * sizeof(uint8_t));
	C3D_TexInit(&whiteTex, 16, 16, GPU_L8);
	C3D_TexLoadImage(&whiteTex, data, GPU_TEXFACE_2D, 0);

	memset(&input, 0x0, sizeof(InputData));
	memset(&oldInput, 0x0, sizeof(InputData));
}
void Gui_Deinit() {
	vec_deinit(&cmdList);
	linearFree(vertexList[0]);
	linearFree(vertexList[1]);

	C3D_TexDelete(&font->texture);
	free(font);

	C3D_TexDelete(&whiteTex);
	C3D_TexDelete(&widgetsTex);
}

void Gui_BindTexture(C3D_Tex* texture) { currentTexture = texture; }
void Gui_BindGuiTexture(GuiTexture texture) {
	switch (texture) {
		case GuiTexture_Blank:
			currentTexture = &whiteTex;
			break;
		case GuiTexture_Font:
			currentTexture = &font->texture;
			break;
		case GuiTexture_Widgets:
			currentTexture = &widgetsTex;
			break;
		case GuiTexture_Icons:
			currentTexture = &iconsTex;
			break;
		default:
			break;
	}
}

void Gui_PushSingleColorQuad(int x, int y, int z, int w, int h, int16_t color) {
	Gui_BindTexture(&whiteTex);
	Gui_PushQuadColor(x, y, z, w, h, 0, 0, 4, 4, color);
}
void Gui_PushQuad(int x, int y, int z, int w, int h, int rx, int ry, int rw, int rh) { Gui_PushQuadColor(x, y, z, w, h, rx, ry, rw, rh, INT16_MAX); }
void Gui_PushQuadColor(int x, int y, int z, int w, int h, int rx, int ry, int rw, int rh, int16_t color) {
	vec_push(&cmdList, ((GuiCmd){GuiCmd_PushQuad, z, currentTexture,
				     .pushQuad = {x * guiScale, y * guiScale, (x + w) * guiScale, (y + h) * guiScale, rx, ry, rx + rw, ry + rh, color}}));
}
void Gui_PushBlock(Block block, int x, int y, int z, int size) { vec_push(&cmdList, ((GuiCmd){GuiCmd_PushIcon, z, .pushIcon = {block, x, y, size}})); }

int Gui_PushText(int x, int y, int z, int16_t color, bool shadow, int wrap, int* ySize, const char* fmt, ...) {
	va_list arg;
	va_start(arg, fmt);
	int length = Gui_PushTextVargs(x, y, z, color, shadow, wrap, ySize, fmt, arg);
	va_end(arg);
	return length;
}

int Gui_PushTextVargs(int x, int y, int z, int16_t color, bool shadow, int wrap, int* ySize, const char* fmt, va_list arg) {
	Gui_BindTexture(&font->texture);
#define CHAR_WIDTH 8
#define TAB_SIZE 4

	char buffer[256];
	vsprintf(buffer, fmt, arg);

	int offsetX = 0;
	int offsetY = 0;

	int maxWidth = 0;

	char* text = buffer;

	while (*text != '\0') {
		bool implicitBreak = offsetX + font->fontWidth[(int)*text] >= wrap;
		if (*text == '\n' || implicitBreak) {
			offsetY += CHAR_HEIGHT;
			maxWidth = MAX(maxWidth, offsetX);
			offsetX = 0;
			if (implicitBreak) --text;
		} else if (*text == '\t') {
			offsetX = ((offsetX / CHAR_WIDTH) / TAB_SIZE + 1) * TAB_SIZE * CHAR_WIDTH;
		} else {
			if (*text != ' ') {
				int texX = *text % 16 * 8, texY = *text / 16 * 8;
				Gui_PushQuadColor(x + offsetX, y + offsetY, z, 8, 8, texX, texY, 8, 8, color);
				if (shadow) Gui_PushQuadColor(x + offsetX + 1, y + offsetY + 1, z - 1, 8, 8, texX, texY, 8, 8, SHADER_RGB(10, 10, 10));
			}
			offsetX += font->fontWidth[(int)*text];
		}
		++text;
	}

	if (ySize != NULL) *ySize = offsetY + CHAR_HEIGHT;

	return maxWidth;
}

int Gui_CalcTextWidth(const char* text, ...) {
	va_list args;
	va_start(args, text);
	int length = Gui_CalcTextWidthVargs(text, args);
	va_end(args);

	return length;
}

int Gui_CalcTextWidthVargs(const char* text, va_list args) {
	char fmtedText[256];
	vsprintf(fmtedText, text, args);

	char* it = fmtedText;

	int length = 0;
	while (*it != '\0') length += font->fontWidth[(int)*(it++)];

	return length;
}

void Gui_InputData(InputData data) {
	oldInput = input;
	input = data;
}
bool Gui_IsCursorInside(int x, int y, int w, int h) {
	int sclInputX = input.touchX / guiScale;
	int sclInputY = input.touchY / guiScale;
	return sclInputX != 0 && sclInputY != 0 && sclInputX >= x && sclInputX < x + w && sclInputY >= y && sclInputY < y + h;
}
bool Gui_WasCursorInside(int x, int y, int w, int h) {
	int sclOldInputX = oldInput.touchX / guiScale;
	int sclOldInputY = oldInput.touchY / guiScale;
	return sclOldInputX != 0 && sclOldInputY != 0 && sclOldInputX >= x && sclOldInputX < x + w && sclOldInputY >= y && sclOldInputY < y + h;
}
void Gui_GetCursorMovement(int* x, int* y) {
	*x = input.touchX - oldInput.touchX;
	*y = input.touchY - oldInput.touchX;
}

bool Gui_Button(int x, int y, int w, const char* text) {
#define SLICE_SIZE 8

	int textWidth = Gui_CalcTextWidth(text);

	if (w == -1) w = textWidth + SLICE_SIZE;

	bool pressed = Gui_IsCursorInside(x, y, w, BUTTON_HEIGHT);

	int middlePieceSize = w - SLICE_SIZE * 2;

	Gui_BindGuiTexture(GuiTexture_Widgets);
	Gui_PushQuad(x, y, -2, SLICE_SIZE, 20, 0, 46 + (pressed * BUTTON_HEIGHT * 2), SLICE_SIZE, 20);
	Gui_PushQuad(x + SLICE_SIZE, y, -2, middlePieceSize, 20, SLICE_SIZE, 46 + (pressed * BUTTON_HEIGHT * 2), middlePieceSize, 20);
	Gui_PushQuad(x + SLICE_SIZE + middlePieceSize, y, -2, SLICE_SIZE, 20, 192, 46 + (pressed * BUTTON_HEIGHT * 2), SLICE_SIZE, 20);

	Gui_PushText(x + (w / 2 - textWidth / 2), y + (BUTTON_HEIGHT - CHAR_HEIGHT) / 2, 0, SHADER_RGB(31, 31, 31), true, INT_MAX, NULL, text);

	if (input.keysup & KEY_TOUCH && Gui_WasCursorInside(x, y, w, BUTTON_HEIGHT)) return true;

	return false;
}

static int compareDrawCommands(const void* a, const void* b) {
	GuiCmd* ga = ((GuiCmd*)a);
	GuiCmd* gb = ((GuiCmd*)b);

	return ga->depth == gb->depth ? gb->texture - ga->texture : gb->depth - ga->depth;
}

void Gui_SetScale(int scale) { guiScale = scale; }

void Gui_Render(gfxScreen_t screen) {
	vec_sort(&cmdList, &compareDrawCommands);

	C3D_Mtx projMtx;
	Mtx_OrthoTilt(&projMtx, 0.f, screen == GFX_BOTTOM ? 320.f : 400.f, 240.f, 0.f, 1.f, -1.f, false);

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projUniform, &projMtx);

	// C3D_AlphaTest(true, GPU_GREATER, 0);
	C3D_DepthTest(false, GPU_GREATER, GPU_WRITE_ALL);

	Vertex* usedVertexList = vertexList[screen];

	int verticesTotal = 0;

	size_t vtx = 0;
	while (cmdList.length > 0) {
		if (vec_last(&cmdList).type == GuiCmd_PushQuad) {
			size_t vtxStart = vtx;

			C3D_Tex* texture = vec_last(&cmdList).texture;
			float divW = 1.f / texture->width * INT16_MAX, divH = 1.f / texture->height * INT16_MAX;

			while (cmdList.length > 0 && vec_last(&cmdList).type == GuiCmd_PushQuad && vec_last(&cmdList).texture == texture) {
				GuiCmd cmd = vec_pop(&cmdList);
				int16_t color = cmd.pushQuad.color;

				int16_t u0 = (int16_t)((float)cmd.pushQuad.u0 * divW), v0 = (int16_t)((float)cmd.pushQuad.v0 * divH);
				int16_t u1 = (int16_t)((float)cmd.pushQuad.u1 * divW), v1 = (int16_t)((float)cmd.pushQuad.v1 * divH);

				usedVertexList[vtx++] = (Vertex){{cmd.pushQuad.x1, cmd.pushQuad.y1, 0}, {u1, v1, color}};
				usedVertexList[vtx++] = (Vertex){{cmd.pushQuad.x1, cmd.pushQuad.y0, 0}, {u1, v0, color}};
				usedVertexList[vtx++] = (Vertex){{cmd.pushQuad.x0, cmd.pushQuad.y0, 0}, {u0, v0, color}};

				usedVertexList[vtx++] = (Vertex){{cmd.pushQuad.x0, cmd.pushQuad.y0, 0}, {u0, v0, color}};
				usedVertexList[vtx++] = (Vertex){{cmd.pushQuad.x0, cmd.pushQuad.y1, 0}, {u0, v1, color}};
				usedVertexList[vtx++] = (Vertex){{cmd.pushQuad.x1, cmd.pushQuad.y1, 0}, {u1, v1, color}};
			}

			C3D_TexBind(0, texture);

			C3D_BufInfo* bufInfo = C3D_GetBufInfo();
			BufInfo_Init(bufInfo);
			BufInfo_Add(bufInfo, usedVertexList + vtxStart, sizeof(Vertex), 2, 0x10);

			C3D_DrawArrays(GPU_TRIANGLES, 0, vtx - vtxStart);

			verticesTotal += vtx - vtxStart;
		} else
			vec_pop(&cmdList);
	}
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);

	currentTexture = NULL;
	guiScale = 2;
}