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
	bool alteredFate[PORT_MAX_CHANNELS];
	float addCVs0[PORT_MAX_CHANNELS];
	float addCVs1[PORT_MAX_CHANNELS];
	float sampledClock[PORT_MAX_CHANNELS];// introduce a 1-sample delay on clock input so that when receiving same clock as sequencer, Fate will sample the proper current CV from the seq.
	int numChan;
	
	// No need to save, no reset
	RefreshCounter refresh;
	Trigger clockTrigger[PORT_MAX_CHANNELS];
	float trigLightsWhite = 0.0f;
	float trigLightsBlue = 0.0f;


	Fate() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		
		configParam(Fate::FREEWILL_PARAM, 0.0f, 1.0f, 0.0f, "Free will");
		configParam(Fate::CHOICESDEPTH_PARAM, -1.0f, 1.0f, 0.0f, "Choices depth");
	
		configInput(FREEWILL_INPUT, "Free will");
		configInput(CLOCK_INPUT, "Clock (trigger)");
		configInput(MAIN_INPUTS + 0, "Event 1");
		configInput(MAIN_INPUTS + 1, "Event 2");
		configInput(CHOICSDEPTH_INPUT, "Choice depth");
		
		configOutput(MAIN_OUTPUTS + 0, "Event 1");
		configOutput(MAIN_OUTPUTS + 1, "Event 2");
		configOutput(TRIGGER_OUTPUT, "Trigger");

		onReset();

		panelTheme = (loadDarkAsDefault() ? 1 : 0);
	}

	
	void onReset() override {
		holdTrigOut = 0;
		resetNonJson(false);
	}
	void resetNonJson(bool recurseNonJson) {
		for (int i = 0; i < PORT_MAX_CHANNELS; i++) {
			alteredFate[i] = false;
			addCVs0[i] = 0.0f;
			addCVs1[i] = 0.0f;
			sampledClock[i] = 0.0f;
		}
		numChan = 0;
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
		int numClocks = inputs[CLOCK_INPUT].getChannels();
		int numChan0 = inputs[MAIN_INPUTS + 0].getChannels();
		int numChan1 = inputs[MAIN_INPUTS + 1].getChannels();	
		int numChan = std::max(numClocks, std::max(numChan0, numChan1));

		// user inputs
		if (refresh.processInputs()) {
			outputs[MAIN_OUTPUTS + 0].setChannels(numChan);
			outputs[MAIN_OUTPUTS + 1].setChannels(numChan);
			outputs[TRIGGER_OUTPUT].setChannels(numChan);			
		}// userInputs refresh
		
		
		// clock

		for (int c = 0; c < numChan; c++) {
			int clkIn = std::min(c, numClocks - 1);
			if (clockTrigger[c].process(sampledClock[clkIn])) {
				float freeWill = params[FREEWILL_PARAM].getValue();
				if (inputs[FREEWILL_INPUT].isConnected()) {
					int freeWillCvNumChan = inputs[FREEWILL_INPUT].getChannels();// >= 1 when connected
					freeWill += inputs[FREEWILL_INPUT].getVoltage(std::min(c, freeWillCvNumChan - 1)) / 10.0f;
				}
				alteredFate[c] = (random::uniform() < freeWill);// randomUniform is [0.0, 1.0)
				if (alteredFate[c]) {
					float choiceDepth = params[CHOICESDEPTH_PARAM].getValue();
					if (inputs[CHOICSDEPTH_INPUT].isConnected()) {
						int choiceDepthNumChan = inputs[CHOICSDEPTH_INPUT].getChannels();// >= 1 when connected
						choiceDepth += inputs[CHOICSDEPTH_INPUT].getVoltage(std::min(c, choiceDepthNumChan - 1)) / 10.0f;
					}
					addCVs0[c] = (random::uniform() * 10.0f - 5.0f);
					addCVs1[c] = (random::uniform() * 10.0f - 5.0f);
					if (choiceDepth < 0.0f) {
						addCVs0[c] = std::fabs(addCVs0[c]);// positive random offset only when knob is CCW
						addCVs1[c] = -std::fabs(addCVs1[c]);// negative random offset only when knob is CCW	
					}
					addCVs0[c] *= clamp(std::fabs(choiceDepth), 0.0f, 1.0f);
					addCVs1[c] *= clamp(std::fabs(choiceDepth), 0.0f, 1.0f);
					trigLightsBlue = 1.0f;
				}
				else {
					addCVs0[c] = 0.0f;
					addCVs1[c] = 0.0f;
					trigLightsWhite = 1.0f;
				}
			}
		}
		inputs[CLOCK_INPUT].readVoltages(sampledClock);

		
		// main outputs
		for (int c = 0; c < numChan; c++) {
			float port0input = inputs[MAIN_INPUTS + 0].isConnected() ? 
									inputs[MAIN_INPUTS + 0].getVoltage(c) : inputs[MAIN_INPUTS + 1].getVoltage(c);
			float port1input = inputs[MAIN_INPUTS + 1].isConnected() ? 
									inputs[MAIN_INPUTS + 1].getVoltage(c) : inputs[MAIN_INPUTS + 0].getVoltage(c);
			
			float chan0input = alteredFate[c] ? port1input : port0input;
			float chan1input = alteredFate[c] ? port0input : port1input;
			
			outputs[MAIN_OUTPUTS + 0].setVoltage(chan0input + addCVs0[c], c);
			outputs[MAIN_OUTPUTS + 1].setVoltage(chan1input + addCVs1[c], c);
			
			// trigger output
			bool trigOut = (alteredFate[c] && (holdTrigOut != 0 || clockTrigger[c].isHigh())); 
			outputs[TRIGGER_OUTPUT].setVoltage(trigOut ? 10.0f : 0.0f, c);
		}
		
		// lights
		if (refresh.processLights()) {
			float deltaTime = args.sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2);
			lights[TRIG_LIGHT + 0].setSmoothBrightness(trigLightsWhite, deltaTime);// white
			lights[TRIG_LIGHT + 1].setSmoothBrightness(trigLightsBlue, deltaTime);// blue
			trigLightsWhite = 0.0f;
			trigLightsBlue = 0.0f;
		}// lightRefreshCounter
	}// step()
};


