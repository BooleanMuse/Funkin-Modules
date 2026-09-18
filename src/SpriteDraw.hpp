#pragma once
// ============================================================================
// Drawing one frame of a character.
//
// Small, and the most likely place in the plugin for a bug you can only see:
// a spritesheet is trimmed, so where a frame goes is worked out from four
// numbers in the XML and two more in the character file, and getting any of
// them wrong gives a character who shakes, drifts or reaches the wrong way.
//
// So it lives here, away from Rack, and both the module and tools/mockup draw
// with it. If the character comes out wrong in the rack it comes out wrong in
// the mockup, where it can be looked at.
// ============================================================================
#include <nanovg.h>

#include "Character.hpp"

namespace funkin {

/** Draws frame `frame` of animation `animIndex`.

`anchorX` is the middle of the character and `anchorY` is the ground under its
feet, both in whatever coordinates the caller is drawing in; `targetH` is how
tall the character should be. The whole sheet is one texture and the frame is a
window onto it, which is what keeps this to one upload per character rather than
one per frame. */
inline void DrawCharacterFrame(NVGcontext* vg, int image, int imageW, int imageH,
                               const fnf::Character& c, int animIndex, int frame,
                               float anchorX, float anchorY, float targetH, bool flip,
                               float alpha = 1.f) {
	if (image < 0 || imageW <= 0 || imageH <= 0 || targetH <= 0.f)
		return;
	float nominal = c.nominalHeight();
	float nominalW = c.nominalWidth();
	if (nominal <= 0.f || nominalW <= 0.f)
		return;
	fnf::FrameQuad q = c.quad(animIndex, frame);
	if (!q.valid())
		return;

	float k = targetH / nominal;
	// The top-left of the character's nominal box. Everything the frame says is
	// measured from there.
	float boxX = anchorX - nominalW * k * 0.5f;
	float boxY = anchorY - targetH;

	nvgSave(vg);
	if (flip) {
		// About the character's own centre, so it does not slide sideways when
		// it turns round.
		nvgTranslate(vg, anchorX, 0.f);
		nvgScale(vg, -1.f, 1.f);
		nvgTranslate(vg, -anchorX, 0.f);
	}

	float x = boxX + q.dx * k;
	float y = boxY + q.dy * k;
	float w = q.dw * k;
	float h = q.dh * k;

	if (q.rotated) {
		// Sparrow allows a frame to be stored turned a quarter turn clockwise.
		// FNF sheets almost never are; a sheet out of a general-purpose packer
		// can be, and a rotated frame drawn flat is unmistakably wrong.
		nvgTranslate(vg, x + w * 0.5f, y + h * 0.5f);
		nvgRotate(vg, -1.5707963f);
		nvgTranslate(vg, -(h * 0.5f), -(w * 0.5f));
		x = 0.f;
		y = 0.f;
		float t = w;
		w = h;
		h = t;
	}

	nvgScissor(vg, x, y, w, h);
	NVGpaint paint = nvgImagePattern(vg, x - (float) q.sx * k, y - (float) q.sy * k,
	                                 (float) imageW * k, (float) imageH * k, 0.f, image, alpha);
	nvgBeginPath(vg);
	nvgRect(vg, x, y, w, h);
	nvgFillPaint(vg, paint);
	nvgFill(vg);
	nvgRestore(vg);
}

}  // namespace funkin
