// deps: Json.cpp Sparrow.cpp Character.cpp Library.cpp Files.cpp
// Finding characters inside a mod, on a real folder on the disk. tools/
// make_fixtures.py builds the two mods this reads: a Psych one laid out the way
// one really is, and a loose PNG+XML pair with no character file.
#include "Library.hpp"
#include "check.hpp"

#include <string>

using namespace fnf;

static std::string Fixture(const char* rest) {
	return std::string(FIXTURE_DIR) + "/" + rest;
}

static const CharacterRef* ByName(const Library& lib, const char* name) {
	for (const CharacterRef& r : lib.entries) {
		if (r.name == name)
			return &r;
	}
	return nullptr;
}

int main() {
	SECTION("scanning a Psych Engine mod");
	{
		Library lib;
		lib.scan({Fixture("mod")});
		CHECK_EQ(lib.entries.size(), (size_t) 1);
		const CharacterRef* dad = ByName(lib, "dad");
		CHECK(dad != nullptr);
		if (dad) {
			// The character file says image: "characters/DADDY_DEAREST", and the
			// sheet is three folders away under images/. Matching by the tail of
			// the path is what finds it without knowing the layout.
			CHECK(dad->imagePath.find("DADDY_DEAREST.png") != std::string::npos);
			CHECK(dad->xmlPath.find("DADDY_DEAREST.xml") != std::string::npos);
			CHECK(dad->iconPath.find("icon-dad.png") != std::string::npos);
			CHECK_EQ(dad->valid(), true);
		}
		// The mod also contains a song file. A mod is full of JSON that is not a
		// character, and picking one up would put a phantom in the menu.
		CHECK(ByName(lib, "song-notes") == nullptr);
	}

	SECTION("a loose sheet with no character file is a character too");
	{
		Library lib;
		lib.scan({Fixture("bare")});
		CHECK_EQ(lib.entries.size(), (size_t) 1);
		const CharacterRef* bf = ByName(lib, "BOYFRIEND");
		CHECK(bf != nullptr);
		if (bf) {
			CHECK_EQ(bf->jsonPath, std::string(""));
			CHECK_EQ(bf->valid(), true);
		}
	}

	SECTION("two folders at once, and the key that survives a rescan");
	{
		Library lib;
		lib.scan({Fixture("mod"), Fixture("bare")});
		CHECK_EQ(lib.entries.size(), (size_t) 2);
		const CharacterRef* dad = ByName(lib, "dad");
		CHECK(dad != nullptr);
		std::string key = dad ? dad->key() : "";
		CHECK(lib.indexOf(key) >= 0);

		// The patch remembers the key, never the index. Scan the folders in the
		// other order and the indices move; the key has to still find the same
		// character or a saved patch comes back wearing somebody else's face.
		Library again;
		again.scan({Fixture("bare"), Fixture("mod")});
		const CharacterRef* found = again.find(key);
		CHECK(found != nullptr);
		if (found)
			CHECK_EQ(found->name, std::string("dad"));
	}

	SECTION("a folder that is not there says so instead of going quiet");
	{
		Library lib;
		lib.scan({Fixture("no-such-folder")});
		CHECK_EQ(lib.entries.size(), (size_t) 0);
		CHECK(!lib.note.empty());
	}

	SECTION("opening the character out of the mod");
	{
		Library lib;
		lib.scan({Fixture("mod")});
		const CharacterRef* dad = ByName(lib, "dad");
		CHECK(dad != nullptr);
		if (dad) {
			Character c;
			std::string err;
			CHECK_EQ(LoadCharacter(*dad, &c, &err), true);
			CHECK_EQ(err, std::string(""));
			CHECK(c.loaded());
			CHECK_NEAR(c.singDuration, 6.1f, 1e-5);
			CHECK_EQ((int) c.barColor[0], 175);
			for (int d = 0; d < kDirCount; d++)
				CHECK(c.anim(SingSlot(d)) != nullptr);
			// The sing frames in this sheet are trimmed. Their untrimmed box is
			// the same size as the idle's, which is the whole reason a packer
			// writes frameWidth at all.
			CHECK_NEAR(c.nominalHeight(), 30.f, 1e-4);

			Image img;
			CHECK_EQ(img.load(c.imagePath, &err), true);
			CHECK_EQ(img.w, 120);
			CHECK_EQ(img.h, 30);
			CHECK_EQ(img.hasPixels(), true);
			CHECK_EQ(img.bytes(), (size_t) 120 * 30 * 4);
			img.release();
			CHECK_EQ(img.hasPixels(), false);
			CHECK_EQ(img.valid(), true);  // the size is still known
		}
	}

	SECTION("opening the bare sheet");
	{
		Library lib;
		lib.scan({Fixture("bare")});
		CHECK_EQ(lib.entries.size(), (size_t) 1);
		Character c;
		std::string err;
		CHECK_EQ(LoadCharacter(lib.entries[0], &c, &err), true);
		CHECK(c.loaded());
		// Guessed from the sheet: an idle, an up, a left, and the up miss kept
		// separate from the up.
		CHECK(c.slot[kAnimIdle] >= 0);
		CHECK(c.slot[kAnimSingUp] >= 0);
		CHECK(c.slot[kAnimSingLeft] >= 0);
		CHECK(c.slot[kAnimMissUp] >= 0);
		CHECK(c.slot[kAnimMissUp] != c.slot[kAnimSingUp]);
		CHECK_EQ(c.anims[(size_t) c.slot[kAnimSingUp]].frames.size(), (size_t) 2);
	}

	SECTION("the icon strip is measured, not assumed");
	{
		Image icon;
		std::string err;
		CHECK_EQ(icon.load(Fixture("mod/images/icons/icon-dad.png"), &err), true);
		IconFrames f = IconLayout(icon);
		CHECK_EQ(f.size, 30);
		CHECK_EQ(f.count, 2);

		// A single square icon, which plenty of mods ship.
		Image one;
		one.w = 150;
		one.h = 150;
		one.rgba.assign(150 * 150 * 4, 0);
		CHECK_EQ(IconLayout(one).count, 1);
	}

	SECTION("who a module wears when nobody has said");
	{
		// A module that has never been told who to be picks somebody out of the
		// library. An empty library is the only case with no answer, and then
		// the module wears nobody and its panel says so.
		CHECK_EQ(PickDefaultKey({}, kRolePlayer), std::string(""));
		CHECK_EQ(PickDefaultKey({}, kRoleOpponent), std::string(""));

		auto make = [](const char* mod, const char* name) {
			CharacterRef r;
			r.modName = mod;
			r.name = name;
			return r;
		};
		std::vector<CharacterRef> lib = {make("Base", "dad"), make("Base", "bf"),
		                                 make("Base", "gf")};
		// All three reach for different people, so the first thing you see is the
		// three of them and not the same character three times.
		CHECK_EQ(PickDefaultKey(lib, kRolePlayer), std::string("Base/bf"));
		CHECK_EQ(PickDefaultKey(lib, kRoleOpponent), std::string("Base/dad"));
		CHECK_EQ(PickDefaultKey(lib, kRoleDancer), std::string("Base/gf"));

		// Nobody recognisable: anybody is better than nobody, and the two sides
		// still must not land on the same one if there is a choice.
		std::vector<CharacterRef> odd = {make("Mod", "zebra"), make("Mod", "walrus")};
		CHECK_EQ(PickDefaultKey(odd, kRolePlayer), std::string("Mod/zebra"));
		CHECK_EQ(PickDefaultKey(odd, kRoleOpponent), std::string("Mod/zebra"));
		CHECK_EQ(PickDefaultKey(odd, kRoleDancer), std::string("Mod/zebra"));

		// One character, and it is plainly the player's. The opponent still has
		// to wear something: an empty module would look broken.
		std::vector<CharacterRef> one = {make("Mod", "BOYFRIEND")};
		CHECK_EQ(PickDefaultKey(one, kRoleOpponent), std::string("Mod/BOYFRIEND"));
	}

	return check::summary("library");
}
