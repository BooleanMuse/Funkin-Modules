#include "Character.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace fnf {

const char* DirectionName(int dir) {
	switch (dir) {
		case kLeft: return "LEFT";
		case kDown: return "DOWN";
		case kUp: return "UP";
		case kRight: return "RIGHT";
		default: return "?";
	}
}

int CharAnim::frameAt(float t) const {
	if (frames.empty())
		return 0;
	if (fps <= 0.f)
		return 0;
	int n = (int) frames.size();
	int f = (int) std::floor(t * fps);
	if (f < 0)
		f = 0;
	if (loop)
		return f % n;
	// Not looping: hold the last frame. That is what makes a sing a *pose* —
	// the mouth stays open until the note is over instead of snapping shut.
	return f >= n ? n - 1 : f;
}

Character::Character() {
	for (int i = 0; i < kAnimCount; i++)
		slot[i] = -1;
}

namespace {

/** Lower case, letters and digits only. Every name comparison in this file goes
through it, because the same animation is written "singLEFT", "sing left",
"Sing_Left" and "BF NOTE LEFT" depending on who made the character. */
std::string Norm(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		if (std::isalnum((unsigned char) c))
			out.push_back((char) std::tolower((unsigned char) c));
	}
	return out;
}

bool Has(const std::string& hay, const char* needle) {
	return hay.find(needle) != std::string::npos;
}

/** Which slot a name means, or -1. The order of the tests is the whole trick:
"singLEFTmiss" contains both "sing" and "miss", so miss has to be asked about
first or every miss animation would be filed as an ordinary sing. */
int SlotForName(const std::string& rawName, bool loose = false) {
	std::string n = Norm(rawName);
	if (n.empty())
		return -1;

	auto dirOf = [&](int* dir) {
		// "up" is checked last on purpose: it is two letters and turns up inside
		// other words, and a name that says both is being explicit about the
		// longer one.
		if (Has(n, "left")) { *dir = kLeft; return true; }
		if (Has(n, "down")) { *dir = kDown; return true; }
		if (Has(n, "right")) { *dir = kRight; return true; }
		if (Has(n, "up")) { *dir = kUp; return true; }
		return false;
	};

	int dir = -1;
	if (Has(n, "miss") && dirOf(&dir))
		return MissSlot(dir);
	// "danc", not "dance": the animation is called danceLeft in a character file
	// but the frames in the sheet are named "GF Dancing Beat Left", and both have
	// to land in the same place.
	if (Has(n, "danc")) {
		if (Has(n, "left"))
			return kAnimDanceLeft;
		if (Has(n, "right"))
			return kAnimDanceRight;
		return kAnimIdle;  // "BF idle dance", "Dancing Beat"
	}
	if (Has(n, "idle"))
		return kAnimIdle;
	if (Has(n, "hey") || Has(n, "cheer"))
		return kAnimHey;
	// A sing is either said outright, or implied by the sheet's own wording.
	// Three are common enough to be worth knowing by heart: "Dad Sing Note
	// LEFT", "BF NOTE LEFT", and the base game's mother, whose frames are called
	// "Mom Up Pose" and never say sing or note at all.
	if ((Has(n, "sing") || Has(n, "note") || Has(n, "pose")) && dirOf(&dir))
		return SingSlot(dir);

	// Last resort, and only when guessing: a name that is nothing but a
	// direction is a sing. It has to come after everything else — "danceLeft"
	// and "singLEFTmiss" both contain a direction and neither is an ordinary
	// sing — but without it a sheet that calls its animations "left" and "up"
	// loads with an idle and nothing else.
	if (loose && dirOf(&dir))
		return SingSlot(dir);
	return -1;
}

}  // namespace

int Character::animIndexByName(const std::string& n) const {
	std::string want = Norm(n);
	for (size_t i = 0; i < anims.size(); i++) {
		if (Norm(anims[i].name) == want)
			return (int) i;
	}
	return -1;
}

std::vector<int> Character::playableAnims() const {
	std::vector<int> out;
	for (size_t i = 0; i < anims.size(); i++) {
		if (anims[i].valid())
			out.push_back((int) i);
	}
	return out;
}

