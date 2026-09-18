#pragma once
// ============================================================================
// The note highway: the half of Friday Night Funkin' that is a game.
//
// Where the chart comes from is the idea the whole plugin turns on. There is no
// song file and no chart editor: **a note is a gate on a cable**. The opponent
// module sings whatever its inputs tell it to and sends the same four gates back
// out; you patch that into the player module, and what the opponent just sang
// becomes what you have to play. Anything in the rack that can make four gates —
// a sequencer, a drum machine, a MIDI file player, a clock divider, another
// player's hands — is a chart.
//
// Which leaves one thing to solve: a note you can see coming. In the game the
// chart is known in advance; here it arrives at the moment the opponent sings
// it, and a note you are told about at the instant it is due is not a game, it
// is a reflex test. So a note that arrives now is due LEAD seconds from now and
// spends that time coming down the lane. That delay is not a compromise, it is
// the shape of the thing: the opponent sings a phrase, and one lead later you
// answer it. Call and response, which is what the game is.
//
// Rack-free and allocation-free. This runs in the audio thread, one step per
// sample, because a hit window measured in the graphics thread would be measured
// in whatever the frame rate happened to be.
// ============================================================================
#include <cstdint>

#include "Character.hpp"

namespace fnf {

/** The game's own hit window, in seconds: ten frames at sixty, which is what
Conductor.safeZoneOffset works out to. Everything else is a fraction of it, and
the fractions are the game's too — so a player who knows what "sick" feels like
finds it in the same place here. */
static const float kSafeZone = 10.f / 60.f;
static const float kSickWindow = kSafeZone * 0.2f;   // 33 ms
static const float kGoodWindow = kSafeZone * 0.75f;  // 125 ms
static const float kBadWindow = kSafeZone * 0.9f;    // 150 ms

enum Judgement {
	kJudgeSick = 0,
	kJudgeGood,
	kJudgeBad,
	kJudgeShit,
	kJudgeMiss,
	kJudgeCount,
};

const char* JudgementName(int j);
int JudgementScore(int j);
/** Which judgement an offset in seconds earns, or kJudgeMiss if it is outside
the window altogether — which is not a hit at all, just air. */
int JudgeOffset(float offsetSeconds, float windowScale);

struct Note {
	float time = 0.f;   // when it is due, on the highway's own clock
	float length = 0.f; // sustain, in seconds. Grows while the chart gate is up
	int dir = 0;
	bool hit = false;
	bool missed = false;
	bool held = false;      // being held down right now, on a sustain
	bool growing = false;   // the chart gate that made it is still high
	int judge = kJudgeMiss;
	/** Set when the note is drawn as it dies, so the pop happens once. */
	float resolvedAt = -1.f;

	bool resolved() const { return hit || missed; }
};

/** What just happened, for the module to turn into gates and animations. The
highway never touches a port itself: it is compiled into the desktop tests where
there are no ports. */
enum HighwayEventType {
	kEvNone = 0,
	kEvHit,    // a press landed on a note
	kEvMiss,   // a note went past unplayed, or a press was punished for nothing
	kEvFree,   // a press with no note under it, and nothing riding on it
	kEvHold,   // a sustain ended
};

struct HighwayEvent {
	int type = kEvNone;
	int dir = 0;
	int judge = kJudgeMiss;
	float offset = 0.f;  // seconds early (negative) or late (positive)
};

/** The lanes, the notes in them, and the score.

A ring, not a list that gets compacted: the graphics thread reads these notes
sixty times a second while the audio thread is writing them, and notes that never
move can be read mid-write at worst one frame stale. Shuffling them down an array
would let the drawing see the same note twice, or a hole. */
struct Highway {
	static const int kMaxNotes = 256;
	static const int kMaxEvents = 32;

	Note notes[kMaxNotes];
	int head = 0;  // oldest live note
	int tail = 0;  // one past the newest

	/** The highway's clock, in seconds. Driven by the module, one sample at a
	time, so it is exactly as accurate as the audio. */
	float now = 0.f;
	/** How long a note takes to come down. Also, exactly, how far behind the
	opponent you are answering. */
	float lead = 2.f;
	/** Multiplies every hit window. 1 is the game's. */
	float windowScale = 1.f;
	/** With this on, a press with no note under it is free and the character
	sings it anyway. Off, it is a miss, which is how the base game plays. On by
	default: in a rack, four gates arriving from somewhere else in the patch are
	a normal thing to be playing along with, and being punished for it would make
	the module unusable as an instrument. */
	bool ghostTapping = true;

	/** No chart, no scoring, no misses — just a character that sings what you
	play. This is what the module does when nothing is patched into CHART, and it
	is the mode most people will actually leave it in: an instrument that happens
	to look like Friday Night Funkin' rather than a game you have to win. */
	bool freestyle = false;

	// -- score ---------------------------------------------------------------
	int score = 0;
	int combo = 0;
	int maxCombo = 0;
	int hits = 0;
	int misses = 0;
	int judgeCount[kJudgeCount] = {0, 0, 0, 0, 0};
	/** 0..2, and 1 is the middle, like the game's health bar. At 0 you have
	lost — the module says so and keeps playing, because a module that stops
	making sound in the middle of a patch is a fault, not a game over. */
	float health = 1.f;
	bool dead = false;

	HighwayEvent events[kMaxEvents];
	int eventHead = 0, eventTail = 0;

	/** Which lane is being held down, and the note it is holding. */
	bool keyDown[kDirCount] = {false, false, false, false};
	int holding[kDirCount] = {-1, -1, -1, -1};

	void reset();

	int liveCount() const { return tail - head; }
	/** Notes are indexed from `head` to `tail`; use this to read one. */
	const Note& note(int i) const { return notes[(unsigned) i % kMaxNotes]; }
	Note& note(int i) { return notes[(unsigned) i % kMaxNotes]; }

	/** The chart gate for `dir` went up: a note is due one lead from now. */
	void chartOn(int dir);
	/** ...and is still up: the sustain is that much longer. */
	void chartHold(int dir, float dt);
	/** ...and went down. */
	void chartOff(int dir);

	/** You pressed. Pushes an event, and returns the judgement — or -1 when there
	was nothing to judge, which is a press in freestyle or on an empty lane.
	Nothing to judge is not the same as a miss, and the panel must not flash
	"MISS" at somebody who is only playing. */
	int press(int dir);
	void release(int dir);

	/** Moves the clock on, retires what is finished, and misses what went past.
	`dt` is one sample's worth. */
	void advance(float dt);

	bool popEvent(HighwayEvent* out);

	/** 0..1, hits weighted by how good they were, which is the game's accuracy
	rather than a plain ratio. Zero notes so far reads as 1: nothing has gone
	wrong yet, and starting a patch at 0% would look like a fault. */
	float accuracy() const;

private:
	void pushEvent(const HighwayEvent& e);
	/** The note in this lane that a press should be judged against: the one
	closest to due that has not been resolved. Nearest, not earliest — two notes
	half a second apart and a press between them belongs to whichever it is
	nearer, or an early press on the second would kill the first. */
	int bestNote(int dir) const;
	void retire();
};

/** Seconds per beat and per step at a tempo. The step — a sixteenth — is the
unit the game measures a held sing in, and it is why singDuration comes out of a
character file as a number like 6.1 rather than a time. */
inline float SecondsPerBeat(float bpm) { return bpm > 1.f ? 60.f / bpm : 0.5f; }
inline float SecondsPerStep(float bpm) { return SecondsPerBeat(bpm) * 0.25f; }

}  // namespace fnf