struct FateWidget : ModuleWidget {
	int lastPanelTheme = -1;
	std::shared_ptr<window::Svg> light_svg;
	std::shared_ptr<window::Svg> dark_svg;

	struct HoldTrigOutItem : MenuItem {
		Fate *module;
		void onAction(const event::Action &e) override {
			module->holdTrigOut ^= 0x1;
		}
	};	
	void appendContextMenu(Menu *menu) override {
		Fate *module = dynamic_cast<Fate*>(this->module);
		assert(module);
		
		createPanelThemeMenu(menu, &(module->panelTheme));
		
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Settings"));

		HoldTrigOutItem *htItem = createMenuItem<HoldTrigOutItem>("Hold trigger out during step", CHECKMARK(module->holdTrigOut != 0));
		htItem->module = module;
		menu->addChild(htItem);
	}	
	
	FateWidget(Fate *module) {
		setModule(module);

		// Main panels from Inkscape
 		light_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/Fate-WL.svg"));
		dark_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/Fate-DM.svg"));
		int panelTheme = isDark(module ? (&(((Fate*)module)->panelTheme)) : NULL) ? 1 : 0;// need this here since step() not called for module browser
		setPanel(panelTheme == 0 ? light_svg : dark_svg);		

		// Screws 
		// part of svg panel, no code required
	
		float colRulerCenter = box.size.x / 2.0f;
		float offsetX = 20.0f;

		// free will knob and cv input
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter, 380 - 326), module, Fate::FREEWILL_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetX, 380.0f - 287.5f), true, module, Fate::FREEWILL_INPUT, module ? &module->panelTheme : NULL));
		
		// choice trigger input and light (clock), and output
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetX, 380.0f - 262.5f), true, module, Fate::CLOCK_INPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteBlueLight>>(VecPx(colRulerCenter, 380.0f - 168.5f), module, Fate::TRIG_LIGHT));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetX, 380.0f - 251.5f), false, module, Fate::TRIGGER_OUTPUT, module ? &module->panelTheme : NULL));

		// main outputs (left is old main output, right was non-existant before)
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetX, 380.0f - 223.5f), false, module, Fate::MAIN_OUTPUTS + 0, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetX, 380.0f - 212.5f), false, module, Fate::MAIN_OUTPUTS + 1, module ? &module->panelTheme : NULL));
		
		// main inputs (left is old established order, right is old ex-machina)
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetX, 380.0f - 130.5f), true, module, Fate::MAIN_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetX, 380.0f - 119.5f), true, module, Fate::MAIN_INPUTS + 1, module ? &module->panelTheme : NULL));
		
		// choices depth knob and cv input
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter, 380.0f - 83.5f), module, Fate::CHOICESDEPTH_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, 380.0f - 45.5f), true, module, Fate::CHOICSDEPTH_INPUT, module ? &module->panelTheme : NULL));
	}
	
	void step() override {
		int panelTheme = isDark(module ? (&(((Fate*)module)->panelTheme)) : NULL) ? 1 : 0;
		if (lastPanelTheme != panelTheme) {
			lastPanelTheme = panelTheme;
			SvgPanel* panel = (SvgPanel*)getPanel();
			panel->setBackground(panelTheme == 0 ? light_svg : dark_svg);
			panel->fb->dirty = true;
		}
		Widget::step();
	}
};

Model *modelFate = createModel<Fate, FateWidget>("Fate");