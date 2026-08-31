#pragma once
// ============================================================================
// The two panels.
//
// Everything either module prints is drawn here, from a struct of plain values,
// with no Rack and no module in sight. That is what lets tools/mockup render
// both panels into a PNG in half a second — and it is why the labels are part of
// the drawing rather than separate label widgets: a label that is not in this
// file is a label the mockup cannot show overlapping a jack.
// ============================================================================
#include <cstdio>

#include "Layout.hpp"
#include "Look.hpp"

namespace funkin {

/** Left, down, up, right — the order everything in this plugin uses, from poly
cable channels to lanes to keys. */
inline const char* LaneName(int dir) {
	static const char* kNames[4] = {"LEFT", "DOWN", "UP", "RIGHT"};
	return kNames[dir & 3];
}

/** Who the module is wearing, and the icon to prove it. */
struct CharacterCard {
	const char* name = "";
	const char* mod = "";
	int iconImage = -1;
	int iconW = 0, iconH = 0;
	int iconFrames = 1;
	int iconFrame = 0;
	NVGcolor color = nvgRGB(0x66, 0xff, 0x33);
	bool loading = false;
};

/** One note on its way down, already reduced to where it is on the road.
`progress` is 0 where it appeared and 1 where it is due; past 1 it is late. */
struct PanelNote {
	float progress = 0.f;
	float tail = 0.f;  // sustain, in the same units as progress
	int dir = 0;
	bool hit = false;
	bool missed = false;
	bool held = false;
};

struct OpponentPanelInfo {
	CharacterCard card;
	float laneLit[4] = {0.f, 0.f, 0.f, 0.f};
	bool laneWired[4] = {false, false, false, false};
	float bpm = 100.f;
	bool externalClock = false;
	float beatPhase = 0.f;  // 0..1 through the current beat
	const char* status = "";
	bool statusBad = false;
};

struct PlayerPanelInfo {
	CharacterCard card;
	CharacterCard rival;  // the module next door, if there is one
	float health = 1.f;
	int score = 0;
	int combo = 0;
	float accuracy = 1.f;
	int misses = 0;
	const char* judgement = "";
	float judgementAge = 1.f;  // seconds since it happened
	float laneLit[4] = {0.f, 0.f, 0.f, 0.f};
	const char* binding[5] = {"", "", "", "", ""};
	int learning = -1;
	const PanelNote* notes = nullptr;
	int noteCount = 0;
	bool chartConnected = false;
	/** No chart, no scoring: the module is an instrument rather than a game. */
	bool freestyle = false;
	bool dead = false;
	float bpm = 100.f;
	bool externalClock = false;
	float beatPhase = 0.f;
	const char* status = "";
	bool statusBad = false;
};

struct DancerPanelInfo {
	CharacterCard card;
	/** Which beat of the bar it is on, 0 to 3, and how far through it. */
	int beatInBar = 0;
	float beatPhase = 0.f;
	bool externalClock = false;
	const char* status = "";
	bool statusBad = false;
};

namespace detail {

/** The strip along the top. The module's name goes here, in the pink the game
uses for everything it wants you to read. */
inline void titleBar(NVGcontext* vg, const Fonts& fonts, float s, float w, float h,
                     const char* left, const char* right) {
	const Palette& p = pal();
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, w * s, h * s);
	NVGpaint g = nvgLinearGradient(vg, 0, 0, w * s, 0, p.title, nvgRGB(0xc0, 0x2d, 0xa0));
	nvgFillPaint(vg, g);
	nvgFill(vg);

	nvgBeginPath(vg);
	nvgRect(vg, 0, (h - 0.7f) * s, w * s, 0.7f * s);
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 90));
	nvgFill(vg);

	punchText(vg, fonts.display, 3.2f * s, h * 0.58f * s, 5.4f * s,
	          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, p.titleInk, left);
	if (right && *right) {
		punchText(vg, fonts.label, (w - 3.2f) * s, h * 0.6f * s, 3.1f * s,
		          NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, nvgRGBA(255, 255, 255, 180), right);
	}
}

