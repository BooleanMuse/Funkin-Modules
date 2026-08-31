#pragma once
// ============================================================================
// A Friday Night Funkin' character, as it exists on disk, and what it is doing
// right now.
//
// Two JSON dialects are read, because both are out there in quantity:
//
//   Psych Engine    {"anim": "singLEFT", "name": "Dad Sing Note LEFT",
//                    "fps": 24, "loop": false, "offsets": [-10, 0]}
//   V-Slice (0.3+)  {"name": "singLEFT", "prefix": "Dad Sing Note LEFT",
//                    "frameRate": 24, "looped": false, "offsets": [-10, 0]}
//
// They disagree about which key is the animation's name and which is the
// spritesheet prefix, and that is the whole of the difference that matters here.
// A file with "prefix" in it is V-Slice; anything else is read as Psych.
//
// And a character with no JSON at all still works: a bare PNG + XML pair — which
// is what the spritesheet generators hand you — is opened by guessing the
// animations from the prefixes in the sheet. That is the case the plugin has to
// be good at, because it is what most people will drop in first.
//
// Rack-free: no NanoVG, no Rack, no OpenGL. What comes out is where the pixels
// are and which rectangle to draw; who draws it is not this file's business.
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "Json.hpp"
#include "Sparrow.hpp"

namespace fnf {

/** The four arrows, in the order Friday Night Funkin' puts them in: left, down,
up, right. Everything in this plugin that is "one per arrow" is in this order —
poly cable channels, panel columns, note lanes — so that a cable, a lane and a
key never disagree about which is which. */
enum Direction {
	kLeft = 0,
	kDown,
	kUp,
	kRight,
	kDirCount,
};

const char* DirectionName(int dir);

/** The animations we know how to ask for by meaning rather than by name. A
character may have far more than these; the rest are still loaded and can be
played from the menu, but only these are wired to anything. */
enum AnimSlot {
	kAnimIdle = 0,
	kAnimSingLeft,
	kAnimSingDown,
	kAnimSingUp,
	kAnimSingRight,
	kAnimMissLeft,
	kAnimMissDown,
	kAnimMissUp,
	kAnimMissRight,
	kAnimDanceLeft,
	kAnimDanceRight,
	kAnimHey,
	kAnimCount,
};

inline int SingSlot(int dir) { return kAnimSingLeft + dir; }
inline int MissSlot(int dir) { return kAnimMissLeft + dir; }

struct CharAnim {
	std::string name;    // "singLEFT" — what the game calls it
	std::string prefix;  // "Dad Sing Note LEFT" — what the sheet calls it
	float fps = 24.f;
	bool loop = false;
	bool flipX = false, flipY = false;
	/** The offsets line of the character file. In the game these are subtracted
	from the draw position, and they are the reason a character's head does not
	jump half a body sideways between idle and a sing. */
	float offsetX = 0.f, offsetY = 0.f;
	/** Psych's "indices": play only these frames of the prefix, in this order.
	Empty means all of them. */
	std::vector<int> indices;
	/** Resolved into the atlas, once there is an atlas. */
	std::vector<int> frames;

	bool valid() const { return !frames.empty(); }
	/** Which of `frames` is showing at `t` seconds. A non-looping animation
	stops on its last frame, which is how a sing pose holds. */
	int frameAt(float t) const;
	float duration() const { return fps > 0.f ? (float) frames.size() / fps : 0.f; }
};

/** One rectangle to draw: where it is in the sheet, and where it goes relative
to the character's anchor. Both in sheet pixels; scaling is the caller's. */
struct FrameQuad {
	int sx = 0, sy = 0, sw = 0, sh = 0;  // in the sheet
	float dx = 0.f, dy = 0.f;            // offset from the anchor
	float dw = 0.f, dh = 0.f;
	bool rotated = false;
	bool valid() const { return sw > 0 && sh > 0; }
};

struct Character {
	// -- who it is -----------------------------------------------------------
	std::string name;      // what the panel shows; the file stem by default
	std::string modName;   // the mod it came out of, for the picker
	std::string jsonPath;  // may be empty: a PNG+XML pair is a character too
	std::string imagePath;
	std::string xmlPath;
	std::string iconPath;  // may be empty
	std::string iconName;  // "dad"

