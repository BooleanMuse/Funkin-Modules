#pragma once
// ============================================================================
// Where everything on the two panels is, in millimetres.
//
// One copy of these numbers, used by three things: the module, which puts the
// knobs and the jacks there; the panel drawing, which puts the labels and the
// boxes there; and tools/mockup, which draws the whole thing into a PNG with
// every knob and socket at its real diameter so that two things touching can be
// seen instead of worked out.
//
// The real diameters, because they are what the gaps have to clear:
//     PJ301M jack      8.0 mm
//     RoundBlackKnob  12.9 mm
//     RoundSmallBlack  9.5 mm
//     Trimpot          6.1 mm
//
// Knob labels go ABOVE their knobs and jack labels above their jacks. With both
// panels carrying two rows of jacks and a row of knobs, a label under the knobs
// lands on the label over the jacks — which is exactly what the first mockup
// showed, and what these numbers are arranged to avoid.
// ============================================================================

namespace lay {

/** Rack's own: 1 HP is 5.08 mm, and a panel is 128.5 mm tall. */
static constexpr float kHP = 5.08f;
static constexpr float kPanelH = 128.5f;

/** Rack's SVG_DPI over MM_PER_IN, which is 75 over 25.4 — the number mm2px()
multiplies by, and the number the panel drawing has to scale by so that the two
land in the same place.

It is 75, not 240. With 240 the panel is drawn three and a bit times too big:
the module comes out sixty-odd HP wide, and everything below about 40 mm — the
road, the knobs, both rows of jacks — falls off the bottom of a box that is still
only 380 px tall. The static_assert against RACK_GRID_WIDTH in the modules is
there so that this can never be wrong again quietly. */
static constexpr float kPxPerMm = 75.f / 25.4f;

constexpr float hp(float n) { return n * kHP; }

/** A jack row is 8 mm of socket; a small knob is 9.5. Half of each, plus a
little, is how far a label has to sit to clear one. */
static constexpr float kJackLabelGap = 6.4f;
static constexpr float kKnobLabelGap = 7.0f;

// ---------------------------------------------------------------------------
// The opponent: 20 HP.
// ---------------------------------------------------------------------------
namespace opp {

static constexpr float kW = hp(20);  // 101.6 mm
static constexpr float kMargin = 5.f;

static constexpr float kTitleH = 10.f;

/** The character plate: the icon, its name, and which mod it came out of. */
static constexpr float kPlateY = 12.5f;
static constexpr float kPlateH = 21.f;
static constexpr float kIconSize = 17.f;
static constexpr float kIconX = kMargin + 2.f;

/** The four lanes. Everything that is "one per arrow" sits on these. */
static constexpr float kSectionY = 37.2f;  // the heading over the arrows
static constexpr float kArrowSize = 15.f;
static constexpr float kArrowY = 46.f;
inline float laneX(int i) {
	float usable = kW - 2.f * kMargin;
	return kMargin + usable * ((float) i + 0.5f) / 4.f;
}
/** The jack under each arrow: one input per lane, which is how a sequencer
patched with four mono cables drives it. */
static constexpr float kLaneJackY = 62.f;

static constexpr float kKnobY = 77.5f;
inline float knobX(int i) {
	// Three knobs, evenly spaced across the same width the lanes use.
	float usable = kW - 2.f * kMargin;
	return kMargin + usable * ((float) i + 0.5f) / 3.f;
}

/** The two rows at the bottom: what comes in that is not a lane, and what goes
out. Five columns, because CHART, SING and BEAT are three and a row of three
across a 20 HP panel looks like something has fallen off. */
static constexpr float kInRowY = 94.f;
static constexpr float kOutRowY = 110.f;
inline float colX(int i, int of) {
	float usable = kW - 2.f * kMargin;
	return kMargin + usable * ((float) i + 0.5f) / (float) of;
}

static constexpr float kStatusY = 117.6f;
static constexpr float kStatusH = 5.4f;

}  // namespace opp

// ---------------------------------------------------------------------------
// The player: 26 HP. Wider than the opponent because it has a highway on it,
// and the highway has to be wide enough that four lanes of arrows are still
// arrows rather than four coloured smudges.
// ---------------------------------------------------------------------------
namespace ply {

static constexpr float kW = hp(26);  // 132.08 mm
static constexpr float kMargin = 5.f;

static constexpr float kTitleH = 10.f;

static constexpr float kPlateY = 12.5f;
static constexpr float kPlateH = 19.f;
static constexpr float kIconSize = 15.5f;
static constexpr float kIconX = kMargin + 2.f;

/** The health bar runs the full width under the plate, the way it runs the full
width of the screen in the game. The two faces on it stand taller than the bar
itself — also as in the game — so the gap above and below it is theirs. */
static constexpr float kHealthY = 33.6f;
static constexpr float kHealthH = 5.2f;
static constexpr float kHealthIcon = kHealthH * 1.6f;

/** The highway. Notes come down it and are hit at the bottom, which is the
"downscroll" half of the game's two options — chosen because the receptors then
sit on the side of the panel nearest the jacks that report what you hit. */
static constexpr float kRoadX = kMargin;
static constexpr float kRoadY = 42.f;
static constexpr float kRoadW = 60.f;
static constexpr float kRoadH = 37.f;
static constexpr float kLaneW = kRoadW / 4.f;
static constexpr float kNoteSize = kLaneW * 0.86f;
inline float roadLaneX(int i) { return kRoadX + kLaneW * ((float) i + 0.5f); }
/** Where a note is due: the receptor line, near the bottom of the road. */
static constexpr float kReceptorY = kRoadY + kRoadH - kNoteSize * 0.62f;

/** The controls box: one row per arrow, saying what plays it. */
static constexpr float kKeysX = kRoadX + kRoadW + 3.5f;
static constexpr float kKeysY = kRoadY;
static constexpr float kKeysW = kW - kMargin - kKeysX;
static constexpr float kKeysH = kRoadH;
static constexpr float kKeyRowH = 5.6f;
inline float keyRowY(int i) { return kKeysY + 7.6f + (float) i * kKeyRowH; }

static constexpr float kKnobY = 91.f;
inline float knobX(int i) {
	float usable = kW - 2.f * kMargin;
	return kMargin + usable * ((float) i + 0.5f) / 4.f;
}

static constexpr float kInRowY = 107.f;
static constexpr float kOutRowY = 122.f;
inline float colX(int i, int of) {
	float usable = kW - 2.f * kMargin;
	return kMargin + usable * ((float) i + 0.5f) / (float) of;
}

}  // namespace ply

// ---------------------------------------------------------------------------
// The dancer: 12 HP. The one in the background who is not in the fight — she
// keeps the beat, and here she hands it back out as one.
// ---------------------------------------------------------------------------
namespace dan {

static constexpr float kW = hp(12);  // 60.96 mm
static constexpr float kMargin = 4.f;

static constexpr float kTitleH = 10.f;

static constexpr float kPlateY = 12.5f;
static constexpr float kPlateH = 21.f;
static constexpr float kIconSize = 17.f;
static constexpr float kIconX = kMargin + 1.5f;

/** Four dots, one per beat of the bar, lit up to where the bar has got to. It is
the only part of this panel that has to be read at a glance from across the room,
which is why it is the biggest thing on it. */
static constexpr float kBeatY = 44.f;
static constexpr float kBeatR = 4.2f;
inline float beatX(int i) {
	float usable = kW - 2.f * kMargin;
	return kMargin + usable * ((float) i + 0.5f) / 4.f;
}

static constexpr float kKnobY = 66.f;
inline float knobX(int i) {
	float usable = kW - 2.f * kMargin;
	return kMargin + usable * ((float) i + 0.5f) / 2.f;
}

static constexpr float kInRowY = 89.f;
static constexpr float kOutRowY = 109.f;
inline float colX(int i, int of) {
	float usable = kW - 2.f * kMargin;
	return kMargin + usable * ((float) i + 0.5f) / (float) of;
}

static constexpr float kStatusY = 117.6f;
static constexpr float kStatusH = 5.4f;

}  // namespace dan

}  // namespace lay
