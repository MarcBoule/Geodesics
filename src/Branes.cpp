//***********************************************************************************************
//Colliding Sample and Hold module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental plugins by Andrew Belt and graphics  
//  from the Component Library by Wes Milholen. 
//Also based on code from Joel Robichaud's Nohmad Noise module
//See ./LICENSE.txt for all licenses
//
//***********************************************************************************************


#include <dsp/filter.hpp>
#include <random>
#include "Geodesics.hpp"



struct PinkNoise {
	// the filter in this code is adapted from http://www.firstpr.com.au/dsp/pink-noise/#Filtering
	
	// from the above link:	
	/* 
	Most of this material is written by other people, especially Allan Herriman, James McCartney, Phil Burk and Paul Kellet – all from the music-dsp mailing list. 
	
	...
	
	On 17 October 1999, Paul put up a further refinement: "instrumentation grade" and "economy" filters.

	This is an approximation to a -10dB/decade filter using a weighted sum 
	of first order filters. It is accurate to within +/-0.05dB above 9.2Hz 
	(44100Hz sampling rate). Unity gain is at Nyquist, but can be adjusted 
	by scaling the numbers at the end of each line.
	
	(This is pk3 = (Black) Paul Kellet's refined method in Allan's analysis.)
	*/
	
	float b0, b1, b2, b3, b4, b5, b6;

	float process() {
		// noise source
		const float white = random::uniform() * 1.2f - 0.6f;// values adjusted so that returned pink noise is in -5V to +5V range
		
		// filter
		b0 = 0.99886f * b0 + white * 0.0555179f;
		b1 = 0.99332f * b1 + white * 0.0750759f;
		b2 = 0.96900f * b2 + white * 0.1538520f;
		b3 = 0.86650f * b3 + white * 0.3104856f;
		b4 = 0.55000f * b4 + white * 0.5329522f;
		b5 = -0.7616f * b5 - white * 0.0168980f;
		const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
		b6 = white * 0.115926f;
		return pink;
	}
};


//*****************************************************************************


struct NoiseEngine {
	enum NoiseId {NONE, WHITE, PINK, RED, BLUE};//use negative value for inv phase
	int noiseSources[14] = {PINK, RED, BLUE, WHITE, BLUE, RED, PINK,   PINK, RED, BLUE, WHITE, BLUE, RED, PINK};


	PinkNoise pinkNoise[2];
	PinkNoise pinkForBlueNoise[2];
	dsp::RCFilter redFilter[2];// for lowpass
	dsp::RCFilter blueFilter[2];// for highpass
	bool cacheHitRed[2];// no need to init; index is braneIndex
	float cacheValRed[2];
	bool cacheHitBlue[2];// no need to init; index is braneIndex
	float cacheValBlue[2];
	bool cacheHitPink[2];// no need to init; index is braneIndex
	float cacheValPink[2];
	
	
	float whiteNoise() {
		return random::uniform() * 10.0f - 5.0f;
	}	
	
	
	void setCutoffs(float sampleRate) {
		redFilter[0].setCutoffFreq(70.0f / sampleRate);// low pass
		redFilter[1].setCutoffFreq(70.0f / sampleRate);
		blueFilter[0].setCutoffFreq(4410.0f / sampleRate);// high pass
		blueFilter[1].setCutoffFreq(4410.0f / sampleRate);
	}		
	
	
	void clearCache() {
		// optimizations for noise generators
		for (int i = 0; i < 2; i++) {
			cacheHitRed[i] = false;
			cacheHitBlue[i] = false;
			cacheHitPink[i] = false;
		}
	}		
	
	
	float getNoise(int sh) {
		float ret = 0.0f;
		int braneIndex = sh < 7 ? 0 : 1;
		int noiseIndex = noiseSources[sh];
		if (noiseIndex == WHITE) {
			ret = whiteNoise();
		}
		else if (noiseIndex == RED) {
			if (cacheHitRed[braneIndex])
				ret = -1.0f * cacheValRed[braneIndex];
			else {
				redFilter[braneIndex].process(whiteNoise());
				cacheValRed[braneIndex] = 5.0f * redFilter[braneIndex].lowpass();
				cacheHitRed[braneIndex] = true;
				ret = cacheValRed[braneIndex];
			}
		}
		else if (noiseIndex == PINK) {
			if (cacheHitPink[braneIndex])
				ret = -1.0f * cacheValPink[braneIndex];
			else {
				cacheValPink[braneIndex] = pinkNoise[braneIndex].process();
				cacheHitPink[braneIndex] = true;
				ret = cacheValPink[braneIndex];
			}
		}
		else {// noiseIndex == BLUE
			if (cacheHitBlue[braneIndex])
				ret = -1.0f * cacheValBlue[braneIndex];
			else {
				float pinkForBlue = pinkForBlueNoise[braneIndex].process();			
				blueFilter[braneIndex].process(pinkForBlue);
				cacheValBlue[braneIndex] = 5.8f * blueFilter[braneIndex].highpass();
				cacheHitBlue[braneIndex] = true;
				ret = cacheValBlue[braneIndex];
			}
		}
		return ret;
	}		
};


