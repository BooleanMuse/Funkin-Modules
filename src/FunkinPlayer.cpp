// ============================================================================
// FUNKIN' PLAYER — the character you play.
//
// A chart comes in on one polyphonic cable, four arrows come down a road, and
// you hit them with a gamepad, the computer keyboard, a MIDI controller or four
// gates from the patch. What you hit goes back out as gates, so playing the game
// *is* playing the rack.
//
// The one thing worth understanding before reading further is the lead. Notes
// arrive from the opponent module at the moment it sings them, and a note you
// are told about at the instant it is due is a reflex test rather than a game.
// So a note that arrives now is due LEAD seconds from now, and spends that time
// coming down the road. Which means you are always answering the opponent one
// lead late — call and response, which is what Friday Night Funkin' is.
//
// The game runs in process(), sample by sample: the narrowest hit window in the
// game is 33 ms, and judging that on the graphics thread would judge it at
// whatever the frame rate happened to be. The animation runs on the graphics
// thread, where the frames are, and the two are joined by one ring.
// ============================================================================
#include "FunkinShared.hpp"

#include <set>

using namespace funkin;

// Control.hpp writes GLFW's key codes out by hand, because it has to compile
// with no Rack and no GLFW in sight. This is where the two meet.
static_assert(fnf::kKeyLeft == GLFW_KEY_LEFT && fnf::kKeyRight == GLFW_KEY_RIGHT &&
                  fnf::kKeyUp == GLFW_KEY_UP && fnf::kKeyDown == GLFW_KEY_DOWN &&
                  fnf::kKeySpace == GLFW_KEY_SPACE && fnf::kKeyEnter == GLFW_KEY_ENTER,
              "fnf::KeyCode has drifted from GLFW's key codes");

struct FunkinPlayer : Module, CharacterHost {
	enum ParamId {
		BPM_PARAM,
		LEAD_PARAM,
		WINDOW_PARAM,
		SIZE_PARAM,
		PARAMS_LEN,
	};
	enum InputId {
		CHART_INPUT,
		LEFT_INPUT,
		DOWN_INPUT,
		UP_INPUT,
		RIGHT_INPUT,
		BEAT_INPUT,
		INPUTS_LEN,
	};
	enum OutputId {
		KEYS_OUTPUT,
		HIT_OUTPUT,
		MISS_OUTPUT,
		HEALTH_OUTPUT,
		SING_OUTPUT,
		BEAT_OUTPUT,
		OUTPUTS_LEN,
	};
	enum LightId {
		ENUMS(LANE_LIGHT, 4),
		LIGHTS_LEN,
	};

	// -- the audio thread's ---------------------------------------------------
	midi::InputQueue midiInput;
	KeyRing keyRing;
	fnf::ArrowBindings bindings;
	fnf::Highway highway;
	BeatClock clock;
	SingRing ring;

	bool keyDown[fnf::kActCount] = {false, false, false, false, false};
	dsp::SchmittTrigger chartTrigger[fnf::kDirCount];
	bool chartHigh[fnf::kDirCount] = {false, false, false, false};
	dsp::SchmittTrigger cvTrigger[fnf::kDirCount];
	/** How long the hit gate for each lane still has to run. */
	float hitGate[fnf::kDirCount] = {0.f, 0.f, 0.f, 0.f};
	float singHold = 0.f;
	dsp::PulseGenerator missPulse;
	dsp::PulseGenerator beatPulse;

	/** Set by the panel, acted on by the audio thread. The bindings live where
	the notes are judged, so learning has to be asked for rather than done. */
	std::atomic<int> learnRequest{-2};
	std::atomic<int> learningNow{-1};
	std::atomic<bool> resetRequest{false};

	// -- the graphics thread's ------------------------------------------------
	CharacterSlot slot;
	float posX = 0.f, posY = 0.f;
	bool pinned = false;
	bool mirror = true;  // the player faces the other way: that is what makes it a duel
	/** Freestyle even when a chart is plugged in. Off by default, because with
	nothing patched the module is already in freestyle without being told. */
	bool forceFreestyle = false;
	bool showName = false;
	float characterHeight = RACK_GRID_HEIGHT;
	std::string wantedKey;
	bool wantedPending = false;
	/** Set once somebody — the user, or a patch — has said who to be. Until
	then the module picks a real character out of the library rather than
	sitting there in the built-in dummy. */
	bool autoPicked = false;
	/** The last judgement, for the panel to flash. */
	std::atomic<int> lastJudge{-1};
	std::atomic<uint32_t> judgeSerial{0};
	uint32_t seenJudgeSerial = 0;
	double judgeTime = -10.0;