/** The plate that says who this is: the health icon out of the mod, the name of
the character, and the mod it came from. Two characters called "dad" from two
mods are told apart by the second line, which is the only thing that does. */
inline void characterCard(NVGcontext* vg, const Fonts& fonts, float s, const CharacterCard& c,
                          float x, float y, float w, float h, float iconX, float iconSize) {
	const Palette& p = pal();
	plate(vg, x * s, y * s, w * s, h * s, 2.2f * s, p.plate, p.line, 1.2f * s * 0.5f);

	float iy = y + (h - iconSize) * 0.5f;
	// The well the icon sits in, so that an icon with a transparent background
	// still reads as a picture of somebody rather than as floating pixels.
	plate(vg, iconX * s, iy * s, iconSize * s, iconSize * s, 1.6f * s, p.bgDeep,
	      nvgRGBA(0, 0, 0, 160), 1.f);
	if (c.iconImage >= 0) {
		iconFrame(vg, c.iconImage, c.iconW, c.iconH, c.iconFrame, c.iconFrames,
		          (iconX + 0.6f) * s, (iy + 0.6f) * s, (iconSize - 1.2f) * s);
	}
	else {
		// No icon in the mod, which is common for a bare spritesheet. A question
		// mark is honest; an empty box looks like the icon failed to load.
		punchText(vg, fonts.label, (iconX + iconSize * 0.5f) * s, (iy + iconSize * 0.62f) * s,
		          iconSize * 0.7f * s, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, p.faint, "?");
	}

	float tx = iconX + iconSize + 2.6f;
	float tw = x + w - tx - 2.f;
	fitText(vg, fonts.display, tx * s, (y + h * 0.44f) * s, tw * s, 5.6f * s,
	        NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, c.loading ? p.faint : p.ink,
	        c.loading ? "loading..." : c.name);
	fitText(vg, fonts.label, tx * s, (y + h * 0.76f) * s, tw * s, 3.2f * s,
	        NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, p.faint, c.mod, 2.4f);

	// A stripe of the character's own health-bar colour, straight out of its mod
	// file. It is the one piece of a character's identity that is a colour, and
	// it is what ties this plate to its half of the health bar.
	nvgBeginPath(vg);
	nvgRoundedRect(vg, (x + 1.2f) * s, (y + h - 2.6f) * s, (w - 2.4f) * s, 1.2f * s, 0.6f * s);
	nvgFillColor(vg, c.color);
	nvgFill(vg);
}

/** A label under a jack or a knob. */
inline void tag(NVGcontext* vg, const Fonts& fonts, float s, float x, float y,
                const char* str, NVGcolor color) {
	punchText(vg, fonts.label, x * s, y * s, 3.f * s, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE,
	          color, str, 1.f);
}

/** The little heading over a row of things. */
inline void rowLabel(NVGcontext* vg, const Fonts& fonts, float s, float x, float y,
                     const char* str) {
	punchText(vg, fonts.label, x * s, y * s, 3.4f * s, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE,
	          pal().accent, str, 1.f);
}

/** The beat light: a dot that pulses with whatever clock the module is on. It is
the only way to see, without patching anything, that an external clock has been
taken up — and which of the two you are looking at. */
inline void beatDot(NVGcontext* vg, float s, float x, float y, float phase, bool external) {
	float pulse = 1.f - phase;
	if (pulse < 0.f) pulse = 0.f;
	float r = (1.2f + 0.9f * pulse) * s;
	NVGcolor c = external ? pal().good : pal().accent;
	nvgBeginPath(vg);
	nvgCircle(vg, x * s, y * s, r);
	nvgFillColor(vg, nvgRGBAf(c.r, c.g, c.b, 0.35f + 0.65f * pulse));
	nvgFill(vg);
}

}  // namespace detail

// ---------------------------------------------------------------------------