//*****************************************************************************


struct Branes : Module {
	enum ParamIds {
		ENUMS(TRIG_BYPASS_PARAMS, 2),
		ENUMS(NOISE_RANGE_PARAMS, 2),
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(IN_INPUTS, 14),
		ENUMS(TRIG_INPUTS, 2),
		ENUMS(TRIG_BYPASS_INPUTS, 2),
		ENUMS(NOISE_RANGE_INPUTS, 2),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(OUT_OUTPUTS, 14),
		// S&H are numbered 0 to 6 in BraneA from lower left to lower right
		// S&H are numbered 7 to 13 in BraneB from top right to top left
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(UNUSED1, 2 * 2),// no longer used
		ENUMS(BYPASS_TRIG_LIGHTS, 4 * 2),// room for blue-yellow-red-white
		ENUMS(NOISE_RANGE_LIGHTS, 2),
		NUM_LIGHTS
	};
	
	
	// Constants
	// none
	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	int vibrations[2];
	bool noiseRange[2];
	
	// No need to save, with reset
	float heldOuts[14];
	
	// No need to save, no reset
	Trigger sampleTriggers[2];
	Trigger trigBypassTriggers[2];
	Trigger noiseRangeTriggers[2];
	float trigLights[2] = {0.0f, 0.0f};
	RefreshCounter refresh;
	HoldDetect secretHoldDetect[2];
	NoiseEngine noiseEngine;
	
	
	Branes() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		
		configParam(TRIG_BYPASS_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Top brane bypass");
		configParam(TRIG_BYPASS_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Bottom brane bypass");
		configParam(NOISE_RANGE_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Top brane noise range");
		configParam(NOISE_RANGE_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Bottom brane noise range");		
		
		noiseEngine.setCutoffs(APP->engine->getSampleRate());
		
		onReset();

		panelTheme = (loadDarkAsDefault() ? 1 : 0);
	}

	
	void onReset() override {
		for (int i = 0; i < 2; i++) {
			vibrations[i] = 0;
			noiseRange[i] = false;
		}
		resetNonJson();
	}
	void resetNonJson() {
		for (int i = 0; i < 14; i++)
			heldOuts[i] = 0.0f;
	}

	
	void onRandomize() override {
		for (int i = 0; i < 2; i++) {
			vibrations[i] = (random::u32() % 2);
			noiseRange[i] = (random::u32() % 2) > 0;
		}
		for (int i = 0; i < 14; i++)
			heldOuts[i] = 0.0f;
	}

	
	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// trigBypass (DEPRECATED, replaced by vibrations below)
		//json_object_set_new(rootJ, "trigBypass0", json_real(trigBypass[0]));// should have been bool instead of real
		//json_object_set_new(rootJ, "trigBypass1", json_real(trigBypass[1]));// should have been bool instead of real
		
		// vibrations (normal, trig bypass, yellow, blue)
		json_object_set_new(rootJ, "vibrations0", json_integer(vibrations[0]));
		json_object_set_new(rootJ, "vibrations1", json_integer(vibrations[1]));

		// noiseRange
		json_object_set_new(rootJ, "noiseRange0", json_real(noiseRange[0]));
		json_object_set_new(rootJ, "noiseRange1", json_real(noiseRange[1]));

		return rootJ;
	}

	
	void dataFromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);

		// vibrations (0 - top)
		json_t *vibrations0J = json_object_get(rootJ, "vibrations0");
		if (vibrations0J)
			vibrations[0] = json_integer_value(vibrations0J);
		else {// legacy
			json_t *trigBypass0J = json_object_get(rootJ, "trigBypass0");
			if (trigBypass0J)
				vibrations[0] = json_number_value(trigBypass0J);// this was a real instead of bool by accident
		}
		// vibrations (1 - bottom)
		json_t *vibrations1J = json_object_get(rootJ, "vibrations1");
		if (vibrations1J)
			vibrations[1] = json_integer_value(vibrations1J);
		else {// legacy
			json_t *trigBypass1J = json_object_get(rootJ, "trigBypass1");
			if (trigBypass1J)
				vibrations[1] = json_number_value(trigBypass1J);// this was a real instead of bool by accident
		}

		// noiseRange
		json_t *noiseRange0J = json_object_get(rootJ, "noiseRange0");
		if (noiseRange0J)
			noiseRange[0] = json_number_value(noiseRange0J);
		json_t *noiseRange1J = json_object_get(rootJ, "noiseRange1");
		if (noiseRange1J)
			noiseRange[1] = json_number_value(noiseRange1J);

		resetNonJson();
	}