	FunkinPlayer() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BPM_PARAM, 40.f, 240.f, 100.f, "Tempo", " BPM");
		// Half a second is about as short as a note can be seen coming; four
		// seconds is two bars at a hundred, which is a whole phrase of call and
		// response. Two is the middle and plays like the game.
		configParam(LEAD_PARAM, 0.4f, 4.f, 2.f, "Note lead", " s");
		configParam(WINDOW_PARAM, 0.5f, 3.f, 1.f, "Hit window", "x");
		configParam(SIZE_PARAM, 0.15f, 3.f, 0.8f, "Size in the rack", " racks tall");

		configInput(CHART_INPUT, "Chart (polyphonic: left, down, up, right)");
		for (int i = 0; i < fnf::kDirCount; i++)
			configInput(LEFT_INPUT + i, std::string(fnf::DirectionName(i)) + " key");
		configInput(BEAT_INPUT, "Beat clock");

		configOutput(KEYS_OUTPUT, "Keys played (polyphonic)");
		configOutput(HIT_OUTPUT, "Notes hit (polyphonic)");
		configOutput(MISS_OUTPUT, "Miss");
		configOutput(HEALTH_OUTPUT, "Health (0-10V)");
		configOutput(SING_OUTPUT, "Singing gate");
		configOutput(BEAT_OUTPUT, "Beat");

		configLight(LANE_LIGHT + 0, "Left");
		configLight(LANE_LIGHT + 1, "Down");
		configLight(LANE_LIGHT + 2, "Up");
		configLight(LANE_LIGHT + 3, "Right");

	}

	CharacterSlot* characterSlot() override { return &slot; }

	/** One MIDI message, reduced to the shape the bindings understand. A note-off
	is a note at zero, which is what lets one binding take a button, a key or a
	stick axis without knowing which it got. */
	static bool toArrowEvent(const midi::Message& msg, fnf::ArrowEvent* out) {
		int status = msg.getStatus();
		out->channel = msg.getChannel();
		switch (status) {
			case 0x9:  // note on
				out->type = fnf::kBindNote;
				out->number = msg.getNote();
				out->value = msg.getValue() / 127.f;
				return true;
			case 0x8:  // note off
				out->type = fnf::kBindNote;
				out->number = msg.getNote();
				out->value = 0.f;
				return true;
			case 0xb:  // control change
				out->type = fnf::kBindCC;
				out->number = msg.getNote();
				out->value = msg.getValue() / 127.f;
				return true;
			default:
				return false;
		}
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;
		float bpm = params[BPM_PARAM].getValue();
		characterHeight = params[SIZE_PARAM].getValue() * RACK_GRID_HEIGHT;

		// -- what you pressed -------------------------------------------------
		int wanted = learnRequest.exchange(-2);
		if (wanted != -2) {
			if (wanted >= 0) {
				bindings.startLearning(wanted);
				// Nothing may move while a slot is learning, or binding the key
				// that plays a note would mean playing the note.
				bindings.releaseAll();
			}
			else {
				bindings.startLearning(-1);
			}
		}

		// The computer keyboard, from the graphics thread. Drained before MIDI so
		// that a key and a note pressed in the same frame land in the order they
		// were pressed rather than in the order the sources are read.
		fnf::ArrowEvent keyEvent;
		while (keyRing.pop(&keyEvent))
			bindings.feed(keyEvent);

		midi::Message msg;
		// tryPop is called here because here is where Rack timestamps messages:
		// a MIDI note is placed at a sample, not at a frame.
		while (midiInput.tryPop(&msg, args.frame)) {
			fnf::ArrowEvent e;
			if (toArrowEvent(msg, &e))
				bindings.feed(e);
		}
		learningNow.store(bindings.learning, std::memory_order_relaxed);

		if (resetRequest.exchange(false))
			highway.reset();

		// -- the clock --------------------------------------------------------
		if (clock.process(dt, bpm, inputs[BEAT_INPUT].isConnected(),
		                  inputs[BEAT_INPUT].getVoltage())) {
			ring.push(SingRing::kBeat, 0, 0.f);
			beatPulse.trigger(1e-3f);
		}

		float hold = slot.singSeconds(bpm, 1.f);
		highway.lead = params[LEAD_PARAM].getValue();
		highway.windowScale = params[WINDOW_PARAM].getValue();
		// Freestyle is not a mode you have to find: with nothing patched into
		// CHART there is no chart, so there is nothing to score, and the module
		// is simply a character that sings what you play. The menu switch is for
		// the other case — a chart is coming in and you would rather jam over it
		// than be marked on it.
		highway.freestyle = forceFreestyle || !inputs[CHART_INPUT].isConnected();

		// -- the chart coming in ----------------------------------------------
		for (int dir = 0; dir < fnf::kDirCount; dir++) {
			float v = 0.f;
			if (inputs[CHART_INPUT].getChannels() > dir)
				v = inputs[CHART_INPUT].getVoltage(dir);
			bool wasHigh = chartHigh[dir];
			chartTrigger[dir].process(v, 0.1f, 1.f);
			chartHigh[dir] = chartTrigger[dir].isHigh();

			if (chartHigh[dir] && !wasHigh)
				highway.chartOn(dir);
			else if (chartHigh[dir])
				highway.chartHold(dir, dt);  // the gate is still up: a sustain
			else if (wasHigh)
				highway.chartOff(dir);
		}

		// -- the keys ---------------------------------------------------------
		bool learning = bindings.isLearning();
		for (int act = 0; act < fnf::kActCount; act++) {
			bool down = !learning && bindings.down(act);
			if (act < fnf::kDirCount && inputs[LEFT_INPUT + act].isConnected()) {
				// A gate from the patch plays the same arrow a button does, so a
				// sequencer can take a turn at the player's side.
				cvTrigger[act].process(inputs[LEFT_INPUT + act].getVoltage(), 0.1f, 1.f);
				down = down || cvTrigger[act].isHigh();
			}
			if (down == keyDown[act])
				continue;
			keyDown[act] = down;

			if (act == fnf::kActHey) {
				if (down)
					ring.push(SingRing::kHey, 0, 0.f);
				continue;
			}
			if (down) {
				int judge = highway.press(act);
				// -1 means there was nothing to judge. Flashing "MISS" at
				// somebody who is only playing would be a lie.
				if (judge >= 0) {
					lastJudge.store(judge, std::memory_order_relaxed);
					judgeSerial.fetch_add(1, std::memory_order_release);
				}
			}
			else {
				highway.release(act);
			}
		}

		highway.advance(dt);

		// -- what the game has to say ------------------------------------------
		fnf::HighwayEvent ev;
		while (highway.popEvent(&ev)) {
			switch (ev.type) {
				case fnf::kEvHit:
					ring.push(SingRing::kSing, (int8_t) ev.dir, hold);
					hitGate[ev.dir] = std::max(hitGate[ev.dir], hold);
					singHold = std::max(singHold, hold);
					break;
				case fnf::kEvFree:
					// Freestyle, or a free press with ghost tapping on. It sings
					// like any other note and costs nothing.
					ring.push(SingRing::kSing, (int8_t) ev.dir, hold);
					hitGate[ev.dir] = std::max(hitGate[ev.dir], hold);
					singHold = std::max(singHold, hold);
					break;
				case fnf::kEvMiss:
					// A dropped note still sings — the wincing kind. A key that
					// moved the character only when you got it right would feel
					// broken on every note you got wrong.
					ring.push(SingRing::kMiss, (int8_t) ev.dir, hold);
					singHold = std::max(singHold, hold);
					missPulse.trigger(1e-3f);
					break;
				case fnf::kEvHold:
					break;
				default:
					break;
			}
		}

		// A sustain being held keeps its gate up for as long as it lasts, which
		// is what turns a held note into a held note in the patch as well.
		for (int dir = 0; dir < fnf::kDirCount; dir++) {
			int i = highway.holding[dir];
			if (i >= highway.head && i < highway.tail) {
				const fnf::Note& n = highway.note(i);
				if (n.held) {
					float left = n.time + n.length - highway.now;
					if (left > hitGate[dir])
						hitGate[dir] = left;
					if (left > singHold)
						singHold = left;
				}
			}
			if (hitGate[dir] > 0.f) {
				hitGate[dir] -= dt;
				if (hitGate[dir] < 0.f)
					hitGate[dir] = 0.f;
			}
		}
		if (singHold > 0.f) {
			singHold -= dt;
			if (singHold < 0.f)
				singHold = 0.f;
		}

		// -- out ---------------------------------------------------------------
		outputs[KEYS_OUTPUT].setChannels(4);
		outputs[HIT_OUTPUT].setChannels(4);
		for (int dir = 0; dir < fnf::kDirCount; dir++) {
			outputs[KEYS_OUTPUT].setVoltage(keyDown[dir] ? 10.f : 0.f, dir);
			outputs[HIT_OUTPUT].setVoltage(hitGate[dir] > 0.f ? 10.f : 0.f, dir);
			float lit = keyDown[dir] ? 1.f : (hitGate[dir] > 0.f ? 0.6f : 0.f);
			lights[LANE_LIGHT + dir].setBrightnessSmooth(lit, dt * 12.f);
		}
		outputs[MISS_OUTPUT].setVoltage(missPulse.process(dt) ? 10.f : 0.f);
		outputs[HEALTH_OUTPUT].setVoltage(highway.health * 5.f);
		outputs[SING_OUTPUT].setVoltage(singHold > 0.f ? 10.f : 0.f);
		outputs[BEAT_OUTPUT].setVoltage(beatPulse.process(dt) ? 10.f : 0.f);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		resetRequest.store(true);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "character", json_string(slot.key.c_str()));
		json_object_set_new(root, "posX", json_real(posX));
		json_object_set_new(root, "posY", json_real(posY));
		json_object_set_new(root, "pinned", json_boolean(pinned));
		json_object_set_new(root, "mirror", json_boolean(mirror));
		json_object_set_new(root, "showName", json_boolean(showName));
		json_object_set_new(root, "ghostTapping", json_boolean(highway.ghostTapping));
		json_object_set_new(root, "freestyle", json_boolean(forceFreestyle));
		json_object_set_new(root, "midi", midiInput.toJson());

		// The bindings, because which button is which arrow depends on the
		// controller and re-teaching five of them every time a patch opens is
		// not something anybody would put up with.
		json_t* binds = json_array();
		for (int i = 0; i < fnf::kActCount; i++) {
			json_t* b = json_object();
			json_object_set_new(b, "type", json_integer(bindings.slot[i].type));
			json_object_set_new(b, "number", json_integer(bindings.slot[i].number));
			json_object_set_new(b, "channel", json_integer(bindings.slot[i].channel));
			json_array_append_new(binds, b);
		}
		json_object_set_new(root, "bindings", binds);

		json_t* folders = json_array();
		for (const std::string& f : hub().folders)
			json_array_append_new(folders, json_string(f.c_str()));
		json_object_set_new(root, "folders", folders);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (!root)
			return;
		if (json_t* j = json_object_get(root, "posX"))
			posX = (float) json_number_value(j);
		if (json_t* j = json_object_get(root, "posY"))
			posY = (float) json_number_value(j);
		if (json_t* j = json_object_get(root, "pinned"))
			pinned = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "mirror"))
			mirror = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "showName"))
			showName = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "ghostTapping"))
			highway.ghostTapping = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "freestyle"))
			forceFreestyle = json_boolean_value(j);
		if (json_t* j = json_object_get(root, "midi"))
			midiInput.fromJson(j);

		if (json_t* binds = json_object_get(root, "bindings")) {
			size_t i;
			json_t* v;
			json_array_foreach(binds, i, v) {
				if (i >= (size_t) fnf::kActCount)
					break;
				json_t* t = json_object_get(v, "type");
				json_t* n = json_object_get(v, "number");
				json_t* c = json_object_get(v, "channel");
				if (!t || !n)
					continue;
				bindings.slot[i].type = (int) json_integer_value(t);
				bindings.slot[i].number = (int) json_integer_value(n);
				bindings.slot[i].channel = c ? (int) json_integer_value(c) : -1;
			}
			bindings.releaseAll();
		}
		if (json_t* folders = json_object_get(root, "folders")) {
			size_t i;
			json_t* v;
			json_array_foreach(folders, i, v) {
				if (json_is_string(v))
					hub().addFolder(json_string_value(v));
			}
		}
		if (json_t* j = json_object_get(root, "character")) {
			wantedKey = json_string_value(j) ? json_string_value(j) : "";
			// An empty name is what a patch from before there was a library
			// saved. That is not a decision, so it is left to be picked over.
			autoPicked = !wantedKey.empty();
			wantedPending = autoPicked;
		}
	}
};

