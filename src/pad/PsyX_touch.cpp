#if defined(__ANDROID__)

#include "PsyX_touch.h"

#include "../platform.h"
#include "PsyX/PsyX_render.h"

#include <math.h>
#include <stdlib.h>

// PNG decoding for the control sprites
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

extern SDL_Window* g_window;

#define MAX_TOUCH_FINGERS	10

//------------------------------------------------------------------------
// Sprites: Kenney "Mobile Controls" (CC0), stored in the APK assets (app/src/main/assets/touch)

enum SpriteId
{
	SPR_NONE = -1,

	SPR_DPAD,
	SPR_CIRCLE,
	SPR_WIDE,
	SPR_BEAN,
	SPR_DIR_LEFT,
	SPR_DIR_RIGHT,

	SPR_DPAD_HL,
	SPR_CIRCLE_HL,
	SPR_WIDE_HL,
	SPR_BEAN_HL,
	SPR_DIR_LEFT_HL,
	SPR_DIR_RIGHT_HL,

	SPR_ICON_PEDAL,
	SPR_ICON_BRAKE,
	SPR_ICON_HAND,
	SPR_ICON_VIEW,
	SPR_ICON_L,
	SPR_ICON_R,
	SPR_ICON_PAUSE,
	SPR_ICON_MENU,

	SPR_COUNT
};

static const char* s_spriteFiles[SPR_COUNT] =
{
	"touch/dpad.png",
	"touch/circle.png",
	"touch/wide.png",
	"touch/bean.png",
	"touch/dir_left.png",
	"touch/dir_right.png",

	"touch/dpad_hl.png",
	"touch/circle_hl.png",
	"touch/wide_hl.png",
	"touch/bean_hl.png",
	"touch/dir_left_hl.png",
	"touch/dir_right_hl.png",

	"touch/icon_pedal.png",
	"touch/icon_pedal_brake.png",
	"touch/icon_hand.png",
	"touch/icon_arrow_rotate.png",
	"touch/icon_button_l.png",
	"touch/icon_button_r.png",
	"touch/icon_pause.png",
	"touch/icon_menu.png",
};

//------------------------------------------------------------------------
// Controls

enum TouchControlType
{
	TOUCH_BUTTON,		// pressed when a finger is inside the shape
	TOUCH_DPAD,			// 4 directions + diagonals, depending on the finger offset from the center
};

// pad bits, same as for the keyboard
enum
{
	PAD_SELECT	= 0x1,
	PAD_START	= 0x8,
	PAD_UP		= 0x10,
	PAD_RIGHT	= 0x20,
	PAD_DOWN	= 0x40,
	PAD_LEFT	= 0x80,
	PAD_L2		= 0x100,
	PAD_R2		= 0x200,
	PAD_L1		= 0x400,
	PAD_R1		= 0x800,
	PAD_TRIANGLE= 0x1000,
	PAD_CIRCLE	= 0x2000,
	PAD_CROSS	= 0x4000,
	PAD_SQUARE	= 0x8000,
};

struct TouchControl
{
	TouchControlType	type;
	float				cx, cy;			// center, in touch units (see TOUCH_UNIT). cx is relative to anchorX, negative cy is relative to the bottom edge
	float				hw, hh;			// half of the size, in touch units
	unsigned short		mask;			// pad bit pressed by the button
	float				anchorX;		// 0 = left edge, 0.5 = center, 1 = right edge
	SpriteId			base;
	SpriteId			highlight;		// drawn on top while pressed
	SpriteId			icon;
};

// one touch unit is a fifth of the screen height, so the layout scales with the display
#define TOUCH_UNIT	0.2f