	void onSampleRateChange() override {
		noiseEngine.setCutoffs(APP->engine->getSampleRate());
	}		

	
	void process(const ProcessArgs &args) override {
		static const float holdDetectTime = 2.0f;// seconds

		if (refresh.processInputs()) {
			// vibrations buttons and cv inputs
			for (int i = 0; i < 2; i++) {
				if (trigBypassTriggers[i].process(params[TRIG_BYPASS_PARAMS + i].getValue() + inputs[TRIG_BYPASS_INPUTS + i].getVoltage())) {
					vibrations[i] ^= 0x1;
					secretHoldDetect[i].start((long) (holdDetectTime * args.sampleRate / RefreshCounter::displayRefreshStepSkips));
				}
			}
			
			// noiseRange buttons and cv inputs
			for (int i = 0; i < 2; i++) {
				if (noiseRangeTriggers[i].process(params[NOISE_RANGE_PARAMS + i].getValue() + inputs[NOISE_RANGE_INPUTS + i].getVoltage())) {
					noiseRange[i] = !noiseRange[i];
				}
			}
		}// userInputs refresh

		// trig inputs
		bool trigs[2];
		for (int i = 0; i < 2; i++)	{	
			trigs[i] = sampleTriggers[i].process(inputs[TRIG_INPUTS + i].getVoltage());
			if (trigs[i])
				trigLights[i] = 1.0f;
		}
		
		
		// prepare triggering info for the sample and hold + noise code below
		// -----------------------
		
		bool trigInConnect[2];// incorporates bypass mechanism (vibrations < 2)
		trigInConnect[0] = (vibrations[0] == 1 ? false : inputs[TRIG_INPUTS + 0].isConnected());
		trigInConnect[1] = (vibrations[1] == 1 ? false : inputs[TRIG_INPUTS + 1].isConnected());
		
		// The 0x2000 bit in the next line is to cross trigger the top left of BraneB with trigger of BraneA
		int hasTrigSourceBits = (trigInConnect[0] ? 0x207F : 0x0);// brane 0 is lsbit, brane 13 is bit 13
		// The 0x0040 bit in the next line is to cross trigger the lower right of BraneA with trigger of BraneB
		hasTrigSourceBits |= (trigInConnect[1] ? 0x3FC0 : 0x0);
		
		int receivedTrigBits = 0x0;// brane 0 is lsbit, brane 13 is bit 13
		for (int bi = 0; bi < 2; bi++) {// brane index
			if (vibrations[bi] < 2) {// normal or bypass mode
				if (trigs[bi] && trigInConnect[bi]) {
					receivedTrigBits |= (bi == 0 ? 0x7F : 0x3F80);
				}
			}
			else if (vibrations[bi] == 2) {// yellow mode (only one of the active outs gets the trigger, random choice)
				if (trigs[bi] && trigInConnect[bi]) {
					int cnt = 0;
					int connectedIndexes[7] = {0};
					for (int i = 7 * bi; i < (7 * bi + 7); i++) {
						if (outputs[OUT_OUTPUTS + i].isConnected()) {
							connectedIndexes[cnt++] = i;
						}
					}
					if (cnt > 0) {
						int selected = random::u32() % cnt;	
						receivedTrigBits |= (0x1 << (connectedIndexes[selected]));
					}
				}
			}
			else {// vibrations[bi] == 3 // blue mode (each active active out has 50% chance to get the trigger)
				if (trigs[bi] && trigInConnect[bi]) {
					for (int i = 7 * bi; i < (7 * bi + 7); i++) {
						if (outputs[OUT_OUTPUTS + i].isConnected()) {
							receivedTrigBits |= ((random::u32() % 2) << i);
						}
					}
				}
			}			
		}
		// perform the actual cross triggering
		if (trigs[0] && trigInConnect[0]) {
			receivedTrigBits |= 0x2000;
		}
		if (trigs[1] && trigInConnect[1]) {
			receivedTrigBits |= 0x0040;
		}

		
		// main branes code
		// -----------------------
		
		// sample and hold outputs (noise continually generated or else stepping non-white on S&H only will not work well because of filters)
		noiseEngine.clearCache();
		for (int sh = 0; sh < 14; sh++) {
			if (outputs[OUT_OUTPUTS + sh].isConnected()) {
				float noise = getNoise(sh);// must call even if won't get used below so that proper noise is produced when s&h colored noise
				if ((hasTrigSourceBits & (0x1 << sh)) != 0) {
					if ((receivedTrigBits & (0x1 << sh)) != 0) {
						if (inputs[IN_INPUTS + sh].isConnected())// if input cable
							heldOuts[sh] = inputs[IN_INPUTS + sh].getVoltage();// sample and hold input
						else
							heldOuts[sh] = noise; // sample and hold noise
					}
					// else no rising edge, so simply preserve heldOuts[sh], nothing to do
				}
				else { // no trig connected
					if (inputs[IN_INPUTS + sh].isConnected())
						heldOuts[sh] = inputs[IN_INPUTS + sh].getVoltage();// copy of input if no trig and input
					else
						heldOuts[sh] = noise; // continuous noise if no trig and no input
				}
				outputs[OUT_OUTPUTS + sh].setVoltage(heldOuts[sh]);
			}
		}
		
		if (refresh.processLights()) {
			// Lights
			for (int i = 0; i < 2; i++) {
				float blue = (vibrations[i] == 3 ? 1.0f : 0.0f);
				float yellow = (vibrations[i] == 2 ? 1.0f : 0.0f);
				float red = (vibrations[i] == 1 ? 1.0f : 0.0f);
				float white = (vibrations[i] == 0 ? trigLights[i] : 0.0f);
				trigLights[i] = 0.0f;
				lights[BYPASS_TRIG_LIGHTS + i * 4 + 3].setSmoothBrightness(white, (float)args.sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));
				lights[BYPASS_TRIG_LIGHTS + i * 4 + 2].setBrightness(red);
				lights[BYPASS_TRIG_LIGHTS + i * 4 + 1].setBrightness(yellow);
				lights[BYPASS_TRIG_LIGHTS + i * 4 + 0].setBrightness(blue);
				lights[NOISE_RANGE_LIGHTS + i].setBrightness(noiseRange[i] ? 1.0f : 0.0f);
				
				if (secretHoldDetect[i].process(params[TRIG_BYPASS_PARAMS + i].getValue())) {
					if (vibrations[i] > 1) 
						vibrations[i] = 0;
					else
						vibrations[i] = 2;
				}
			}
			
		}// lightRefreshCounter
		
	}// step()
	
	float getNoise(int sh) {
		float ret = noiseEngine.getNoise(sh);
		
		// noise ranges
		if (noiseRange[0]) {
			if (sh >= 3 && sh <= 6)// 0 to 10 instead of -5 to 5
				ret += 5.0f;
		}
		if (noiseRange[1]) {
			if (sh >= 7 && sh <= 10) {// 0 to 1 instead of -5 to 5
				ret += 5.0f;
				ret *= 0.1f;
			}
			else if (sh >= 11) {// -1 to 1 instead of -5 to 5
				ret *= 0.2f;
			}	
		}
		return ret;
	}
};