inline void DrawOpponentPanel(NVGcontext* vg, const Fonts& fonts, float s, const OpponentPanelInfo& in) {
	using namespace lay::opp;
	const Palette& p = pal();

	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, kW * s, lay::kPanelH * s);
	nvgFillColor(vg, p.bg);
	nvgFill(vg);

	detail::titleBar(vg, fonts, s, kW, kTitleH, "OPPONENT", "FUNKIN'");
	detail::characterCard(vg, fonts, s, in.card, kMargin, kPlateY, kW - 2.f * kMargin, kPlateH,
	                      kIconX, kIconSize);
	detail::beatDot(vg, s, kW - kMargin - 3.2f, kPlateY + 3.4f, in.beatPhase, in.externalClock);

	// The four lanes: an arrow that lights when the character sings it, and the
	// jack that makes it. They share a column so that the jack you patch and the
	// arrow that answers are plainly the same thing.
	for (int i = 0; i < 4; i++) {
		float x = laneX(i);
		drawArrow(vg, x * s, kArrowY * s, kArrowSize * s, i, in.laneLit[i]);
		detail::tag(vg, fonts, s, x, kLaneJackY - lay::kJackLabelGap, LaneName(i),
		            in.laneWired[i] ? p.ink : p.faint);
	}
	detail::rowLabel(vg, fonts, s, kMargin + 1.f, kSectionY, "SING WHEN THESE FIRE");

	static const char* kKnobs[3] = {"BPM", "HOLD", "SIZE"};
	for (int i = 0; i < 3; i++)
		detail::tag(vg, fonts, s, knobX(i), kKnobY - lay::kKnobLabelGap, kKnobs[i], p.faint);

	// The two rows at the bottom. The outputs sit on a darker plate, which is
	// the one thing every Rack panel does to tell the two apart at a glance.
	detail::rowLabel(vg, fonts, s, kMargin + 1.f, kInRowY - lay::kJackLabelGap, "IN");
	detail::tag(vg, fonts, s, colX(1, 5), kInRowY - lay::kJackLabelGap, "NOTES", p.faint);
	detail::tag(vg, fonts, s, colX(2, 5), kInRowY - lay::kJackLabelGap, "BEAT", p.faint);

	plate(vg, (kMargin - 1.f) * s, (kOutRowY - 9.4f) * s, (kW - 2.f * kMargin + 2.f) * s,
	      14.f * s, 1.6f * s, p.bgDeep, nvgRGBA(0, 0, 0, 110), 1.f);
	detail::rowLabel(vg, fonts, s, kMargin + 1.f, kOutRowY - lay::kJackLabelGap, "OUT");
	detail::tag(vg, fonts, s, colX(1, 5), kOutRowY - lay::kJackLabelGap, "CHART", p.accent);
	detail::tag(vg, fonts, s, colX(2, 5), kOutRowY - lay::kJackLabelGap, "SING", p.accent);
	detail::tag(vg, fonts, s, colX(3, 5), kOutRowY - lay::kJackLabelGap, "BEAT", p.accent);

	// The status strip. It is always there and always says something, because a
	// module that only speaks up when it is broken is a module whose silence you
	// cannot read.
	plate(vg, kMargin * s, kStatusY * s, (kW - 2.f * kMargin) * s, kStatusH * s, 1.2f * s,
	      p.plate, nvgRGBA(0, 0, 0, 120), 1.f);
	punchText(vg, fonts.label, (kMargin + 2.f) * s, (kStatusY + 3.7f) * s, 3.1f * s,
	          NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, in.statusBad ? p.bad : p.faint,
	          in.status, 1.f);
}

// ---------------------------------------------------------------------------

