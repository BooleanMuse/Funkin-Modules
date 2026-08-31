#include "Json.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fnf {

const JsonValue* JsonValue::get(const char* key) const {
	static const JsonValue kEmpty;
	if (type != kObject || !key)
		return &kEmpty;
	for (const auto& m : members) {
		if (m.first == key)
			return &m.second;
	}
	return &kEmpty;
}

const JsonValue* JsonValue::at(size_t i) const {
	static const JsonValue kEmpty;
	if (type != kArray || i >= items.size())
		return &kEmpty;
	return &items[i];
}

bool JsonValue::flag(bool def) const {
	if (type == kBool)
		return boolean;
	if (type == kNumber)
		return number != 0.0;
	if (type == kString)
		return str == "true" || str == "1";
	return def;
}

namespace {

struct Parser {
	const char* p = nullptr;
	const char* end = nullptr;
	int line = 1;
	std::string err;

	bool fail(const char* what) {
		if (err.empty()) {
			char buf[160];
			std::snprintf(buf, sizeof(buf), "line %d: %s", line, what);
			err = buf;
		}
		return false;
	}

	void bump() {
		if (p < end && *p == '\n')
			line++;
		p++;
	}

	/** Whitespace, and the comments that hand-edited files pick up: both the
	line kind and the block kind. Neither is legal JSON and both are common. */
	void skip() {
		while (p < end) {
			if (std::isspace((unsigned char) *p)) {
				bump();
				continue;
			}
			if (*p == '/' && p + 1 < end && p[1] == '/') {
				while (p < end && *p != '\n')
					p++;
				continue;
			}
			if (*p == '/' && p + 1 < end && p[1] == '*') {
				p += 2;
				while (p < end && !(*p == '*' && p + 1 < end && p[1] == '/'))
					bump();
				if (p < end)
					p += 2;
				continue;
			}
			return;
		}
	}

	bool literal(const char* word) {
		size_t n = std::strlen(word);
		if ((size_t) (end - p) < n || std::strncmp(p, word, n) != 0)
			return false;
		p += n;
		return true;
	}

	bool parseString(std::string* out) {
		if (p >= end || *p != '"')
			return fail("expected a string");
		p++;
		out->clear();
		while (p < end && *p != '"') {
			if (*p != '\\') {
				if (*p == '\n')
					line++;
				out->push_back(*p++);
				continue;
			}
			p++;
			if (p >= end)
				return fail("the string never ends");
			char c = *p++;
			switch (c) {
				case 'n': out->push_back('\n'); break;
				case 't': out->push_back('\t'); break;
				case 'r': out->push_back('\r'); break;
				case 'b': out->push_back('\b'); break;
				case 'f': out->push_back('\f'); break;
				case 'u': {
					if (end - p < 4)
						return fail("a \\u escape is cut short");
					unsigned cp = 0;
					for (int i = 0; i < 4; i++) {
						char h = p[i];
						cp <<= 4;
						if (h >= '0' && h <= '9') cp |= (unsigned) (h - '0');
						else if (h >= 'a' && h <= 'f') cp |= (unsigned) (h - 'a' + 10);
						else if (h >= 'A' && h <= 'F') cp |= (unsigned) (h - 'A' + 10);
						else return fail("a \\u escape is not hexadecimal");
					}
					p += 4;
					// Straight to UTF-8. Surrogate pairs are left as the
					// replacement character rather than half-decoded: a
					// character name is not worth a second parser.
					if (cp < 0x80) {
						out->push_back((char) cp);
					}
					else if (cp < 0x800) {
						out->push_back((char) (0xC0 | (cp >> 6)));
						out->push_back((char) (0x80 | (cp & 0x3F)));
					}
					else if (cp >= 0xD800 && cp <= 0xDFFF) {
						out->append("\xEF\xBF\xBD");
					}
					else {
						out->push_back((char) (0xE0 | (cp >> 12)));
						out->push_back((char) (0x80 | ((cp >> 6) & 0x3F)));
						out->push_back((char) (0x80 | (cp & 0x3F)));
					}
					break;
				}
				default: out->push_back(c); break;  // \\ \/ \" and anything else
			}
		}
		if (p >= end)
			return fail("the string never ends");
		p++;
		return true;
	}