// ---------------------------------------------------------------------------
// One row of the controls box. Clicking it teaches it.
// ---------------------------------------------------------------------------

struct LearnRow : widget::OpaqueWidget {
	FunkinPlayer* module = nullptr;
	int action = 0;

	void onButton(const event::Button& e) override {
		if (!module) {
			OpaqueWidget::onButton(e);
			return;
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			bool alreadyLearning = module->learningNow.load(std::memory_order_relaxed) == action;
			module->learnRequest.store(alreadyLearning ? -1 : action);
			return;
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			e.consume(this);
			ui::Menu* menu = createMenu();
			FunkinPlayer* m = module;
			int a = action;
			menu->addChild(createMenuLabel(fnf::ArrowActionName(a)));
			menu->addChild(createMenuItem("Learn what plays it", "", [m, a]() {
				m->learnRequest.store(a);
			}));
			menu->addChild(createMenuItem("Unbind", "", [m, a]() { m->bindings.clear(a); }));
			menu->addChild(new ui::MenuSeparator);
			menu->addChild(createMenuLabel("A gamepad or the computer keyboard"));
			menu->addChild(createMenuLabel("is a MIDI device in Rack: pick one"));
			menu->addChild(createMenuLabel("from the MIDI menu, then learn."));
			return;
		}
		OpaqueWidget::onButton(e);
	}

