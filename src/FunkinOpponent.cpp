// ============================================================================
// FUNKIN' OPPONENT — the character the rack plays.
//
// Four gates go in and a character sings them. That is the whole module, and
// everything else follows from it: the gates can come from anywhere in the patch
// — a sequencer, a drum machine, a clock divider, a MIDI file — so anything that
// can make four gates can make this character perform.
//
// What comes out is the same four gates, on one polyphonic cable, labelled
// CHART. Patch that into the player module and what the opponent just sang
// becomes what you have to play back. That single cable is the game.
// ============================================================================
#include "FunkinShared.hpp"

using namespace funkin;

struct FunkinOpponent : Module, CharacterHost {
	enum ParamId {
		BPM_PARAM,
		HOLD_PARAM,
		SIZE_PARAM,
		PARAMS_LEN,
	};
	enum InputId {
		LEFT_INPUT,
		DOWN_INPUT,
		UP_INPUT,
		RIGHT_INPUT,
		NOTES_INPUT,
		BEAT_INPUT,
		INPUTS_LEN,
	};
	enum OutputId {
		CHART_OUTPUT,
		SING_OUTPUT,
		BEAT_OUTPUT,
		OUTPUTS_LEN,
	};
	enum LightId {
		ENUMS(LANE_LIGHT, 4),
		LIGHTS_LEN,
	};

	// -- the audio thread's ---------------------------------------------------
	BeatClock clock;
	SingRing ring;
	dsp::SchmittTrigger laneTrigger[fnf::kDirCount];
	bool laneHigh[fnf::kDirCount] = {false, false, false, false};
	/** Seconds of sing pose still owed, per lane. This is what SING is made of
	and what the lights show, and it is kept here rather than read back off the
	graphics thread so that the connectors and the panel cannot disagree. */
	float laneHold[fnf::kDirCount] = {0.f, 0.f, 0.f, 0.f};
	float refresh[fnf::kDirCount] = {0.f, 0.f, 0.f, 0.f};
	dsp::PulseGenerator beatPulse;

	// -- the graphics thread's ------------------------------------------------
	CharacterSlot slot;
	float posX = 0.f, posY = 0.f;
	bool pinned = false;
	bool mirror = false;
	bool showName = false;
	float characterHeight = RACK_GRID_HEIGHT;
	/** What the patch asked for, before the library had been scanned. */
	std::string wantedKey;
	bool wantedPending = false;
	/** Set once somebody — the user, or a patch — has said who to be. Until
	then the module picks a real character out of the library rather than
	sitting there in the built-in dummy. */
	bool autoPicked = false;

