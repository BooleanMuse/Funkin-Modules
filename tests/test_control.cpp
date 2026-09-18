// deps: Character.cpp Json.cpp Sparrow.cpp
// What you press. A gamepad, the computer keyboard and a MIDI controller all
// arrive as the same messages, so all three are tested by sending messages.
#include "Control.hpp"
#include "check.hpp"

using namespace fnf;

static ArrowEvent Note(int number, float value, int channel = 0) {
	ArrowEvent e;
	e.type = kBindNote;
	e.number = number;
	e.value = value;
	e.channel = channel;
	return e;
}

static ArrowEvent Key(int code, float value) {
	ArrowEvent e;
	e.type = kBindKey;
	e.number = code;
	e.value = value;
	e.channel = -1;
	return e;
}

static ArrowEvent CC(int number, float value) {
	ArrowEvent e;
	e.type = kBindCC;
	e.number = number;
	e.value = value;
	return e;
}

int main() {
	SECTION("it plays with nothing set up at all");
	{
		// The arrow keys, which is what somebody who has played the game reaches
		// for. No driver to choose and nothing to learn first.
		ArrowBindings b;
		for (int i = 0; i < kActCount; i++)
			CHECK(b.slot[i].set());
		CHECK_EQ(b.slot[kActLeft].type, (int) kBindKey);
		CHECK_EQ(b.slot[kActLeft].number, (int) kKeyLeft);

		b.feed(Key(kKeyLeft, 1.f));
		CHECK_EQ(b.down(kActLeft), true);
		b.feed(Key(kKeyLeft, 0.f));
		CHECK_EQ(b.down(kActLeft), false);
		b.feed(Key(kKeyRight, 1.f));
		CHECK_EQ(b.down(kActRight), true);
		b.feed(Key(kKeySpace, 1.f));
		CHECK_EQ(b.down(kActHey), true);

		// Only our own keys. Every other key on the keyboard still belongs to
		// Rack, and a module that swallowed them all would break the host.
		CHECK_EQ(b.wantsKey(kKeyLeft), true);
		CHECK_EQ(b.wantsKey(kKeyEnter), false);
		CHECK_EQ(b.wantsKey('Q'), false);
	}

	SECTION("and it plays MIDI just as well, one menu item away");
	{
		ArrowBindings b;
		b.midiDefaults();
		CHECK_EQ(b.slot[kActLeft].type, (int) kBindNote);
		b.feed(Note(60, 1.f));
		CHECK_EQ(b.down(kActLeft), true);
		b.feed(Note(63, 1.f));
		CHECK_EQ(b.down(kActRight), true);
		// The arrow keys are no longer bound, so they are Rack's again.
		CHECK_EQ(b.wantsKey(kKeyLeft), false);
	}

	SECTION("a key and a note can share a set of arrows");
	{
		// Learn a note over one arrow and the other three stay on their keys:
		// nothing about a binding says the five have to be the same kind.
		ArrowBindings b;
		b.startLearning(kActUp);
		b.feed(Note(72, 1.f));
		CHECK_EQ(b.slot[kActUp].type, (int) kBindNote);
		CHECK_EQ(b.slot[kActLeft].type, (int) kBindKey);
		b.feed(Note(72, 1.f));
		b.feed(Key(kKeyLeft, 1.f));
		CHECK_EQ(b.down(kActUp), true);
		CHECK_EQ(b.down(kActLeft), true);
	}

	SECTION("the channel is not the user's business");
	{
		ArrowBindings b;
		b.midiDefaults();
		// A pad or a keyboard sends on whatever channel it likes, and being told
		// "your left arrow works but only on channel 1" is not something anybody
		// could act on.
		b.feed(Note(60, 1.f, 7));
		CHECK_EQ(b.down(kActLeft), true);
	}

	SECTION("learning takes the first thing you actually press");
	{
		ArrowBindings b;
		b.startLearning(kActUp);
		CHECK(b.isLearning());
		// A release is not a press: learning on a note-off would bind the button
		// you were letting go of, which is never the one you meant.
		b.feed(Note(42, 0.f));
		CHECK(b.isLearning());
		b.feed(Note(42, 1.f));
		CHECK(!b.isLearning());
		CHECK_EQ(b.slot[kActUp].number, 42);

		// And nothing moved while it was learning: you are not made to play the
		// note in order to bind the note.
		CHECK_EQ(b.down(kActUp), false);
		b.feed(Note(42, 1.f));
		CHECK_EQ(b.down(kActUp), true);
	}

	SECTION("one button cannot play two arrows");
	{
		ArrowBindings b;
		b.midiDefaults();
		b.startLearning(kActUp);
		b.feed(Note(60, 1.f));  // 60 was the left arrow
		CHECK_EQ(b.slot[kActUp].number, 60);
		CHECK_EQ(b.slot[kActLeft].set(), false);
		b.feed(Note(60, 1.f));
		CHECK_EQ(b.down(kActUp), true);
		CHECK_EQ(b.down(kActLeft), false);
	}

	SECTION("a stick resting on the line does not machine-gun");
	{
		ArrowBindings b;
		b.startLearning(kActRight);
		b.feed(CC(17, 1.f));
		CHECK_EQ(b.slot[kActRight].type, (int) kBindCC);

		b.feed(CC(17, 0.4f));
		CHECK_EQ(b.down(kActRight), true);
		// Between the two thresholds: still down. One threshold and an axis
		// sitting here would fire a note every sample.
		b.feed(CC(17, 0.3f));
		CHECK_EQ(b.down(kActRight), true);
		b.feed(CC(17, 0.24f));
		CHECK_EQ(b.down(kActRight), false);
		b.feed(CC(17, 0.3f));
		CHECK_EQ(b.down(kActRight), false);
		b.feed(CC(17, 0.36f));
		CHECK_EQ(b.down(kActRight), true);
	}

	SECTION("an unbound arrow is silent, not stuck");
	{
		ArrowBindings b;
		b.midiDefaults();
		b.clear(kActDown);
		CHECK_EQ(b.slot[kActDown].set(), false);
		b.feed(Note(61, 1.f));
		CHECK_EQ(b.down(kActDown), false);
	}

	SECTION("unplugging the controller lets go of everything");
	{
		ArrowBindings b;
		b.midiDefaults();
		b.feed(Note(60, 1.f));
		b.feed(Note(62, 1.f));
		CHECK_EQ(b.down(kActLeft), true);
		// Without this, an arrow that happened to be down when the device went
		// away is held forever, and the character sings until Rack is restarted.
		b.releaseAll();
		CHECK_EQ(b.down(kActLeft), false);
		CHECK_EQ(b.down(kActUp), false);
	}

	SECTION("the labels are in the user's terms");
	{
		char buf[32];
		Binding b;
		b.describe(buf, sizeof(buf));
		CHECK_EQ(std::string(buf), std::string("--"));
		b.type = kBindNote;
		b.number = 60;
		b.describe(buf, sizeof(buf));
		CHECK_EQ(std::string(buf), std::string("C4"));
		b.number = 61;
		b.describe(buf, sizeof(buf));
		CHECK_EQ(std::string(buf), std::string("C#4"));
		b.type = kBindCC;
		b.number = 17;
		b.describe(buf, sizeof(buf));
		CHECK_EQ(std::string(buf), std::string("CC 17"));

		// And a key says which key, not which number.
		b.type = kBindKey;
		b.number = kKeyLeft;
		b.describe(buf, sizeof(buf));
		CHECK_EQ(std::string(buf), std::string("Left"));
		b.number = kKeySpace;
		b.describe(buf, sizeof(buf));
		CHECK_EQ(std::string(buf), std::string("Space"));
		b.number = 'K';
		b.describe(buf, sizeof(buf));
		CHECK_EQ(std::string(buf), std::string("K"));
	}

	return check::summary("control");
}
