#pragma once
// ============================================================================
// The four arrows, and what you press them with.
//
// A gamepad, the computer keyboard and a MIDI controller all arrive here as the
// same thing, and that is not a simplification — it is how Rack is built. The
// SDK ships two MIDI drivers of its own:
//
//     include/gamepad.hpp    "Gamepad/joystick/controller MIDI driver"
//     include/keyboard.hpp   "Computer keyboard MIDI driver"
//
// so a pad and a keyboard are already MIDI inputs, chosen from the same dropdown
// on the port as a real one. The module needs one midi::InputQueue and no
// joystick code at all, and — the part that would otherwise be the hard problem
// — no fight with Rack over who owns the keyboard focus.
//
// What is left to solve is that the D-pad on one controller is not the D-pad on
// the next. So nothing is hardcoded: every arrow is a slot, and every slot
// learns whatever you press into it.
//
// Rack-free: the module turns a midi::Message into an ArrowEvent and hands it
// over, which is also what lets the desktop tests play the game without Rack.
// ============================================================================
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "Character.hpp"

namespace fnf {

/** The bindable actions: the four arrows, in the usual order, and the taunt.
"Hey" is in here because it is the one thing in the game you press that is not
a note, and leaving it out would mean the pad has a spare button and the
character has an animation nobody can ever see. */
enum ArrowAction {
	kActLeft = kLeft,
	kActDown = kDown,
	kActUp = kUp,
	kActRight = kRight,
	kActHey,
	kActCount,
};

inline const char* ArrowActionName(int a) {
	switch (a) {
		case kActLeft: return "LEFT";
		case kActDown: return "DOWN";
		case kActUp: return "UP";
		case kActRight: return "RIGHT";
		case kActHey: return "HEY";
		default: return "?";
	}
}

enum BindType {
	kBindNone = 0,
	kBindNote = 1,
	kBindCC = 2,
	/** A key on the computer keyboard, by GLFW's code.

	MIDI covers a pad, a controller and Rack's own keyboard driver, and it covers
	them well — but that driver is a piano, and the four keys a Friday Night
	Funkin' player reaches for are the arrow keys, which no piano has. So they
	are bound directly, and the module reads them while the pointer is over it.
	*/
	kBindKey = 3,
};

/** GLFW's codes for the keys worth naming. Written out rather than included
because this header has to compile with no Rack and no GLFW; the module checks
them against the real ones at compile time. */
enum KeyCode {
	kKeySpace = 32,
	kKeyEnter = 257,
	kKeyRight = 262,
	kKeyLeft = 263,
	kKeyDown = 264,
	kKeyUp = 265,
};

/** One message, with everything we do not care about taken off. */
struct ArrowEvent {
	int type = kBindNone;
	int channel = 0;
	int number = 0;
	/** 0..1. A note-off is a note at 0, which is what makes a note and a CC the
	same shape here — and is what lets a stick axis work an arrow. */
	float value = 0.f;
};

/** Past this a control counts as pressed: well over the resting jitter of an
analogue stick, well under any deliberate push. */
static const float kBindOnThreshold = 0.35f;
/** ...and it stays pressed until it falls back under this. The gap is what stops
a stick resting exactly on the line from machine-gunning notes. */
static const float kBindOffThreshold = 0.25f;

struct Binding {
	int type = kBindNone;
	int channel = -1;  // -1 is any, which is what you want from a pad
	int number = 0;

	bool set() const { return type != kBindNone; }
	bool matches(const ArrowEvent& e) const {
		if (type == kBindNone || e.type != type || e.number != number)
			return false;
		return channel < 0 || channel == e.channel;
	}
	bool same(const Binding& o) const {
		return type == o.type && number == o.number && channel == o.channel;
	}
	/** What the panel prints: "C4", "CC 17", "Left". Never a raw code. */
	void describe(char* out, size_t n) const {
		if (!set()) {
			std::snprintf(out, n, "--");
			return;
		}
		if (type == kBindKey) {
			switch (number) {
				case kKeyLeft: std::snprintf(out, n, "Left"); return;
				case kKeyRight: std::snprintf(out, n, "Right"); return;
				case kKeyUp: std::snprintf(out, n, "Up"); return;
				case kKeyDown: std::snprintf(out, n, "Down"); return;
				case kKeySpace: std::snprintf(out, n, "Space"); return;
				case kKeyEnter: std::snprintf(out, n, "Enter"); return;
				default: break;
			}
			// GLFW numbers the printable keys by their ASCII capital, so most of
			// a keyboard prints itself.
			if (number > 32 && number < 127)
				std::snprintf(out, n, "%c", (char) number);
			else
				std::snprintf(out, n, "Key %d", number);
			return;
		}
		if (type == kBindCC) {
			std::snprintf(out, n, "CC %d", number);
			return;
		}
		static const char* kNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
		                                 "F#", "G",  "G#", "A",  "A#", "B"};
		std::snprintf(out, n, "%s%d", kNames[number % 12], number / 12 - 1);
	}
};

