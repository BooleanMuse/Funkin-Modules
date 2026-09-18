#include "Sparrow.hpp"

#include "Json.hpp"  // ReadWholeFile

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace fnf {

int AtlasFrame::sequence() const {
	size_t end = name.size();
	while (end > 0 && std::isdigit((unsigned char) name[end - 1]))
		end--;
	if (end == name.size())
		return -1;
	return std::atoi(name.c_str() + end);
}

namespace {

/** A frame's name with its number, and any space or underscore before it, taken
off: "GF Dancing Beat0007" -> "GF Dancing Beat". This is the animation a frame
belongs to, said exactly. */
std::string GroupOf(const std::string& name) {
	size_t end = name.size();
	while (end > 0 && std::isdigit((unsigned char) name[end - 1]))
		end--;
	if (end == name.size())
		return name;  // no number at all: the whole name is the group
	while (end > 0 && (name[end - 1] == ' ' || name[end - 1] == '_'))
		end--;
	return name.substr(0, end);
}

}  // namespace

std::vector<int> Atlas::framesForPrefix(const std::string& prefix) const {
	std::vector<int> exact;
	for (size_t i = 0; i < frames.size(); i++) {
		if (GroupOf(frames[i].name) == prefix)
			exact.push_back((int) i);
	}
	if (exact.empty())
		return framesWithPrefix(prefix);

	std::stable_sort(exact.begin(), exact.end(), [&](int a, int b) {
		int sa = frames[(size_t) a].sequence();
		int sb = frames[(size_t) b].sequence();
		if (sa < 0 || sb < 0)
			return false;
		return sa < sb;
	});
	return exact;
}

std::vector<int> Atlas::framesWithPrefix(const std::string& prefix) const {
	std::vector<int> out;
	if (prefix.empty())
		return out;
	for (size_t i = 0; i < frames.size(); i++) {
		if (frames[i].name.compare(0, prefix.size(), prefix) == 0)
			out.push_back((int) i);
	}
	// Stable, and only by the trailing number: frames without one keep the order
	// the file gave them, which is the only order they have.
	std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
		int sa = frames[(size_t) a].sequence();
		int sb = frames[(size_t) b].sequence();
		if (sa < 0 || sb < 0)
			return false;
		return sa < sb;
	});
	return out;
}

namespace {

/** The value of `name="..."` inside one tag, or false. Single quotes count:
they are not legal XML either, and hand-written sheets have them. */
bool Attribute(const std::string& tag, const char* name, std::string* out) {
	size_t nameLen = std::strlen(name);
	size_t at = 0;
	while (true) {
		at = tag.find(name, at);
		if (at == std::string::npos)
			return false;
		// A real attribute, not the tail of another one ("frameX" contains "x").
		bool leftOk = at == 0 || std::isspace((unsigned char) tag[at - 1]);
		size_t after = at + nameLen;
		while (after < tag.size() && std::isspace((unsigned char) tag[after]))
			after++;
		if (leftOk && after < tag.size() && tag[after] == '=') {
			size_t q = after + 1;
			while (q < tag.size() && std::isspace((unsigned char) tag[q]))
				q++;
			if (q < tag.size() && (tag[q] == '"' || tag[q] == '\'')) {
				char quote = tag[q];
				size_t endq = tag.find(quote, q + 1);
				if (endq == std::string::npos)
					return false;
				*out = tag.substr(q + 1, endq - q - 1);
				return true;
			}
		}
		at += nameLen;
	}
}

bool IntAttribute(const std::string& tag, const char* name, int* out) {
	std::string s;
	if (!Attribute(tag, name, &s))
		return false;
	// Some packers write fractional pixels. Truncating is what the game does.
	*out = (int) std::strtod(s.c_str(), nullptr);
	return true;
}

/** XML's five entities, which is all a sprite name can legally contain. Names
with an ampersand in them do exist. */
std::string Unescape(const std::string& in) {
	if (in.find('&') == std::string::npos)
		return in;
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); i++) {
		if (in[i] != '&') {
			out.push_back(in[i]);
			continue;
		}
		if (in.compare(i, 5, "&amp;") == 0) { out.push_back('&'); i += 4; }
		else if (in.compare(i, 4, "&lt;") == 0) { out.push_back('<'); i += 3; }
		else if (in.compare(i, 4, "&gt;") == 0) { out.push_back('>'); i += 3; }
		else if (in.compare(i, 6, "&quot;") == 0) { out.push_back('"'); i += 5; }
		else if (in.compare(i, 6, "&apos;") == 0) { out.push_back('\''); i += 5; }
		else out.push_back('&');
	}
	return out;
}

}  // namespace

bool AtlasParse(const std::string& xml, Atlas* out, std::string* err) {
	if (!out)
		return false;
	*out = Atlas();

	size_t at = 0;
	while (true) {
		size_t open = xml.find('<', at);
		if (open == std::string::npos)
			break;
		size_t close = xml.find('>', open);
		if (close == std::string::npos)
			break;
		std::string tag = xml.substr(open + 1, close - open - 1);
		at = close + 1;

		if (tag.compare(0, 12, "TextureAtlas") == 0) {
			std::string img;
			if (Attribute(tag, "imagePath", &img))
				out->imageName = Unescape(img);
			continue;
		}
		if (tag.compare(0, 10, "SubTexture") != 0)
			continue;

		AtlasFrame f;
		std::string name;
		if (Attribute(tag, "name", &name))
			f.name = Unescape(name);
		IntAttribute(tag, "x", &f.x);
		IntAttribute(tag, "y", &f.y);
		IntAttribute(tag, "width", &f.w);
		IntAttribute(tag, "height", &f.h);

		std::string rot;
		if (Attribute(tag, "rotated", &rot))
			f.rotated = (rot == "true" || rot == "1");

		// The four optional ones. Absent means the frame was never trimmed, so
		// the untrimmed box *is* the trimmed one — not zero, which would give a
		// frame of no size and a character that does not appear at all.
		if (!IntAttribute(tag, "frameX", &f.frameX))
			f.frameX = 0;
		if (!IntAttribute(tag, "frameY", &f.frameY))
			f.frameY = 0;
		if (!IntAttribute(tag, "frameWidth", &f.frameWidth) || f.frameWidth <= 0)
			f.frameWidth = f.rotated ? f.h : f.w;
		if (!IntAttribute(tag, "frameHeight", &f.frameHeight) || f.frameHeight <= 0)
			f.frameHeight = f.rotated ? f.w : f.h;

		if (f.w <= 0 || f.h <= 0)
			continue;  // an empty frame: nothing to draw and nothing to fix
		out->frames.push_back(std::move(f));
	}

	if (out->frames.empty()) {
		if (err)
			*err = "no <SubTexture> frames in the XML";
		return false;
	}
	return true;
}

bool AtlasParseFile(const std::string& path, Atlas* out, std::string* err) {
	std::string xml;
	if (!ReadWholeFile(path, &xml)) {
		if (err)
			*err = "could not open " + path;
		return false;
	}
	if (!AtlasParse(xml, out, err)) {
		if (err)
			*err = path + ": " + *err;
		return false;
	}
	return true;
}

}  // namespace fnf