static const TouchControl s_controls[] =
{
	{ TOUCH_DPAD,	 1.45f, -1.35f, 1.15f, 1.15f, 0,			0.0f, SPR_DPAD, SPR_DPAD_HL, SPR_NONE },

	// face buttons in a diamond, bottom right
	{ TOUCH_BUTTON,	-1.90f, -0.85f, 0.52f, 0.52f, PAD_CROSS,	1.0f, SPR_CIRCLE, SPR_CIRCLE_HL, SPR_ICON_PEDAL },		// accelerate
	{ TOUCH_BUTTON,	-2.75f, -1.70f, 0.52f, 0.52f, PAD_SQUARE,	1.0f, SPR_CIRCLE, SPR_CIRCLE_HL, SPR_ICON_BRAKE },		// brake / reverse
	{ TOUCH_BUTTON,	-1.05f, -1.70f, 0.52f, 0.52f, PAD_CIRCLE,	1.0f, SPR_CIRCLE, SPR_CIRCLE_HL, SPR_ICON_HAND },		// handbrake
	{ TOUCH_BUTTON,	-1.90f, -2.55f, 0.52f, 0.52f, PAD_TRIANGLE,	1.0f, SPR_CIRCLE, SPR_CIRCLE_HL, SPR_ICON_VIEW },

	// shoulder buttons, top corners
	{ TOUCH_BUTTON,	 1.00f,  0.65f, 0.80f, 0.40f, PAD_L1,		0.0f, SPR_WIDE, SPR_WIDE_HL, SPR_ICON_L },
	{ TOUCH_BUTTON,	 0.80f,  1.65f, 0.50f, 0.50f, PAD_L2,		0.0f, SPR_DIR_LEFT, SPR_DIR_LEFT_HL, SPR_ICON_L },
	{ TOUCH_BUTTON,	-1.00f,  0.65f, 0.80f, 0.40f, PAD_R1,		1.0f, SPR_WIDE, SPR_WIDE_HL, SPR_ICON_R },
	{ TOUCH_BUTTON,	-0.80f,  1.65f, 0.50f, 0.50f, PAD_R2,		1.0f, SPR_DIR_RIGHT, SPR_DIR_RIGHT_HL, SPR_ICON_R },

	{ TOUCH_BUTTON,	-0.70f,  0.45f, 0.50f, 0.33f, PAD_SELECT,	0.5f, SPR_BEAN, SPR_BEAN_HL, SPR_ICON_MENU },
	{ TOUCH_BUTTON,	 0.70f,  0.45f, 0.50f, 0.33f, PAD_START,	0.5f, SPR_BEAN, SPR_BEAN_HL, SPR_ICON_PAUSE },
};

static const int NUM_CONTROLS = sizeof(s_controls) / sizeof(s_controls[0]);

struct TouchPoint
{
	float x, y;		// in window pixels
};

static unsigned short	s_pressedControls = 0;		// bit per control index, for drawing
static int				s_numPoints = 0;
static TouchPoint		s_points[MAX_TOUCH_FINGERS];

struct ControlRect
{
	float x, y;		// center
	float hw, hh;	// half size
};

static ControlRect GetControlRect(const TouchControl& c, float w, float h)
{
	const float unit = TOUCH_UNIT * h;

	ControlRect r;
	r.x = c.cx * unit + c.anchorX * w;
	r.y = c.cy >= 0.0f ? c.cy * unit : h + c.cy * unit;
	r.hw = c.hw * unit;
	r.hh = c.hh * unit;
	return r;
}

static void CollectTouchPoints(float w, float h)
{
	s_numPoints = 0;

	const int numDevices = SDL_GetNumTouchDevices();
	for (int d = 0; d < numDevices && s_numPoints < MAX_TOUCH_FINGERS; d++)
	{
		const SDL_TouchID id = SDL_GetTouchDevice(d);
		const int numFingers = SDL_GetNumTouchFingers(id);

		for (int f = 0; f < numFingers && s_numPoints < MAX_TOUCH_FINGERS; f++)
		{
			const SDL_Finger* finger = SDL_GetTouchFinger(id, f);
			if (!finger)
				continue;

			s_points[s_numPoints].x = finger->x * w;
			s_points[s_numPoints].y = finger->y * h;
			s_numPoints++;
		}
	}
}