	FunkinOpponent() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BPM_PARAM, 40.f, 240.f, 100.f, "Tempo", " BPM");
		// The character file gives the hold in steps; this scales it. At 1 the
		// character holds a pose for exactly as long as it does in the game.
		configParam(HOLD_PARAM, 0.25f, 4.f, 1.f, "Sing hold", "x");
		configParam(SIZE_PARAM, 0.15f, 3.f, 0.8f, "Size in the rack", " racks tall");

		for (int i = 0; i < fnf::kDirCount; i++) {
			configInput(LEFT_INPUT + i,
			            std::string(fnf::DirectionName(i)) + " note");
		}
		configInput(NOTES_INPUT, "Notes (polyphonic: left, down, up, right)");
		configInput(BEAT_INPUT, "Beat clock");
		configOutput(CHART_OUTPUT, "Chart (polyphonic: left, down, up, right)");
		configOutput(SING_OUTPUT, "Singing gate");
		configOutput(BEAT_OUTPUT, "Beat");

		configLight(LANE_LIGHT + 0, "Left");
		configLight(LANE_LIGHT + 1, "Down");
		configLight(LANE_LIGHT + 2, "Up");
		configLight(LANE_LIGHT + 3, "Right");

	}

	CharacterSlot* characterSlot() override { return &slot; }

	/** The gate for one lane: its own jack, or its channel of the polyphonic
	one. Either works and both together work, because a patch that uses four mono
	cables and a patch that uses one poly cable are both normal, and being made
	to choose would mean a merge module for no reason. */
	float laneVoltage(int dir) {
		float v = inputs[LEFT_INPUT + dir].getVoltage();
		if (inputs[NOTES_INPUT].getChannels() > dir)
			v = std::max(v, inputs[NOTES_INPUT].getVoltage(dir));
		return v;
	}

	bool laneWired(int dir) {
		return inputs[LEFT_INPUT + dir].isConnected() ||
		       inputs[NOTES_INPUT].getChannels() > dir;
	}

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;
		float bpm = params[BPM_PARAM].getValue();
		characterHeight = params[SIZE_PARAM].getValue() * RACK_GRID_HEIGHT;

		if (clock.process(dt, bpm, inputs[BEAT_INPUT].isConnected(),
		                  inputs[BEAT_INPUT].getVoltage())) {
			ring.push(SingRing::kBeat, 0, 0.f);
			beatPulse.trigger(1e-3f);
		}

		// The hold, in seconds, at this tempo. Read once a sample rather than
		// captured at the trigger, so that turning the tempo down lengthens the
		// pose you are already holding instead of only the next one.
		float hold = slot.singSeconds(bpm, params[HOLD_PARAM].getValue());

		outputs[CHART_OUTPUT].setChannels(4);
		bool anySinging = false;

		for (int dir = 0; dir < fnf::kDirCount; dir++) {
			float v = laneVoltage(dir);
			bool wasHigh = laneHigh[dir];
			// The trigger's own return value is the rising edge; what is wanted
			// here is the *state*, because a note that is still held down is
			// still being sung and has to keep its gate up.
			laneTrigger[dir].process(v, 0.1f, 1.f);
			laneHigh[dir] = laneTrigger[dir].isHigh();

			if (laneHigh[dir] && !wasHigh) {
				ring.push(SingRing::kSing, (int8_t) dir, hold);
				laneHold[dir] = hold;
				refresh[dir] = hold * 0.5f;
			}
			else if (laneHigh[dir]) {
				// A note held down is a note still being sung. The pose is
				// renewed rather than left to expire, which is what the game
				// does with a sustain — and half a hold apart is often enough
				// to be seamless without filling the ring.
				laneHold[dir] = hold;
				refresh[dir] -= dt;
				if (refresh[dir] <= 0.f) {
					ring.push(SingRing::kSing, (int8_t) dir, hold);
					refresh[dir] = hold * 0.5f;
				}
			}
			else if (wasHigh) {
				// Let go: the hold starts counting from here, not from when the
				// note began.
				ring.push(SingRing::kSing, (int8_t) dir, hold);
				laneHold[dir] = hold;
			}
			else if (laneHold[dir] > 0.f) {
				laneHold[dir] -= dt;
				if (laneHold[dir] < 0.f)
					laneHold[dir] = 0.f;
			}

			// CHART carries the gate as it came in, not a fixed pulse: the
			// length of the note is part of the note, and it is what becomes a
			// sustain on the player's road.
			outputs[CHART_OUTPUT].setVoltage(laneHigh[dir] ? 10.f : 0.f, dir);
			if (laneHold[dir] > 0.f)
				anySinging = true;

			float lit = laneHigh[dir] ? 1.f : (hold > 0.f ? laneHold[dir] / hold : 0.f);
			lights[LANE_LIGHT + dir].setBrightnessSmooth(lit, args.sampleTime * 8.f);
		}

		outputs[SING_OUTPUT].setVoltage(anySinging ? 10.f : 0.f);
		outputs[BEAT_OUTPUT].setVoltage(beatPulse.process(dt) ? 10.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "character", json_string(slot.key.c_str()));
		json_object_set_new(root, "posX", json_real(posX));
		json_object_set_new(root, "posY", json_real(posY));
		json_object_set_new(root, "pinned", json_boolean(pinned));
		json_object_set_new(root, "mirror", json_boolean(mirror));
		json_object_set_new(root, "showName", json_boolean(showName));
		// The mod folders travel with the patch. Opening it on another machine
		// will not find them, and the module says so rather than pretending the
		// character never existed.
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
		if (json_t* folders = json_object_get(root, "folders")) {
			size_t i;
			json_t* v;
			json_array_foreach(folders, i, v) {
				if (json_is_string(v))
					hub().addFolder(json_string_value(v));
			}
		}
		// The patch has an opinion, even if that opinion is the built-in dummy.
		autoPicked = true;
		if (json_t* j = json_object_get(root, "character")) {
			// Not loaded here: the library may not have been scanned yet, and a
			// patch that loads before the scan finishes would fall back to the
			// dummy for good. The widget asks again once the shelves are full.
			wantedKey = json_string_value(j) ? json_string_value(j) : "";
			wantedPending = true;
		}
	}
};

// ---------------------------------------------------------------------------

struct OpponentPanel : widget::Widget {
	FunkinOpponent* module = nullptr;

