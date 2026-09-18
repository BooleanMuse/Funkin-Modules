// deps: Json.cpp Sparrow.cpp Character.cpp
// The atlas, the character file, and what the character is doing. This is where
// a sheet cut up in the wrong place, an animation filed under the wrong arrow or
// a sing that never lets go would show up.
#include "Character.hpp"
#include "check.hpp"

#include <cstring>

using namespace fnf;

static const char* kAtlasXml = R"(<?xml version="1.0" encoding="utf-8"?>
<TextureAtlas imagePath="DADDY_DEAREST.png">
  <SubTexture name="Dad idle dance0000" x="0" y="0" width="20" height="30"/>
  <SubTexture name="Dad idle dance0001" x="20" y="0" width="20" height="30"/>
  <SubTexture name="Dad Sing Note LEFT0000" x="40" y="0" width="12" height="18" frameX="-4" frameY="-6" frameWidth="20" frameHeight="30"/>
  <SubTexture name="Dad Sing Note UP0000" x="60" y="0" width="12" height="18" frameX="-4" frameY="-6" frameWidth="20" frameHeight="30"/>
  <SubTexture name="Dad Sing Note UP MISS0000" x="80" y="0" width="12" height="18"/>
</TextureAtlas>)";

int main() {
	SECTION("the atlas");
	{
		Atlas atlas;
		std::string err;
		CHECK_EQ(AtlasParse(kAtlasXml, &atlas, &err), true);
		CHECK_EQ(atlas.imageName, std::string("DADDY_DEAREST.png"));
		CHECK_EQ(atlas.frames.size(), (size_t) 5);
		CHECK_EQ(atlas.frames[0].w, 20);
		// Absent frame* attributes mean the frame was never trimmed, so the
		// untrimmed box is the trimmed one. Zero would give a frame with no
		// size, and a character that never appears at all.
		CHECK_EQ(atlas.frames[0].frameWidth, 20);
		CHECK_EQ(atlas.frames[0].frameHeight, 30);
		CHECK_EQ(atlas.frames[2].frameX, -4);
		CHECK_EQ(atlas.frames[2].frameWidth, 20);
		CHECK_EQ(atlas.frames[0].sequence(), 0);
		CHECK_EQ(atlas.frames[1].sequence(), 1);

		// Flixel's prefix rule, kept on purpose: "Dad Sing Note UP" also catches
		// the MISS frames, which is exactly why real character files write the
		// prefix as "...UP0". Matching more cleverly here would make animations
		// that are right in the game wrong in the rack.
		CHECK_EQ(atlas.framesWithPrefix("Dad idle dance").size(), (size_t) 2);
		CHECK_EQ(atlas.framesWithPrefix("Dad Sing Note UP").size(), (size_t) 2);
		CHECK_EQ(atlas.framesWithPrefix("Dad Sing Note UP0").size(), (size_t) 1);
		CHECK_EQ(atlas.framesWithPrefix("nothing").size(), (size_t) 0);

		// ...and what an animation actually asks for: the exact group first, so
		// "Dad Sing Note UP" means the up note and not the up note plus its miss.
		CHECK_EQ(atlas.framesForPrefix("Dad Sing Note UP").size(), (size_t) 1);
		// But a prefix that is nobody's group falls back to the loose match, and
		// has to, because that is how a character file says "not the miss".
		CHECK_EQ(atlas.framesForPrefix("Dad Sing Note UP0").size(), (size_t) 1);
		CHECK_EQ(atlas.framesForPrefix("Dad Sing").size(), (size_t) 3);
		CHECK_EQ(atlas.framesForPrefix("nothing").size(), (size_t) 0);
	}

	SECTION("a Psych Engine character file");
	{
		const char* json = R"({
			"animations": [
				{"anim": "idle", "name": "Dad idle dance", "fps": 24, "loop": false, "offsets": [0, 0]},
				{"anim": "singLEFT", "name": "Dad Sing Note LEFT0", "fps": 24, "loop": false, "offsets": [-10, 4]},
				{"anim": "singUP", "name": "Dad Sing Note UP0", "fps": 24, "loop": false, "offsets": [0, 20]}
			],
			"image": "characters/DADDY_DEAREST",
			"healthicon": "dad",
			"sing_duration": 6.1,
			"flip_x": false,
			"healthbar_colors": [175, 102, 6],
			"no_antialiasing": true
		})";
		JsonValue root;
		std::string err;
		CHECK_EQ(JsonParse(json, &root, &err), true);

		Character c;
		CHECK_EQ(CharacterParseJson(root, &c, &err), true);
		CHECK_EQ(c.imagePath, std::string("characters/DADDY_DEAREST"));
		CHECK_EQ(c.iconName, std::string("dad"));
		CHECK_NEAR(c.singDuration, 6.1f, 1e-5);
		CHECK_EQ(c.antialias, false);
		CHECK_EQ((int) c.barColor[0], 175);
		CHECK_EQ(c.anims.size(), (size_t) 3);
		CHECK_EQ(c.anims[1].name, std::string("singLEFT"));
		CHECK_EQ(c.anims[1].prefix, std::string("Dad Sing Note LEFT0"));
		CHECK_NEAR(c.anims[1].offsetX, -10.f, 1e-5);

		CHECK_EQ(AtlasParse(kAtlasXml, &c.atlas, &err), true);
		c.resolve();
		CHECK(c.loaded());
		CHECK(c.slot[kAnimIdle] >= 0);
		CHECK(c.slot[kAnimSingLeft] >= 0);
		CHECK(c.slot[kAnimSingUp] >= 0);
		// It has no down and no misses. Both must still play something, or a
		// note in that lane would make the character vanish.
		CHECK_EQ(c.slot[kAnimSingDown], -1);
		CHECK(c.anim(kAnimSingDown) != nullptr);
		CHECK(c.anim(kAnimMissUp) == c.anim(kAnimSingUp));

		SECTION("  where a frame is drawn");
		{
			int up = c.slot[kAnimSingUp];
			FrameQuad q = c.quad(up, 0);
			CHECK_EQ(q.sx, 60);
			CHECK_EQ(q.sw, 12);
			// The trimmed pixels go at (-frameX, -frameY) inside the frame box,
			// and then the whole sprite is shifted by -offset. Both, in that
			// order, or the character shakes on the spot as it sings.
			CHECK_NEAR(q.dx, 4.f, 1e-4);    // -0 - (-4)
			CHECK_NEAR(q.dy, -20.f + 6.f, 1e-4);
			CHECK_NEAR(c.nominalHeight(), 30.f, 1e-4);
		}
	}

	SECTION("a V-Slice character file: name and prefix swap places");
	{
		const char* json = R"({
			"version": "1.0.0",
			"renderType": "sparrow",
			"assetPath": "characters/dad",
			"healthIcon": {"id": "dad"},
			"singTime": 8,
			"animations": [
				{"name": "idle", "prefix": "Dad idle dance", "frameRate": 24, "looped": false},
				{"name": "singLEFT", "prefix": "Dad Sing Note LEFT", "frameRate": 24, "looped": false}
			]
		})";
		JsonValue root;
		std::string err;
		CHECK_EQ(JsonParse(json, &root, &err), true);
		Character c;
		CHECK_EQ(CharacterParseJson(root, &c, &err), true);
		CHECK_EQ(c.imagePath, std::string("characters/dad"));
		CHECK_EQ(c.iconName, std::string("dad"));
		CHECK_NEAR(c.singDuration, 8.f, 1e-5);
		CHECK_EQ(c.anims[0].name, std::string("idle"));
		CHECK_EQ(c.anims[0].prefix, std::string("Dad idle dance"));
		CHECK_EQ(c.anims[1].name, std::string("singLEFT"));
	}

	SECTION("the girlfriend case, which a real character caught");
	{
		// Straight out of the base game, and it breaks two rules at once:
		//
		//  - danceLeft and danceRight share ONE prefix, "GF Dancing Beat", and
		//    are told apart only by indices — the first half of the loop and the
		//    second.
		//  - that prefix is also the start of "GF Dancing Beat Hair blowing".
		//    Matching loosely gives three times the frames and the indices then
		//    point into somebody else's hair.
		std::string xml = "<TextureAtlas imagePath=\"GF_assets.png\">";
		for (int i = 0; i < 30; i++) {
			char buf[256];
			std::snprintf(buf, sizeof(buf),
			              "<SubTexture name=\"GF Dancing Beat%04d\" x=\"%d\" y=\"0\""
			              " width=\"10\" height=\"10\"/>"
			              "<SubTexture name=\"GF Dancing Beat Hair blowing%04d\" x=\"%d\""
			              " y=\"20\" width=\"10\" height=\"10\"/>",
			              i, i * 10, i, i * 10);
			xml += buf;
		}
		xml += "</TextureAtlas>";

		const char* json = R"({
			"animations": [
				{"anim": "danceLeft", "name": "GF Dancing Beat", "fps": 24, "loop": false,
				 "indices": [30, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]},
				{"anim": "danceRight", "name": "GF Dancing Beat", "fps": 24, "loop": false,
				 "indices": [15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29]},
				{"anim": "hairBlow", "name": "GF Dancing Beat Hair blowing", "fps": 24,
				 "loop": true, "indices": [0, 1, 2, 3]}
			],
			"image": "characters/GF_assets", "healthicon": "gf"
		})";
		JsonValue root;
		std::string err;
		CHECK_EQ(JsonParse(json, &root, &err), true);
		Character c;
		CHECK_EQ(CharacterParseJson(root, &c, &err), true);
		CHECK_EQ(AtlasParse(xml, &c.atlas, &err), true);
		CHECK_EQ(c.atlas.frames.size(), (size_t) 60);
		c.resolve();

		// Fifteen and fifteen: the two halves of a thirty-frame loop. Index 30 is
		// out of range and is dropped rather than clamped — clamping would repeat
		// frame 29 at the front of every dance.
		CHECK(c.slot[kAnimDanceLeft] >= 0);
		CHECK(c.slot[kAnimDanceRight] >= 0);
		CHECK_EQ(c.anims[(size_t) c.slot[kAnimDanceLeft]].frames.size(), (size_t) 15);
		CHECK_EQ(c.anims[(size_t) c.slot[kAnimDanceRight]].frames.size(), (size_t) 15);
		// ...and the two halves really are different frames.
		CHECK(c.anims[(size_t) c.slot[kAnimDanceLeft]].frames[0] !=
		      c.anims[(size_t) c.slot[kAnimDanceRight]].frames[0]);
		// The hair is its own animation and keeps its own four frames.
		CHECK_EQ(c.anims[2].frames.size(), (size_t) 4);

		// She has no idle, so the idle falls back to a dance half. What it must
		// NOT do is take hairBlow: its prefix says "Dancing", and letting a
		// prefix name an animation the character file already named left her
		// standing there with her hair streaming sideways forever.
		CHECK_EQ(c.slot[kAnimIdle], -1);
		const CharAnim* idle = c.anim(kAnimIdle);
		CHECK(idle != nullptr);
		if (idle)
			CHECK(idle->name != "hairBlow");
	}

	SECTION("a sheet with no character file at all");
	{
		Character c;
		std::string err;
		CHECK_EQ(AtlasParse(kAtlasXml, &c.atlas, &err), true);
		CharacterGuessAnims(&c);
		c.resolve();
		CHECK(c.loaded());
		CHECK(c.slot[kAnimIdle] >= 0);
		CHECK(c.slot[kAnimSingLeft] >= 0);
		CHECK(c.slot[kAnimSingUp] >= 0);
		CHECK(c.slot[kAnimMissUp] >= 0);
		// The trap: "BF NOTE UP" is a prefix of "BF NOTE UP MISS". Guessing by
		// grouping whole names instead of by matching prefixes is what keeps the
		// miss frame out of the sing — a sing of two frames here, not three.
		CHECK_EQ(c.anims[(size_t) c.slot[kAnimSingUp]].frames.size(), (size_t) 1);
		CHECK_EQ(c.anims[(size_t) c.slot[kAnimMissUp]].frames.size(), (size_t) 1);
		CHECK(c.slot[kAnimSingUp] != c.slot[kAnimMissUp]);
		// An idle with no character file has nothing but the beat to restart it,
		// and a module left unpatched has no beat. It loops so the character
		// keeps breathing rather than freezing on its last frame.
		CHECK_EQ(c.anims[(size_t) c.slot[kAnimIdle]].loop, true);
	}

	SECTION("a sheet that never says sing or note");
	{
		// The base game's mother. Her frames are called "Mom Up Pose" and
		// "MOM DOWN POSE" — no "sing", no "note", and she has no character file
		// in the version those sheets come from. Guessing has to be generous
		// enough to find her arrows, or she loads with an idle and nothing else.
		const char* xml = R"(<TextureAtlas imagePath="Mom_Assets.png">
			<SubTexture name="Mom Idle0000" x="0" y="0" width="10" height="10"/>
			<SubTexture name="Mom Up Pose0000" x="10" y="0" width="10" height="10"/>
			<SubTexture name="MOM DOWN POSE0000" x="20" y="0" width="10" height="10"/>
			<SubTexture name="Mom Left Pose0000" x="30" y="0" width="10" height="10"/>
			<SubTexture name="Mom Pose Right0000" x="40" y="0" width="10" height="10"/>
		</TextureAtlas>)";
		Character c;
		std::string err;
		CHECK_EQ(AtlasParse(xml, &c.atlas, &err), true);
		CharacterGuessAnims(&c);
		c.resolve();
		CHECK(c.loaded());
		CHECK(c.slot[kAnimIdle] >= 0);
		for (int d = 0; d < kDirCount; d++)
			CHECK(c.slot[SingSlot(d)] >= 0);
	}

	SECTION("guessing loosely never overrules a character file");
	{
		// The generosity above is only for sheets with nothing else to go on.
		// A character file that names an animation "danceLeft" means a dance,
		// and a loose reading that saw the word "left" and filed it as a sing
		// would take the arrow away from the real one.
		const char* json = R"({
			"animations": [
				{"anim": "danceLeft", "name": "Beat", "fps": 24},
				{"anim": "leftThing", "name": "Something Left", "fps": 24}
			],
			"image": "x"
		})";
		JsonValue root;
		std::string err;
		CHECK_EQ(JsonParse(json, &root, &err), true);
		Character c;
		CHECK_EQ(CharacterParseJson(root, &c, &err), true);
		const char* xml = R"(<TextureAtlas imagePath="x.png">
			<SubTexture name="Beat0000" x="0" y="0" width="10" height="10"/>
			<SubTexture name="Something Left0000" x="10" y="0" width="10" height="10"/>
		</TextureAtlas>)";
		CHECK_EQ(AtlasParse(xml, &c.atlas, &err), true);
		c.resolve();
		CHECK(c.slot[kAnimDanceLeft] >= 0);
		CHECK_EQ(c.slot[kAnimSingLeft], -1);
	}

	SECTION("frames over time");
	{
		CharAnim a;
		a.fps = 10.f;
		a.frames = {0, 1, 2};
		CHECK_EQ(a.frameAt(0.f), 0);
		CHECK_EQ(a.frameAt(0.15f), 1);
		// Not looping: the last frame is held. That is what makes a sing a pose
		// rather than a flicker.
		CHECK_EQ(a.frameAt(5.f), 2);
		a.loop = true;
		CHECK_EQ(a.frameAt(0.35f), 0);
		CHECK_NEAR(a.duration(), 0.3f, 1e-5);
	}

	SECTION("what the character is doing");
	{
		Character c;
		std::string err;
		CHECK_EQ(AtlasParse(kAtlasXml, &c.atlas, &err), true);
		CharacterGuessAnims(&c);
		c.resolve();

		CharacterState st;
		CHECK_EQ(st.singing(), false);
		st.sing(kUp, 0.25f);
		CHECK_EQ(st.singing(), true);
		CHECK_EQ(st.currentAnim(c), c.slot[kAnimSingUp]);

		// The beat must not cut a sing short: the game holds the pose for
		// sing_duration steps whatever the tempo is doing.
		st.beat(c);
		CHECK_EQ(st.currentAnim(c), c.slot[kAnimSingUp]);
		st.advance(0.1f, c);
		CHECK_EQ(st.currentAnim(c), c.slot[kAnimSingUp]);

		// ...and when the hold runs out it goes back to dancing *immediately*,
		// not at the next beat. Waiting would leave the mouth hanging open for
		// up to a beat after a short note.
		st.advance(0.2f, c);
		CHECK_EQ(st.singing(), false);
		CHECK_EQ(st.currentAnim(c), c.slot[kAnimIdle]);

		st.miss(kLeft, 0.2f);
		CHECK_EQ(st.currentAnim(c), c.anim(kAnimMissLeft) ? c.slot[kAnimSingLeft] : -1);

		// A one-off from the menu plays through and then gives up on its own.
		int idle = c.slot[kAnimIdle];
		st.play(c.slot[kAnimSingLeft]);
		CHECK_EQ(st.currentAnim(c), c.slot[kAnimSingLeft]);
		st.beat(c);
		CHECK_EQ(st.currentAnim(c), c.slot[kAnimSingLeft]);
		st.advance(5.f, c);
		st.beat(c);
		CHECK_EQ(st.currentAnim(c), idle);
	}

	SECTION("a character with two dance halves alternates them");
	{
		Character c;
		std::string err;
		const char* xml = R"(<TextureAtlas imagePath="gf.png">
			<SubTexture name="GF Dancing Beat Left0000" x="0" y="0" width="10" height="10"/>
			<SubTexture name="GF Dancing Beat Right0000" x="10" y="0" width="10" height="10"/>
		</TextureAtlas>)";
		CHECK_EQ(AtlasParse(xml, &c.atlas, &err), true);
		CharacterGuessAnims(&c);
		c.resolve();
		CHECK(c.slot[kAnimDanceLeft] >= 0);
		CHECK(c.slot[kAnimDanceRight] >= 0);
		// No idle at all: it has to fall back to a dance half or the character
		// would be invisible whenever it was not singing.
		CHECK_EQ(c.slot[kAnimIdle], -1);
		CHECK(c.anim(kAnimIdle) != nullptr);

		CharacterState st;
		st.beat(c);
		int first = st.currentAnim(c);
		st.beat(c);
		int second = st.currentAnim(c);
		CHECK(first != second);
		st.beat(c);
		CHECK_EQ(st.currentAnim(c), first);
	}

	return check::summary("character");
}