struct ArrowBindings {
	Binding slot[kActCount];
	bool held[kActCount] = {false, false, false, false, false};
	/** Which slot is waiting to be taught, or -1. */
	int learning = -1;

	ArrowBindings() { defaults(); }

	/** The arrows, on the arrow keys, which is what somebody who has played the
	game will press first. No driver to pick and nothing to learn: place the
	module, put the pointer on it and play.

	MIDI is one click away for anybody who would rather have it — the notes it
	used to default to are still there under "MIDI notes" in the menu. */
	void defaults() {
		static const int kKeys[kActCount] = {kKeyLeft, kKeyDown, kKeyUp, kKeyRight, kKeySpace};
		for (int i = 0; i < kActCount; i++) {
			slot[i].type = kBindKey;
			slot[i].channel = -1;
			slot[i].number = kKeys[i];
			held[i] = false;
		}
		learning = -1;
	}

	/** Middle C upwards: one per arrow on any MIDI keyboard, and also Z S X D C
	on Rack's own computer-keyboard driver, whose home octave starts there. */
	void midiDefaults() {
		for (int i = 0; i < kActCount; i++) {
			slot[i].type = kBindNote;
			slot[i].channel = -1;
			slot[i].number = 60 + i;
			held[i] = false;
		}
		learning = -1;
	}

	/** Is this key one of ours? Asked before a key press is taken, so that every
	other key on the keyboard still belongs to Rack. */
	bool wantsKey(int code) const {
		for (int i = 0; i < kActCount; i++) {
			if (slot[i].type == kBindKey && slot[i].number == code)
				return true;
		}
		return false;
	}

	void clear(int action) {
		if (action < 0 || action >= kActCount)
			return;
		slot[action] = Binding();
		held[action] = false;
	}
	void startLearning(int action) {
		learning = (action >= 0 && action < kActCount) ? action : -1;
	}
	bool isLearning() const { return learning >= 0; }

	/** Feeds one message in.

	While a slot is learning, the first thing you actually push lands in it and
	nothing else happens: you are not made to play the note to bind the note. Two
	slots may never hold the same control, so teaching a button that is already
	spoken for takes it off the other slot rather than leaving one button
	playing two arrows. */
	void feed(const ArrowEvent& e) {
		if (e.type == kBindNone)
			return;

		if (learning >= 0) {
			// A release is not a press. Learning on a note-off would bind the
			// button you were letting go of, which is never the one you meant.
			if (e.value < kBindOnThreshold)
				return;
			Binding b;
			b.type = e.type;
			b.channel = -1;  // any: the pad's channel is not the user's business
			b.number = e.number;
			for (int i = 0; i < kActCount; i++) {
				if (i != learning && slot[i].same(b))
					clear(i);
			}
			slot[learning] = b;
			held[learning] = false;
			learning = -1;
			return;
		}

		for (int i = 0; i < kActCount; i++) {
			if (!slot[i].matches(e))
				continue;
			// Schmitt, not one line: an axis resting on the threshold would
			// otherwise stutter on and off every sample.
			if (held[i] ? (e.value < kBindOffThreshold) : (e.value >= kBindOnThreshold))
				held[i] = !held[i];
		}
	}

	/** Nothing is held. Used when the MIDI device goes away, so an arrow that
	was down at the time does not stick forever. */
	void releaseAll() {
		for (int i = 0; i < kActCount; i++)
			held[i] = false;
	}
	bool down(int action) const {
		return action >= 0 && action < kActCount && held[action];
	}
};

}  // namespace fnf