inline void DrawDancerPanel(NVGcontext* vg, const Fonts& fonts, float s, const DancerPanelInfo& in) {
	using namespace lay::dan;
	const Palette& p = pal();

	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, kW * s, lay::kPanelH * s);
	nvgFillColor(vg, p.bg);
	nvgFill(vg);

	detail::titleBar(vg, fonts, s, kW, kTitleH, "DANCER", "");
	detail::characterCard(vg, fonts, s, in.card, kMargin, kPlateY, kW - 2.f * kMargin, kPlateH,
	                      kIconX, kIconSize);

	// The bar, as four dots. The one it is on swells and fades over the beat, so
	// the panel keeps time whether or not you can hear it.
	punchText(vg, fonts.label, kMargin + 1.f, (kBeatY - kBeatR - 3.f) * s, 3.4f * s,
	          NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, p.accent, "BAR", 1.f);
	for (int i = 0; i < 4; i++) {
		bool on = i == in.beatInBar;
		float pulse = on ? (1.f - in.beatPhase) : 0.f;
		NVGcolor c = in.externalClock ? p.good : p.accent;
		nvgBeginPath(vg);
		nvgCircle(vg, beatX(i) * s, kBeatY * s, (kBeatR + pulse * 1.4f) * s);
		nvgFillColor(vg, on ? nvgRGBAf(c.r, c.g, c.b, 0.35f + 0.65f * pulse) : p.bgDeep);
		nvgFill(vg);
		nvgStrokeColor(vg, i == 0 ? p.faint : nvgRGBA(0, 0, 0, 140));
		nvgStrokeWidth(vg, 0.7f * s);
		nvgStroke(vg);
	}

	static const char* kKnobs[2] = {"BPM", "SIZE"};
	for (int i = 0; i < 2; i++)
		detail::tag(vg, fonts, s, knobX(i), kKnobY - lay::kKnobLabelGap, kKnobs[i], p.faint);

	detail::rowLabel(vg, fonts, s, kMargin + 0.5f, kInRowY - lay::kJackLabelGap - 3.6f, "IN");
	detail::tag(vg, fonts, s, colX(0, 2), kInRowY - lay::kJackLabelGap, "BEAT", p.faint);
	detail::tag(vg, fonts, s, colX(1, 2), kInRowY - lay::kJackLabelGap, "HEY", p.faint);

	plate(vg, (kMargin - 1.f) * s, (kOutRowY - 9.4f) * s, (kW - 2.f * kMargin + 2.f) * s,
	      14.f * s, 1.6f * s, p.bgDeep, nvgRGBA(0, 0, 0, 110), 1.f);
	static const char* kOuts[3] = {"BEAT", "BAR", "BOP"};
	for (int i = 0; i < 3; i++)
		detail::tag(vg, fonts, s, colX(i, 3), kOutRowY - lay::kJackLabelGap, kOuts[i], p.accent);

	plate(vg, kMargin * s, kStatusY * s, (kW - 2.f * kMargin) * s, kStatusH * s, 1.2f * s,
	      p.plate, nvgRGBA(0, 0, 0, 120), 1.f);
	punchText(vg, fonts.label, (kMargin + 1.6f) * s, (kStatusY + 3.7f) * s, 2.9f * s,
	          NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, in.statusBad ? p.bad : p.faint,
	          in.status, 1.f);
}

// ---------------------------------------------------------------------------

