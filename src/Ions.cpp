//***********************************************************************************************
//Atomic Duophonic Voltage Sequencer module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental plugins by Andrew Belt and graphics  
//  from the Component Library by Wes Milholen. 
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//***********************************************************************************************


#include "Geodesics.hpp"


struct Ions : Module {
	enum ParamIds {
		RUN_PARAM,
		RESET_PARAM,
		ENUMS(CV_PARAMS, 15),// 0 is center, move conter clockwise top atom, then clockwise bot atom
		PROB_PARAM,
		ENUMS(OCT_PARAMS, 2),
		LEAP_PARAM,
		ENUMS(STATE_PARAMS, 2),// 3 states : global, local, global+local
		PLANK_PARAM,
		UNCERTANTY_PARAM,
		RESETONRUN_PARAM,
		STEPCLOCKS_PARAM,
		PLANK2_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLK_INPUT,
		ENUMS(CLK_INPUTS, 2),
		RUN_INPUT,
		RESET_INPUT,
		PROB_INPUT,// CV_value/10  is added to PROB_PARAM, which is a 0 to 1 knob
		ENUMS(OCTCV_INPUTS, 2),
		ENUMS(STATECV_INPUTS, 2),
		LEAP_INPUT,
		UNCERTANTY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(SEQ_OUTPUTS, 2),
		ENUMS(JUMP_OUTPUTS, 2),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(BLUE_LIGHTS, 16),
		ENUMS(YELLOW_LIGHTS, 16),
		RUN_LIGHT,
		RESET_LIGHT,
		ENUMS(GLOBAL_LIGHTS, 2),// 0 is top atom, 1 is bottom atom
		ENUMS(LOCAL_LIGHTS, 2),
		LEAP_LIGHT,
		ENUMS(OCTA_LIGHTS, 3),// 0 is center, 1 is inside mirrors, 2 is outside mirrors
		ENUMS(OCTB_LIGHTS, 3),
		ENUMS(PLANK_LIGHTS, 2),// blue, yellow (both white leds) (modified for 0.6.4 but no effect on patches)
		UNCERTANTY_LIGHT,
		ENUMS(JUMP_LIGHTS, 2),
		RESETONRUN_LIGHT,
		STEPCLOCKS_LIGHT,
		NUM_LIGHTS
	};
	
	
	// Constants
	const int cvMap[2][16] = {{0, 1, 2, 3, 4, 5, 6, 7, 0, 8, 9, 10, 11, 12, 13, 14},
							  {0, 8, 9 ,10, 11, 12, 13, 14, 0, 1, 2, 3, 4, 5, 6, 7}};// map each of the 16 steps of a sequence step to a CV knob index (0-14)

	// Need to save, no reset
	int panelTheme;
							  
	// Need to save, with reset
	bool running;
	bool resetOnRun;
	int quantize;// a.k.a. plank constant, bit0 = blue, bit1 = yellow
	bool uncertainty;
	int stepIndexes[2];// position of electrons (sequencers)
	int states[2];// which clocks to use (0 = global, 1 = local, 2 = both)
	int ranges[2];// [0; 2], number of extra octaves to span each side of central octave (which is C4: 0 - 1V) 
	bool leap;
	
	// No need to save, with reset
	bool rangeInc[2] = {true, true};// true when 1-3-5 increasing, false when 5-3-1 decreasing
	long clockIgnoreOnReset;
	
