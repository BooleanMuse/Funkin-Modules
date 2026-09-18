// deps: Chart.cpp Character.cpp Json.cpp Sparrow.cpp
// The game. Everything here is timing, which is exactly what cannot be checked
// by looking at it: a hit window is a third of a frame wide at its narrowest.
#include "Chart.hpp"
#include "check.hpp"

using namespace fnf;

/** Runs the clock the way the module does: one sample at a time. Times measured
any other way are not the times the player gets. */
static void Run(Highway& h, float seconds, float rate = 48000.f) {
	int n = (int) (seconds * rate);
	for (int i = 0; i < n; i++)
		h.advance(1.f / rate);
}

static int CountEvents(Highway& h, int type) {
	int n = 0;
	HighwayEvent e;
	while (h.popEvent(&e)) {
		if (e.type == type)
			n++;
	}
	return n;
}

int main() {
	SECTION("the windows are the game's");
	{
		CHECK_EQ(JudgeOffset(0.f, 1.f), kJudgeSick);
		CHECK_EQ(JudgeOffset(0.02f, 1.f), kJudgeSick);
		CHECK_EQ(JudgeOffset(-0.02f, 1.f), kJudgeSick);  // early counts the same
		CHECK_EQ(JudgeOffset(0.05f, 1.f), kJudgeGood);
		CHECK_EQ(JudgeOffset(0.13f, 1.f), kJudgeBad);
		CHECK_EQ(JudgeOffset(0.16f, 1.f), kJudgeShit);
		CHECK_EQ(JudgeOffset(0.2f, 1.f), kJudgeMiss);
		// Widening the window is what makes the module playable with a pad on a
		// slow frame; it must widen every step, not just the outside one.
		CHECK_EQ(JudgeOffset(0.05f, 2.f), kJudgeSick);
		CHECK_EQ(JudgeOffset(0.2f, 2.f), kJudgeGood);
	}

	SECTION("a note arrives one lead before it is due");
	{
		Highway h;
		h.lead = 2.f;
		h.chartOn(kLeft);
		CHECK_EQ(h.liveCount(), 1);
		CHECK_NEAR(h.note(h.head).time, 2.f, 1e-4);
		CHECK_EQ(h.note(h.head).dir, kLeft);

		// Pressing the moment it appears is not a hit: it is two seconds early,
		// which is the entire point of the lead. It is not a miss either — with
		// ghost tapping on there was simply nothing to judge, which is -1.
		CHECK_EQ(h.press(kLeft), -1);
		h.release(kLeft);
		CHECK_EQ(h.combo, 0);
		CHECK_EQ(h.misses, 0);
	}

	SECTION("hitting it");
	{
		Highway h;
		h.lead = 1.f;
		h.chartOn(kDown);
		h.chartOff(kDown);
		Run(h, 1.f);
		CountEvents(h, kEvNone);  // clear

		CHECK_EQ(h.press(kDown), kJudgeSick);
		CHECK_EQ(h.combo, 1);
		CHECK_EQ(h.hits, 1);
		CHECK_EQ(h.misses, 0);
		CHECK_EQ(h.score, 350);
		CHECK(h.health > 1.f);
		HighwayEvent e;
		CHECK_EQ(h.popEvent(&e), true);
		CHECK_EQ(e.type, (int) kEvHit);
		CHECK_EQ(e.dir, (int) kDown);
		h.release(kDown);

		// And it cannot be hit twice: the second press has nothing under it.
		CHECK_EQ(h.press(kDown), -1);
		CHECK_EQ(h.hits, 1);
	}

	SECTION("letting one go past");
	{
		Highway h;
		h.lead = 0.5f;
		h.chartOn(kUp);
		h.chartOff(kUp);
		Run(h, 0.5f + kSafeZone + 0.01f);
		CHECK_EQ(h.misses, 1);
		CHECK_EQ(h.combo, 0);
		CHECK(h.health < 1.f);
		CHECK_EQ(CountEvents(h, kEvMiss), 1);
		// Once only, however long it is left.
		Run(h, 1.f);
		CHECK_EQ(h.misses, 1);
	}

	SECTION("a press on nothing: ghost tapping on and off");
	{
		Highway h;
		h.ghostTapping = true;
		CHECK_EQ(h.press(kRight), -1);
		CHECK_EQ(h.misses, 0);   // free
		CHECK_EQ(h.combo, 0);
		CHECK_NEAR(h.health, 1.f, 1e-6);
		// It still reports the press, because the character has to sing anyway:
		// a key that moves nothing feels broken. But it is a *free* press, not a
		// miss — the difference is whether the character sings the note or
		// winces at it, and wincing at every note you play off the chart would
		// make the module unusable as an instrument.
		CHECK_EQ(CountEvents(h, kEvFree), 1);
		CHECK_EQ(CountEvents(h, kEvMiss), 0);

		Highway strict;
		strict.ghostTapping = false;
		CHECK_EQ(strict.press(kRight), (int) kJudgeMiss);
		CHECK_EQ(strict.misses, 1);
		CHECK(strict.health < 1.f);
		CHECK_EQ(CountEvents(strict, kEvMiss), 1);
	}

	SECTION("freestyle: no chart, no score, just playing");
	{
		Highway h;
		h.freestyle = true;

		// Nothing arrives on the road at all, however hard the chart tries.
		h.chartOn(kLeft);
		h.chartOff(kLeft);
		CHECK_EQ(h.liveCount(), 0);

		for (int i = 0; i < 20; i++) {
			CHECK_EQ(h.press(i % kDirCount), -1);
			h.release(i % kDirCount);
			Run(h, 0.05f);
		}
		// Twenty presses and the scoreboard has not moved. That is the whole
		// point: it is an instrument, not a game you are being marked on.
		CHECK_EQ(h.misses, 0);
		CHECK_EQ(h.hits, 0);
		CHECK_EQ(h.score, 0);
		CHECK_NEAR(h.health, 1.f, 1e-6);
		CHECK_EQ(h.dead, false);
		CHECK_EQ(CountEvents(h, kEvFree), 20);
	}

	SECTION("pulling the chart cable out mid-phrase empties the road");
	{
		Highway h;
		h.lead = 1.f;
		for (int i = 0; i < 4; i++) {
			h.chartOn(i);
			h.chartOff(i);
			Run(h, 0.1f);
		}
		CHECK_EQ(h.liveCount(), 4);

		// The cable goes. Those four notes are still falling and must be allowed
		// to land — freezing them on the road would leave four arrows stuck
		// there for as long as the patch is open — but nobody is marked on them.
		h.freestyle = true;
		Run(h, 3.f);
		CHECK_EQ(h.liveCount(), 0);
		CHECK_EQ(h.misses, 0);
		CHECK_NEAR(h.health, 1.f, 1e-6);
	}

	SECTION("a combo, and what breaks it");
	{
		Highway h;
		h.lead = 0.2f;
		for (int i = 0; i < 5; i++) {
			h.chartOn(kLeft);
			h.chartOff(kLeft);
			Run(h, 0.2f);
			h.press(kLeft);
			h.release(kLeft);
			Run(h, 0.05f);
		}
		CHECK_EQ(h.combo, 5);
		CHECK_EQ(h.maxCombo, 5);
		CHECK_NEAR(h.accuracy(), 1.f, 1e-4);

		h.chartOn(kLeft);
		h.chartOff(kLeft);
		Run(h, 0.2f + kSafeZone + 0.01f);
		CHECK_EQ(h.combo, 0);
		CHECK_EQ(h.maxCombo, 5);
		CHECK(h.accuracy() < 1.f);
	}

	SECTION("the nearest note is the one you hit, not the earliest");
	{
		// Two notes a quarter of a second apart, and a press right on the second
		// one. Judging against the earliest would kill the first note *and* mark
		// the press as an early hit on it, losing both.
		Highway h;
		h.lead = 0.5f;
		h.chartOn(kUp);
		h.chartOff(kUp);
		Run(h, 0.25f);
		h.chartOn(kUp);
		h.chartOff(kUp);
		Run(h, 0.5f);  // now = 0.75, notes are due at 0.5 and 0.75

		CHECK_EQ(h.press(kUp), kJudgeSick);
		h.release(kUp);
		// The first is still unresolved and still hittable — it is 0.25 s late,
		// so it is outside the window and will be missed on its own.
		CHECK_EQ(h.hits, 1);
		int hitIndex = -1;
		for (int i = h.head; i < h.head + h.liveCount(); i++) {
			if (h.note(i).hit)
				hitIndex = i;
		}
		CHECK(hitIndex >= 0);
		if (hitIndex >= 0)
			CHECK_NEAR(h.note(hitIndex).time, 0.75f, 1e-4);
	}

	SECTION("a sustain is a gate that was held down");
	{
		Highway h;
		h.lead = 0.5f;
		h.chartOn(kRight);
		// The gate is still up: the tail grows as the chart arrives, because the
		// length is not knowable when the note is made.
		for (int i = 0; i < 100; i++)
			h.chartHold(kRight, 0.003f);
		h.chartOff(kRight);
		CHECK_NEAR(h.note(h.head).length, 0.3f, 1e-3);

		Run(h, 0.5f);
		CHECK_EQ(h.press(kRight), kJudgeSick);
		CHECK_EQ(h.note(h.head).held, true);
		Run(h, 0.1f);
		CHECK_EQ(h.note(h.head).held, true);  // still holding
		// Held to the end: the gate falls when the tail runs out, on its own.
		Run(h, 0.3f);
		CHECK_EQ(h.note(h.head).held, false);
		CHECK_EQ(CountEvents(h, kEvHold), 1);
	}

	SECTION("letting a sustain go early costs the rest of it and nothing more");
	{
		Highway h;
		h.lead = 0.2f;
		h.chartOn(kLeft);
		for (int i = 0; i < 100; i++)
			h.chartHold(kLeft, 0.005f);
		h.chartOff(kLeft);
		Run(h, 0.2f);
		h.press(kLeft);
		int comboWas = h.combo;
		Run(h, 0.1f);
		h.release(kLeft);
		CHECK_EQ(h.combo, comboWas);  // not punished
		CHECK_EQ(h.misses, 0);
	}

	SECTION("notes retire, and the ring never runs away");
	{
		Highway h;
		h.lead = 0.05f;
		for (int i = 0; i < 400; i++) {
			h.chartOn(i % kDirCount);
			h.chartOff(i % kDirCount);
			Run(h, 0.02f);
			CHECK(h.liveCount() <= Highway::kMaxNotes);
		}
		Run(h, 2.f);
		// Everything resolved and long past: the ring empties and the indices go
		// back to zero rather than climbing for the length of the session.
		CHECK_EQ(h.liveCount(), 0);
		CHECK_EQ(h.head, 0);
	}

	SECTION("health runs out, and the module keeps working anyway");
	{
		Highway h;
		h.lead = 0.05f;
		for (int i = 0; i < 40; i++) {
			h.chartOn(kUp);
			h.chartOff(kUp);
			Run(h, 0.05f + kSafeZone + 0.005f);
		}
		CHECK_EQ(h.dead, true);
		CHECK_NEAR(h.health, 0.f, 1e-6);
		// Dead is a thing the panel says, not a thing that stops the module:
		// silence in the middle of a patch is a fault, not a game over.
		h.chartOn(kUp);
		h.chartOff(kUp);
		Run(h, 0.05f);
		CHECK_EQ(h.press(kUp), kJudgeSick);
		CHECK_EQ(h.dead, false);
		CHECK(h.health > 0.f);
	}

	SECTION("resetting the score leaves the notes in the air alone");
	{
		Highway h;
		h.lead = 1.f;
		h.chartOn(kLeft);
		h.chartOff(kLeft);
		float when = h.note(h.head).time;
		Run(h, 0.3f);
		h.reset();
		CHECK_EQ(h.score, 0);
		CHECK_EQ(h.liveCount(), 0);
		// The clock is not rewound: a note still on its way down would otherwise
		// jump back to the top of the lane.
		CHECK(h.now > 0.29f);
		CHECK_NEAR(when, 1.f, 1e-4);
	}

	SECTION("steps and beats");
	{
		CHECK_NEAR(SecondsPerBeat(120.f), 0.5f, 1e-6);
		CHECK_NEAR(SecondsPerStep(120.f), 0.125f, 1e-6);
		// A character file says "sing_duration": 6.1, in steps. At 120 that is
		// three quarters of a second, and it has to follow the tempo.
		CHECK_NEAR(6.1f * SecondsPerStep(120.f), 0.7625f, 1e-5);
		CHECK_NEAR(6.1f * SecondsPerStep(240.f), 0.38125f, 1e-5);
		// A tempo of zero cannot divide by zero.
		CHECK(SecondsPerBeat(0.f) > 0.f);
	}

	return check::summary("chart");
}
