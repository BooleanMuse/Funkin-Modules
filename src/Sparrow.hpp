#pragma once
// ============================================================================
// The Sparrow atlas: one PNG plus one XML that says where every frame is in it.
//
// This is *the* format of a Friday Night Funkin' character. It is what the
// spritesheet-and-XML generators on GameBanana spit out, what Adobe Animate's
// "Starling/Sparrow" export writes, and what Flixel's FlxAtlasFrames.fromSparrow
// reads, so anything a modder has on their disk is already in it.
//
//   <TextureAtlas imagePath="DADDY_DEAREST.png">
//     <SubTexture name="Dad idle dance0000" x="0" y="0" width="410" height="580"
//                 frameX="-4" frameY="-11" frameWidth="420" frameHeight="600"/>
//
// The four frame* attributes are the part that matters and the part that is easy
// to get wrong. A packer trims the transparent border off each frame, so `x y
// width height` is the *trimmed* pixels, and frameX/frameY/frameWidth/frameHeight
// describe the untrimmed box it came out of. Draw with only the first four and
// every frame is centred on its own ink: the character shakes on the spot
// through the whole animation. Draw the trimmed pixels at (-frameX, -frameY)
// inside the frame box and it stands still, which is what Flixel does.
//
// An anti-parser: no DOM, no entities, no namespaces. It looks for SubTexture
// tags and reads their attributes, and that is the entire job.
// ============================================================================
#include <string>
#include <vector>

namespace fnf {

struct AtlasFrame {
	std::string name;
	/** The trimmed pixels, in the sheet. */
	int x = 0, y = 0, w = 0, h = 0;
	/** Where those pixels sit inside the untrimmed frame. Normally <= 0. */
	int frameX = 0, frameY = 0;
	/** The untrimmed frame — the box the animation is really made of. */
	int frameWidth = 0, frameHeight = 0;
	/** Sparrow allows a frame to be stored turned 90 degrees clockwise. FNF
	sheets almost never are, but a sheet from a general-purpose packer can be,
	and a rotated frame drawn flat is unmistakably wrong rather than subtly. */
	bool rotated = false;

	/** The number at the end of the name ("Dad idle dance0003" -> 3), or -1.
	This is the frame order; the XML is normally already in it, but a sheet
	edited by hand is not always. */
	int sequence() const;
};

struct Atlas {
	/** The imagePath attribute, as written. The PNG is nearly always next to the
	XML and named the same, but a mod that renamed one and not the other is
	something we can still open by trusting this. */
	std::string imageName;
	std::vector<AtlasFrame> frames;

	bool valid() const { return !frames.empty(); }

	/** Indices of every frame whose name starts with `prefix`, in playing order.
	Flixel's own rule, and a blunt one: "BF NOTE UP" also catches
	"BF NOTE UP MISS0000". Characters are written around that, which is why their
	prefixes say "BF NOTE UP0". */
	std::vector<int> framesWithPrefix(const std::string& prefix) const;

	/** Indices of every frame that *is* this animation, in playing order.

	The exact group first — every frame whose name is `prefix` followed by
	nothing but its number — and only if there is no such group does it fall back
	to the loose match above. Both halves are needed, and the real characters say
	why:

	    "GF Dancing Beat"   is a group of 30, and is also the start of
	                        "GF Dancing Beat Hair blowing" and "... Hair Landing".
	                        Matching loosely gives 90 frames, and gf.json then
	                        indexes into the wrong ones and the dance is nonsense.

	    "BF NOTE LEFT0"     is not a group at all — the group is "BF NOTE LEFT".
	                        The trailing 0 is there precisely to shut out
	                        "BF NOTE LEFT MISS", and only the loose match honours it.

	    "BF HEY"            is not a group either: the sheet calls it "BF HEY!!".
	                        Again only the loose match finds it.

	Exact-first also quietly fixes the case a mod gets wrong: a character file
	that says "BF NOTE LEFT" without the digit means the fifteen left-note frames,
	not those plus thirty-four misses, and here it gets them. */
	std::vector<int> framesForPrefix(const std::string& prefix) const;
};

bool AtlasParse(const std::string& xml, Atlas* out, std::string* err);
bool AtlasParseFile(const std::string& path, Atlas* out, std::string* err);

}  // namespace fnf