	// No need to save, no reset
	float resetLight = 0.0f;
	float jumpLights[2] = {0.0f, 0.0f};
	float stepClocksLight = 0.0f;
	Trigger runningTrigger;
	Trigger clockTrigger;
	Trigger clocksTriggers[2];
	Trigger resetTrigger;
	Trigger stateTriggers[2];
	Trigger octTriggers[2];
	Trigger stateCVTriggers[2];
	Trigger leapTrigger;
	Trigger plankTrigger;
	Trigger plank2Trigger;
	Trigger uncertaintyTrigger;
	Trigger resetOnRunTrigger;
	Trigger stepClocksTrigger;
	dsp::PulseGenerator jumpPulses[2];
	RefreshCounter refresh;

	
	inline float quantizeCV(float cv) {return std::round(cv * 12.0f) / 12.0f;}
	inline bool jumpRandom() {return (random::uniform() < (params[PROB_PARAM].getValue() + inputs[PROB_INPUT].getVoltage() / 10.0f));}// randomUniform is [0.0, 1.0)
	
	
	Ions() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(CV_PARAMS + 0, 0.0f, 1.0f, 0.5f, "Step 1 blue/yellow");
		char strBuf[32];				
		for (int i = 1; i < 8; i++) {// Blue CV knobs
			snprintf(strBuf, 32, "Step %i blue", i + 1);		
			configParam(CV_PARAMS + i, 0.0f, 1.0f, 0.5f, strBuf);
		}
		for (int i = 8; i < 15; i++) {// Blue CV knobs
			snprintf(strBuf, 32, "Step %i yellow", i - 6);		
			configParam(CV_PARAMS + i, 0.0f, 1.0f, 0.5f, strBuf);
		}
		configParam(PROB_PARAM, 0.0f, 1.0f, 0.0f, "Probability");
		configParam(PLANK_PARAM, 0.0f, 1.0f, 0.0f, "Quantize (Planck) blue");	
		configParam(PLANK2_PARAM, 0.0f, 1.0f, 0.0f, "Quantize (Planck) yellow");	
		configParam(OCT_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Octaves blue");	
		configParam(OCT_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Octaves yellow");	
		configParam(RUN_PARAM, 0.0f, 1.0f, 0.0f, "Run");	
		configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f, "Reset");	
		configParam(STEPCLOCKS_PARAM, 0.0f, 1.0f, 0.0f, "Magnetic clock");			
		configParam(RESETONRUN_PARAM, 0.0f, 1.0f, 0.0f, "Reset on run");	
		configParam(STATE_PARAMS + 0, 0.0f, 1.0f, 0.0f, "State blue");	
		configParam(STATE_PARAMS + 1, 0.0f, 1.0f, 0.0f, "State yellow");	
		configParam(LEAP_PARAM, 0.0f, 1.0f, 0.0f, "Quantum leap");	
		configParam(UNCERTANTY_PARAM, 0.0f, 1.0f, 0.0f, "Uncertainty");	

		onReset();

		panelTheme = (loadDarkAsDefault() ? 1 : 0);
	}

	
	void onReset() override {
		running = true;
		resetOnRun = false;
		quantize = 3;
		uncertainty = false;
		// stepIndexes done in initRun()
		for (int i = 0; i < 2; i++) {
			states[i] = 0;
			ranges[i] = 1;
		}
		leap = false;
		resetNonJson(true);
	}
	void resetNonJson(bool hard) {
		rangeInc[0] = true;
		rangeInc[1] = true;
		initRun(hard);
	}
	void initRun(bool hard) {// run button activated or run edge in run input jack
		clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * APP->engine->getSampleRate());
		if (hard) {
			stepIndexes[0] = 0;
			stepIndexes[1] = 0;
		}
	}
	
	
	void onRandomize() override {
	}
	
	
	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// running
		json_object_set_new(rootJ, "running", json_boolean(running));

		// resetOnRun
		json_object_set_new(rootJ, "resetOnRun", json_boolean(resetOnRun));
		
		// quantize
		json_object_set_new(rootJ, "quantize", json_integer(quantize));
		
		// uncertainty
		json_object_set_new(rootJ, "uncertainty", json_boolean(uncertainty));
		
		// stepIndexes
		json_object_set_new(rootJ, "stepIndexes0", json_integer(stepIndexes[0]));
		json_object_set_new(rootJ, "stepIndexes1", json_integer(stepIndexes[1]));

		// states
		json_object_set_new(rootJ, "states0", json_integer(states[0]));
		json_object_set_new(rootJ, "states1", json_integer(states[1]));

		// ranges
		json_object_set_new(rootJ, "ranges0", json_integer(ranges[0]));
		json_object_set_new(rootJ, "ranges1", json_integer(ranges[1]));

		// leap
		json_object_set_new(rootJ, "leap", json_boolean(leap));

		return rootJ;
	}

	
	void dataFromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);

		// running
		json_t *runningJ = json_object_get(rootJ, "running");
		if (runningJ)
			running = json_is_true(runningJ);

		// resetOnRun
		json_t *resetOnRunJ = json_object_get(rootJ, "resetOnRun");
		if (resetOnRunJ)
			resetOnRun = json_is_true(resetOnRunJ);

		// quantize
		json_t *quantizeJ = json_object_get(rootJ, "quantize");
		if (quantizeJ)
			quantize = json_integer_value(quantizeJ);

		// uncertainty
		json_t *uncertaintyJ = json_object_get(rootJ, "uncertainty");
		if (uncertaintyJ)
			uncertainty = json_is_true(uncertaintyJ);

		// stepIndexes
		json_t *stepIndexes0J = json_object_get(rootJ, "stepIndexes0");
		if (stepIndexes0J)
			stepIndexes[0] = json_integer_value(stepIndexes0J);
		json_t *stepIndexes1J = json_object_get(rootJ, "stepIndexes1");
		if (stepIndexes1J)
			stepIndexes[1] = json_integer_value(stepIndexes1J);

		// states
		json_t *states0J = json_object_get(rootJ, "states0");
		if (states0J)
			states[0] = json_integer_value(states0J);
		json_t *states1J = json_object_get(rootJ, "states1");
		if (states1J)
			states[1] = json_integer_value(states1J);

		// ranges
		json_t *ranges0J = json_object_get(rootJ, "ranges0");
		if (ranges0J)
			ranges[0] = json_integer_value(ranges0J);
		json_t *ranges1J = json_object_get(rootJ, "ranges1");
		if (ranges1J)
			ranges[1] = json_integer_value(ranges1J);

		// leap
		json_t *leapJ = json_object_get(rootJ, "leap");
		if (leapJ)
			leap = json_is_true(leapJ);

		resetNonJson(false);// soft init, don't want to init stepIndexes
	}

	
	void process(const ProcessArgs &args) override {
	
		//********** Buttons, knobs, switches and inputs **********
	
		// Run button
		if (runningTrigger.process(params[RUN_PARAM].getValue() + inputs[RUN_INPUT].getVoltage())) {// no input refresh here, don't want to introduce startup skew
			running = !running;
			if (running ) {
				if (resetOnRun) {
					initRun(true);
				}
			}
		}
		
		if (refresh.processInputs()) {
			// Leap button
			if (leapTrigger.process(params[LEAP_PARAM].getValue() + inputs[LEAP_INPUT].getVoltage())) {
				leap = !leap;
			}

			// Plank buttons (quatize)
			if (plankTrigger.process(params[PLANK_PARAM].getValue()))
				quantize ^= 0x1;
			if (plank2Trigger.process(params[PLANK2_PARAM].getValue()))
				quantize ^= 0x2;

			// uncertainty button
			if (uncertaintyTrigger.process(params[UNCERTANTY_PARAM].getValue() + inputs[UNCERTANTY_INPUT].getVoltage())) {
				uncertainty = !uncertainty;
			}

			// Reset on Run button
			if (resetOnRunTrigger.process(params[RESETONRUN_PARAM].getValue())) {
				resetOnRun = !resetOnRun;
			}

			// State buttons and CV inputs (state: 0 = global, 1 = local, 2 = both)
			for (int i = 0; i < 2; i++) {
				int stateTrig = stateTriggers[i].process(params[STATE_PARAMS + i].getValue());
				if (inputs[STATECV_INPUTS + i].isConnected()) {
					if (inputs[STATECV_INPUTS + i].getVoltage() <= -1.0f)
						states[i] = 1;
					else if (inputs[STATECV_INPUTS + i].getVoltage() < 1.0f)
						states[i] = 2;
					else 
						states[i] = 0;
				}
				else if (stateTrig) {
					states[i]++;
					if (states[i] >= 3)
						states[i] = 0;
				}
			}
			
			// Range buttons and CV inputs
			for (int i = 0; i < 2; i++) {
				bool rangeTrig = octTriggers[i].process(params[OCT_PARAMS + i].getValue());
				if (inputs[OCTCV_INPUTS + i].isConnected()) {
					if (inputs[OCTCV_INPUTS + i].getVoltage() <= -1.0f)
						ranges[i] = 0;
					else if (inputs[OCTCV_INPUTS + i].getVoltage() < 1.0f)
						ranges[i] = 1;
					else 
						ranges[i] = 2;
				}
				else if (rangeTrig) {
					if (rangeInc[i]) {
						ranges[i]++;
						if (ranges[i] >= 3) {
							ranges[i] = 1;
							rangeInc[i] = false;
						}
					}
					else {
						ranges[i]--;
						if (ranges[i] < 0) {
							ranges[i] = 1;
							rangeInc[i] = true;
						}
					}
				}
			}
		}// userInputs refresh
		

		//********** Clock and reset **********
		
		// Clocks
		bool stepClocksTrig = stepClocksTrigger.process(params[STEPCLOCKS_PARAM].getValue());
		bool globalClockTrig = false;
		if (running && clockIgnoreOnReset == 0l)
			globalClockTrig = clockTrigger.process(inputs[CLK_INPUT].getVoltage());// keep outside of loop, only need to call once per step()
		for (int i = 0; i < 2; i++) {
			int jumpCount = 0;
			
			if (running && clockIgnoreOnReset == 0l) {	
				
				// Local clocks and uncertainty
				bool localClockTrig = clocksTriggers[i].process(inputs[CLK_INPUTS + i].getVoltage());
				localClockTrig &= (states[i] >= 1);
				if (localClockTrig) {
					if (uncertainty) {// local clock modified by uncertainty
						for (int n = 0; n < getWeighted1to8random(); n++)
							jumpCount += stepElectron(i, leap);
					}
					else 
						jumpCount += stepElectron(i, leap);// normal local clock
				}				
				
				// Global clock
				if (globalClockTrig && ((states[i] & 0x1) == 0) && !localClockTrig) {
					jumpCount += stepElectron(i, leap);
				}
				
			}
			
			// Magnetic clock (step clock)
			if (stepClocksTrig) {
				stepClocksLight = 1.0f;// will be done twice, but no problem
				jumpCount += stepElectron(i, leap);
			}
			
			// Jump occurred feedback
			if ((jumpCount & 0x1) != 0) {
				jumpPulses[i].trigger(0.001f);
				jumpLights[i] = 1.0f;				
			}
		}
		
		
		// Reset
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage() + params[RESET_PARAM].getValue())) {
			initRun(true);
			resetLight = 1.0f;
			clockTrigger.reset();
			clocksTriggers[0].reset();
			clocksTriggers[1].reset();
		}
		
		
		//********** Outputs and lights **********

		// Outputs
		for (int i = 0; i < 2; i++) {
			float knobVal = params[CV_PARAMS + cvMap[i][stepIndexes[i]]].getValue();
			float cv = 0.0f;
			int range = ranges[i];
			if ( (i == 0 && (quantize & 0x1) != 0) || (i == 1 && (quantize > 1)) ) {
				cv = (knobVal * (float)(range * 2 + 1) - (float)range);
				cv = quantizeCV(cv);
			}
			else {
				int maxCV = (range == 0 ? 1 : (range * 5));// maxCV is [1, 5, 10]
				cv = knobVal * (float)(maxCV * 2) - (float)maxCV;
			}
			outputs[SEQ_OUTPUTS + i].setVoltage(cv);
			outputs[JUMP_OUTPUTS + i].setVoltage(jumpPulses[i].process((float)args.sampleTime) ? 10.0f : 0.0f);
		}
		
		// lights
		if (refresh.processLights()) {
			float deltaTime = args.sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2);
			// Blue and Yellow lights
			for (int i = 0; i < 16; i++) {
				lights[BLUE_LIGHTS + i].setBrightness(stepIndexes[0] == i ? 1.0f : 0.0f);
				lights[YELLOW_LIGHTS + i].setBrightness(stepIndexes[1] == i ? 1.0f : 0.0f);
			}
			
			// Reset light
			lights[RESET_LIGHT].setSmoothBrightness(resetLight, deltaTime);	
			resetLight = 0.0f;	
			
			// Run light
			lights[RUN_LIGHT].setBrightness(running ? 1.0f : 0.0f);

			// State lights
			for (int i = 0; i < 2; i++) {
				lights[GLOBAL_LIGHTS + i].setBrightness((states[i] & 0x1) == 0 ? 1.0f : 0.0f);
				lights[LOCAL_LIGHTS + i].setBrightness(states[i] >= 1 ? 1.0f : 0.0f);
			}
			
			// Leap, Plank, uncertainty and ResetOnRun lights
			lights[LEAP_LIGHT].setBrightness(leap ? 1.0f : 0.0f);
			lights[PLANK_LIGHTS + 0].setBrightness((quantize & 0x1) ? 1.0f : 0.0f);// Blue
			lights[PLANK_LIGHTS + 1].setBrightness((quantize & 0x2) ? 1.0f : 0.0f);// Yellow
			lights[UNCERTANTY_LIGHT].setBrightness(uncertainty ? 1.0f : 0.0f);
			lights[RESETONRUN_LIGHT].setBrightness(resetOnRun ? 1.0f : 0.0f);
			
			// Range lights
			for (int i = 0; i < 3; i++) {
				lights[OCTA_LIGHTS + i].setBrightness(i <= ranges[0] ? 1.0f : 0.0f);
				lights[OCTB_LIGHTS + i].setBrightness(i <= ranges[1] ? 1.0f : 0.0f);
			}

			// Jump lights
			for (int i = 0; i < 2; i++) {
				lights[JUMP_LIGHTS + i].setSmoothBrightness(jumpLights[i], deltaTime);
				jumpLights[i] = 0.0f;
			}

			// Step clocks light
			lights[STEPCLOCKS_LIGHT].setSmoothBrightness(stepClocksLight, deltaTime);
			stepClocksLight = 0.0f;
		
		}// lightRefreshCounter
		
		if (clockIgnoreOnReset > 0l)
			clockIgnoreOnReset--;
	}// step()
	
	
	int stepElectron(int i, bool leap) {
		int jumped = 0;
		int base = stepIndexes[i] & 0x8;// 0 or 8
		int step8 = stepIndexes[i] & 0x7;// 0 to 7
		if ( (step8 == 7 || leap) && jumpRandom() ) {
			jumped = 1;
			base = 8 - base;// change atom
		}
		step8++;
		if (step8 > 7)
			step8 = 0;
		stepIndexes[i] = base | step8;
		return jumped;
	}
};


