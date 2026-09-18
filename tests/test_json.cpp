// deps: Json.cpp
// The JSON reader. Most of these are about the files being *wrong*: a character
// file is hand-edited by a modder in a text editor, and the reader exists to
// open the ones a strict parser would throw out.
#include "Json.hpp"
#include "check.hpp"

using namespace fnf;

static JsonValue Parse(const char* text, bool expectOk = true) {
	JsonValue v;
	std::string err;
	bool ok = JsonParse(text, &v, &err);
	if (ok != expectOk)
		std::printf("  (parse said %s: %s)\n", ok ? "ok" : "no", err.c_str());
	CHECK_EQ(ok, expectOk);
	return v;
}

int main() {
	SECTION("the shapes a character file is made of");
	{
		JsonValue v = Parse(R"({"image": "characters/BF", "scale": 6, "flip_x": true,
		                        "offsets": [-10, 4.5], "animations": []})");
		CHECK_EQ(v.get("image")->text(), std::string("characters/BF"));
		CHECK_EQ(v.get("scale")->num(1.0), 6.0);
		CHECK_EQ(v.get("flip_x")->flag(false), true);
		CHECK_EQ(v.get("offsets")->num(0, 0.0), -10.0);
		CHECK_NEAR(v.get("offsets")->num(1, 0.0), 4.5, 1e-9);
		CHECK_EQ(v.get("animations")->isArray(), true);
		CHECK_EQ(v.get("animations")->size(), (size_t) 0);
	}

	SECTION("a miss reads as a default rather than a crash");
	{
		JsonValue v = Parse(R"({"a": 1})");
		// The whole point of the null-object: three steps down a path that does
		// not exist has to be safe, because a character file that is laid out
		// differently is the normal case, not the broken one.
		CHECK_EQ(v.get("nope")->get("deeper")->get("deeper")->num(7.0), 7.0);
		CHECK_EQ(v.get("nope")->text("fallback"), std::string("fallback"));
		CHECK_EQ(v.get("a")->text("fallback"), std::string("fallback"));
	}

	SECTION("what modders leave in their files");
	{
		// A trailing comma, both kinds of comment, and a byte-order mark from a
		// Windows editor. None of it is legal JSON; all of it is common.
		JsonValue v = Parse("\xEF\xBB\xBF{\n"
		                    "  // the dad\n"
		                    "  \"sing_duration\": 6.1, /* steps */\n"
		                    "  \"animations\": [1, 2,],\n"
		                    "}");
		CHECK_NEAR(v.get("sing_duration")->num(0.0), 6.1, 1e-9);
		CHECK_EQ(v.get("animations")->size(), (size_t) 2);
	}

	SECTION("numbers and words where a bool belongs");
	{
		JsonValue v = Parse(R"({"a": 1, "b": 0, "c": "true", "d": false})");
		CHECK_EQ(v.get("a")->flag(false), true);
		CHECK_EQ(v.get("b")->flag(true), false);
		CHECK_EQ(v.get("c")->flag(false), true);
		CHECK_EQ(v.get("d")->flag(true), false);
	}

	SECTION("escapes, including a name with an accent in it");
	{
		JsonValue v = Parse(R"({"name": "Señor \"Dad\"\nline"})");
		CHECK_EQ(v.get("name")->text(), std::string("Se\xc3\xb1or \"Dad\"\nline"));
	}

	SECTION("broken files are refused, and say where");
	{
		std::string err;
		JsonValue v;
		CHECK_EQ(JsonParse("{\"a\": 1\n\"b\": 2}", &v, &err), false);
		// The message has to name a line: it ends up on the panel, and "invalid
		// JSON" would send the user looking through the whole file.
		CHECK(err.find("line 2") != std::string::npos);

		CHECK_EQ(JsonParse("", &v, &err), false);
		CHECK_EQ(JsonParse("{\"a\": }", &v, &err), false);
	}

	SECTION("a file deep enough to be an attack is refused, not followed");
	{
		std::string deep;
		for (int i = 0; i < 500; i++)
			deep += "[";
		std::string err;
		JsonValue v;
		CHECK_EQ(JsonParse(deep, &v, &err), false);
	}

	return check::summary("json");
}