struct BranesWidget : ModuleWidget {
	SvgPanel* darkPanel;

	struct PanelThemeItem : MenuItem {
		Branes *module;
		int theme;
		void onAction(const event::Action &e) override {
			module->panelTheme = theme;
		}
		void step() override {
			rightText = (module->panelTheme == theme) ? "✔" : "";
		}
	};	
	struct SecretModeItem : MenuItem {
		Branes *module;
		int braneIndex = 0;
		void onAction(const event::Action &e) override {
			if (module->vibrations[braneIndex] > 1)
				module->vibrations[braneIndex] = 0;// turn off secret mode
			else
				module->vibrations[braneIndex] = 2;// turn on secret mode
		}
	};	
	void appendContextMenu(Menu *menu) override {
		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		Branes *module = dynamic_cast<Branes*>(this->module);
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

		menu->addChild(new MenuLabel());// empty line
		
		MenuLabel *settingsLabel = new MenuLabel();
		settingsLabel->text = "Settings";
		menu->addChild(settingsLabel);
		
		SecretModeItem *secretItemH = createMenuItem<SecretModeItem>("High brane Young mode (long push)", CHECKMARK(module->vibrations[0] > 1));
		secretItemH->module = module;
		menu->addChild(secretItemH);
		
		SecretModeItem *secretItemL = createMenuItem<SecretModeItem>("Low brane Young mode (long push)", CHECKMARK(module->vibrations[1] > 1));
		secretItemL->module = module;
		secretItemL->braneIndex = 1;
		menu->addChild(secretItemL);
	}	
	