	// The row is drawn by the panel, with everything else. This widget is only
	// its hit box, which is why it draws nothing at all.
	void draw(const DrawArgs& args) override {}
};

// ---------------------------------------------------------------------------
// One arrow, in the menu, learning what plays it.
// ---------------------------------------------------------------------------

/** Click it and press the thing you want. The menu stays open while it waits,
which is the whole point: a learn that closed the menu first would leave you
pressing buttons at a module with nothing on screen telling you it is listening.

A pad button or a MIDI note is picked up the way it always is, by the audio
thread, which does not care what is on screen. A key on the computer keyboard is
not — Rack only hands those to whatever is hovered or selected — so this item
takes the keyboard while it waits and posts what you press down the same ring the
module reads. Either way the binding is made in one place, by the audio thread,
and there is no second copy of it to keep in step. */
struct LearnMenuItem : ui::MenuItem {
	FunkinPlayer* module = nullptr;
	int action = 0;
	bool waiting = false;

	~LearnMenuItem() override {
		// Closed without pressing anything: stop listening, or the panel would
		// sit saying "press it..." until something happened to be pressed.
		if (waiting && module)
			module->learnRequest.store(-1);
	}

	void onAction(const ActionEvent& e) override {
		if (!module)
			return;
		// Unconsumed on purpose: this is what keeps the menu open.
		e.unconsume();
		module->learnRequest.store(action);
		waiting = true;
		APP->event->setSelectedWidget(this);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (!waiting || !module) {
			MenuItem::onSelectKey(e);
			return;
		}
		if (e.action == GLFW_PRESS) {
			if (e.key == GLFW_KEY_ESCAPE) {
				module->learnRequest.store(-1);
				waiting = false;
				e.consume(this);
				return;
			}
			// Down and straight back up. The press is what gets bound; the
			// release is what stops the arrow being held from the moment it was
			// learned, which would sing forever.
			module->keyRing.push(e.key, 1.f);
			module->keyRing.push(e.key, 0.f);
			e.consume(this);
			return;
		}
		if (e.action == GLFW_RELEASE) {
			e.consume(this);
			return;
		}
		MenuItem::onSelectKey(e);
	}