	// -- how it is drawn -----------------------------------------------------
	float scale = 1.f;
	bool flipX = false;
	bool antialias = true;
	float positionX = 0.f, positionY = 0.f;
	/** The health bar colour out of the character file. Straight from the mod,
	so a character brings its own colour with it. */
	uint8_t barColor[3] = {0x66, 0xff, 0x33};
	bool hasBarColor = false;

	/** How long a sing pose is held, in *steps* — a sixteenth note. That is the
	game's unit, and keeping it means a character tuned to hold for 6.1 steps in
	the game holds for 6.1 steps here, whatever the tempo is set to. */
	float singDuration = 4.f;

	// -- what it can do ------------------------------------------------------
	Atlas atlas;
	std::vector<CharAnim> anims;
	int slot[kAnimCount];

	Character();

	bool loaded() const { return atlas.valid() && !anims.empty(); }
	/** The animation in a slot, following the fallbacks (a missing miss falls
	back to the sing, a missing idle to danceLeft), or null. */
	const CharAnim* anim(int slotIndex) const;
	int animIndexByName(const std::string& name) const;
	/** Every animation that resolved to at least one frame. What the menu
	offers, so a character with a "hey" or a "scared" is not a dead end. */
	std::vector<int> playableAnims() const;

	/** The size the character occupies, in sheet pixels, from its idle. Used to
	turn "one module tall" into a scale factor, and it has to come from a fixed
	animation or the character would change size as it sang. */
	float nominalHeight() const;
	float nominalWidth() const;

	/** The rectangle for one frame of one animation. `anchor` is the top-left of
	the character's nominal box; the animation's own offsets are applied here so
	that callers never have to know about them. */
	FrameQuad quad(int animIndex, int frame) const;

	/** Once the atlas is in, turn every prefix into frame indices and work out
	which animation fills which slot. Safe to call twice. */
	void resolve();
};

/** Reads a character file into `out` — the metadata and the animation list, but
not the atlas: that is a separate file and a separate failure. */
bool CharacterParseJson(const JsonValue& root, Character* out, std::string* err);

/** Invents the animation list for a sheet that has no character file, out of the
prefixes in the XML. It finds prefixes by stripping the frame numbers off the
end of every name, then matches them against the words the game uses. This is
the only way a raw spritesheet-and-XML export can be usable, and it is worth
being generous about: "BF NOTE LEFT0", "bf sing left", "singLEFT" and
"Dad Sing Note LEFT" all have to land on the same arrow. */
void CharacterGuessAnims(Character* out);

// ---------------------------------------------------------------------------
// What the character is doing.
//
// This is the game's own logic and it is small: you sing when you are told to,
// you hold the pose for singDuration steps, and when the hold runs out you go
// back to dancing on the beat. Kept separate from Character so that two modules
// can wear the same character without sharing a mood.
// ---------------------------------------------------------------------------
struct CharacterState {
	int slot = kAnimIdle;
	/** Seconds into the current animation. */
	float time = 0.f;
	/** Seconds of sing pose still owed. Zero means free to dance. */
	float hold = 0.f;
	/** Which way the last dance went, so danceLeft/danceRight alternate the way
	they do in the game. */
	int danceStep = 0;
	/** Set while the character is being played by hand from the menu, so the
	beat does not cut a one-off animation short. */
	int forced = -1;

	void sing(int dir, float holdSeconds);
	void miss(int dir, float holdSeconds);
	/** A one-off, by animation index, from the menu. */
	void play(int animIndex);
	/** The beat clock ticked: dance, unless still holding a pose. */
	void beat(const Character& c);
	void advance(float dt, const Character& c);

	/** Which animation to draw, as an index into Character::anims, or -1. */
	int currentAnim(const Character& c) const;
	int currentFrame(const Character& c) const;
	bool singing() const { return hold > 0.f; }
};

}  // namespace fnf
