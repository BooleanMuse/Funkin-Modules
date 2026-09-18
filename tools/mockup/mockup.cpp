// ============================================================================
// Renders the real panels into a PNG without opening Rack.
//
// Why this exists: reading coordinates does not tell you that a label overlaps a
// socket, or that the health bar is a millimetre into the title strip. It calls
// the very same DrawOpponentPanel and DrawPlayerPanel the modules call, then
// puts every knob and socket on top at its true diameter, so an overlap is
// something you can see rather than something you have to work out.
//
//     ./build.sh mockup          ->  build/mockup/opponent.png, player.png
//
// It gets NanoVG and GLEW out of libRack, which already contains both, and an
// OpenGL context out of EGL with no window and no display server.
// ============================================================================
#include <EGL/egl.h>
#include <GL/glew.h>

#include <nanovg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stb_image_write.h>
#pragma GCC diagnostic pop

#include "Layout.hpp"
#include "Library.hpp"
#include "Look.hpp"
#include "PanelDraw.hpp"
#include "SpriteDraw.hpp"

// Declared here rather than included: nanovg_gl.h wants a GL loader set up in a
// particular order, and these two are all we need out of it.
extern "C" NVGcontext* nvgCreateGL2(int flags);
extern "C" void nvgDeleteGL2(NVGcontext* ctx);
#define NVG_ANTIALIAS 1
#define NVG_STENCIL_STROKES 2

static const float kScale = 5.f;  // pixels per millimetre, for a readable picture

// Rack's own component diameters, in millimetres. These are what the gaps have
// to clear, and they are the reason this tool exists.
static const float kJackD = 8.0f;
static const float kKnobD = 12.9f;
static const float kSmallKnobD = 9.5f;
static const float kButtonD = 9.0f;

// ---------------------------------------------------------------------------
// A headless OpenGL context
// ---------------------------------------------------------------------------

struct Headless {
	EGLDisplay display = EGL_NO_DISPLAY;
	EGLContext context = EGL_NO_CONTEXT;
	EGLSurface surface = EGL_NO_SURFACE;

	bool start(int w, int h) {
		display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		if (display == EGL_NO_DISPLAY) {
			std::fprintf(stderr, "no EGL display\n");
			return false;
		}
		if (!eglInitialize(display, NULL, NULL)) {
			std::fprintf(stderr, "eglInitialize failed\n");
			return false;
		}
		// Desktop GL, not GLES: the NanoVG inside libRack is the GL2 backend.
		if (!eglBindAPI(EGL_OPENGL_API)) {
			std::fprintf(stderr, "no desktop OpenGL through EGL\n");
			return false;
		}
		const EGLint attribs[] = {
			EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 24,
			// NanoVG strokes and clips with the stencil buffer; without one the
			// outlines come out as solid blocks.
			EGL_STENCIL_SIZE, 8,
			EGL_NONE,
		};
		EGLConfig config;
		EGLint numConfigs = 0;
		if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs) || numConfigs < 1) {
			std::fprintf(stderr, "no usable EGL config\n");
			return false;
		}
		const EGLint surfaceAttribs[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE};
		surface = eglCreatePbufferSurface(display, config, surfaceAttribs);
		if (surface == EGL_NO_SURFACE) {
			std::fprintf(stderr, "could not make a %dx%d pbuffer\n", w, h);
			return false;
		}
		context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
		if (context == EGL_NO_CONTEXT) {
			std::fprintf(stderr, "could not make a GL context\n");
			return false;
		}
		if (!eglMakeCurrent(display, surface, surface, context)) {
			std::fprintf(stderr, "eglMakeCurrent failed\n");
			return false;
		}
		glewExperimental = GL_TRUE;
		GLenum glewStatus = glewInit();
		// GLEW insists on probing GLX even when the context came from EGL, and
		// says NO_GLX_DISPLAY when there is no X display to probe. Everything it
		// actually loaded is fine; only its GLX afterthought failed.
		if (glewStatus != GLEW_OK && glewStatus != GLEW_ERROR_NO_GLX_DISPLAY) {
			std::fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(glewStatus));
			return false;
		}
		glGetError();  // GLEW's own probing leaves one behind
		return true;
	}

	void stop() {
		if (display != EGL_NO_DISPLAY) {
			eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			if (context != EGL_NO_CONTEXT)
				eglDestroyContext(display, context);
			if (surface != EGL_NO_SURFACE)
				eglDestroySurface(display, surface);
			eglTerminate(display);
		}
	}
};