	void onContextDestroy(const ContextDestroyEvent& e) override {
		if (module)
			module->slot.forgetIcon();
		Widget::onContextDestroy(e);
	}

	void draw(const DrawArgs& args) override {
		Fonts fonts = PanelFonts();

		OpponentPanelInfo info;
		char status[192];
		std::string name, mod;

		if (module) {
			module->slot.ensureIcon(args.vg);
			CharacterCardText(module->slot, &name, &mod);
			module->slot.fillCard(&info.card, name, mod);

			for (int i = 0; i < 4; i++) {
				info.laneLit[i] = module->lights[FunkinOpponent::LANE_LIGHT + i].getBrightness();
				info.laneWired[i] = module->laneWired(i);
			}
			info.externalClock = module->clock.external;
			info.beatPhase = module->clock.phase;
			info.bpm = module->params[FunkinOpponent::BPM_PARAM].getValue();

			if (!module->slot.error.empty()) {
				std::snprintf(status, sizeof(status), "%s", module->slot.error.c_str());
				info.statusBad = true;
			}
			else if (hub().scanning()) {
				std::snprintf(status, sizeof(status), "looking for characters...");
			}
			else if (hub().lib.entries.empty()) {
				std::snprintf(status, sizeof(status),
				              "no mods yet - right-click to add a folder");
			}
			else {
				std::snprintf(status, sizeof(status), "%d characters in %d folder(s)",
				              (int) hub().lib.entries.size(), (int) hub().folders.size());
			}
			info.status = status;
		}
		else {
			// The module browser: no module behind the panel, so it shows what
			// one looks like rather than an empty box.
			info.card.name = "dad";
			info.card.mod = "drop a Friday Night Funkin' mod in";
			info.laneLit[2] = 1.f;
			info.status = "four gates in, a character sings them";
		}

		DrawOpponentPanel(args.vg, fonts, lay::kPxPerMm, info);
		Widget::draw(args);
	}
};

struct FunkinOpponentWidget : ModuleWidget {
	CharacterOverlay* overlay = nullptr;
	FunkinOpponent* opp = nullptr;

	FunkinOpponentWidget(FunkinOpponent* module) {
		setModule(module);
		opp = module;
		box.size = Vec(lay::opp::kW * lay::kPxPerMm, RACK_GRID_HEIGHT);

		OpponentPanel* panel = new OpponentPanel;
		panel->module = module;
		panel->box.size = box.size;
		addChild(panel);

		// Screws at the bottom only: the title strip runs the full width of the
		// top and a screw in it looks like a mistake.
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - 15)));
		addChild(createWidget<ScrewSilver>(
		    Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - 15)));

		using namespace lay::opp;
		for (int i = 0; i < 4; i++) {
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(laneX(i), kLaneJackY)), module,
			                                         FunkinOpponent::LEFT_INPUT + i));
		}
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(knobX(0), kKnobY)), module,
		                                                  FunkinOpponent::BPM_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(knobX(1), kKnobY)), module,
		                                                  FunkinOpponent::HOLD_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(knobX(2), kKnobY)), module,
		                                                  FunkinOpponent::SIZE_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colX(1, 5), kInRowY)), module,
		                                         FunkinOpponent::NOTES_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colX(2, 5), kInRowY)), module,
		                                         FunkinOpponent::BEAT_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colX(1, 5), kOutRowY)), module,
		                                           FunkinOpponent::CHART_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colX(2, 5), kOutRowY)), module,
		                                           FunkinOpponent::SING_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colX(3, 5), kOutRowY)), module,
		                                           FunkinOpponent::BEAT_OUTPUT));

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
			overlay->sizeParamId = FunkinOpponent::SIZE_PARAM;
			overlay->sideSign = -1.f;  // the opponent stands to the left, as in the game
			overlay->menuBuilder = [this](ui::Menu* menu) { buildMenu(menu); };
			APP->scene->rack->addChild(overlay);
		}
	}

	~FunkinOpponentWidget() override {
		if (overlay && overlay->parent) {
			overlay->parent->removeChild(overlay);
			delete overlay;
		}
	}

	void step() override {
		ModuleWidget::step();
		if (!opp)
			return;
		StepCharacterHome(opp->slot, opp->ring, opp->mirror, opp->wantedKey,
		                  opp->wantedPending, opp->autoPicked,
		                  fnf::kRoleOpponent);
	}

	void buildMenu(ui::Menu* menu) {
		FunkinOpponent* m = opp;
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
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

Model* modelFunkinOpponent =
    createModel<FunkinOpponent, FunkinOpponentWidget>("FunkinOpponent");