/** The slot fallbacks, as one chain. A character missing its miss animations —
most of them are — must still do *something* when you drop a note, and the
something the game does is play the ordinary sing. */
static int SlotFallback(const Character& c, int want, int depth = 0) {
	if (want < 0 || want >= kAnimCount || depth > 6)
		return -1;
	int direct = c.slot[want];
	if (direct >= 0 && c.anims[(size_t) direct].valid())
		return direct;

	switch (want) {
		case kAnimIdle: {
			int l = SlotFallback(c, kAnimDanceLeft, depth + 1);
			if (l >= 0)
				return l;
			// Nothing named like an idle at all: the first animation that has
			// frames, so the character at least appears.
			for (size_t i = 0; i < c.anims.size(); i++) {
				if (c.anims[i].valid())
					return (int) i;
			}
			return -1;
		}
		case kAnimDanceLeft:
		case kAnimDanceRight: {
			int other = (want == kAnimDanceLeft) ? kAnimDanceRight : kAnimDanceLeft;
			int d = c.slot[other];
			if (d >= 0 && c.anims[(size_t) d].valid())
				return d;
			int i = c.slot[kAnimIdle];
			if (i >= 0 && c.anims[(size_t) i].valid())
				return i;
			return SlotFallback(c, kAnimIdle, depth + 1);
		}
		case kAnimMissLeft:
		case kAnimMissDown:
		case kAnimMissUp:
		case kAnimMissRight:
			return SlotFallback(c, SingSlot(want - kAnimMissLeft), depth + 1);
		default:
			return SlotFallback(c, kAnimIdle, depth + 1);
	}
}

const CharAnim* Character::anim(int slotIndex) const {
	int i = SlotFallback(*this, slotIndex);
	return (i >= 0) ? &anims[(size_t) i] : nullptr;
}

float Character::nominalHeight() const {
	const CharAnim* a = anim(kAnimIdle);
	if (!a || a->frames.empty())
		return 0.f;
	const AtlasFrame& f = atlas.frames[(size_t) a->frames[0]];
	return (float) f.frameHeight;
}

float Character::nominalWidth() const {
	const CharAnim* a = anim(kAnimIdle);
	if (!a || a->frames.empty())
		return 0.f;
	const AtlasFrame& f = atlas.frames[(size_t) a->frames[0]];
	return (float) f.frameWidth;
}

FrameQuad Character::quad(int animIndex, int frame) const {
	FrameQuad q;
	if (animIndex < 0 || animIndex >= (int) anims.size())
		return q;
	const CharAnim& a = anims[(size_t) animIndex];
	if (a.frames.empty())
		return q;
	if (frame < 0)
		frame = 0;
	if (frame >= (int) a.frames.size())
		frame = (int) a.frames.size() - 1;
	const AtlasFrame& f = atlas.frames[(size_t) a.frames[(size_t) frame]];

	q.sx = f.x;
	q.sy = f.y;
	q.sw = f.w;
	q.sh = f.h;
	q.rotated = f.rotated;
	q.dw = (float) (f.rotated ? f.h : f.w);
	q.dh = (float) (f.rotated ? f.w : f.h);
	// Flixel draws the trimmed pixels at (-frameX, -frameY) inside the untrimmed
	// box, then shifts the whole sprite by -offset. Both, in that order, or the
	// character shakes.
	q.dx = -a.offsetX - (float) f.frameX;
	q.dy = -a.offsetY - (float) f.frameY;
	return q;
}

void Character::resolve() {
	for (int i = 0; i < kAnimCount; i++)
		slot[i] = -1;

	for (size_t i = 0; i < anims.size(); i++) {
		CharAnim& a = anims[i];
		if (a.frames.empty() && !a.prefix.empty()) {
			std::vector<int> all = atlas.framesForPrefix(a.prefix);
			if (!a.indices.empty()) {
				// Psych's "indices": a hand-picked order, sometimes with the
				// same frame twice. Anything out of range is dropped rather
				// than clamped — clamping would silently invent a pose.
				for (int idx : a.indices) {
					if (idx >= 0 && idx < (int) all.size())
						a.frames.push_back(all[(size_t) idx]);
				}
			}
			else {
				a.frames = all;
			}
		}
		if (!a.valid())
			continue;

		bool guessed = a.name == a.prefix;
		int s = SlotForName(a.name, guessed);
		// The prefix is only worth asking when it *is* the name — which is the
		// case for a sheet with no character file, where the frame names are all
		// there is to go on. When a character file gave the animation a name,
		// that name is the last word on it.
		//
		// GF is why. Her "hairBlow" is named after the frames
		// "GF Dancing Beat Hair blowing", and a prefix that says "Dancing" filed
		// her hair blowing in the wind as her idle: she stood on her speakers
		// with her hair permanently streaming sideways and never danced.
		if (s < 0 && guessed)
			s = SlotForName(a.prefix, true);
		// First one wins. A character file lists its animations in the order the
		// author meant them, and a later "singLEFT-alt" should not take over the
		// arrow from the real one.
		if (s >= 0 && slot[s] < 0)
			slot[s] = (int) i;
	}
}

