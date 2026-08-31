// ============================================================================
// FUNKIN' DANCER — the one in the background.
//
// In the game there is a third character who is not in the fight: she sits
// behind the two of them and keeps time, and the whole song is easier to feel
// because she is there. This is that character.
//
// It has no arrows and no game in it. It dances on the beat, and it hands the
// beat back out — as a trigger, as a bar marker, and as a falling envelope that
// is literally the shape of the bop. So the character in the background is not
// decoration bolted onto a clock: the clock is what she is doing.
// ============================================================================
#include "FunkinShared.hpp"

using namespace funkin;

struct FunkinDancer : Module, CharacterHost {
	enum ParamId {
		BPM_PARAM,
		SIZE_PARAM,
		PARAMS_LEN,
	};
	enum InputId {
		BEAT_INPUT,
		HEY_INPUT,
		INPUTS_LEN,
	};
	enum OutputId {
		BEAT_OUTPUT,
		BAR_OUTPUT,
		BOP_OUTPUT,
		OUTPUTS_LEN,
	};
	enum LightId {
		LIGHTS_LEN,
	};

	// -- the audio thread's ---------------------------------------------------
	BeatClock clock;
	SingRing ring;
	dsp::SchmittTrigger heyTrigger;
	dsp::PulseGenerator beatPulse;
	dsp::PulseGenerator barPulse;
	/** Which beat of the bar we are on. Four, because that is what a bar is in
	every song this plugin will ever be pointed at. */
	int beatInBar = 0;

	// -- the graphics thread's ------------------------------------------------
	CharacterSlot slot;
	float posX = 0.f, posY = 0.f;
	bool pinned = false;
	bool mirror = false;
	bool showName = false;
	float characterHeight = RACK_GRID_HEIGHT;
	std::string wantedKey;
	bool wantedPending = false;
	bool autoPicked = false;

	FunkinDancer() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(BPM_PARAM, 40.f, 240.f, 100.f, "Tempo", " BPM");
		configParam(SIZE_PARAM, 0.15f, 3.f, 0.8f, "Size in the rack", " racks tall");