unsigned short PsyX_Touch_GetButtons()
{
	if (!g_window)
		return 0xFFFF;

	int iw, ih;
	SDL_GetWindowSize(g_window, &iw, &ih);

	const float w = (float)iw, h = (float)ih;
	if (w <= 0.0f || h <= 0.0f)
		return 0xFFFF;

	CollectTouchPoints(w, h);

	unsigned short ret = 0xFFFF;
	unsigned short pressed = 0;

	for (int i = 0; i < NUM_CONTROLS; i++)
	{
		const TouchControl& c = s_controls[i];
		const ControlRect r = GetControlRect(c, w, h);

		for (int p = 0; p < s_numPoints; p++)
		{
			const float dx = s_points[p].x - r.x;
			const float dy = s_points[p].y - r.y;

			if (c.type == TOUCH_BUTTON)
			{
				// ellipse a bit bigger than the sprite so thumbs don't miss
				const float nx = dx / r.hw, ny = dy / r.hh;
				if (nx * nx + ny * ny <= 1.5f)
				{
					ret &= ~c.mask;
					pressed |= 1 << i;
				}
			}
			else if (dx * dx + dy * dy <= r.hw * r.hw * 1.2f)
			{
				const float dead = r.hw * 0.25f;

				if (dx < -dead)	ret &= ~PAD_LEFT;
				if (dx > dead)	ret &= ~PAD_RIGHT;
				if (dy < -dead)	ret &= ~PAD_UP;
				if (dy > dead)	ret &= ~PAD_DOWN;

				pressed |= 1 << i;
			}
		}
	}

	s_pressedControls = pressed;

	return ret;
}

//------------------------------------------------------------------------
// drawing

#if defined(RENDERER_OGLES) && OGLES_VERSION == 3

static const char* s_vertexSrc =
	"#version 300 es\n"
	"uniform vec2 u_screen;\n"
	"uniform vec2 u_center;\n"
	"uniform vec2 u_half;\n"
	"out vec2 v_uv;\n"
	"void main() {\n"
	"	vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;\n"
	"	v_uv = corner * 0.5 + 0.5;\n"
	"	vec2 px = u_center + corner * u_half;\n"
	"	gl_Position = vec4(px.x / u_screen.x * 2.0 - 1.0, 1.0 - px.y / u_screen.y * 2.0, 0.0, 1.0);\n"
	"}\n";

static const char* s_fragmentSrc =
	"#version 300 es\n"
	"precision mediump float;\n"
	"uniform sampler2D u_tex;\n"
	"uniform vec4 u_color;\n"
	"in vec2 v_uv;\n"
	"out vec4 o_color;\n"
	"void main() {\n"
	"	o_color = texture(u_tex, v_uv) * u_color;\n"
	"}\n";

struct Sprite
{
	GLuint	tex;
	int		w, h;
};

static Sprite s_sprites[SPR_COUNT];
static GLuint s_program = 0;
static GLint s_uScreen, s_uCenter, s_uHalf, s_uColor, s_uTex;
static bool s_initDone = false;
static bool s_initFailed = false;

static GLuint CompileShader(GLenum type, const char* src)
{
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, 1, &src, NULL);
	glCompileShader(sh);

	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[512];
		glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		eprinterr("touch overlay shader error: %s\n", log);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

static bool LoadSprite(SpriteId id)
{
	SDL_RWops* rw = SDL_RWFromFile(s_spriteFiles[id], "rb");	// reads from the APK assets
	if (!rw)
	{
		eprinterr("touch overlay: cannot open %s\n", s_spriteFiles[id]);
		return false;
	}

	const Sint64 size = SDL_RWsize(rw);
	unsigned char* fileData = size > 0 ? (unsigned char*)malloc((size_t)size) : NULL;
	if (!fileData || SDL_RWread(rw, fileData, 1, (size_t)size) != (size_t)size)
	{
		free(fileData);
		SDL_RWclose(rw);
		return false;
	}
	SDL_RWclose(rw);

	int w, h, comp;
	unsigned char* pixels = stbi_load_from_memory(fileData, (int)size, &w, &h, &comp, 4);
	free(fileData);

	if (!pixels)
	{
		eprinterr("touch overlay: cannot decode %s\n", s_spriteFiles[id]);
		return false;
	}

	Sprite& spr = s_sprites[id];
	spr.w = w;
	spr.h = h;

	glGenTextures(1, &spr.tex);
	glBindTexture(GL_TEXTURE_2D, spr.tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	stbi_image_free(pixels);
	return true;
}

// needs the GL state to be saved by the caller, it changes bindings
static bool InitOverlay()
{
	GLuint vs = CompileShader(GL_VERTEX_SHADER, s_vertexSrc);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, s_fragmentSrc);
	if (!vs || !fs)
		return false;

	s_program = glCreateProgram();
	glAttachShader(s_program, vs);
	glAttachShader(s_program, fs);
	glLinkProgram(s_program);
	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint ok = 0;
	glGetProgramiv(s_program, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		glDeleteProgram(s_program);
		s_program = 0;
		return false;
	}

	s_uScreen = glGetUniformLocation(s_program, "u_screen");
	s_uCenter = glGetUniformLocation(s_program, "u_center");
	s_uHalf = glGetUniformLocation(s_program, "u_half");
	s_uColor = glGetUniformLocation(s_program, "u_color");
	s_uTex = glGetUniformLocation(s_program, "u_tex");

	// uploads must not go through a pixel buffer or odd row alignment left by the renderer
	GLint prevUnpackBuffer, prevAlignment;
	glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &prevUnpackBuffer);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlignment);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	for (int i = 0; i < SPR_COUNT; i++)
		LoadSprite((SpriteId)i);

	glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlignment);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, prevUnpackBuffer);

	return true;
}

