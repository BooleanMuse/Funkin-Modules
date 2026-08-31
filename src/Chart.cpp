#include "Chart.hpp"

#include <cmath>

namespace fnf {

const char* JudgementName(int j) {
	switch (j) {
		case kJudgeSick: return "SICK!!";
		case kJudgeGood: return "GOOD";
		case kJudgeBad: return "BAD";
		case kJudgeShit: return "SHIT";
		default: return "MISS";
	}
}

int JudgementScore(int j) {
	// The game's numbers.
	switch (j) {
		case kJudgeSick: return 350;
		case kJudgeGood: return 200;
		case kJudgeBad: return 100;
		case kJudgeShit: return 50;
		default: return -10;
	}
}

int JudgeOffset(float offsetSeconds, float windowScale) {
	if (windowScale <= 0.f)
		windowScale = 1.f;
	float d = std::fabs(offsetSeconds);
	if (d > kSafeZone * windowScale)
		return kJudgeMiss;
	if (d > kBadWindow * windowScale)
		return kJudgeShit;
	if (d > kGoodWindow * windowScale)
		return kJudgeBad;
	if (d > kSickWindow * windowScale)
		return kJudgeGood;
	return kJudgeSick;
}

void Highway::reset() {
	head = tail = 0;
	eventHead = eventTail = 0;
	score = 0;
	combo = 0;
	maxCombo = 0;
	hits = 0;
	misses = 0;
	for (int i = 0; i < kJudgeCount; i++)
		judgeCount[i] = 0;
	health = 1.f;
	dead = false;
	for (int d = 0; d < kDirCount; d++) {
		keyDown[d] = false;
		holding[d] = -1;
	}
	// `now` is deliberately left alone: resetting the score should not teleport
	// the clock out from under the notes that are still on their way down.
}

void Highway::pushEvent(const HighwayEvent& e) {
	if (eventTail - eventHead >= kMaxEvents) {
		// Full means nobody has read them for a while — the module reads every
		// process() call, so this is a stall, not a burst. Drop the oldest: what
		// just happened matters more than what happened before the stall.
		eventHead++;
	}
	events[(unsigned) eventTail % kMaxEvents] = e;
	eventTail++;
}

bool Highway::popEvent(HighwayEvent* out) {
	if (eventHead >= eventTail)
		return false;
	if (out)
		*out = events[(unsigned) eventHead % kMaxEvents];
	eventHead++;
	return true;
}

void Highway::chartOn(int dir) {
	if (dir < 0 || dir >= kDirCount || freestyle)
		return;
	if (tail - head >= kMaxNotes) {
		// The lane is full: something is sending gates far faster than they can
		// be played. Dropping the *newest* keeps the notes you are about to have
		// to hit, which is the only sane half to keep.
		return;
	}
	Note n;
	n.time = now + lead;
	n.dir = dir;
	n.growing = true;
	note(tail) = n;
	tail++;
}

void Highway::chartHold(int dir, float dt) {
	if (dir < 0 || dir >= kDirCount || dt <= 0.f)
		return;
	// The newest still-growing note in this lane. A sustain's length is not known
	// when the note is made — the gate is still up — so it is measured as it
	// arrives and the tail on screen grows with it.
	for (int i = tail - 1; i >= head; i--) {
		Note& n = note(i);
		if (n.dir == dir && n.growing) {
			n.length += dt;
			return;
		}
	}
}

void Highway::chartOff(int dir) {
	if (dir < 0 || dir >= kDirCount)
		return;
	for (int i = tail - 1; i >= head; i--) {
		Note& n = note(i);
		if (n.dir == dir && n.growing) {
			n.growing = false;
			return;
		}
	}
}

int Highway::bestNote(int dir) const {
	int best = -1;
	float bestDist = 1e9f;
	for (int i = head; i < tail; i++) {
		const Note& n = note(i);
		if (n.dir != dir || n.resolved())
			continue;
		float d = std::fabs(n.time - now);
		if (d < bestDist) {
			bestDist = d;
			best = i;
		}
	}
	return best;
}

int Highway::press(int dir) {
	if (dir < 0 || dir >= kDirCount)
		return -1;
	keyDown[dir] = true;

	if (freestyle) {
		// Nothing is being played *at* you, so there is nothing to get right or
		// wrong. The press is only a press, and the character sings it.
		HighwayEvent e;
		e.type = kEvFree;
		e.dir = dir;
		pushEvent(e);
		return -1;
	}

	int i = bestNote(dir);
	float offset = 0.f;
	int judge = kJudgeMiss;
	if (i >= 0) {
		offset = now - note(i).time;
		judge = JudgeOffset(offset, windowScale);
	}

	HighwayEvent e;
	e.dir = dir;
	e.offset = offset;

	if (judge == kJudgeMiss) {
		// Nothing under the press. With ghost tapping on that is free, and the
		// character sings it the way it sings anything else — a key that makes
		// the character *wince* every time you play a note that was not on the
		// chart would be unusable as an instrument.
		if (ghostTapping) {
			e.type = kEvFree;
			pushEvent(e);
			return -1;
		}
		e.type = kEvMiss;
		e.judge = kJudgeMiss;
		combo = 0;
		misses++;
		judgeCount[kJudgeMiss]++;
		score += JudgementScore(kJudgeMiss);
		health -= 0.0475f;
		pushEvent(e);
		if (health < 0.f)
			health = 0.f;
		dead = health <= 0.f;
		return kJudgeMiss;
	}

	Note& n = note(i);
	n.hit = true;
	n.judge = judge;
	n.held = n.length > 0.f;
	if (n.held)
		holding[dir] = i;

	hits++;
	judgeCount[judge]++;
	score += JudgementScore(judge);
	combo++;
	if (combo > maxCombo)
		maxCombo = combo;
	health += 0.023f;
	if (health > 2.f)
		health = 2.f;
	dead = false;

	e.type = kEvHit;
	e.judge = judge;
	pushEvent(e);
	return judge;
}

void Highway::release(int dir) {
	if (dir < 0 || dir >= kDirCount)
		return;
	keyDown[dir] = false;
	int i = holding[dir];
	holding[dir] = -1;
	if (i < head || i >= tail)
		return;
	Note& n = note(i);
	if (!n.held)
		return;
	n.held = false;
	// Letting go early is not punished. It costs the rest of the sustain, which
	// is a gate that stops early in the patch — audible, and enough.
	HighwayEvent e;
	e.type = kEvHold;
	e.dir = dir;
	e.judge = n.judge;
	pushEvent(e);
}

void Highway::advance(float dt) {
	if (dt <= 0.f)
		return;
	now += dt;
	if (freestyle) {
		// Notes already on their way down still have to finish falling — the
		// chart cable being pulled out mid-phrase should empty the road, not
		// freeze it — but nothing is missed and nothing is scored on the way.
		for (int i = head; i < tail; i++) {
			Note& n = note(i);
			if (!n.resolved())
				n.missed = true;
		}
		retire();
		return;
	}

	float late = kSafeZone * (windowScale > 0.f ? windowScale : 1.f);

	for (int i = head; i < tail; i++) {
		Note& n = note(i);
		if (n.resolved()) {
			// A sustain being held keeps the gate up and the mouth open.
			if (n.hit && n.held && now > n.time + n.length) {
				n.held = false;
				holding[n.dir] = -1;
				HighwayEvent e;
				e.type = kEvHold;
				e.dir = n.dir;
				e.judge = n.judge;
				pushEvent(e);
			}
			continue;
		}
		if (now > n.time + late) {
			n.missed = true;
			n.resolvedAt = now;
			combo = 0;
			misses++;
			judgeCount[kJudgeMiss]++;
			score += JudgementScore(kJudgeMiss);
			health -= 0.0475f;
			if (health < 0.f)
				health = 0.f;
			dead = health <= 0.f;

			HighwayEvent e;
			e.type = kEvMiss;
			e.dir = n.dir;
			e.judge = kJudgeMiss;
			e.offset = now - n.time;
			pushEvent(e);
		}
	}
	retire();
}

void Highway::retire() {
	// Only from the head, and only once a note has nothing left to do: it has
	// been resolved, its sustain is over, and it has been off the bottom of the
	// screen long enough to have been drawn dying.
	while (head < tail) {
		const Note& n = note(head);
		if (!n.resolved())
			break;
		if (n.held)
			break;
		if (now < n.time + n.length + 0.35f)
			break;
		head++;
	}
	if (head == tail) {
		// Empty: reset the indices while nothing can be pointing at them, so
		// they never run away over a long session.
		head = tail = 0;
		for (int d = 0; d < kDirCount; d++)
			holding[d] = -1;
	}
}

float Highway::accuracy() const {
	int total = hits + misses;
	if (total <= 0)
		return 1.f;
	// The game's weights: a sick is worth a whole note, a shit barely anything.
	static const float kWeight[kJudgeCount] = {1.f, 0.7f, 0.4f, 0.2f, 0.f};
	float got = 0.f;
	for (int j = 0; j < kJudgeCount; j++)
		got += kWeight[j] * (float) judgeCount[j];
	float acc = got / (float) total;
	return acc < 0.f ? 0.f : (acc > 1.f ? 1.f : acc);
}

}  // namespace fnf
