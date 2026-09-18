#include "Library.hpp"

#include <algorithm>
#include <cctype>

#include "Files.hpp"

// Ours, in third_party/, compiled static: libRack already carries a copy of
// stb_image inside NanoVG, and without STB_IMAGE_STATIC the dynamic linker is
// free to tie our calls to that other one.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_GIF
#define STBI_NO_STDIO
#include "stb_image.h"

namespace fnf {

namespace {

std::string Lower(std::string s) {
	for (char& c : s)
		c = (char) std::tolower((unsigned char) c);
	return s;
}

/** Forward slashes and lower case, so that a path written in a character file
on Windows matches a path read off a disk on Linux. */
std::string Slashed(std::string s) {
	for (char& c : s) {
		if (c == '\\')
			c = '/';
	}
	return Lower(std::move(s));
}

bool EndsWith(const std::string& hay, const std::string& tail) {
	return hay.size() >= tail.size() &&
	       hay.compare(hay.size() - tail.size(), tail.size(), tail) == 0;
}

/** Everything found in one walk, before any of it is matched up. */
struct Harvest {
	struct File {
		std::string path;   // as it is on disk
		std::string lower;  // slashed and lowered, for matching
		std::string stem;   // lowered
	};
	std::vector<File> jsons;
	std::vector<File> sheets;  // PNGs that have an XML beside them
	std::vector<File> icons;
	std::vector<std::string> xmlOf;  // alongside `sheets`
};

/** One pass over a folder. Depth-limited and count-limited: this runs when the
user points us at a folder, and a mistyped path or a folder chosen one level too
high should cost a moment, not a frozen Rack.

Its own stack rather than a recursive iterator, because the standard one comes
from <filesystem>, and <filesystem> cannot be used here: libc++ marks it
unavailable below macOS 10.15 and Rack targets older than that, so a plugin
built on it does not compile at all on the VCV build farm's macOS job. */
void Walk(const std::string& root, Harvest* h, int maxDepth = 8, size_t maxFiles = 60000) {
	if (!IsDirectory(root))
		return;

	// A file is only a sheet if there is an XML beside it with the same stem, so
	// the whole folder is collected first and paired afterwards.
	std::vector<Harvest::File> pngs;
	std::vector<std::string> xmls;  // lowered, full path
	size_t seen = 0;

	struct Pending {
		std::string path;
		int depth;
	};
	std::vector<Pending> stack;
	stack.push_back({NormalizePath(root), 0});

	std::vector<DirEntry> entries;
	while (!stack.empty()) {
		Pending here = stack.back();
		stack.pop_back();
		if (!ListDirectory(here.path, &entries))
			continue;  // no permission, or it went away mid-walk

		for (DirEntry& e : entries) {
			if (++seen > maxFiles)
				return;
			// Symlinks are not followed: a mod folder with a link back up to the
			// home directory would otherwise take the walk with it.
			if (e.isSymlink)
				continue;
			std::string full = JoinPath(here.path, e.name);
			if (e.isDir) {
				if (here.depth + 1 < maxDepth)
					stack.push_back({full, here.depth + 1});
				continue;
			}

			Harvest::File f;
			f.path = full;
			f.lower = Slashed(f.path);
			f.stem = Lower(FileStem(e.name));
			std::string ext = Lower(FileExtension(e.name));

			if (ext == ".json")
				h->jsons.push_back(f);
			else if (ext == ".xml")
				xmls.push_back(f.lower);
			else if (ext == ".png")
				pngs.push_back(f);
		}
	}

	std::sort(xmls.begin(), xmls.end());
	for (Harvest::File& p : pngs) {
		// An icon is a PNG that is named like one or lives in an icons folder.
		// Icons never have an XML, so they can never be mistaken for a sheet.
		if (p.stem.compare(0, 5, "icon-") == 0 || p.lower.find("/icons/") != std::string::npos) {
			h->icons.push_back(p);
			continue;
		}
		std::string want = p.lower.substr(0, p.lower.size() - 4) + ".xml";
		if (std::binary_search(xmls.begin(), xmls.end(), want)) {
			h->sheets.push_back(p);
			// The real path, not the lowered one: this is what gets opened.
			h->xmlOf.push_back(p.path.substr(0, p.path.size() - 4) + ".xml");
		}
	}
	// Some sheets are shipped with the XML in capitals. Rare, but it costs one
	// more pass to find them and a character that will not open costs a lot more.
	if (h->sheets.empty() && !pngs.empty()) {
		for (Harvest::File& p : pngs) {
			for (const std::string& x : xmls) {
				if (Lower(FileStem(x)) == p.stem) {
					h->sheets.push_back(p);
					h->xmlOf.push_back(JoinPath(ParentPath(p.path), FileName(x)));
					break;
				}
			}
		}
	}
}

/** The best sheet for what a character file asked for. `want` is a path with no
extension, relative to the mod's images folder, e.g. "characters/DADDY_DEAREST".
The tail is tried first because two mods can both have a "bf.png" but only one
has "characters/bf.png"; the bare name is the fallback, for the mods that keep
their sheets somewhere else entirely. */
int MatchSheet(const Harvest& h, const std::string& want) {
	if (want.empty())
		return -1;
	std::string w = Slashed(want);
	std::string tail = "/" + w + ".png";

	for (size_t i = 0; i < h.sheets.size(); i++) {
		if (EndsWith(h.sheets[i].lower, tail))
			return (int) i;
	}
	std::string base = w;
	size_t slash = base.find_last_of('/');
	if (slash != std::string::npos)
		base = base.substr(slash + 1);
	for (size_t i = 0; i < h.sheets.size(); i++) {
		if (h.sheets[i].stem == base)
			return (int) i;
	}
	return -1;
}

int MatchIcon(const Harvest& h, const std::string& iconName, const std::string& charName) {
	auto byStem = [&](const std::string& stem) {
		for (size_t i = 0; i < h.icons.size(); i++) {
			if (h.icons[i].stem == stem)
				return (int) i;
		}
		return -1;
	};
	std::string icon = Lower(iconName);
	if (!icon.empty()) {
		int i = byStem("icon-" + icon);
		if (i >= 0)
			return i;
		i = byStem(icon);
		if (i >= 0)
			return i;
	}
	std::string cn = Lower(charName);
	if (!cn.empty()) {
		int i = byStem("icon-" + cn);
		if (i >= 0)
			return i;
		i = byStem(cn);
		if (i >= 0)
			return i;
	}
	return -1;
}

/** The name of the mod a file belongs to: the first folder under the root that
we were pointed at, or the root itself. It is only ever shown to the user, to
tell two characters called "dad" apart. */
std::string ModNameFor(const std::string& root, const std::string& file) {
	std::string r = NormalizePath(root);
	std::string f = NormalizePath(file);
	std::string rootName = FileName(r);

	// The file has to actually be under the root for any of this to mean
	// anything. It always is, but a walk that followed something odd would
	// otherwise index into the wrong string.
	if (f.size() <= r.size() + 1 || f.compare(0, r.size(), r) != 0 || f[r.size()] != '/')
		return rootName;

	std::vector<std::string> parts;
	std::string rest = f.substr(r.size() + 1);
	size_t at = 0;
	while (at < rest.size()) {
		size_t slash = rest.find('/', at);
		if (slash == std::string::npos)
			break;
		parts.push_back(rest.substr(at, slash - at));
		at = slash + 1;
	}
	// Everything left is the file's own name, which is never a mod name.
	if (parts.empty())
		return rootName;  // the file sits directly in the root: the root is the mod

	// "mods/MyMod/..." and "assets/..." — a folder that is plainly plumbing is
	// not a mod name, so go one deeper.
	std::string low = Lower(parts[0]);
	if ((low == "mods" || low == "assets" || low == "content") && parts.size() >= 2)
		return parts[1];
	return parts[0];
}

}  // namespace

void Library::scan(const std::vector<std::string>& folders) {
	entries.clear();
	roots = folders;
	note.clear();
	version++;

	int missing = 0, unreadable = 0;

	for (const std::string& folder : folders) {
		if (folder.empty())
			continue;
		std::string root = NormalizePath(folder);
		if (!IsDirectory(root)) {
			missing++;
			continue;
		}

		Harvest h;
		Walk(root, &h);
		std::vector<bool> claimed(h.sheets.size(), false);

		// --- characters that have a character file ---------------------------
		for (const Harvest::File& jf : h.jsons) {
			JsonValue root_;
			std::string err;
			if (!JsonParseFile(jf.path, &root_, &err))
				continue;
			// Any JSON at all is fair game as long as it looks like a character:
			// mods keep songs, weeks, dialogue and settings in JSON too, and
			// only one of those has an animation list with an image beside it.
			if (!root_.get("animations")->isArray())
				continue;

			Character probe;
			if (!CharacterParseJson(root_, &probe, &err)) {
				unreadable++;
				continue;
			}
			int sheet = MatchSheet(h, probe.imagePath);
			if (sheet < 0) {
				// The character file is fine and its sheet is not there. That is
				// worth counting: it is the usual sign of a half-copied mod.
				unreadable++;
				continue;
			}
			claimed[(size_t) sheet] = true;

			CharacterRef ref;
			ref.name = FileStem(jf.path);
			ref.modName = ModNameFor(root, jf.path);
			ref.jsonPath = jf.path;
			ref.imagePath = h.sheets[(size_t) sheet].path;
			ref.xmlPath = h.xmlOf[(size_t) sheet];
			int icon = MatchIcon(h, probe.iconName, ref.name);
			if (icon >= 0)
				ref.iconPath = h.icons[(size_t) icon].path;
			entries.push_back(std::move(ref));
		}

		// --- and the bare sheets ---------------------------------------------
		for (size_t i = 0; i < h.sheets.size(); i++) {
			if (claimed[i])
				continue;
			CharacterRef ref;
			ref.name = FileStem(h.sheets[i].path);
			ref.modName = ModNameFor(root, h.sheets[i].path);
			ref.imagePath = h.sheets[i].path;
			ref.xmlPath = h.xmlOf[i];
			int icon = MatchIcon(h, "", ref.name);
			if (icon >= 0)
				ref.iconPath = h.icons[(size_t) icon].path;
			entries.push_back(std::move(ref));
		}
	}

	std::sort(entries.begin(), entries.end(), [](const CharacterRef& a, const CharacterRef& b) {
		if (a.modName != b.modName)
			return Lower(a.modName) < Lower(b.modName);
		return Lower(a.name) < Lower(b.name);
	});
	// Two mods can hold the same character under the same name. Keeping both
	// would make the key ambiguous, and a patch would load whichever came first.
	entries.erase(std::unique(entries.begin(), entries.end(),
	                          [](const CharacterRef& a, const CharacterRef& b) {
		                          return a.key() == b.key();
	                          }),
	              entries.end());

	if (missing > 0)
		note = std::to_string(missing) + " folder(s) are not there any more";
	else if (unreadable > 0)
		note = std::to_string(unreadable) + " character(s) could not be opened";
}

const CharacterRef* Library::find(const std::string& key) const {
	int i = indexOf(key);
	return i >= 0 ? &entries[(size_t) i] : nullptr;
}

int Library::indexOf(const std::string& key) const {
	for (size_t i = 0; i < entries.size(); i++) {
		if (entries[i].key() == key)
			return (int) i;
	}
	return -1;
}

std::string PickDefaultKey(const std::vector<CharacterRef>& entries, int role) {
	if (entries.empty())
		return "";  // nobody, and the panel will say so

	static const char* kPlayerish[] = {"boyfriend", "bf", "player", nullptr};
	static const char* kOpponentish[] = {"dad",  "daddy",   "mom",  "opponent",
	                                     "pico", "monster", nullptr};
	static const char* kDancerish[] = {"gf", "girlfriend", "nene", "speaker", nullptr};
	static const char* kNobody[] = {nullptr};

	const char* const* want = kPlayerish;
	const char* const* avoid = kNobody;
	switch (role) {
		case kRoleOpponent: want = kOpponentish; avoid = kPlayerish; break;
		case kRoleDancer: want = kDancerish; avoid = kPlayerish; break;
		default: want = kPlayerish; avoid = kOpponentish; break;
	}

	auto mentions = [](const std::string& name, const char* const* words) {
		std::string low = Lower(name);
		for (const char* const* w = words; *w; w++) {
			if (low.find(*w) != std::string::npos)
				return true;
		}
		return false;
	};

	for (const CharacterRef& r : entries) {
		if (mentions(r.name, want))
			return r.key();
	}
	// Nobody obvious. Anybody who is plainly the *other* side's is worse than
	// nobody, so those are skipped before falling back to the first.
	for (const CharacterRef& r : entries) {
		if (!mentions(r.name, avoid))
			return r.key();
	}
	return entries[0].key();
}

bool LoadCharacter(const CharacterRef& ref, Character* out, std::string* err) {
	if (!out)
		return false;
	*out = Character();
	out->name = ref.name;
	out->modName = ref.modName;
	out->jsonPath = ref.jsonPath;
	out->imagePath = ref.imagePath;
	out->xmlPath = ref.xmlPath;
	out->iconPath = ref.iconPath;

	if (!AtlasParseFile(ref.xmlPath, &out->atlas, err))
		return false;

	if (!ref.jsonPath.empty()) {
		JsonValue root;
		std::string jerr;
		if (JsonParseFile(ref.jsonPath, &root, &jerr)) {
			Character meta;
			if (CharacterParseJson(root, &meta, &jerr)) {
				// The paths were already resolved by the scan; everything else
				// comes from the file.
				meta.name = out->name;
				meta.modName = out->modName;
				meta.jsonPath = out->jsonPath;
				meta.imagePath = out->imagePath;
				meta.xmlPath = out->xmlPath;
				meta.iconPath = out->iconPath;
				meta.atlas = std::move(out->atlas);
				*out = std::move(meta);
			}
			else if (err) {
				*err = jerr;  // kept, but not fatal: see below
			}
		}
		else if (err) {
			*err = jerr;
		}
	}

	if (out->anims.empty()) {
		// Either there was no character file, or it was broken. Either way the
		// sheet is right there and the prefixes in it say what the animations
		// are, so the character opens anyway. A broken file costs the offsets,
		// not the character.
		CharacterGuessAnims(out);
	}
	out->resolve();

	if (!out->loaded()) {
		if (err && err->empty())
			*err = "no animations could be made out of " + ref.xmlPath;
		return false;
	}
	if (err)
		err->clear();
	return true;
}

bool Image::load(const std::string& path, std::string* err) {
	w = h = 0;
	rgba.clear();

	std::string bytes;
	if (!ReadWholeFile(path, &bytes)) {
		if (err)
			*err = "could not open " + path;
		return false;
	}
	int comp = 0;
	// Always four channels out, whatever went in: the graphics card wants RGBA
	// and a sheet saved as an indexed PNG is still a sheet.
	stbi_uc* pixels = stbi_load_from_memory((const stbi_uc*) bytes.data(), (int) bytes.size(),
	                                        &w, &h, &comp, 4);
	if (!pixels) {
		w = h = 0;
		if (err)
			*err = std::string("could not decode ") + path;
		return false;
	}
	rgba.assign(pixels, pixels + (size_t) w * (size_t) h * 4);
	stbi_image_free(pixels);
	if (err)
		err->clear();
	return true;
}

IconFrames IconLayout(const Image& img) {
	IconFrames f;
	if (!img.valid())
		return f;
	// The strip is a row of squares, so the height is the frame size. Icons are
	// 150 px in the game and 300x150 for a two-frame strip, but mods ship them
	// at every size, and measuring beats assuming.
	f.size = img.h;
	f.count = img.w / (img.h > 0 ? img.h : 1);
	if (f.count < 1) {
		// Taller than it is wide: one frame, squashed. Draw the whole thing.
		f.size = img.w;
		f.count = 1;
	}
	return f;
}

}  // namespace fnf
