//***********************************************************************************************
//Event modifier module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental plugins by Andrew Belt and graphics  
//  from the Component Library by Wes Milholen. 
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//***********************************************************************************************


#include "Geodesics.hpp"


struct Fate : Module {
	enum ParamIds {
		FREEWILL_PARAM,
		CHOICESDEPTH_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		FREEWILL_INPUT,// CV_value/10  is added to FREEWILL_PARAM, which is a 0 to 1 knob
		CLOCK_INPUT,// choice trigger
		ENUMS(MAIN_INPUTS, 2),
		CHOICSDEPTH_INPUT,// -10V to 10V range, /10 is added to CHOICESDEPTH_PARAM, which is a -1 to 1 knob
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(MAIN_OUTPUTS, 2),
		TRIGGER_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(TRIG_LIGHT, 2), // room for WhiteBlue (white is normal unaltered fate trigger, blue is for altered fate trigger)
		NUM_LIGHTS
	};
	
	
	// Constants
	// none
	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	int holdTrigOut;
	
	// No need to save, with reset
	bool alteredFate;
	float addCVs[2];
	
	// No need to save, no reset
	RefreshCounter refresh;
	Trigger clockTrigger;
	float trigLights[2] = {0.0f, 0.0f};// white, blue


	inline bool isAlteredFate() {return (random::uniform() < (params[FREEWILL_PARAM].getValue() + inputs[FREEWILL_INPUT].getVoltage() / 10.0f));}// randomUniform is [0.0, 1.0)

	
	Fate() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		
		configParam(Fate::FREEWILL_PARAM, 0.0f, 1.0f, 0.0f, "Free will");
		configParam(Fate::CHOICESDEPTH_PARAM, -1.0f, 1.0f, 0.0f, "Choices depth");

		onReset();

		panelTheme = (loadDarkAsDefault() ? 1 : 0);
	}

	
	void onReset() override {
		holdTrigOut = 0;
		resetNonJson(false);
	}
	void resetNonJson(bool recurseNonJson) {
		alteredFate = false;
		addCVs[0] = addCVs[1] = 0.0f;
	}


	void onRandomize() override {
	}
	

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// holdTrigOut
		json_object_set_new(rootJ, "holdTrigOut", json_integer(holdTrigOut));

		return rootJ;
	}

	
	void dataFromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);
		
		// holdTrigOut
		json_t *holdTrigOutJ = json_object_get(rootJ, "holdTrigOut");
		if (holdTrigOutJ)
			holdTrigOut = json_integer_value(holdTrigOutJ);
		
		resetNonJson(true);
	}

	void process(const ProcessArgs &args) override {		
		// user inputs
		//if (refresh.processInputs()) {
			// none
		//}// userInputs refresh
		
		
		// clock
		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage())) {
			if (isAlteredFate()) {
				alteredFate = true;
				float choiceDepth = params[CHOICESDEPTH_PARAM].getValue();
				if (inputs[CHOICSDEPTH_INPUT].isConnected()) {
					choiceDepth += inputs[CHOICSDEPTH_INPUT].getVoltage() / 10.0f;
				}
				addCVs[0] = (random::uniform() * 10.0f - 5.0f);
				addCVs[1] = (random::uniform() * 10.0f - 5.0f);
				if (choiceDepth < 0.0f) {
					addCVs[0] = std::fabs(addCVs[0]);// positive random offset only when knob is CCW
					addCVs[1] = -std::fabs(addCVs[1]);// negative random offset only when knob is CCW	
				}
				addCVs[0] *= clamp(std::fabs(choiceDepth), 0.0f, 1.0f);
				addCVs[1] *= clamp(std::fabs(choiceDepth), 0.0f, 1.0f);
				trigLights[1] = 1.0f;
			}
			else {
				alteredFate = false;
				addCVs[0] = addCVs[1] = 0.0f;
				trigLights[0] = 1.0f;
			}
		}
		
		// main outputs
		float port0input = inputs[MAIN_INPUTS + 0].isConnected() ? 
								inputs[MAIN_INPUTS + 0].getVoltage() : inputs[MAIN_INPUTS + 1].getVoltage();
		float port1input = inputs[MAIN_INPUTS + 1].isConnected() ? 
								inputs[MAIN_INPUTS + 1].getVoltage() : inputs[MAIN_INPUTS + 0].getVoltage();
		
		float chan0input = alteredFate ? port1input : port0input;
		outputs[MAIN_OUTPUTS + 0].setVoltage(chan0input + addCVs[0]);
		float chan1input = alteredFate ? port0input : port1input;
		outputs[MAIN_OUTPUTS + 1].setVoltage(chan1input + addCVs[1]);
		
		// trigger output
		bool trigOut = (alteredFate && (holdTrigOut != 0 || clockTrigger.isHigh())); 
		outputs[TRIGGER_OUTPUT].setVoltage(trigOut ? 10.0f : 0.0f);

		// lights
		if (refresh.processLights()) {
			float deltaTime = args.sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2);
			lights[TRIG_LIGHT + 0].setSmoothBrightness(trigLights[0], deltaTime);
			lights[TRIG_LIGHT + 1].setSmoothBrightness(trigLights[1], deltaTime);
			trigLights[0] = 0.0f;
			trigLights[1] = 0.0f;
		}// lightRefreshCounter
	}// step()
};


