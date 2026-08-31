#pragma once
// ============================================================================
// The look: the game's, as far as a module panel can carry it.
//
// Drawn with NanoVG rather than shipped as an SVG panel, for one hard reason and
// one soft one. The hard one: nanosvg, which is what Rack rasterises panels
// with, does not render <text> at all, so a panel full of labels would have to
// be a panel full of outlined paths — and it draws no gradients either, which is
// most of what this style is. The soft one: half of these panels move while you
// look at them, and drawing the fixed parts the same way as the moving parts
// keeps one set of coordinates instead of two.
//
// Rack-free on purpose: NanoVG and a font handle, nothing else. That is what
// lets tools/mockup render the real panels into a PNG without opening Rack, so a
// label that overlaps a jack is caught by looking at a picture instead of by
// reading coordinates.
// ============================================================================
#include <cmath>
#include <cstdio>

#include <nanovg.h>

namespace funkin {

// The arrow colours are the game's own, and they are not decoration: a player
// who has ever touched Friday Night Funkin' reads the lane by colour before they
// read the shape, and getting them wrong would be like moving the black keys.
inline NVGcolor ArrowColor(int dir) {
	switch (dir) {
		case 0: return nvgRGB(0xc2, 0x4b, 0x99);  // left  — purple
		case 1: return nvgRGB(0x00, 0xff, 0xff);  // down  — cyan
		case 2: return nvgRGB(0x12, 0xfa, 0x05);  // up    — green
		default: return nvgRGB(0xf9, 0x39, 0x3f); // right — red
	}
}

/** The two fonts a panel draws with.

Two, and not one, because the game uses two. Its lettering is a hand-drawn
display face — lovely at the size of a title and unreadable at the size of a jack
label — and everything it actually has to *tell* you is set in a plain mono. So
`display` carries the module's name, the character's name, the score and the
judgement, and `label` carries the rest.

Either may be -1, and nothing is drawn with a font that is not there. */
struct Fonts {
	int display = -1;
	int label = -1;

	Fonts() {}
	explicit Fonts(int both) : display(both), label(both) {}
	Fonts(int d, int l) : display(d), label(l) {}
};

struct Palette {
	NVGcolor bg;        // the panel itself
	NVGcolor bgDeep;    // the inside of anything you look into
	NVGcolor plate;     // a raised box
	NVGcolor line;      // its outline
	NVGcolor ink;       // text
	NVGcolor faint;     // text that is only there if you look
	NVGcolor title;     // the strip along the top
	NVGcolor titleInk;
	NVGcolor accent;
	NVGcolor good;
	NVGcolor bad;
};

/** Night, neon and a pink stripe: the colours the game gets remembered in. */
static const Palette kNight = {
	nvgRGB(0x1b, 0x1a, 0x2e),  // bg
	nvgRGB(0x0e, 0x0d, 0x1a),  // bgDeep
	nvgRGB(0x2a, 0x28, 0x45),  // plate
	nvgRGB(0x0a, 0x09, 0x14),  // line
	nvgRGB(0xf4, 0xf2, 0xff),  // ink
	nvgRGB(0x9a, 0x95, 0xc0),  // faint
	nvgRGB(0xff, 0x3d, 0x9a),  // title
	nvgRGB(0xff, 0xff, 0xff),  // titleInk
	nvgRGB(0xff, 0xd9, 0x4a),  // accent — the yellow of the score
	nvgRGB(0x3d, 0xdc, 0x84),  // good
	nvgRGB(0xff, 0x4c, 0x4c),  // bad
};

/** The palette in force. Only ever touched from the graphics thread, one panel
at a time, which is the same rule the rest of the drawing follows. */
inline Palette& pal() {
	static Palette p = kNight;
	return p;
}

inline void text(NVGcontext* vg, int font, float x, float y, float size, int align,
                 NVGcolor color, const char* str) {
	if (font < 0 || !str || !*str)
		return;
	nvgFontFaceId(vg, font);
	nvgFontSize(vg, size);
	nvgTextAlign(vg, align);
	nvgFillColor(vg, color);
	nvgText(vg, x, y, str, nullptr);
}

/** Text with a hard black shadow one pixel down and right. Everything the game
prints is outlined like this, and on a dark panel it is also what keeps a bright
label legible where it crosses something bright. */
inline void punchText(NVGcontext* vg, int font, float x, float y, float size, int align,
                      NVGcolor color, const char* str, float drop = 1.6f) {
	if (font < 0 || !str || !*str)
		return;
	text(vg, font, x + drop, y + drop, size, align, nvgRGBA(0, 0, 0, 190), str);
	text(vg, font, x, y, size, align, color, str);
}

/** Text squeezed to fit a width, rather than spilling over the box next door.
Returns the size it settled on. A character name comes out of a mod folder and
can be any length at all, so nothing that prints one may assume it fits. */
inline float fitText(NVGcontext* vg, int font, float x, float y, float maxWidth,
                     float size, int align, NVGcolor color, const char* str,
                     float minSize = 5.f) {
	if (font < 0 || !str || !*str)
		return size;
	nvgFontFaceId(vg, font);
	while (size > minSize) {
		nvgFontSize(vg, size);
		float bounds[4];
		nvgTextBounds(vg, 0, 0, str, nullptr, bounds);
		if (bounds[2] - bounds[0] <= maxWidth)
			break;
		size -= 0.5f;
	}
	punchText(vg, font, x, y, size, align, color, str);
	return size;
}

/** A raised box: the panel's one piece of furniture. */
inline void plate(NVGcontext* vg, float x, float y, float w, float h, float radius,
                  NVGcolor fill, NVGcolor line, float lineWidth = 1.4f) {
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, radius);
	nvgFillColor(vg, fill);
	nvgFill(vg);
	if (lineWidth > 0.f) {
		nvgStrokeColor(vg, line);
		nvgStrokeWidth(vg, lineWidth);
		nvgStroke(vg);
	}
}