static void DrawSprite(SpriteId id, float cx, float cy, float hw, float hh, float alpha)
{
	if (id == SPR_NONE || !s_sprites[id].tex)
		return;

	glBindTexture(GL_TEXTURE_2D, s_sprites[id].tex);
	glUniform2f(s_uCenter, cx, cy);
	glUniform2f(s_uHalf, hw, hh);
	glUniform4f(s_uColor, 1.0f, 1.0f, 1.0f, alpha);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void PsyX_Touch_Draw()
{
	if (s_initFailed || !g_window)
		return;

	int iw, ih;
	SDL_GL_GetDrawableSize(g_window, &iw, &ih);
	int ww, wh;
	SDL_GetWindowSize(g_window, &ww, &wh);
	if (iw <= 0 || ih <= 0 || ww <= 0 || wh <= 0)
		return;

	// touch coordinates are in window space, drawing is in drawable pixels
	const float scale = (float)iw / (float)ww;
	const float w = (float)ww, h = (float)wh;

	// the renderer caches its GL state, so everything touched here has to be put back
	GLint prevProgram, prevVAO, prevViewport[4], prevActiveTexture, prevTexture0;
	GLint prevSrcRGB, prevDstRGB, prevSrcA, prevDstA, prevEqRGB, prevEqA;
	GLboolean prevColorMask[4];
	const GLboolean prevBlend = glIsEnabled(GL_BLEND);
	const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
	const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
	const GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
	glGetIntegerv(GL_VIEWPORT, prevViewport);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture0);
	glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrcRGB);
	glGetIntegerv(GL_BLEND_DST_RGB, &prevDstRGB);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcA);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDstA);
	glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevEqRGB);
	glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevEqA);
	glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);

	if (!s_initDone)
	{
		s_initDone = true;
		if (!InitOverlay())
			s_initFailed = true;
	}

	if (!s_initFailed)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, iw, ih);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_STENCIL_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glUseProgram(s_program);
		glBindVertexArray(0);
		glUniform2f(s_uScreen, (float)iw, (float)ih);
		glUniform1i(s_uTex, 0);

		for (int i = 0; i < NUM_CONTROLS; i++)
		{
			const TouchControl& c = s_controls[i];
			const ControlRect r = GetControlRect(c, w, h);
			const bool pressed = (s_pressedControls >> i) & 1;

			const float x = r.x * scale, y = r.y * scale;
			const float hw = r.hw * scale, hh = r.hh * scale;

			DrawSprite(c.base, x, y, hw, hh, pressed ? 0.90f : 0.38f);

			if (pressed)
				DrawSprite(c.highlight, x, y, hw, hh, 1.0f);

			if (c.icon != SPR_NONE)
			{
				const float ih2 = (hw < hh ? hw : hh) * 0.55f;	// icons are square
				DrawSprite(c.icon, x, y, ih2, ih2, pressed ? 1.0f : 0.65f);
			}
		}
	}

	glUseProgram(prevProgram);
	glBindVertexArray(prevVAO);
	glBindTexture(GL_TEXTURE_2D, prevTexture0);
	glActiveTexture(prevActiveTexture);
	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
	glBlendFuncSeparate(prevSrcRGB, prevDstRGB, prevSrcA, prevDstA);
	glBlendEquationSeparate(prevEqRGB, prevEqA);
	glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);

	if (!prevBlend)		glDisable(GL_BLEND);
	if (prevDepth)		glEnable(GL_DEPTH_TEST);
	if (prevScissor)	glEnable(GL_SCISSOR_TEST);
	if (prevStencil)	glEnable(GL_STENCIL_TEST);
}

#else

void PsyX_Touch_Draw() {}

#endif // GLES3

#endif // __ANDROID__