static int loadFont(NVGcontext* vg, const char* name) {
	// Rack's own first, then the usual places a Linux box keeps a font.
	const char* candidates[] = {
		"/home/vironlap/Documents/Software/Rack2Free/res/fonts/Nunito-Bold.ttf",
		"/home/vironlap/Documents/Software/Rack2Free/res/fonts/DejaVuSans.ttf",
		"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
	};
	if (const char* env = std::getenv("MOCKUP_FONT")) {
		int f = nvgCreateFont(vg, name, env);
		if (f >= 0)
			return f;
	}
	for (const char* path : candidates) {
		int f = nvgCreateFont(vg, name, path);
		if (f >= 0)
			return f;
	}
	std::fprintf(stderr, "warning: no font found, the mockup will have no labels\n");
	return -1;
}

/** The same two roles the modules use, resolved the same way: the display font
is whatever is in the fonts folder, and Rack's own carries everything small and
stands in for the glyphs a traced game font does not have. */
static funkin::Fonts loadFonts(NVGcontext* vg, const std::string& displayPath) {
	funkin::Fonts f;
	f.label = loadFont(vg, "label");
	f.display = f.label;
	if (!displayPath.empty()) {
		int d = nvgCreateFont(vg, "display", displayPath.c_str());
		if (d >= 0) {
			f.display = d;
			if (f.label >= 0)
				nvgAddFallbackFontId(vg, d, f.label);
			std::printf("-- display font: %s\n", displayPath.c_str());
		}
		else {
			std::fprintf(stderr, "could not open %s\n", displayPath.c_str());
		}
	}
	return f;
}

// ---------------------------------------------------------------------------
// The components, at the sizes Rack really draws them
// ---------------------------------------------------------------------------

static void component(NVGcontext* vg, float mmX, float mmY, float mmD, NVGcolor fill) {
	float x = mmX * kScale, y = mmY * kScale, r = mmD * 0.5f * kScale;
	nvgBeginPath(vg);
	nvgCircle(vg, x, y, r);
	nvgFillColor(vg, fill);
	nvgFill(vg);
	nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 200));
	nvgStrokeWidth(vg, 1.f);
	nvgStroke(vg);
}

static void outline(NVGcontext* vg, float mmX, float mmY, float mmW, float mmH, NVGcolor c) {
	nvgBeginPath(vg);
	nvgRect(vg, mmX * kScale, mmY * kScale, mmW * kScale, mmH * kScale);
	nvgStrokeColor(vg, c);
	nvgStrokeWidth(vg, 1.f);
	nvgStroke(vg);
}

static NVGcolor kKnobInk = nvgRGBA(0x30, 0x30, 0x30, 210);
static NVGcolor kJackInk = nvgRGBA(0x20, 0x30, 0x50, 210);
static NVGcolor kButtonInk = nvgRGBA(0x70, 0x30, 0x30, 210);

/** A stand-in face for the plate.

Drawn here, out of four circles, rather than borrowed from the library — and that
is not laziness. These pictures go in the manual, the manual goes in the package,
and the package may not carry a single pixel of anybody's game. A real health
icon in this well would be exactly that. */
struct SampleIcon {
	int image = -1;
	int w = 0, h = 0, frames = 1;
};
static SampleIcon g_sampleIcon;