// ---------------------------------------------------------------------------
// Reading the character file
// ---------------------------------------------------------------------------

bool CharacterParseJson(const JsonValue& root, Character* out, std::string* err) {
	if (!out)
		return false;
	if (!root.isObject()) {
		if (err)
			*err = "the character file is not a JSON object";
		return false;
	}

	const JsonValue* anims = root.get("animations");
	if (!anims->isArray() || anims->size() == 0) {
		if (err)
			*err = "the character file has no \"animations\" list";
		return false;
	}

	// V-Slice writes "assetPath", Psych writes "image". Both name the sheet
	// without its extension, relative to the mod's images folder.
	std::string image = root.get("image")->text();
	if (image.empty())
		image = root.get("assetPath")->text();
	out->imagePath = image;  // resolved to a real path by the library

	// The icon is a plain string in Psych and an object in V-Slice.
	std::string icon = root.get("healthicon")->text();
	if (icon.empty())
		icon = root.get("healthIcon")->get("id")->text();
	if (icon.empty())
		icon = root.get("icon")->text();
	out->iconName = icon;

	out->flipX = root.get("flip_x")->flag(root.get("flipX")->flag(false));
	out->antialias = !root.get("no_antialiasing")->flag(false);
	if (root.get("antialiasing")->type == JsonValue::kBool)
		out->antialias = root.get("antialiasing")->flag(true);
	// V-Slice says it the other way round again: "isPixel" rather than
	// "no_antialiasing". A pixel character smoothed is a smear, and the mods
	// that get this wrong are the ones that look worst.
	if (root.get("isPixel")->flag(false))
		out->antialias = false;

	const JsonValue* sc = root.get("scale");
	out->scale = (float) (sc->type == JsonValue::kNumber ? sc->num(1.0) : 1.0);
	if (out->scale <= 0.f)
		out->scale = 1.f;

	out->positionX = (float) root.get("position")->num(0, 0.0);
	out->positionY = (float) root.get("position")->num(1, 0.0);

	// Psych: sing_duration. V-Slice: singTime. Both in steps.
	double sing = root.get("sing_duration")->num(-1.0);
	if (sing < 0.0)
		sing = root.get("singTime")->num(-1.0);
	out->singDuration = (float) (sing > 0.0 ? sing : 4.0);

	const JsonValue* bar = root.get("healthbar_colors");
	if (!bar->isArray())
		bar = root.get("healthbar_color");
	if (bar->isArray() && bar->size() >= 3) {
		for (int i = 0; i < 3; i++) {
			double v = bar->num((size_t) i, 0.0);
			out->barColor[i] = (uint8_t) (v < 0 ? 0 : (v > 255 ? 255 : v));
		}
		out->hasBarColor = true;
	}

	for (size_t i = 0; i < anims->size(); i++) {
		const JsonValue* a = anims->at(i);
		if (!a->isObject())
			continue;
		CharAnim ca;

		// The one real difference between the dialects. A "prefix" key means
		// V-Slice, where "name" is the animation; otherwise "anim" is the
		// animation and "name" is the prefix.
		const JsonValue* prefix = a->get("prefix");
		if (prefix->type == JsonValue::kString) {
			ca.name = a->get("name")->text();
			ca.prefix = prefix->str;
		}
		else {
			ca.name = a->get("anim")->text();
			ca.prefix = a->get("name")->text();
		}
		if (ca.name.empty())
			ca.name = ca.prefix;
		if (ca.prefix.empty())
			continue;

		double fps = a->get("fps")->num(-1.0);
		if (fps < 0.0)
			fps = a->get("frameRate")->num(-1.0);
		ca.fps = (float) (fps > 0.0 ? fps : 24.0);

		ca.loop = a->get("loop")->flag(a->get("looped")->flag(false));
		ca.flipX = a->get("flipX")->flag(a->get("flip_x")->flag(false));
		ca.flipY = a->get("flipY")->flag(a->get("flip_y")->flag(false));

		const JsonValue* off = a->get("offsets");
		ca.offsetX = (float) off->num(0, 0.0);
		ca.offsetY = (float) off->num(1, 0.0);

		const JsonValue* idx = a->get("indices");
		for (size_t k = 0; k < idx->size(); k++)
			ca.indices.push_back((int) idx->at(k)->num(0.0));

		out->anims.push_back(std::move(ca));
	}

	if (out->anims.empty()) {
		if (err)
			*err = "none of the animations in the character file could be read";
		return false;
	}
	return true;
}

