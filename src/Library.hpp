#pragma once
// ============================================================================
// Finding characters inside a Friday Night Funkin' mod.
//
// There is no one layout. A Psych mod keeps character files in `characters/`
// and sheets in `images/characters/`; the base game buries them under
// `assets/shared/`; a V-Slice mod uses `data/characters/` and `images/`; and
// half of what people download from GameBanana is a loose PNG and XML in a
// folder with no character file at all, because that is what the spritesheet
// generators produce.
//
// So nothing here assumes a layout. It walks whatever folder it is given, picks
// up three kinds of thing — character files, sheet pairs, icons — and then
// matches them to each other by name. A mod laid out in a way nobody has thought
// of yet still works as long as the files are in it somewhere, and a sheet with
// no character file is a character in its own right rather than an error.
// ============================================================================
#include <string>
#include <vector>

#include "Character.hpp"

namespace fnf {

/** One character the library found: enough to show it in a menu, and enough to
load it, without having opened a single PNG. Scanning must stay cheap — the
picker lists everything, and decoding every sheet to fill a menu would stall
Rack for as long as the mod is big. */
struct CharacterRef {
	std::string name;      // "dad" — what the menu shows
	std::string modName;   // the folder it came out of
	std::string jsonPath;  // empty for a bare sheet
	std::string imagePath;
	std::string xmlPath;
	std::string iconPath;  // empty if the mod has no icon for it

	/** Identity across a rescan. A patch remembers the character by this, not by
	its place in the list: install one more mod and every index moves, and a
	saved patch would come back wearing somebody else's face. */
	std::string key() const { return modName + "/" + name; }
	bool valid() const { return !imagePath.empty() && !xmlPath.empty(); }
};

struct Library {
	std::vector<CharacterRef> entries;
	/** Roots that were scanned, and what went wrong, if anything. Both are shown
	in the menu: a library that is empty because the folder is empty and one that
	is empty because the folder does not exist need different actions from the
	user, and only saying "no characters" tells them neither. */
	std::vector<std::string> roots;
	std::string note;
	/** Bumped on every scan, so a widget can tell that its cached picture of the
	list is out of date without comparing the list itself. */
	uint64_t version = 0;

	void scan(const std::vector<std::string>& folders);
	const CharacterRef* find(const std::string& key) const;
	int indexOf(const std::string& key) const;
};

/** Which character a module should wear when nobody has said yet.

There is no stand-in character any more, so this is the only thing standing
between a freshly placed module and an empty panel: a module that has never been
told who to be picks somebody out of the library. With no library at all it wears
nobody, and its panel says which of the reasons it is.

The name matching is only there so the first thing you see is the three of them
and not the same character three times: the player reaches for a boyfriend, the
opponent for a dad, and the one in the background for a girlfriend. It is a
nicety and it is allowed to miss. What it must never do is come back empty when
the library is not. */
enum DefaultRole {
	kRolePlayer = 0,
	kRoleOpponent,
	kRoleDancer,
};
std::string PickDefaultKey(const std::vector<CharacterRef>& entries, int role);

/** Opens one. This is the expensive half: it reads the XML, the character file
if there is one, and works out the animations. The PNG is *not* read here —
pixels are the graphics thread's business and are loaded separately. */
bool LoadCharacter(const CharacterRef& ref, Character* out, std::string* err);

/** The pixels. Kept apart from Character because a spritesheet is tens of
megabytes, is only wanted once there is somewhere to draw it, and is the one
thing here that can be thrown away and read again. */
struct Image {
	int w = 0, h = 0;
	std::vector<uint8_t> rgba;

	bool valid() const { return w > 0 && h > 0; }
	bool hasPixels() const { return valid() && !rgba.empty(); }
	size_t bytes() const { return rgba.size(); }
	bool load(const std::string& path, std::string* err);
	/** Lets the pixels go but remembers the size. Called once the sheet is on
	the graphics card, where it is going to be read from anyway. */
	void release() { rgba.clear(); rgba.shrink_to_fit(); }
};

/** A health icon is a strip of square frames: two normally (winning, losing),
sometimes three (winning, losing, dead). This is which square to draw. */
struct IconFrames {
	int size = 0;   // one frame is size x size
	int count = 0;  // how many are in the strip
};
IconFrames IconLayout(const Image& img);

}  // namespace fnf