static void makeSampleIcon(NVGcontext* vg) {
	const int size = 96;
	std::vector<unsigned char> px((size_t) size * size * 2 * 4, 0);
	auto put = [&](int frame, float cx, float cy, float r, unsigned char cr,
	               unsigned char cg, unsigned char cb) {
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) {
				float dx = (float) x + 0.5f - cx, dy = (float) y + 0.5f - cy;
				float d = std::sqrt(dx * dx + dy * dy);
				float a = r - d;              // one pixel of feathering
				if (a <= 0.f)
					continue;
				if (a > 1.f)
					a = 1.f;
				size_t i = ((size_t) y * (size_t) size * 2 + (size_t) (frame * size + x)) * 4;
				px[i] = cr; px[i + 1] = cg; px[i + 2] = cb;
				px[i + 3] = (unsigned char) (a * 255.f);
			}
		}
	};
	for (int f = 0; f < 2; f++) {
		put(f, size * 0.5f, size * 0.5f, size * 0.44f, 0x31, 0xb0, 0xd1);
		put(f, size * 0.35f, size * 0.42f, size * 0.07f, 0x14, 0x12, 0x22);
		put(f, size * 0.65f, size * 0.42f, size * 0.07f, 0x14, 0x12, 0x22);
		// The second frame is the losing one, so its mouth is where a frown is.
		put(f, size * 0.5f, f ? size * 0.78f : size * 0.66f, size * 0.12f, 0x14, 0x12, 0x22);
		put(f, size * 0.5f, f ? size * 0.90f : size * 0.60f, size * 0.12f, 0x31, 0xb0, 0xd1);
	}
	g_sampleIcon.image = nvgCreateImageRGBA(vg, size * 2, size, 0, px.data());
	g_sampleIcon.w = size * 2;
	g_sampleIcon.h = size;
	g_sampleIcon.frames = 2;
}

static void fillCard(NVGcontext* vg, funkin::CharacterCard* card, const char* name,
                     const char* mod) {
	card->name = name;
	card->mod = mod;
	card->iconImage = g_sampleIcon.image;
	card->iconW = g_sampleIcon.w;
	card->iconH = g_sampleIcon.h;
	card->iconFrames = g_sampleIcon.frames;
	card->color = nvgRGB(0x31, 0xb0, 0xd1);
}

static void drawOpponentMockup(NVGcontext* vg, const funkin::Fonts& fonts, bool showComponents) {
	namespace P = lay::opp;

	funkin::OpponentPanelInfo info;
	fillCard(vg, &info.card, "daddy dearest", "Psych Engine - Week 1");
	info.laneLit[2] = 1.f;
	for (int i = 0; i < 4; i++)
		info.laneWired[i] = i < 3;
	info.status = "8 characters   ~/.local/share/Rack2/FunkinRack";
	info.beatPhase = 0.15f;
	funkin::DrawOpponentPanel(vg, fonts, kScale, info);

	if (!showComponents)
		return;
	for (int i = 0; i < 4; i++)
		component(vg, P::laneX(i), P::kLaneJackY, kJackD, kJackInk);
	for (int i = 0; i < 3; i++)
		component(vg, P::knobX(i), P::kKnobY, kSmallKnobD, kKnobInk);
	component(vg, P::colX(1, 5), P::kInRowY, kJackD, kJackInk);
	component(vg, P::colX(2, 5), P::kInRowY, kJackD, kJackInk);
	for (int i = 1; i < 4; i++)
		component(vg, P::colX(i, 5), P::kOutRowY, kJackD, kJackInk);
	outline(vg, 0, 0, P::kW, lay::kPanelH, nvgRGBA(0xa0, 0, 0, 160));
}

static void drawDancerMockup(NVGcontext* vg, const funkin::Fonts& fonts, bool showComponents) {
	namespace P = lay::dan;

	funkin::DancerPanelInfo info;
	fillCard(vg, &info.card, "girlfriend", "Psych Engine");
	info.card.color = nvgRGB(0xa5, 0x00, 0x4c);
	info.beatInBar = 2;
	info.beatPhase = 0.25f;
	info.status = "OWN 100 BPM";
	funkin::DrawDancerPanel(vg, fonts, kScale, info);

	if (!showComponents)
		return;
	for (int i = 0; i < 2; i++) {
		component(vg, P::knobX(i), P::kKnobY, kSmallKnobD, kKnobInk);
		component(vg, P::colX(i, 2), P::kInRowY, kJackD, kJackInk);
	}
	for (int i = 0; i < 3; i++)
		component(vg, P::colX(i, 3), P::kOutRowY, kJackD, kJackInk);
	outline(vg, 0, 0, P::kW, lay::kPanelH, nvgRGBA(0xa0, 0, 0, 160));
}

