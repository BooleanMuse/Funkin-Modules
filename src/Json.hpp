#pragma once
// ============================================================================
// A small JSON reader, because a character file is JSON and this plugin has to
// read one before Rack is anywhere in the picture.
//
// Rack does ship jansson, but only to the plugin: the desktop tests compile
// straight from src/ with plain g++, and a character file that fails to load is
// exactly the thing those tests exist to catch. So the reader is ours, it has no
// dependencies, and it is the same code in both places.
//
// It is deliberately forgiving. These files are hand-edited by modders in a text
// editor, and a trailing comma or a // comment is common enough that refusing
// the whole character over one would look like the plugin is broken.
// ============================================================================
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fnf {

struct JsonValue {
	enum Type { kNull, kBool, kNumber, kString, kArray, kObject };

	Type type = kNull;
	bool boolean = false;
	double number = 0.0;
	std::string str;
	std::vector<JsonValue> items;                            // array
	std::vector<std::pair<std::string, JsonValue>> members;  // object

	bool isNull() const { return type == kNull; }
	bool isArray() const { return type == kArray; }
	bool isObject() const { return type == kObject; }

	/** How many items an array has. Anything else has none, so a caller can loop
	over a field that turned out to be a single number without checking first. */
	size_t size() const { return type == kArray ? items.size() : 0; }

	/** A member of an object, or null if it is not there. Never returns null the
	pointer: every miss lands on one shared empty value, so `v.get("a")->get("b")`
	is safe all the way down and only the last step needs a default. */
	const JsonValue* get(const char* key) const;
	const JsonValue* at(size_t i) const;

	double num(double def = 0.0) const { return type == kNumber ? number : def; }
	/** Array element as a number — for "offsets": [10, -4] and friends. */
	double num(size_t i, double def = 0.0) const { return at(i)->num(def); }
	std::string text(const char* def = "") const { return type == kString ? str : std::string(def); }
	/** Some mods write "flip_x": 1 rather than true, so a number counts. */
	bool flag(bool def = false) const;
};

/** Parses `text`. On failure `err` says what and where, in words a modder could
act on ("line 7: expected ',' or '}'"), because that message is going to end up
on the panel. */
bool JsonParse(const std::string& text, JsonValue* out, std::string* err);

/** Reads a file and parses it. Missing file and bad JSON are different messages:
one means the mod is laid out differently than we guessed, the other means the
file itself is broken, and telling them apart is the difference between the user
looking in the right place or the wrong one. */
bool JsonParseFile(const std::string& path, JsonValue* out, std::string* err);

/** The whole of a file as a string, or false. Shared with the XML reader. */
bool ReadWholeFile(const std::string& path, std::string* out);

}  // namespace fnf