	bool parseValue(JsonValue* v, int depth) {
		// Deep enough for any real file, shallow enough that a malformed one
		// cannot walk the stack off the end.
		if (depth > 64)
			return fail("nested too deeply");
		skip();
		if (p >= end)
			return fail("the file ends where a value should be");

		switch (*p) {
			case '{': {
				v->type = JsonValue::kObject;
				p++;
				skip();
				if (p < end && *p == '}') {
					p++;
					return true;
				}
				while (true) {
					skip();
					// A trailing comma before the brace. Legal here, not in JSON.
					if (p < end && *p == '}') {
						p++;
						return true;
					}
					std::string key;
					if (!parseString(&key))
						return false;
					skip();
					if (p >= end || *p != ':')
						return fail("expected ':' after a name");
					p++;
					v->members.emplace_back(std::move(key), JsonValue());
					if (!parseValue(&v->members.back().second, depth + 1))
						return false;
					skip();
					if (p < end && *p == ',') {
						p++;
						continue;
					}
					if (p < end && *p == '}') {
						p++;
						return true;
					}
					return fail("expected ',' or '}'");
				}
			}
			case '[': {
				v->type = JsonValue::kArray;
				p++;
				skip();
				if (p < end && *p == ']') {
					p++;
					return true;
				}
				while (true) {
					skip();
					if (p < end && *p == ']') {
						p++;
						return true;
					}
					v->items.emplace_back();
					if (!parseValue(&v->items.back(), depth + 1))
						return false;
					skip();
					if (p < end && *p == ',') {
						p++;
						continue;
					}
					if (p < end && *p == ']') {
						p++;
						return true;
					}
					return fail("expected ',' or ']'");
				}
			}
			case '"':
				v->type = JsonValue::kString;
				return parseString(&v->str);
			default: break;
		}

		if (literal("true") || literal("True")) {
			v->type = JsonValue::kBool;
			v->boolean = true;
			return true;
		}
		if (literal("false") || literal("False")) {
			v->type = JsonValue::kBool;
			v->boolean = false;
			return true;
		}
		if (literal("null") || literal("None")) {
			v->type = JsonValue::kNull;
			return true;
		}

		char* stop = nullptr;
		double d = std::strtod(p, &stop);
		if (stop == p)
			return fail("expected a value");
		p = stop;
		v->type = JsonValue::kNumber;
		v->number = d;
		return true;
	}
};

}  // namespace

bool JsonParse(const std::string& text, JsonValue* out, std::string* err) {
	if (!out)
		return false;
	*out = JsonValue();

	Parser ps;
	ps.p = text.c_str();
	ps.end = text.c_str() + text.size();

	// A byte-order mark. Windows editors add one and it is not whitespace, so
	// without this the file fails on its very first character.
	if (text.size() >= 3 && (unsigned char) text[0] == 0xEF &&
	    (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF)
		ps.p += 3;

	if (!ps.parseValue(out, 0)) {
		if (err)
			*err = ps.err;
		*out = JsonValue();
		return false;
	}
	return true;
}

bool ReadWholeFile(const std::string& path, std::string* out) {
	if (!out)
		return false;
	std::FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out->clear();
	char buf[16384];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
		out->append(buf, n);
	bool ok = std::ferror(f) == 0;
	std::fclose(f);
	return ok;
}

bool JsonParseFile(const std::string& path, JsonValue* out, std::string* err) {
	std::string text;
	if (!ReadWholeFile(path, &text)) {
		if (err)
			*err = "could not open " + path;
		return false;
	}
	if (!JsonParse(text, out, err)) {
		if (err)
			*err = path + ": " + *err;
		return false;
	}
	return true;
}

}  // namespace fnf