static void drawPlayerMockup(NVGcontext* vg, const funkin::Fonts& fonts, bool showComponents) {
	namespace P = lay::ply;

	funkin::PlayerPanelInfo info;
	fillCard(vg, &info.card, "boyfriend", "Built-in");
	fillCard(vg, &info.rival, "dad", "Psych Engine");
	info.rival.color = nvgRGB(0xaf, 0x66, 0x06);
	info.health = 1.25f;
	info.score = 12450;
	info.combo = 37;
	info.accuracy = 0.912f;
	info.misses = 4;
	info.judgement = "SICK!!";
	info.judgementAge = 0.2f;
	info.laneLit[1] = 1.f;
	info.chartConnected = true;
	info.beatPhase = 0.4f;
	static const char* kBind[5] = {"C4", "C#4", "D4", "D#4", "E4"};
	for (int i = 0; i < 5; i++)
		info.binding[i] = kBind[i];
	info.learning = 3;
	info.status = "LEAD 2.0 s";

	// A road with notes on it at the sizes they really are: a sustain, a couple
	// of taps, and one that has been missed. The lane is only 13 mm wide and
	// this is the only way to see whether an arrow in it is still an arrow.
	funkin::PanelNote notes[6];
	notes[0].progress = 0.20f; notes[0].dir = 0;
	notes[1].progress = 0.36f; notes[1].dir = 2;
	notes[2].progress = 0.55f; notes[2].dir = 3; notes[2].tail = 0.22f;
	notes[3].progress = 0.72f; notes[3].dir = 1;
	notes[4].progress = 0.95f; notes[4].dir = 0;
	notes[5].progress = 1.12f; notes[5].dir = 2; notes[5].missed = true;
	info.notes = notes;
	info.noteCount = 6;

	funkin::DrawPlayerPanel(vg, fonts, kScale, info);

	if (!showComponents)
		return;
	for (int i = 0; i < 4; i++)
		component(vg, P::knobX(i), P::kKnobY, kSmallKnobD, kKnobInk);
	for (int i = 0; i < 6; i++) {
		component(vg, P::colX(i, 6), P::kInRowY, kJackD, kJackInk);
		component(vg, P::colX(i, 6), P::kOutRowY, kJackD, kJackInk);
	}
	// The five learn rows are click targets, not Rack components, so they are
	// drawn as the boxes they claim.
	for (int i = 0; i < 5; i++)
		outline(vg, P::kKeysX + 2.f, P::keyRowY(i), P::kKeysW - 4.f, P::kKeyRowH - 1.2f,
		        nvgRGBA(0, 0x80, 0xc0, 200));
	
	outline(vg, 0, 0, P::kW, lay::kPanelH, nvgRGBA(0xa0, 0, 0, 160));
}

/** The character, drawn the way the rack draws it: every animation in a row, at
the size a module would give it, out of the same DrawCharacterFrame.

This is the picture that says whether the spritesheet is being cut up correctly.
A frame placed with the wrong offset makes a character who shakes between poses,
and shaking is only visible when the poses are side by side. */
static const float kStripCellW = 165.f;
static const float kStripGround = 300.f;
static const float kStripHeight = 190.f;
static const int kStripCells = 6;
static const int kStripW = (int) (kStripCellW * kStripCells);
static const int kStripH = 360;