inline void DrawPlayerPanel(NVGcontext* vg, const Fonts& fonts, float s, const PlayerPanelInfo& in) {
	using namespace lay::ply;
	const Palette& p = pal();
	char buf[64];

	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, kW * s, lay::kPanelH * s);
	nvgFillColor(vg, p.bg);
	nvgFill(vg);

	// The status goes in the title strip rather than along the bottom: the
	// bottom of this panel is two full rows of jacks, and a line of text under
	// them was landing behind the last one.
	detail::titleBar(vg, fonts, s, kW, kTitleH, "PLAYER",
	                 (in.status && *in.status) ? in.status : "FUNKIN'");

	// The plate takes the left two thirds; the score takes the right.
	float plateW = (kW - 2.f * kMargin) * 0.62f;
	detail::characterCard(vg, fonts, s, in.card, kMargin, kPlateY, plateW, kPlateH,
	                      kIconX, kIconSize);

	float sx = kMargin + plateW + 2.5f;
	float sw = kW - kMargin - sx;
	plate(vg, sx * s, kPlateY * s, sw * s, kPlateH * s, 2.2f * s, p.bgDeep, p.line, 0.6f * s);
	std::snprintf(buf, sizeof(buf), "%d", in.score);
	fitText(vg, fonts.display, (sx + sw * 0.5f) * s, (kPlateY + 6.2f) * s, (sw - 3.f) * s, 6.2f * s,
	        NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, p.accent, buf);
	std::snprintf(buf, sizeof(buf), "%d COMBO", in.combo);
	fitText(vg, fonts.label, (sx + sw * 0.5f) * s, (kPlateY + 12.f) * s, (sw - 3.f) * s, 3.4f * s,
	        NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, in.combo > 0 ? p.ink : p.faint, buf, 2.4f);
	std::snprintf(buf, sizeof(buf), "%.1f%%  %d MISS", (double) (in.accuracy * 100.f), in.misses);
	fitText(vg, fonts.label, (sx + sw * 0.5f) * s, (kPlateY + 16.4f) * s, (sw - 3.f) * s, 3.f * s,
	        NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, p.faint, buf, 2.2f);

	// The health bar, the game's way round: the opponent fills from the left, so
	// being pushed left is losing.
	healthBar(vg, kMargin * s, kHealthY * s, (kW - 2.f * kMargin) * s, kHealthH * s,
	          in.health, in.card.color,
	          in.rival.iconImage >= 0 || in.rival.name[0] ? in.rival.color : nvgRGB(0x88, 0x88, 0x99));
	{
		// The two faces on the bar, as in the game. The one on the left is the
		// module next door if there is one — that is the only place the player
		// module can learn who it is up against, since a cable carries gates and
		// not a face.
		float icon = kHealthIcon;
		float t = in.health * 0.5f;
		if (t < 0.f) t = 0.f;
		if (t > 1.f) t = 1.f;
		float split = kMargin + (kW - 2.f * kMargin) * (1.f - t);
		float iy = kHealthY + kHealthH * 0.5f - icon * 0.5f;
		if (in.rival.iconImage >= 0) {
			iconFrame(vg, in.rival.iconImage, in.rival.iconW, in.rival.iconH,
			          in.health > 1.6f ? 1 : 0, in.rival.iconFrames,
			          (split - icon - 0.8f) * s, iy * s, icon * s);
		}
		if (in.card.iconImage >= 0) {
			iconFrame(vg, in.card.iconImage, in.card.iconW, in.card.iconH,
			          in.health < 0.4f ? 1 : 0, in.card.iconFrames,
			          (split + 0.8f) * s, iy * s, icon * s);
		}
	}

	// ---- the road --------------------------------------------------------
	plate(vg, kRoadX * s, kRoadY * s, kRoadW * s, kRoadH * s, 1.6f * s, p.bgDeep,
	      nvgRGBA(0, 0, 0, 150), 1.f);
	nvgSave(vg);
	nvgScissor(vg, kRoadX * s, kRoadY * s, kRoadW * s, kRoadH * s);

	for (int i = 1; i < 4; i++) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, (kRoadX + kLaneW * i) * s, kRoadY * s);
		nvgLineTo(vg, (kRoadX + kLaneW * i) * s, (kRoadY + kRoadH) * s);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 14));
		nvgStrokeWidth(vg, 0.4f * s);
		nvgStroke(vg);
	}

	float travel = kReceptorY - kRoadY;
	for (int i = 0; i < in.noteCount; i++) {
		const PanelNote& n = in.notes[i];
		if (n.hit && !n.held)
			continue;  // gone: hitting it is what takes it off the road
		float y = kRoadY + n.progress * travel;
		float x = roadLaneX(n.dir);
		if (n.tail > 0.f) {
			// The sustain, drawn behind the head, in the lane's own colour.
			float tailLen = n.tail * travel;
			NVGcolor c = ArrowColor(n.dir);
			nvgBeginPath(vg);
			nvgRoundedRect(vg, (x - kNoteSize * 0.17f) * s, (y - tailLen) * s,
			               kNoteSize * 0.34f * s, tailLen * s, kNoteSize * 0.17f * s);
			nvgFillColor(vg, nvgRGBAf(c.r, c.g, c.b, n.missed ? 0.2f : 0.7f));
			nvgFill(vg);
		}
		if (n.held)
			continue;  // the head is being held on the receptor; the tail says so
		float alpha = n.missed ? 0.28f : 1.f;
		nvgGlobalAlpha(vg, alpha);
		drawArrow(vg, x * s, y * s, kNoteSize * s, n.dir, 1.f);
		nvgGlobalAlpha(vg, 1.f);
	}

	// The receptors, on top of the notes: a note is hit when it is *under* the
	// arrow you can see, and drawing them the other way round makes the moment
	// of the hit invisible.
	for (int i = 0; i < 4; i++) {
		drawArrow(vg, roadLaneX(i) * s, kReceptorY * s, kNoteSize * 1.05f * s, i,
		          in.laneLit[i], in.laneLit[i] < 0.02f);
	}
	nvgRestore(vg);

	if (in.noteCount == 0) {
		// An empty road always says why it is empty. "Freestyle" and "nothing is
		// patched in" look identical otherwise, and one of them is a state you
		// chose while the other is a cable you forgot.
		bool waiting = in.chartConnected && !in.freestyle;
		punchText(vg, fonts.display, (kRoadX + kRoadW * 0.5f) * s, (kRoadY + kRoadH * 0.42f) * s,
		          3.4f * s, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
		          in.freestyle ? p.accent : p.faint,
		          in.freestyle ? "FREESTYLE" : (waiting ? "WAITING FOR NOTES" : "PATCH CHART IN"));
	}
	if (in.judgementAge < 0.9f && in.judgement && *in.judgement) {
		float a = 1.f - in.judgementAge / 0.9f;
		float rise = (1.f - a) * 3.f;
		punchText(vg, fonts.display, (kRoadX + kRoadW * 0.5f) * s, (kRoadY + 8.f - rise) * s,
		          (5.f + a * 1.6f) * s, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
		          nvgRGBAf(1.f, 1.f, 1.f, a), in.judgement);
	}
	if (in.dead) {
		punchText(vg, fonts.display, (kRoadX + kRoadW * 0.5f) * s, (kRoadY + kRoadH * 0.62f) * s,
		          5.f * s, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, p.bad, "BLUE BALLED");
	}

	// ---- the controls box --------------------------------------------------
	plate(vg, kKeysX * s, kKeysY * s, kKeysW * s, kKeysH * s, 1.6f * s, p.plate, p.line, 0.6f * s);
	punchText(vg, fonts.label, (kKeysX + 2.5f) * s, (kKeysY + 5.6f) * s, 3.4f * s,
	          NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, p.accent, "CONTROLS", 1.f);
	punchText(vg, fonts.label, (kKeysX + kKeysW - 2.5f) * s, (kKeysY + 5.6f) * s, 2.7f * s,
	          NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE, p.faint, "CLICK TO LEARN", 1.f);

	static const char* kRowName[5] = {"LEFT", "DOWN", "UP", "RIGHT", "HEY"};
	for (int i = 0; i < 5; i++) {
		float y = keyRowY(i);
		bool learning = in.learning == i;
		plate(vg, (kKeysX + 2.f) * s, y * s, (kKeysW - 4.f) * s, (kKeyRowH - 1.2f) * s,
		      1.f * s, learning ? nvgRGBA(0xff, 0x3d, 0x9a, 200) : p.bgDeep,
		      nvgRGBA(0, 0, 0, 120), 0.6f * s);
		float mid = y + (kKeyRowH - 1.2f) * 0.5f;
		if (i < 4) {
			drawArrow(vg, (kKeysX + 6.f) * s, mid * s, 4.6f * s, i, in.laneLit[i]);
		}
		else {
			punchText(vg, fonts.label, (kKeysX + 6.f) * s, mid * s, 3.4f * s,
			          NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, p.accent, "!", 1.f);
		}
		punchText(vg, fonts.label, (kKeysX + 10.f) * s, mid * s, 3.f * s,
		          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, p.ink, kRowName[i], 1.f);
		punchText(vg, fonts.label, (kKeysX + kKeysW - 4.f) * s, mid * s, 3.f * s,
		          NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
		          learning ? p.titleInk : p.faint,
		          learning ? "press it..." : in.binding[i], 1.f);
	}

	// ---- knobs and jacks ---------------------------------------------------
	static const char* kKnobs[4] = {"BPM", "LEAD", "WINDOW", "SIZE"};
	for (int i = 0; i < 4; i++)
		detail::tag(vg, fonts, s, knobX(i), kKnobY - lay::kKnobLabelGap, kKnobs[i], p.faint);
	detail::beatDot(vg, s, kW - kMargin - 3.2f, kPlateY + 3.4f, in.beatPhase, in.externalClock);

	// Six jacks a row leaves no room at the left for the word IN, so the two
	// rows are told apart the way every Rack panel tells them apart: the outputs
	// sit on a darker plate, and their labels are the bright ones.
	static const char* kIns[6] = {"CHART", "LEFT", "DOWN", "UP", "RIGHT", "BEAT"};
	for (int i = 0; i < 6; i++) {
		detail::tag(vg, fonts, s, colX(i, 6), kInRowY - lay::kJackLabelGap, kIns[i],
		            (i == 0 && in.freestyle) ? p.accent : p.faint);
	}

	plate(vg, (kMargin - 1.f) * s, (kOutRowY - 9.6f) * s, (kW - 2.f * kMargin + 2.f) * s,
	      14.2f * s, 1.6f * s, p.bgDeep, nvgRGBA(0, 0, 0, 110), 1.f);
	static const char* kOuts[6] = {"KEYS", "HIT", "MISS", "HEALTH", "SING", "BEAT"};
	for (int i = 0; i < 6; i++)
		detail::tag(vg, fonts, s, colX(i, 6), kOutRowY - lay::kJackLabelGap, kOuts[i], p.accent);
}

}  // namespace funkin