	void step() override {
		if (module) {
			if (waiting && module->learningNow.load(std::memory_order_relaxed) != action)
				waiting = false;
			char buf[32];
			module->bindings.slot[action].describe(buf, sizeof(buf));
			text = fnf::ArrowActionName(action);
			rightText = waiting ? "press it..." : buf;
		}
		MenuItem::step();
	}
};

// ---------------------------------------------------------------------------

struct PlayerPanel : widget::Widget {
	FunkinPlayer* module = nullptr;

	void onContextDestroy(const ContextDestroyEvent& e) override {
		if (module)
			module->slot.forgetIcon();
		Widget::onContextDestroy(e);
	}

	void draw(const DrawArgs& args) override;
};

/** The opponent module next door, if there is one, and who it is wearing.

A cable carries gates, not a face, so standing next to one is the only way the
player module can know who it is up against — and knowing is what puts the right
icon on the other end of the health bar. Either side counts: which side of a duel
you stand on is a matter of taste, not of wiring. */
static CharacterSlot* AdjacentOpponent(Module* self) {
	if (!self)
		return nullptr;
	for (int side = 0; side < 2; side++) {
		Module* m = side ? self->rightExpander.module : self->leftExpander.module;
		// A few hops, and only through dancers. The one in the background stands
		// between the two of them — that is where you put it, and it is where
		// the game puts it — so it must not hide them from each other. Anything
		// else in the way is somebody else's module and ends the search.
		for (int hops = 0; m && hops < 4; hops++) {
			if (m->model == modelFunkinOpponent) {
				if (CharacterHost* host = dynamic_cast<CharacterHost*>(m))
					return host->characterSlot();
				return nullptr;
			}
			if (m->model != modelFunkinDancer)
				break;
			m = side ? m->rightExpander.module : m->leftExpander.module;
		}
	}
	return nullptr;
}