static void drawCharacterStrip(NVGcontext* vg, const funkin::Fonts& fonts, const fnf::Character& c, int image,
                               int imageW, int imageH, const char* caption) {
	const funkin::Palette& p = funkin::pal();
	int slots[kStripCells] = {fnf::kAnimIdle,   fnf::kAnimSingLeft,  fnf::kAnimSingDown,
	                          fnf::kAnimSingUp, fnf::kAnimSingRight, fnf::kAnimHey};
	const char* names[kStripCells] = {"idle",      "sing LEFT",  "sing DOWN",
	                                  "sing UP",   "sing RIGHT", "hey / dance"};

	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, (float) kStripW, (float) kStripH);
	nvgFillColor(vg, p.bg);
	nvgFill(vg);

	// The floor line: a character that drifts between poses drifts off it, and
	// that is the whole reason this picture exists.
	nvgBeginPath(vg);
	nvgMoveTo(vg, 0, kStripGround);
	nvgLineTo(vg, (float) kStripW, kStripGround);
	nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 60));
	nvgStrokeWidth(vg, 1.f);
	nvgStroke(vg);

	for (int i = 0; i < kStripCells; i++) {
		float x = kStripCellW * ((float) i + 0.5f);
		// The last cell falls back to a dance half, because most real characters
		// have no "hey" but every one of them has an idle of some kind.
		const fnf::CharAnim* want = c.anim(slots[i]);
		if (i == kStripCells - 1 && c.slot[fnf::kAnimHey] < 0)
			want = c.anim(fnf::kAnimDanceRight);
		int animIndex = -1;
		for (size_t k = 0; k < c.anims.size(); k++) {
			if (&c.anims[k] == want)
				animIndex = (int) k;
		}
		if (animIndex < 0)
			continue;
		// The last frame of each, which is the pose it holds.
		int frame = (int) c.anims[(size_t) animIndex].frames.size() - 1;
		funkin::DrawCharacterFrame(vg, image, imageW, imageH, c, animIndex, frame, x,
		                           kStripGround, kStripHeight, false);
		char label[96];
		std::snprintf(label, sizeof(label), "%s  (%s)", names[i],
		              c.anims[(size_t) animIndex].name.c_str());
		funkin::punchText(vg, fonts.label, x, kStripGround + 22.f, 13.f,
		                  NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, p.faint, label, 1.f);
	}
	funkin::punchText(vg, fonts.display, 8.f, 24.f, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, p.accent,
	                  caption, 1.f);
}