		configInput(BEAT_INPUT, "Beat clock");
		configInput(HEY_INPUT, "Hey / cheer");
		configOutput(BEAT_OUTPUT, "Beat");
		configOutput(BAR_OUTPUT, "Bar (every fourth beat)");
		configOutput(BOP_OUTPUT, "Bop envelope (10V on the beat, falling)");

	}

	CharacterSlot* characterSlot() override { return &slot; }

	void process(const ProcessArgs& args) override {
		float dt = args.sampleTime;
		float bpm = params[BPM_PARAM].getValue();
		characterHeight = params[SIZE_PARAM].getValue() * RACK_GRID_HEIGHT;

		bool clockConnected = inputs[BEAT_INPUT].isConnected();
		if (clock.process(dt, bpm, clockConnected, inputs[BEAT_INPUT].getVoltage())) {
			ring.push(SingRing::kBeat, 0, 0.f);
			beatPulse.trigger(1e-3f);
			beatInBar = (beatInBar + 1) & 3;
			if (beatInBar == 0)
				barPulse.trigger(1e-3f);
		}

		if (heyTrigger.process(inputs[HEY_INPUT].getVoltage(), 0.1f, 1.f))
			ring.push(SingRing::kHey, 0, 0.f);

		outputs[BEAT_OUTPUT].setVoltage(beatPulse.process(dt) ? 10.f : 0.f);
		outputs[BAR_OUTPUT].setVoltage(barPulse.process(dt) ? 10.f : 0.f);
		// The bop: full on the beat, gone by the next one. The same number the
		// panel's dot is drawn with, so what you patch and what you see are the
		// same thing rather than two things that agree by coincidence.
		outputs[BOP_OUTPUT].setVoltage((1.f - clock.phase) * 10.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "character", json_string(slot.key.c_str()));
		json_object_set_new(root, "posX", json_real(posX));
		json_object_set_new(root, "posY", json_real(posY));
		json_object_set_new(root, "pinned", json_boolean(pinned));
		json_object_set_new(root, "mirror", json_boolean(mirror));
		json_object_set_new(root, "showName", json_boolean(showName));
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

struct DancerPanel : widget::Widget {
	FunkinDancer* module = nullptr;

	void onContextDestroy(const ContextDestroyEvent& e) override {
		if (module)
			module->slot.forgetIcon();
		Widget::onContextDestroy(e);
	}

	void draw(const DrawArgs& args) override {
		Fonts fonts = PanelFonts();

		DancerPanelInfo info;
		std::string name, mod;
		char status[96];

		if (module) {
			module->slot.ensureIcon(args.vg);
			CharacterCardText(module->slot, &name, &mod);
			module->slot.fillCard(&info.card, name, mod);

			info.beatInBar = module->beatInBar;
			info.beatPhase = module->clock.phase;
			info.externalClock = module->clock.external;
			std::snprintf(status, sizeof(status), "%s %.0f BPM",
			              module->clock.external ? "FOLLOWING" : "OWN",
			              (double) module->params[FunkinDancer::BPM_PARAM].getValue());
			info.status = status;
		}
		else {
			info.card.name = "girlfriend";
			info.card.mod = "keeps the beat, hands it back";
			info.status = "no arrows, just the beat";
		}

		DrawDancerPanel(args.vg, fonts, lay::kPxPerMm, info);
		Widget::draw(args);
	}
};

struct FunkinDancerWidget : ModuleWidget {
	CharacterOverlay* overlay = nullptr;
	FunkinDancer* dan = nullptr;

	FunkinDancerWidget(FunkinDancer* module) {
		setModule(module);
		dan = module;
		box.size = Vec(lay::dan::kW * lay::kPxPerMm, RACK_GRID_HEIGHT);

		DancerPanel* panel = new DancerPanel;
		panel->module = module;
		panel->box.size = box.size;
		addChild(panel);

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - 15)));
		addChild(createWidget<ScrewSilver>(
		    Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - 15)));

		using namespace lay::dan;
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(knobX(0), kKnobY)), module,
		                                                  FunkinDancer::BPM_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(knobX(1), kKnobY)), module,
		                                                  FunkinDancer::SIZE_PARAM));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colX(0, 2), kInRowY)), module,
		                                         FunkinDancer::BEAT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colX(1, 2), kInRowY)), module,
		                                         FunkinDancer::HEY_INPUT));

		const int outIds[3] = {FunkinDancer::BEAT_OUTPUT, FunkinDancer::BAR_OUTPUT,
		                       FunkinDancer::BOP_OUTPUT};
		for (int i = 0; i < 3; i++) {
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colX(i, 3), kOutRowY)), module,
			                                           outIds[i]));
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
			overlay->sizeParamId = FunkinDancer::SIZE_PARAM;
			// Straight up out of its own module rather than off to one side: the
			// one in the background stands *behind* the fight, not beside it, and
			// this module is narrow enough that a character next to it would be
			// three times its width away from anything it belongs to.
			overlay->sideSign = 0.f;
			overlay->menuBuilder = [this](ui::Menu* menu) { buildMenu(menu); };
			APP->scene->rack->addChild(overlay);
		}
	}

	~FunkinDancerWidget() override {
		if (overlay && overlay->parent) {
			overlay->parent->removeChild(overlay);
			delete overlay;
		}
	}

	void step() override {
		ModuleWidget::step();
		if (!dan)
			return;
		StepCharacterHome(dan->slot, dan->ring, dan->mirror, dan->wantedKey,
		                  dan->wantedPending, dan->autoPicked,
		                  fnf::kRoleDancer);
	}

	void buildMenu(ui::Menu* menu) {
		FunkinDancer* m = dan;
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
		menu->addChild(createMenuItem("Bring them back over the module", "", [m]() {
			m->pinned = false;
		}));
	}

	void appendContextMenu(ui::Menu* menu) override { buildMenu(menu); }
};

Model* modelFunkinDancer = createModel<FunkinDancer, FunkinDancerWidget>("FunkinDancer");