void PlayerPanel::draw(const DrawArgs& args) {
	Fonts fonts = PanelFonts();

	PlayerPanelInfo info;
	std::string name, mod, rivalName, rivalMod;
	char status[96];
	char binds[fnf::kActCount][32];
	PanelNote notes[fnf::Highway::kMaxNotes];

	if (module) {
		module->slot.ensureIcon(args.vg);
		CharacterCardText(module->slot, &name, &mod);
		module->slot.fillCard(&info.card, name, mod);

		// The other end of the health bar. Rack keeps one NanoVG context for the
		// whole window, so the neighbour's icon handle is readable from here —
		// and it is the neighbour's own, uploaded once for both panels.
		if (CharacterSlot* rival = AdjacentOpponent(module)) {
			rival->ensureIcon(args.vg);
			CharacterCardText(*rival, &rivalName, &rivalMod);
			rival->fillCard(&info.rival, rivalName, rivalMod);
		}

		const fnf::Highway& h = module->highway;
		info.health = h.health;
		info.score = h.score;
		info.combo = h.combo;
		info.accuracy = h.accuracy();
		info.misses = h.misses;
		info.dead = h.dead;
		info.chartConnected = module->inputs[FunkinPlayer::CHART_INPUT].isConnected();
		info.freestyle = h.freestyle;
		info.externalClock = module->clock.external;
		info.beatPhase = module->clock.phase;
		info.learning = module->learningNow.load(std::memory_order_relaxed);

		for (int i = 0; i < 4; i++)
			info.laneLit[i] = module->lights[FunkinPlayer::LANE_LIGHT + i].getBrightness();
		for (int i = 0; i < fnf::kActCount; i++) {
			module->bindings.slot[i].describe(binds[i], sizeof(binds[i]));
			info.binding[i] = binds[i];
		}

		// The notes are read straight out of the audio thread's ring. They are
		// plain numbers in a fixed array that is never shuffled, so the worst a
		// half-written one can do is put an arrow a frame out of place — and the
		// alternative, a lock, would be a lock the audio thread has to wait for.
		float lead = h.lead > 0.f ? h.lead : 1.f;
		int count = 0;
		int head = h.head, tail = h.tail;
		for (int i = head; i < tail && count < fnf::Highway::kMaxNotes; i++) {
			const fnf::Note& n = h.note(i);
			float progress = 1.f - (n.time - h.now) / lead;
			if (progress < -0.05f || progress > 1.6f)
				continue;
			PanelNote& p = notes[count++];
			p.progress = progress;
			p.tail = n.length / lead;
			p.dir = n.dir;
			p.hit = n.hit;
			p.missed = n.missed;
			p.held = n.held;
		}
		info.notes = notes;
		info.noteCount = count;

		int judge = module->lastJudge.load(std::memory_order_relaxed);
		uint32_t serial = module->judgeSerial.load(std::memory_order_acquire);
		if (serial != module->seenJudgeSerial) {
			module->seenJudgeSerial = serial;
			module->judgeTime = system::getTime();
		}
		if (judge >= 0) {
			info.judgement = fnf::JudgementName(judge);
			info.judgementAge = (float) (system::getTime() - module->judgeTime);
		}

		if (h.freestyle)
			std::snprintf(status, sizeof(status), "FREESTYLE");
		else
			std::snprintf(status, sizeof(status), "LEAD %.1f s",
			              (double) module->params[FunkinPlayer::LEAD_PARAM].getValue());
		info.status = status;
	}
	else {
		info.card.name = "boyfriend";
		info.card.mod = "gamepad, keyboard or MIDI";
		static const char* kBind[5] = {"C4", "C#4", "D4", "D#4", "E4"};
		for (int i = 0; i < 5; i++)
			info.binding[i] = kBind[i];
		info.status = "FUNKIN'";
	}

	DrawPlayerPanel(args.vg, fonts, lay::kPxPerMm, info);
	Widget::draw(args);
}