struct FateWidget : ModuleWidget {
	SvgPanel* darkPanel;

	struct PanelThemeItem : MenuItem {
		Fate *module;
		int theme;
		void onAction(const event::Action &e) override {
			module->panelTheme = theme;
		}
		void step() override {
			rightText = (module->panelTheme == theme) ? "✔" : "";
		}
	};	
	struct HoldTrigOutItem : MenuItem {
		Fate *module;
		void onAction(const event::Action &e) override {
			module->holdTrigOut ^= 0x1;
		}
	};	
	void appendContextMenu(Menu *menu) override {
		Fate *module = dynamic_cast<Fate*>(this->module);
		assert(module);

		menu->addChild(new MenuLabel());// empty space
		
		MenuLabel *themeLabel = new MenuLabel();
		themeLabel->text = "Panel Theme";
		menu->addChild(themeLabel);

		PanelThemeItem *lightItem = new PanelThemeItem();
		lightItem->text = lightPanelID;// Geodesics.hpp
		lightItem->module = module;
		lightItem->theme = 0;
		menu->addChild(lightItem);

		PanelThemeItem *darkItem = new PanelThemeItem();
		darkItem->text = darkPanelID;// Geodesics.hpp
		darkItem->module = module;
		darkItem->theme = 1;
		menu->addChild(darkItem);
		
		menu->addChild(createMenuItem<DarkDefaultItem>("Dark as default", CHECKMARK(loadDarkAsDefault())));
		
		menu->addChild(new MenuLabel());// empty space
	
		MenuLabel *settingsLabel = new MenuLabel();
		settingsLabel->text = "Settings";
		menu->addChild(settingsLabel);

		HoldTrigOutItem *htItem = createMenuItem<HoldTrigOutItem>("Hold trigger out during step", CHECKMARK(module->holdTrigOut != 0));
		htItem->module = module;
		menu->addChild(htItem);
	}	
	
	FateWidget(Fate *module) {
		setModule(module);

		// Main panels from Inkscape
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/Fate-WL.svg")));
        if (module) {
			darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/Fate-DM.svg")));
			darkPanel->visible = false;
			addChild(darkPanel);
		}

		// Screws 
		// part of svg panel, no code required
	
		float colRulerCenter = box.size.x / 2.0f;
		float offsetX = 20.0f;

		// free will knob and cv input
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter, 380 - 326), module, Fate::FREEWILL_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(Vec(colRulerCenter + offsetX, 380.0f - 287.5f), true, module, Fate::FREEWILL_INPUT, module ? &module->panelTheme : NULL));
		
		// choice trigger input and light (clock), and output
		addInput(createDynamicPort<GeoPort>(Vec(colRulerCenter - offsetX, 380.0f - 262.5f), true, module, Fate::CLOCK_INPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteBlueLight>>(Vec(colRulerCenter, 380.0f - 168.5f), module, Fate::TRIG_LIGHT));
		addOutput(createDynamicPort<GeoPort>(Vec(colRulerCenter + offsetX, 380.0f - 251.5f), false, module, Fate::TRIGGER_OUTPUT, module ? &module->panelTheme : NULL));

		// main outputs (left is old main output, right was non-existant before)
		addOutput(createDynamicPort<GeoPort>(Vec(colRulerCenter - offsetX, 380.0f - 223.5f), false, module, Fate::MAIN_OUTPUTS + 0, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(Vec(colRulerCenter + offsetX, 380.0f - 212.5f), false, module, Fate::MAIN_OUTPUTS + 1, module ? &module->panelTheme : NULL));
		
		// main inputs (left is old established order, right is old ex-machina)
		addInput(createDynamicPort<GeoPort>(Vec(colRulerCenter - offsetX, 380.0f - 130.5f), true, module, Fate::MAIN_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(Vec(colRulerCenter + offsetX, 380.0f - 119.5f), true, module, Fate::MAIN_INPUTS + 1, module ? &module->panelTheme : NULL));
		
		// choices depth knob and cv input
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter, 380.0f - 83.5f), module, Fate::CHOICESDEPTH_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(Vec(colRulerCenter, 380.0f - 45.5f), true, module, Fate::CHOICSDEPTH_INPUT, module ? &module->panelTheme : NULL));
	}
	
	void step() override {
		if (module) {
			panel->visible = ((((Fate*)module)->panelTheme) == 0);
			darkPanel->visible  = ((((Fate*)module)->panelTheme) == 1);
		}
		Widget::step();
	}
};

Model *modelFate = createModel<Fate, FateWidget>("Fate");

/*CHANGE LOG

*/