struct IonsWidget : ModuleWidget {
	SvgPanel* darkPanel;

	struct PanelThemeItem : MenuItem {
		Ions *module;
		int theme;
		void onAction(const event::Action &e) override {
			module->panelTheme = theme;
		}
		void step() override {
			rightText = (module->panelTheme == theme) ? "✔" : "";
		}
	};
	void appendContextMenu(Menu *menu) override {
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Ions *module = dynamic_cast<Ions*>(this->module);
		assert(module);

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
	}	
	
	IonsWidget(Ions *module) {
		setModule(module);

		// Main panels from Inkscape
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/Ions-WL.svg")));
        if (module) {
			darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/Ions-DM.svg")));
			darkPanel->visible = false;
			addChild(darkPanel);
		}
		
		// Screws 
		// part of svg panel, no code required
		
		float colRulerCenter = box.size.x / 2.0f;
		static constexpr float rowRulerAtomA = 116.5;
		static constexpr float rowRulerAtomB = 242.5f;
		static constexpr float radius1 = 21.0f;
		static constexpr float offset1 = 14.0f;
		static constexpr float radius2 = 35.0f;
		static constexpr float offset2 = 25.0f;
		static constexpr float radius3 = 61.0f;
		static constexpr float offset3 = 43.0f;
		
		// Outputs
		addOutput(createDynamicPort<GeoPort>(Vec(colRulerCenter, rowRulerAtomA), false, module, Ions::SEQ_OUTPUTS + 0, module ? &module->panelTheme : NULL));		
		addOutput(createDynamicPort<GeoPort>(Vec(colRulerCenter, rowRulerAtomB), false, module, Ions::SEQ_OUTPUTS + 1, module ? &module->panelTheme : NULL));		
		
		// CV knobs
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter, rowRulerAtomA + radius3 + 2.0f), module, Ions::CV_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter + offset3, rowRulerAtomA + offset3), module, Ions::CV_PARAMS + 1, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter + radius3, rowRulerAtomA), module, Ions::CV_PARAMS + 2, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter + offset3, rowRulerAtomA - offset3), module, Ions::CV_PARAMS + 3, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter, rowRulerAtomA - radius3), module, Ions::CV_PARAMS + 4, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter - offset3, rowRulerAtomA - offset3), module, Ions::CV_PARAMS + 5, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter - radius3, rowRulerAtomA), module, Ions::CV_PARAMS + 6, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter - offset3, rowRulerAtomA + offset3), module, Ions::CV_PARAMS + 7, module ? &module->panelTheme : NULL));
		//
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter + offset3, rowRulerAtomB - offset3), module, Ions::CV_PARAMS + 8, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter + radius3, rowRulerAtomB), module, Ions::CV_PARAMS + 9, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter + offset3, rowRulerAtomB + offset3), module, Ions::CV_PARAMS + 10, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter, rowRulerAtomB + radius3), module, Ions::CV_PARAMS + 11, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter - offset3, rowRulerAtomB + offset3), module, Ions::CV_PARAMS + 12, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter - radius3, rowRulerAtomB), module, Ions::CV_PARAMS + 13, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter - offset3, rowRulerAtomB - offset3), module, Ions::CV_PARAMS + 14, module ? &module->panelTheme : NULL));
		
		// Prob knob and CV inuput
		float probX = colRulerCenter + 2.0f * offset3;
		float probY = rowRulerAtomA + radius3 + 2.0f;
		addParam(createDynamicParam<GeoKnobLeft>(Vec(probX, probY), module, Ions::PROB_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(Vec(probX + 32.0f, probY), true, module, Ions::PROB_INPUT, module ? &module->panelTheme : NULL));
		
		// Jump pulses
		addOutput(createDynamicPort<GeoPort>(Vec(probX + 18.0f, probY - 37.2f), false, module, Ions::JUMP_OUTPUTS + 0, module ? &module->panelTheme : NULL));		
		addOutput(createDynamicPort<GeoPort>(Vec(probX + 18.0f, probY + 36.7f), false, module, Ions::JUMP_OUTPUTS + 1, module ? &module->panelTheme : NULL));		
		// Jump lights
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(probX - 3.0f, probY - 37.2f - 4.8f), module, Ions::JUMP_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(probX - 3.0f, probY + 36.7f + 5.0f), module, Ions::JUMP_LIGHTS + 1));
		
		// Plank light and button (top)
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(233.5f, 60.5f), module, Ions::PLANK_LIGHTS + 0));
		addParam(createDynamicParam<GeoPushButton>(Vec(225.5f, 48.5f), module, Ions::PLANK_PARAM, module ? &module->panelTheme : NULL));	
		// Plank light and button (bottom)
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(233.5f, 298.5f), module, Ions::PLANK_LIGHTS + 1));
		addParam(createDynamicParam<GeoPushButton>(Vec(225.5f, 310.5f), module, Ions::PLANK2_PARAM, module ? &module->panelTheme : NULL));	

		// Octave buttons and lights
		float octX = colRulerCenter + 107.0f;
		float octOffsetY = 10.0f;
		float octYA = rowRulerAtomA - octOffsetY;
		float octYB = rowRulerAtomB + octOffsetY;
		// top:
		addParam(createDynamicParam<GeoPushButton>(Vec(octX, octYA), module, Ions::OCT_PARAMS + 0, module ? &module->panelTheme : NULL));	
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(octX - 15.0f, octYA + 2.5f), module, Ions::OCTA_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(octX - 12.0f, octYA - 8.0f), module, Ions::OCTA_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(octX - 10.0f, octYA + 11.5f), module, Ions::OCTA_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(octX - 3.0f, octYA - 13.5f), module, Ions::OCTA_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(octX + 0.0f, octYA + 15.0f), module, Ions::OCTA_LIGHTS + 2));		
		// bottom:
		addParam(createDynamicParam<GeoPushButton>(Vec(octX, octYB), module, Ions::OCT_PARAMS + 1, module ? &module->panelTheme : NULL));	
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(octX - 15.0f, octYB - 2.5f), module, Ions::OCTB_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(octX - 12.0f, octYB + 8.0f), module, Ions::OCTB_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(octX - 10.0f, octYB - 11.5f), module, Ions::OCTB_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(octX - 3.0f, octYB + 13.5f), module, Ions::OCTB_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(octX + 0.0f, octYB - 15.0f), module, Ions::OCTB_LIGHTS + 2));
		// Oct CV inputs
		addInput(createDynamicPort<GeoPort>(Vec(octX - 7.0f, octYA - 31.0f), true, module, Ions::OCTCV_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(Vec(octX - 7.0f, octYB + 31.0f), true, module, Ions::OCTCV_INPUTS + 1, module ? &module->panelTheme : NULL));
		
		// Blue electron lights
		// top blue
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter, rowRulerAtomA + radius2), module, Ions::BLUE_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter + offset2, rowRulerAtomA + offset2), module, Ions::BLUE_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter + radius2, rowRulerAtomA), module, Ions::BLUE_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter + offset2, rowRulerAtomA - offset2), module, Ions::BLUE_LIGHTS + 3));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter, rowRulerAtomA - radius2), module, Ions::BLUE_LIGHTS + 4));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter - offset2, rowRulerAtomA - offset2), module, Ions::BLUE_LIGHTS + 5));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter - radius2, rowRulerAtomA), module, Ions::BLUE_LIGHTS + 6));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter - offset2, rowRulerAtomA + offset2), module, Ions::BLUE_LIGHTS + 7));
		// bottom blue
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter, rowRulerAtomB - radius1), module, Ions::BLUE_LIGHTS + 8));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter + offset1, rowRulerAtomB - offset1), module, Ions::BLUE_LIGHTS + 9));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter + radius1, rowRulerAtomB), module, Ions::BLUE_LIGHTS + 10));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter + offset1, rowRulerAtomB + offset1), module, Ions::BLUE_LIGHTS + 11));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter, rowRulerAtomB + radius1), module, Ions::BLUE_LIGHTS + 12));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter - offset1, rowRulerAtomB + offset1), module, Ions::BLUE_LIGHTS + 13));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter - radius1, rowRulerAtomB), module, Ions::BLUE_LIGHTS + 14));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(colRulerCenter - offset1, rowRulerAtomB - offset1), module, Ions::BLUE_LIGHTS + 15));
		
		// Yellow electron lights
		// bottom yellow
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter, rowRulerAtomB - radius2), module, Ions::YELLOW_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter + offset2, rowRulerAtomB - offset2), module, Ions::YELLOW_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter + radius2, rowRulerAtomB), module, Ions::YELLOW_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter + offset2, rowRulerAtomB + offset2), module, Ions::YELLOW_LIGHTS + 3));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter, rowRulerAtomB + radius2), module, Ions::YELLOW_LIGHTS + 4));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter - offset2, rowRulerAtomB + offset2), module, Ions::YELLOW_LIGHTS + 5));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter - radius2, rowRulerAtomB), module, Ions::YELLOW_LIGHTS + 6));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter - offset2, rowRulerAtomB - offset2), module, Ions::YELLOW_LIGHTS + 7));		
		// top yellow
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter, rowRulerAtomA + radius1), module, Ions::YELLOW_LIGHTS + 8));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter + offset1, rowRulerAtomA + offset1), module, Ions::YELLOW_LIGHTS + 9));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter + radius1, rowRulerAtomA), module, Ions::YELLOW_LIGHTS + 10));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter + offset1, rowRulerAtomA - offset1), module, Ions::YELLOW_LIGHTS + 11));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter, rowRulerAtomA - radius1), module, Ions::YELLOW_LIGHTS + 12));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter - offset1, rowRulerAtomA - offset1), module, Ions::YELLOW_LIGHTS + 13));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter - radius1, rowRulerAtomA), module, Ions::YELLOW_LIGHTS + 14));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(colRulerCenter - offset1, rowRulerAtomA + offset1), module, Ions::YELLOW_LIGHTS + 15));

		// Run jack, light and button
		static constexpr float rowRulerRunJack = 348.5f;
		static constexpr float offsetRunJackX = 119.5f;
		addInput(createDynamicPort<GeoPort>(Vec(colRulerCenter - offsetRunJackX, rowRulerRunJack), true, module, Ions::RUN_INPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(colRulerCenter - offsetRunJackX + 18.0f, rowRulerRunJack), module, Ions::RUN_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(Vec(colRulerCenter - offsetRunJackX + 33.0f, rowRulerRunJack), module, Ions::RUN_PARAM, module ? &module->panelTheme : NULL));	
		
		// Reset jack, light and button
		addInput(createDynamicPort<GeoPort>(Vec(colRulerCenter + offsetRunJackX, rowRulerRunJack), true, module, Ions::RESET_INPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(colRulerCenter + offsetRunJackX - 18.0f, rowRulerRunJack), module, Ions::RESET_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(Vec(colRulerCenter + offsetRunJackX - 33.0f, rowRulerRunJack), module, Ions::RESET_PARAM, module ? &module->panelTheme : NULL));	
	
		static constexpr float offsetMagneticButton = 42.5f;
		// Magnetic clock (step clocks)
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(colRulerCenter - offsetMagneticButton - 15.0f, rowRulerRunJack), module, Ions::STEPCLOCKS_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(Vec(colRulerCenter - offsetMagneticButton, rowRulerRunJack), module, Ions::STEPCLOCKS_PARAM, module ? &module->panelTheme : NULL));			
		// Reset on Run light and button
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(colRulerCenter + offsetMagneticButton + 15.0f, rowRulerRunJack), module, Ions::RESETONRUN_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(Vec(colRulerCenter + offsetMagneticButton, rowRulerRunJack), module, Ions::RESETONRUN_PARAM, module ? &module->panelTheme : NULL));	

		
		// Globak clock
		float gclkX = colRulerCenter - 2.0f * offset3;
		float gclkY = rowRulerAtomA + radius3 + 2.0f;
		addInput(createDynamicPort<GeoPort>(Vec(gclkX, gclkY), true, module, Ions::CLK_INPUT, module ? &module->panelTheme : NULL));
		// global lights
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(gclkX - 12.0f, gclkY - 20.0f), module, Ions::GLOBAL_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(gclkX - 12.0f, gclkY + 20.0f), module, Ions::GLOBAL_LIGHTS + 1));
		// state buttons
		addParam(createDynamicParam<GeoPushButton>(Vec(gclkX - 17.0f, gclkY - 34.0f), module, Ions::STATE_PARAMS + 0, module ? &module->panelTheme : NULL));	
		addParam(createDynamicParam<GeoPushButton>(Vec(gclkX - 17.0f, gclkY + 34.0f), module, Ions::STATE_PARAMS + 1, module ? &module->panelTheme : NULL));	
		// local lights
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(Vec(gclkX - 20.0f, gclkY - 48.5f), module, Ions::LOCAL_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(Vec(gclkX - 20.0f, gclkY + 48.5f), module, Ions::LOCAL_LIGHTS + 1));
		// local inputs
		addInput(createDynamicPort<GeoPort>(Vec(gclkX - 21.0f, gclkY - 72.0f), true, module, Ions::CLK_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(Vec(gclkX - 21.0f, gclkY + 72.0f), true, module, Ions::CLK_INPUTS + 1, module ? &module->panelTheme : NULL));
		// state inputs
		addInput(createDynamicPort<GeoPort>(Vec(gclkX - 39.0f, gclkY - 28.0f), true, module, Ions::STATECV_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(Vec(gclkX - 39.0f, gclkY + 28.0f), true, module, Ions::STATECV_INPUTS + 1, module ? &module->panelTheme : NULL));
		
		// Leap light, button and CV input
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(gclkX - 5.0f, 60.5f), module, Ions::LEAP_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(Vec(gclkX + 3.0f, 48.5f), module, Ions::LEAP_PARAM, module ? &module->panelTheme : NULL));	
		addInput(createDynamicPort<GeoPort>(Vec(gclkX - 14.0f, 76.5f), true, module, Ions::LEAP_INPUT, module ? &module->panelTheme : NULL));

		// uncertainty light, button and CV input
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(gclkX - 5.0f, 298.5f), module, Ions::UNCERTANTY_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(Vec(gclkX + 3.0f, 310.5f), module, Ions::UNCERTANTY_PARAM, module ? &module->panelTheme : NULL));	
		addInput(createDynamicPort<GeoPort>(Vec(gclkX - 14.0f, 282.5f), true, module, Ions::UNCERTANTY_INPUT, module ? &module->panelTheme : NULL));
	}
	
	void step() override {
		if (module) {
			panel->visible = ((((Ions*)module)->panelTheme) == 0);
			darkPanel->visible  = ((((Ions*)module)->panelTheme) == 1);
		}
		Widget::step();
	}
};

Model *modelIons = createModel<Ions, IonsWidget>("Ions");

/*CHANGE LOG


*/