	BranesWidget(Branes *module) {
		setModule(module);

		// Main panels from Inkscape
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/Branes-WL.svg")));
        if (module) {
			darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/Branes-DM.svg")));
			darkPanel->visible = false;
			addChild(darkPanel);
		}
		
		// Screws 
		// part of svg panel, no code required
		
		float colRulerCenter = box.size.x / 2.0f;
		static constexpr float rowRulerHoldA = 132.5;
		static constexpr float rowRulerHoldB = 261.5f;
		static constexpr float radiusIn = 35.0f;
		static constexpr float radiusOut = 64.0f;
		static constexpr float offsetIn = 25.0f;
		static constexpr float offsetOut = 46.0f;
		
		
		// BraneA trig intput
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerHoldA), true, module, Branes::TRIG_INPUTS + 0, module ? &module->panelTheme : NULL));
		
		// BraneA inputs
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetIn, rowRulerHoldA + offsetIn), true, module, Branes::IN_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - radiusIn, rowRulerHoldA), true, module, Branes::IN_INPUTS + 1, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetIn, rowRulerHoldA - offsetIn), true, module, Branes::IN_INPUTS + 2, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerHoldA - radiusIn), true, module, Branes::IN_INPUTS + 3, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetIn, rowRulerHoldA - offsetIn), true, module, Branes::IN_INPUTS + 4, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + radiusIn, rowRulerHoldA), true, module, Branes::IN_INPUTS + 5, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetIn, rowRulerHoldA + offsetIn), true, module, Branes::IN_INPUTS + 6, module ? &module->panelTheme : NULL));

		// BraneA outputs
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetOut, rowRulerHoldA + offsetOut), false, module, Branes::OUT_OUTPUTS + 0, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - radiusOut, rowRulerHoldA), false, module, Branes::OUT_OUTPUTS + 1, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetOut, rowRulerHoldA - offsetOut), false, module, Branes::OUT_OUTPUTS + 2, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerHoldA - radiusOut), false, module, Branes::OUT_OUTPUTS + 3, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetOut, rowRulerHoldA - offsetOut), false, module, Branes::OUT_OUTPUTS + 4, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + radiusOut, rowRulerHoldA), false, module, Branes::OUT_OUTPUTS + 5, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetOut, rowRulerHoldA + offsetOut), false, module, Branes::OUT_OUTPUTS + 6, module ? &module->panelTheme : NULL));
		
		
		// BraneB trig intput
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerHoldB), true, module, Branes::TRIG_INPUTS + 1, module ? &module->panelTheme : NULL));
		
		// BraneB inputs
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetIn, rowRulerHoldB - offsetIn), true, module, Branes::IN_INPUTS + 7, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + radiusIn, rowRulerHoldB), true, module, Branes::IN_INPUTS + 8, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetIn, rowRulerHoldB + offsetIn), true, module, Branes::IN_INPUTS + 9, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerHoldB + radiusIn), true, module, Branes::IN_INPUTS + 10, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetIn, rowRulerHoldB + offsetIn), true, module, Branes::IN_INPUTS + 11, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - radiusIn, rowRulerHoldB), true, module, Branes::IN_INPUTS + 12, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetIn, rowRulerHoldB - offsetIn), true, module, Branes::IN_INPUTS + 13, module ? &module->panelTheme : NULL));


		// BraneB outputs
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetOut, rowRulerHoldB - offsetOut), false, module, Branes::OUT_OUTPUTS + 7, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + radiusOut, rowRulerHoldB), false, module, Branes::OUT_OUTPUTS + 8, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetOut, rowRulerHoldB + offsetOut), false, module, Branes::OUT_OUTPUTS + 9, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerHoldB + radiusOut), false, module, Branes::OUT_OUTPUTS + 10, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetOut, rowRulerHoldB + offsetOut), false, module, Branes::OUT_OUTPUTS + 11, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - radiusOut, rowRulerHoldB), false, module, Branes::OUT_OUTPUTS + 12, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetOut, rowRulerHoldB - offsetOut), false, module, Branes::OUT_OUTPUTS + 13, module ? &module->panelTheme : NULL));
		
		
		// Trigger bypass (aka Vibrations)
		// Bypass buttons
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + 40.0f, 380.0f - 334.5f), module, Branes::TRIG_BYPASS_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + 40.0f, 380.0f - 31.5f), module, Branes::TRIG_BYPASS_PARAMS + 1, module ? &module->panelTheme : NULL));
		// Bypass cv inputs
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + 68.0f, 380.0f - 315.5f), true, module, Branes::TRIG_BYPASS_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + 68.0f, 380.0f - 50.5f), true, module, Branes::TRIG_BYPASS_INPUTS + 1, module ? &module->panelTheme : NULL));
		// Bypass LEDs near buttons
		addChild(createLightCentered<SmallLight<GeoBlueYellowRedWhiteLight>>(VecPx(colRulerCenter + 53.0f, 380.0f - 327.5f), module, Branes::BYPASS_TRIG_LIGHTS + 0 * 4));
		addChild(createLightCentered<SmallLight<GeoBlueYellowRedWhiteLight>>(VecPx(colRulerCenter + 53.0f, 380.0f - 38.5f), module, Branes::BYPASS_TRIG_LIGHTS + 1 * 4));
				
		// Noise range
		// Range buttons
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - 40.0f, 380.0f - 334.5f), module, Branes::NOISE_RANGE_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - 40.0f, 380.0f - 31.5f), module, Branes::NOISE_RANGE_PARAMS + 1, module ? &module->panelTheme : NULL));
		// Range cv inputs
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - 68.0f, 380.0f - 315.5f), true, module, Branes::NOISE_RANGE_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - 68.0f, 380.0f - 50.5f), true, module, Branes::NOISE_RANGE_INPUTS + 1, module ? &module->panelTheme : NULL));
		// Range LEDs near buttons
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 53.0f, 380.0f - 327.5f), module, Branes::NOISE_RANGE_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 53.0f, 380.0f - 38.5f), module, Branes::NOISE_RANGE_LIGHTS + 1));

	}
	
	void step() override {
		if (module) {
			panel->visible = ((((Branes*)module)->panelTheme) == 0);
			darkPanel->visible  = ((((Branes*)module)->panelTheme) == 1);
		}
		Widget::step();
	}
};

Model *modelBranes = createModel<Branes, BranesWidget>("Branes");