void CharacterGuessAnims(Character* out) {
	if (!out || !out->atlas.valid())
		return;
	out->anims.clear();

	// Group by the name with its frame number stripped off. Grouping, rather
	// than prefix matching, is what makes this safe: "BF NOTE UP" is a prefix of
	// "BF NOTE UP MISS", so matching would fold the miss frames into the sing.
	// Two names only land in the same group if they are the same word.
	struct Group {
		std::string prefix;
		std::vector<int> frames;
	};
	std::vector<Group> groups;

	for (size_t i = 0; i < out->atlas.frames.size(); i++) {
		const std::string& n = out->atlas.frames[i].name;
		size_t end = n.size();
		while (end > 0 && std::isdigit((unsigned char) n[end - 1]))
			end--;
		while (end > 0 && (n[end - 1] == ' ' || n[end - 1] == '_'))
			end--;
		std::string key = n.substr(0, end);
		if (key.empty())
			key = n;

		Group* g = nullptr;
		for (Group& cand : groups) {
			if (cand.prefix == key) {
				g = &cand;
				break;
			}
		}
		if (!g) {
			groups.push_back(Group{key, {}});
			g = &groups.back();
		}
		g->frames.push_back((int) i);
	}

	for (Group& g : groups) {
		std::stable_sort(g.frames.begin(), g.frames.end(), [&](int a, int b) {
			int sa = out->atlas.frames[(size_t) a].sequence();
			int sb = out->atlas.frames[(size_t) b].sequence();
			if (sa < 0 || sb < 0)
				return false;
			return sa < sb;
		});
		CharAnim ca;
		ca.name = g.prefix;
		ca.prefix = g.prefix;
		ca.fps = 24.f;
		// An idle with no character file has nothing to restart it but the beat,
		// and a beat may never come if the module is left unpatched. Looping it
		// keeps the character breathing instead of freezing on frame four.
		int s = SlotForName(g.prefix, true);
		ca.loop = (s == kAnimIdle || s == kAnimDanceLeft || s == kAnimDanceRight);
		ca.frames = g.frames;  // already resolved: resolve() leaves these alone
		out->anims.push_back(std::move(ca));
	}
}

// ---------------------------------------------------------------------------
// What the character is doing
// ---------------------------------------------------------------------------

void CharacterState::sing(int dir, float holdSeconds) {
	if (dir < 0 || dir >= kDirCount)
		return;
	slot = SingSlot(dir);
	time = 0.f;
	hold = holdSeconds > 0.f ? holdSeconds : 0.001f;
	forced = -1;
}

void CharacterState::miss(int dir, float holdSeconds) {
	if (dir < 0 || dir >= kDirCount)
		return;
	slot = MissSlot(dir);
	time = 0.f;
	hold = holdSeconds > 0.f ? holdSeconds : 0.001f;
	forced = -1;
}

void CharacterState::play(int animIndex) {
	forced = animIndex;
	time = 0.f;
	hold = 0.f;
}

void CharacterState::beat(const Character& c) {
	if (hold > 0.f || forced >= 0)
		return;
	// Alternating is only real if the character has both halves; one that just
	// has an idle plays it again, which is the bop.
	bool twoStep = c.slot[kAnimDanceLeft] >= 0 && c.slot[kAnimDanceRight] >= 0;
	if (twoStep) {
		danceStep ^= 1;
		slot = danceStep ? kAnimDanceRight : kAnimDanceLeft;
	}
	else {
		slot = kAnimIdle;
	}
	time = 0.f;
}

void CharacterState::advance(float dt, const Character& c) {
	if (dt < 0.f)
		return;
	time += dt;

	if (hold > 0.f) {
		hold -= dt;
		if (hold <= 0.f) {
			// The game does not wait for the next beat to let go of a pose: the
			// moment the hold is up it dances. Waiting would leave the mouth
			// hanging open for up to a beat after a short note.
			hold = 0.f;
			beat(c);
		}
		return;
	}
	if (forced >= 0) {
		if (forced >= (int) c.anims.size()) {
			forced = -1;
			return;
		}
		const CharAnim& a = c.anims[(size_t) forced];
		if (!a.loop && time >= a.duration())
			forced = -1;
	}
}

int CharacterState::currentAnim(const Character& c) const {
	if (forced >= 0 && forced < (int) c.anims.size() && c.anims[(size_t) forced].valid())
		return forced;
	return SlotFallback(c, slot);
}

int CharacterState::currentFrame(const Character& c) const {
	int idx = currentAnim(c);
	if (idx < 0)
		return 0;
	return c.anims[(size_t) idx].frameAt(time);
}

}  // namespace fnf