/** The arrow, in a box `size` across, centred on (cx, cy).

One shape, turned. The game's arrows are the same drawing rotated, and drawing
four separate ones would let them drift apart. */
inline void arrowPath(NVGcontext* vg, float cx, float cy, float size, int dir) {
	static const float kPts[7][2] = {
		{0.00f, -0.50f}, {0.50f, 0.02f}, {0.20f, 0.02f}, {0.20f, 0.46f},
		{-0.20f, 0.46f}, {-0.20f, 0.02f}, {-0.50f, 0.02f},
	};
	// left, down, up, right — in the order the lanes are in.
	static const float kAngle[4] = {-1.5707963f, 3.1415927f, 0.f, 1.5707963f};
	float a = kAngle[dir & 3];
	float s = std::sin(a), c = std::cos(a);

	nvgBeginPath(vg);
	for (int i = 0; i < 7; i++) {
		float px = kPts[i][0] * size, py = kPts[i][1] * size;
		float rx = px * c - py * s, ry = px * s + py * c;
		if (i == 0)
			nvgMoveTo(vg, cx + rx, cy + ry);
		else
			nvgLineTo(vg, cx + rx, cy + ry);
	}
	nvgClosePath(vg);
}

/** `lit` is how brightly it is being pressed, 0 to 1. An unlit arrow is the
outline only — that is a receptor waiting for a note, and it has to read as empty
at a glance or the lane looks permanently pressed. */
inline void drawArrow(NVGcontext* vg, float cx, float cy, float size, int dir, float lit,
                      bool outlineOnly = false) {
	NVGcolor c = ArrowColor(dir);
	nvgLineJoin(vg, NVG_ROUND);

	if (!outlineOnly) {
		NVGcolor fill = c;
		if (lit < 1.f) {
			// Dark, not transparent: a see-through arrow over a dark panel
			// disappears, and over the highway it turns into a smear.
			fill = nvgRGBAf(c.r * (0.22f + 0.78f * lit), c.g * (0.22f + 0.78f * lit),
			                c.b * (0.22f + 0.78f * lit), 1.f);
		}
		arrowPath(vg, cx, cy, size, dir);
		nvgFillColor(vg, fill);
		nvgFill(vg);
	}

	arrowPath(vg, cx, cy, size, dir);
	nvgStrokeColor(vg, outlineOnly ? nvgRGBAf(c.r, c.g, c.b, 0.55f + 0.45f * lit)
	                               : nvgRGBA(0, 0, 0, 220));
	nvgStrokeWidth(vg, size * 0.09f);
	nvgStroke(vg);

	if (lit > 0.01f) {
		// The flash of a hit, outside the shape so it does not wash the colour
		// out of it.
		arrowPath(vg, cx, cy, size * (1.f + 0.16f * lit), dir);
		nvgStrokeColor(vg, nvgRGBAf(1.f, 1.f, 1.f, 0.55f * lit));
		nvgStrokeWidth(vg, size * 0.07f);
		nvgStroke(vg);
	}
}

/** The health bar. `health` is 0..2 with 1 in the middle, like the game's, and
the two colours are the two characters' own out of their mod files. Drawn the
game's way round: the *opponent* fills from the left, you fill from the right, so
being pushed left is losing. */
inline void healthBar(NVGcontext* vg, float x, float y, float w, float h, float health,
                      NVGcolor mine, NVGcolor theirs) {
	float t = health * 0.5f;
	if (t < 0.f) t = 0.f;
	if (t > 1.f) t = 1.f;

	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, h * 0.35f);
	nvgFillColor(vg, theirs);
	nvgFill(vg);

	float split = x + w * (1.f - t);
	nvgSave(vg);
	nvgScissor(vg, split, y - 2.f, w, h + 4.f);
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, h * 0.35f);
	nvgFillColor(vg, mine);
	nvgFill(vg);
	nvgRestore(vg);

	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, h * 0.35f);
	nvgStrokeColor(vg, nvgRGB(0, 0, 0));
	nvgStrokeWidth(vg, h * 0.16f);
	nvgStroke(vg);
}

/** One frame out of a health-icon strip, drawn to fit a square. The icon is a
row of squares, so this is a scissor and an offset rather than a separate image
per frame. */
inline void iconFrame(NVGcontext* vg, int image, int imgW, int imgH, int frame, int frames,
                      float x, float y, float size) {
	if (image < 0 || imgW <= 0 || imgH <= 0 || frames <= 0)
		return;
	if (frame < 0)
		frame = 0;
	if (frame >= frames)
		frame = frames - 1;
	float frameW = (float) imgW / (float) frames;
	float scale = size / frameW;
	float drawW = (float) imgW * scale;
	float drawH = (float) imgH * scale;

	nvgSave(vg);
	nvgScissor(vg, x, y, size, size);
	NVGpaint p = nvgImagePattern(vg, x - frame * frameW * scale, y, drawW, drawH, 0.f, image, 1.f);
	nvgBeginPath(vg);
	nvgRect(vg, x, y, size, size);
	nvgFillPaint(vg, p);
	nvgFill(vg);
	nvgRestore(vg);
}

}  // namespace funkin