int main(int argc, char** argv) {
	bool showComponents = true;
	std::string outDir = "build/mockup";
	std::string libraryDir;
	std::string fontPath;
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--bare") == 0)
			showComponents = false;
		else if (std::strcmp(argv[i], "--library") == 0 && i + 1 < argc)
			libraryDir = argv[++i];
		else if (std::strcmp(argv[i], "--font") == 0 && i + 1 < argc)
			fontPath = argv[++i];
		else
			outDir = argv[i];
	}

	const int kPanelHpx = (int) (lay::kPanelH * kScale + 0.5f);
	struct Shot {
		std::string file;
		int w, h;
		int character = -1;  // an index into `library`, or -1 for a panel
	};
	std::vector<Shot> shots = {
		{"/opponent.png", (int) (lay::opp::kW * kScale + 0.5f), kPanelHpx},
		{"/player.png", (int) (lay::ply::kW * kScale + 0.5f), kPanelHpx},
		{"/dancer.png", (int) (lay::dan::kW * kScale + 0.5f), kPanelHpx},
	};

	// Real characters, out of a real mod folder. This is the only way to find
	// out whether somebody else's spritesheet is being cut up correctly without
	// opening Rack — and somebody else's spritesheet is the whole point of the
	// plugin.
	std::vector<fnf::Character> library;
	std::vector<fnf::Image> librarySheets;
	if (!libraryDir.empty()) {
		fnf::Library lib;
		lib.scan({libraryDir});
		std::printf("-- %s: %d character(s)%s%s\n", libraryDir.c_str(),
		            (int) lib.entries.size(), lib.note.empty() ? "" : ", ",
		            lib.note.c_str());
		for (const fnf::CharacterRef& ref : lib.entries) {
			fnf::Character c;
			fnf::Image img;
			std::string err;
			if (!fnf::LoadCharacter(ref, &c, &err)) {
				std::printf("   %-24s FAILED: %s\n", ref.name.c_str(), err.c_str());
				continue;
			}
			if (!img.load(c.imagePath, &err)) {
				std::printf("   %-24s FAILED: %s\n", ref.name.c_str(), err.c_str());
				continue;
			}
			std::printf("   %-24s %s  %d anims, %d frames, sheet %dx%d, sing %.1f steps\n",
			            ref.name.c_str(), ref.jsonPath.empty() ? "sheet only" : "with json",
			            (int) c.anims.size(), (int) c.atlas.frames.size(), img.w, img.h,
			            (double) c.singDuration);
			for (int d = 0; d < fnf::kDirCount; d++) {
				if (c.slot[fnf::SingSlot(d)] < 0)
					std::printf("      (no sing%s of its own)\n", fnf::DirectionName(d));
			}
			shots.push_back({"/char-" + ref.name + ".png", kStripW, kStripH,
			                 (int) library.size()});
			library.push_back(std::move(c));
			librarySheets.push_back(std::move(img));
		}
	}

	int bufW = 0, bufH = 0;
	for (const Shot& s : shots) {
		bufW = std::max(bufW, s.w);
		bufH = std::max(bufH, s.h);
	}

	Headless gl;
	if (!gl.start(bufW, bufH))
		return 1;

	NVGcontext* vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
	if (!vg) {
		std::fprintf(stderr, "nvgCreateGL2 failed\n");
		gl.stop();
		return 1;
	}
	funkin::Fonts fonts = loadFonts(vg, fontPath);

	makeSampleIcon(vg);

	// One texture per sheet, uploaded once, exactly as a module does it.
	std::vector<int> libraryImages(library.size(), -1);
	for (size_t i = 0; i < library.size(); i++) {
		int flags = library[i].antialias ? 0 : NVG_IMAGE_NEAREST;
		libraryImages[i] = nvgCreateImageRGBA(vg, librarySheets[i].w, librarySheets[i].h, flags,
		                                      librarySheets[i].rgba.data());
	}

	int failures = 0;
	for (size_t panel = 0; panel < shots.size(); panel++) {
		int w = shots[panel].w;
		int h = shots[panel].h;
		glViewport(0, 0, bufW, bufH);
		glClearColor(0.6f, 0.6f, 0.6f, 1.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		nvgBeginFrame(vg, (float) bufW, (float) bufH, 1.f);
		if (panel == 0) {
			drawOpponentMockup(vg, fonts, showComponents);
		}
		else if (panel == 1) {
			drawPlayerMockup(vg, fonts, showComponents);
		}
		else if (panel == 2) {
			drawDancerMockup(vg, fonts, showComponents);
		}
		else {
			int ci = shots[panel].character;
			char caption[192];
			std::snprintf(caption, sizeof(caption), "%s  -  %s  (%d frames in the sheet)",
			              library[(size_t) ci].name.c_str(),
			              library[(size_t) ci].jsonPath.empty() ? "no character file"
			                                                    : "Psych character file",
			              (int) library[(size_t) ci].atlas.frames.size());
			drawCharacterStrip(vg, fonts, library[(size_t) ci], libraryImages[(size_t) ci],
			                   librarySheets[(size_t) ci].w, librarySheets[(size_t) ci].h,
			                   caption);
		}
		nvgEndFrame(vg);
		glFinish();

		// The viewport is the whole buffer, so a shot shorter than it starts
		// part way up: OpenGL's origin is the bottom-left, not the top-left.
		int bottom = bufH - h;
		std::vector<unsigned char> pixels((size_t) w * h * 4);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glPixelStorei(GL_PACK_ROW_LENGTH, 0);
		glReadPixels(0, bottom, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

		// OpenGL hands back the bottom row first.
		std::vector<unsigned char> flipped((size_t) w * h * 4);
		for (int y = 0; y < h; y++)
			std::memcpy(&flipped[(size_t) y * w * 4], &pixels[(size_t) (h - 1 - y) * w * 4],
			            (size_t) w * 4);

		std::string out = outDir + shots[panel].file;
		if (!stbi_write_png(out.c_str(), w, h, 4, flipped.data(), w * 4)) {
			std::fprintf(stderr, "could not write %s\n", out.c_str());
			failures++;
			continue;
		}
		std::printf("wrote %s (%dx%d)\n", out.c_str(), w, h);
	}

	nvgDeleteGL2(vg);
	gl.stop();
	return failures ? 1 : 0;
}