struct FunkinPlayerWidget : ModuleWidget {
	CharacterOverlay* overlay = nullptr;
	FunkinPlayer* ply = nullptr;

	FunkinPlayerWidget(FunkinPlayer* module) {
		setModule(module);
		ply = module;
		box.size = Vec(lay::ply::kW * lay::kPxPerMm, RACK_GRID_HEIGHT);

		PlayerPanel* panel = new PlayerPanel;
		panel->module = module;
		panel->box.size = box.size;
		addChild(panel);

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - 15)));
		addChild(createWidget<ScrewSilver>(
		    Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - 15)));

		using namespace lay::ply;
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(knobX(0), kKnobY)), module,
		                                                  FunkinPlayer::BPM_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(knobX(1), kKnobY)), module,
		                                                  FunkinPlayer::LEAD_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(knobX(2), kKnobY)), module,
		                                                  FunkinPlayer::WINDOW_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(knobX(3), kKnobY)), module,
		                                                  FunkinPlayer::SIZE_PARAM));

		const int inIds[6] = {FunkinPlayer::CHART_INPUT, FunkinPlayer::LEFT_INPUT,
		                      FunkinPlayer::DOWN_INPUT, FunkinPlayer::UP_INPUT,
		                      FunkinPlayer::RIGHT_INPUT, FunkinPlayer::BEAT_INPUT};
		for (int i = 0; i < 6; i++) {
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colX(i, 6), kInRowY)), module,
			                                         inIds[i]));
		}
		const int outIds[6] = {FunkinPlayer::KEYS_OUTPUT, FunkinPlayer::HIT_OUTPUT,
		                       FunkinPlayer::MISS_OUTPUT, FunkinPlayer::HEALTH_OUTPUT,
		                       FunkinPlayer::SING_OUTPUT, FunkinPlayer::BEAT_OUTPUT};
		for (int i = 0; i < 6; i++) {
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colX(i, 6), kOutRowY)), module,
			                                           outIds[i]));
		}

		// The five rows of the controls box are click targets over a drawing the
		// panel does. Keeping the drawing in the panel is what lets the mockup
		// show them; keeping the hit boxes here is what makes them clickable.
		for (int i = 0; i < fnf::kActCount; i++) {
			LearnRow* row = new LearnRow;
			row->module = module;
			row->action = i;
			row->box.pos = mm2px(Vec(kKeysX + 2.f, keyRowY(i)));
			row->box.size = mm2px(Vec(kKeysW - 4.f, kKeyRowH - 1.2f));
			addChild(row);
		}

		if (module) {
			overlay = new CharacterOverlay;
			overlay->module = module;
			overlay->slot = &module->slot;
			overlay->posX = &module->posX;
			overlay->posY = &module->posY;
			overlay->pinned = &module->pinned;
			overlay->height = &module->characterHeight;
			overlay->mirror = &module->mirror;
			overlay->showName = &module->showName;
			overlay->sizeParamId = FunkinPlayer::SIZE_PARAM;
			overlay->sideSign = 1.f;  // and the player to the right, facing them
			overlay->menuBuilder = [this](ui::Menu* menu) { buildMenu(menu); };
			APP->scene->rack->addChild(overlay);
		}
	}

	~FunkinPlayerWidget() override {
		if (overlay && overlay->parent) {
			overlay->parent->removeChild(overlay);
			delete overlay;
		}
	}

	/** Which key codes we have told the module are down, so that they can be let
	go of if the pointer leaves mid-press. Without this, walking the mouse off
	the module with an arrow held would hold that arrow forever. */
	std::set<int> keysDown;

	void sendKey(int key, int action) {
		if (!ply)
			return;
		if (action == GLFW_PRESS) {
			if (keysDown.insert(key).second)
				ply->keyRing.push(key, 1.f);
		}
		else if (action == GLFW_RELEASE) {
			if (keysDown.erase(key) > 0)
				ply->keyRing.push(key, 0.f);
		}
		// GLFW_REPEAT is deliberately ignored: a key held down is one press that
		// is still going, not a stream of new ones.
	}

	void releaseKeys() {
		if (!ply)
			return;
		for (int key : keysDown)
			ply->keyRing.push(key, 0.f);
		keysDown.clear();
	}

	/** The keyboard plays the module while the pointer is on it.

	Hovering rather than focus, which is Rack's own convention for a module you
	play with the keyboard, and which is what keeps the arrow keys working as the
	arrow keys everywhere else in the rack. The key is only taken if something is
	actually bound to it; anything else falls through to Rack. */
	void onHoverKey(const event::HoverKey& e) override {
		ModuleWidget::onHoverKey(e);
		if (e.isConsumed() || !ply)
			return;
		if (!ply->bindings.wantsKey(e.key) && !ply->bindings.isLearning())
			return;
		sendKey(e.key, e.action);
		e.consume(this);
	}

	void onSelectKey(const event::SelectKey& e) override {
		ModuleWidget::onSelectKey(e);
		if (e.isConsumed() || !ply)
			return;
		if (!ply->bindings.wantsKey(e.key) && !ply->bindings.isLearning())
			return;
		sendKey(e.key, e.action);
		e.consume(this);
	}

	void onLeave(const event::Leave& e) override {
		releaseKeys();
		ModuleWidget::onLeave(e);
	}

	void step() override {
		ModuleWidget::step();
		if (!ply)
			return;
		StepCharacterHome(ply->slot, ply->ring, ply->mirror, ply->wantedKey,
		                  ply->wantedPending, ply->autoPicked,
		                  fnf::kRolePlayer);
	}

	void buildMenu(ui::Menu* menu) {
		FunkinPlayer* m = ply;
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Controller"));
		// A gamepad and the computer keyboard are MIDI drivers in Rack, so all
		// three arrive here and this one menu covers the lot.
		app::appendMidiMenu(menu, &m->midiInput);

		// Learning, here as well as on the panel. A gamepad button is learned by
		// pressing it, and the place somebody looks for that is the menu.
		menu->addChild(createSubmenuItem("Learn a button", "", [m](ui::Menu* sub) {
			sub->addChild(createMenuLabel("Click an arrow, then press the key,"));
			sub->addChild(createMenuLabel("pad button or note you want on it."));
			for (int i = 0; i < fnf::kActCount; i++) {
				LearnMenuItem* item = new LearnMenuItem;
				item->module = m;
				item->action = i;
				sub->addChild(item);
			}
			sub->addChild(new ui::MenuSeparator);
			sub->addChild(createMenuItem("Bind the arrow keys", "", [m]() {
				m->bindings.defaults();
			}));
			sub->addChild(createMenuItem("Bind MIDI notes C4 upwards", "", [m]() {
				m->bindings.midiDefaults();
			}));
			sub->addChild(createMenuItem("Unbind everything", "", [m]() {
				for (int i = 0; i < fnf::kActCount; i++)
					m->bindings.clear(i);
			}));
		}));

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Game"));
		menu->addChild(createBoolPtrMenuItem(
		    "Freestyle - just play, nothing is scored", "", &m->forceFreestyle));
		menu->addChild(createBoolMenuItem(
		    "Ghost tapping - a press off the chart is free", "",
		    [m]() { return m->highway.ghostTapping; },
		    [m](bool v) { m->highway.ghostTapping = v; }));
		menu->addChild(createMenuItem("Reset the score", "", [m]() {
			m->resetRequest.store(true);
		}));

		AppendCharacterMenu(menu, &m->slot, [m](const std::string& key) {
			m->autoPicked = true;  // chosen by hand: never picked over
			m->slot.requestByKey(key);
			m->slot.error.clear();
		});

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("In the rack"));
		AppendAnimationMenu(menu, &m->slot);
		menu->addChild(createBoolPtrMenuItem("Mirror (and swap left/right)", "", &m->mirror));
		menu->addChild(createBoolPtrMenuItem("Show the name above them", "", &m->showName));
		menu->addChild(createMenuItem("Bring them back under the module", "", [m]() {
			m->pinned = false;
		}));
	}

	void appendContextMenu(ui::Menu* menu) override { buildMenu(menu); }
};

Model* modelFunkinPlayer = createModel<FunkinPlayer, FunkinPlayerWidget>("FunkinPlayer");
