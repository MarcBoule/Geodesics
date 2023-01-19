//***********************************************************************************************
//Neutron Powered Morphing module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental plugins by Andrew Belt and graphics  
//  from the Component Library by Wes Milholen. 
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//***********************************************************************************************


#include "Geodesics.hpp"


struct Pulsars : Module {
	enum ParamIds {
		ENUMS(VOID_PARAMS, 2),// push-button
		ENUMS(REV_PARAMS, 2),// push-button
		ENUMS(RND_PARAMS, 2),// push-button
		ENUMS(CVLEVEL_PARAMS, 2),// push-button
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(INA_INPUTS, 8),
		INB_INPUT,
		ENUMS(LFO_INPUTS, 2),
		ENUMS(VOID_INPUTS, 2),
		ENUMS(REV_INPUTS, 2),
		NUM_INPUTS
	};
	enum OutputIds {
		OUTA_OUTPUT,
		ENUMS(OUTB_OUTPUTS, 8),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(LFO_LIGHTS, 2),
		ENUMS(MIXA_LIGHTS, 8),
		ENUMS(MIXB_LIGHTS, 8),
		ENUMS(VOID_LIGHTS, 2),
		ENUMS(REV_LIGHTS, 2),
		ENUMS(RND_LIGHTS, 2),
		ENUMS(CVALEVEL_LIGHTS, 3),// White, but three lights (light 0 is cvMode == 0, light 1 is cvMode == 1, light 2 is cvMode == 2)
		ENUMS(CVBLEVEL_LIGHTS, 3),// Same but for lower pulsar
		NUM_LIGHTS
	};
	
	
	// Constants
	static constexpr float epsilon = 0.001f;// pulsar crossovers at epsilon and 1-epsilon in 0.0f to 1.0f space

	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	int cvModes[2];// 0 is -5v to 5v, 1 is 0v to 10v, 2 is new ALL mode (0-10V); index 0 is upper Pulsar, index 1 is lower Pulsar
	bool isVoid[2];
	bool isReverse[2];
	bool isRandom[2];
	
	// No need to save, with reset
	int connectedNum[2];
	int connected[2][8];// concatenated list of input indexes of connected ports
	int connectedRand[2][8];// concatenated list of input indexes of connected ports, for ALL mode supernova
	bool topCross[2];
	int index[2];// always between 0 and 7
	int indexNext[2];// always between 0 and 7
	int numChanForPoly[2];
	
	
	// No need to save, no reset
	Trigger voidTriggers[2];
	Trigger revTriggers[2];
	Trigger rndTriggers[2];
	Trigger cvLevelTriggers[2];
	float lfoLights[2] = {0.0f, 0.0f};
	RefreshCounter refresh;

	
	void updateConnected() {
		// builds packed list of connected ports for both pulsars, can be empty list with num = 0
		// this method takes care of isVoid and isReverse
		int oldConnectedNum[2] = {connectedNum[0], connectedNum[1]};
		connectedNum[0] = 0;
		connectedNum[1] = 0;
		for (int i = 0; i < 8; i++) {
			// Pulsar A
			int irA = isReverse[0] ? ((8 - i) & 0x7) : i;
			if (isVoid[0] || inputs[INA_INPUTS + irA].isConnected()) {
				connected[0][connectedNum[0]] = irA;
				connectedNum[0]++;
			}
			// Pulsar B
			int irB = isReverse[1] ? ((8 - i) & 0x7) : i;
			if (isVoid[1] || outputs[OUTB_OUTPUTS + irB].isConnected()) {
				connected[1][connectedNum[1]] = irB;
				connectedNum[1]++;
			}
		}
		for (int i = 0; i < 2; i++) {
			if (oldConnectedNum[i] != connectedNum[i]) {// ok to update when this happens, since user is slow such that one cable changed at most before this method is re-run, and if many cables change at one as a result of dataFromJson(), connectedNum[x] will have been reset anyways to trigger a refresh
				updateConnectedRand(i);
			}
		}
	}
	
	void updateConnectedRand(int bnum) {
		int tmpList[7];
		connectedRand[bnum][0] = connected[bnum][0];// first element is always the same (no random)
		int connectedRandNum = 1;
		if (connectedNum[bnum] < 2) 
			return;
		
		for (int i = 1; i < connectedNum[bnum]; i++) {
			tmpList[i - 1] = connected[bnum][i];
		}
		for (int i = connectedNum[bnum] - 2; i >= 0; i--) {
			int pickIndex = (random::u32() % (i + 1));
			connectedRand[bnum][connectedRandNum] = tmpList[pickIndex];
			connectedRandNum++;
			tmpList[pickIndex] = tmpList[i];
		}
	}
	
	void updateIndexNext(int bnum) {// brane number to update, 0 is upper, 1 is lower
		if (connectedNum[bnum] <= 1) {
			indexNext[bnum] = 0;
		}
		else {
			if (isRandom[bnum]) {
				indexNext[bnum] = random::u32() % (connectedNum[bnum] - 1);
				if (indexNext[bnum] == index[bnum])
					indexNext[bnum] = connectedNum[bnum] - 1;							
			}
			else {
				indexNext[bnum] = (index[bnum] + 1) % connectedNum[bnum];
			}
		}
	}
	
	void updateNumChanForPoly() {// also sets the number of outputs so we don't have to do this every sample
		// calc number of channels		
		// top pulsar
		numChanForPoly[0] = inputs[INA_INPUTS + 0].getChannels();
		for (int c = 1; c < 8; c++) {
			numChanForPoly[0] = std::max(numChanForPoly[0], inputs[INA_INPUTS + c].getChannels());
		}
		// bottom pulsar
		if (inputs[INB_INPUT].isConnected()) {
			numChanForPoly[1] = inputs[INB_INPUT].getChannels();
		}
		else {// multidimensional trick
			numChanForPoly[1] = numChanForPoly[0];
		}
		
		// set outputs
		// top pulsar
		outputs[OUTA_OUTPUT].setChannels(numChanForPoly[0]);
		// bottom pulsar
		for (int c = 0; c < 8; c++) {
			if (inputs[INB_INPUT].isConnected()) {
				outputs[OUTB_OUTPUTS + c].setChannels(numChanForPoly[1]);
			}
			else {
				outputs[OUTB_OUTPUTS + c].setChannels(inputs[INA_INPUTS + c].getChannels());
			}
		}
	}
	
	
	Pulsars() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(VOID_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Top pulsar void");
		configParam(REV_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Top pulsar reverse");	
		configParam(RND_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Top pulsar random");
		configParam(CVLEVEL_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Top pulsar uni/bi-polar");	
		configParam(VOID_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Bottom pulsar void");
		configParam(REV_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Bottom pulsar reverse");	
		configParam(RND_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Bottom pulsar random");		
		configParam(CVLEVEL_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Bottom pulsar uni/bi-polar");

		for (int i = 0; i < 8; i++) {
			configInput(INA_INPUTS + i, string::f("Top pulsar, #%i", i + 1));
		}
		configOutput(OUTA_OUTPUT, "Top pulsar");
		
		configInput(INB_INPUT, "Bottom pulsar");
		for (int i = 0; i < 8; i++) {
			configOutput(OUTB_OUTPUTS + i, string::f("Bottom pulsar, #%i", i + 1));
		}
		
		configInput(LFO_INPUTS + 0, "Top pulsar rotation");
		configInput(LFO_INPUTS + 1, "Bottom pulsar rotation");
		configInput(VOID_INPUTS + 0, "Top pulsar cosmic void");
		configInput(VOID_INPUTS + 1, "Bottom pulsar cosmic void");
		configInput(REV_INPUTS + 0, "Top pulsar reverse");
		configInput(REV_INPUTS + 1, "Bottom pulsar reverse");

		onReset();

		panelTheme = (loadDarkAsDefault() ? 1 : 0);
	}

	
	void onReset() override {
		for (int i = 0; i < 2; i++) {
			cvModes[i] = 0;
			isVoid[i] = false;
			isReverse[i] = false;
			isRandom[i] = false;
		}
		resetNonJson();
	}
	void resetNonJson() {
		connectedNum[0] = 0;// need this to start change detection to trigger new connectedRand[][] generation
		connectedNum[1] = 0;// idem
		updateConnected();// will update connectedRand[][] also if cables connectedNum[x] non-zero
		updateNumChanForPoly();
		for (int i = 0; i < 2; i++) {
			topCross[i] = false;
			index[i] = 0;
			updateIndexNext(i);
		}
	}

	
	void onRandomize() override {
		for (int i = 0; i < 2; i++) {
			isVoid[i] = (random::u32() % 2) > 0;
			isReverse[i] = (random::u32() % 2) > 0;
			isRandom[i] = (random::u32() % 2) > 0;
		}
	}

	
	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// isVoid
		json_object_set_new(rootJ, "isVoid0", json_real(isVoid[0]));
		json_object_set_new(rootJ, "isVoid1", json_real(isVoid[1]));
		
		// isReverse
		json_object_set_new(rootJ, "isReverse0", json_real(isReverse[0]));
		json_object_set_new(rootJ, "isReverse1", json_real(isReverse[1]));
		
		// isRandom
		json_object_set_new(rootJ, "isRandom0", json_real(isRandom[0]));
		json_object_set_new(rootJ, "isRandom1", json_real(isRandom[1]));
		
		// cvMode
		// json_object_set_new(rootJ, "cvMode", json_integer(cvMode));// deprecated
		json_object_set_new(rootJ, "cvMode0", json_integer(cvModes[0]));
		json_object_set_new(rootJ, "cvMode1", json_integer(cvModes[1]));
		
		return rootJ;
	}

	
	void dataFromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);

		// isVoid
		json_t *isVoid0J = json_object_get(rootJ, "isVoid0");
		if (isVoid0J)
			isVoid[0] = json_number_value(isVoid0J);
		json_t *isVoid1J = json_object_get(rootJ, "isVoid1");
		if (isVoid1J)
			isVoid[1] = json_number_value(isVoid1J);

		// isReverse
		json_t *isReverse0J = json_object_get(rootJ, "isReverse0");
		if (isReverse0J)
			isReverse[0] = json_number_value(isReverse0J);
		json_t *isReverse1J = json_object_get(rootJ, "isReverse1");
		if (isReverse1J)
			isReverse[1] = json_number_value(isReverse1J);

		// isRandom
		json_t *isRandom0J = json_object_get(rootJ, "isRandom0");
		if (isRandom0J)
			isRandom[0] = json_number_value(isRandom0J);
		json_t *isRandom1J = json_object_get(rootJ, "isRandom1");
		if (isRandom1J)
			isRandom[1] = json_number_value(isRandom1J);

		// cvMode
		json_t *cvModeJ = json_object_get(rootJ, "cvMode");// legacy
		if (cvModeJ) {
			int cvModeRead = json_integer_value(cvModeJ);
			cvModes[0] = (cvModeRead & 0x1);			
			cvModes[1] = (cvModeRead >> 1);
		}
		else {
			json_t *cvModes0J = json_object_get(rootJ, "cvMode0");
			if (cvModes0J) {
				cvModes[0] = json_integer_value(cvModes0J);
			}
			json_t *cvModes1J = json_object_get(rootJ, "cvMode1");
			if (cvModes1J) {
				cvModes[1] = json_integer_value(cvModes1J);
			}
		}

		resetNonJson();
	}

	
	void process(const ProcessArgs &args) override {
		if (refresh.processInputs()) {
			// Void, Reverse and Random buttons
			for (int i = 0; i < 2; i++) {
				if (voidTriggers[i].process(params[VOID_PARAMS + i].getValue() + inputs[VOID_INPUTS + i].getVoltage())) {
					isVoid[i] = !isVoid[i];
				}
				if (revTriggers[i].process(params[REV_PARAMS + i].getValue() + inputs[REV_INPUTS + i].getVoltage())) {
					isReverse[i] = !isReverse[i];
				}
				if (rndTriggers[i].process(params[RND_PARAMS + i].getValue())) {
					isRandom[i] = !isRandom[i];
					if (isRandom[i] && cvModes[i] == 2) {
						updateConnectedRand(i);
					}
				}
			}
			
			// CV Level buttons
			for (int i = 0; i < 2; i++) {
				if (cvLevelTriggers[i].process(params[CVLEVEL_PARAMS + i].getValue())) {
					cvModes[i]++;
					if (cvModes[i] > 2)
						cvModes[i] = 0;
					topCross[i] = false;
				}
			}
			
			updateConnected();
			updateNumChanForPoly();
		}// userInputs refresh


		// LFO values (normalized to 0.0f to 1.0f space, inputs clamped and offset adjusted depending cvMode)
		float lfoVal[2];
		lfoVal[0] = inputs[LFO_INPUTS + 0].getVoltage();
		lfoVal[1] = inputs[LFO_INPUTS + 1].isConnected() ? inputs[LFO_INPUTS + 1].getVoltage() : lfoVal[0];
		for (int i = 0; i < 2; i++)
			lfoVal[i] = clamp( (lfoVal[i] + (cvModes[i] == 0 ? 5.0f : 0.0f)) / 10.0f , 0.0f , 1.0f);
		
		
		// Pulsar A
		if (connectedNum[0] > 0) {
			float indexPercent;
			float indexNextPercent;
			int *srcConnected = connected[0];
			if (cvModes[0] < 2) {
				// regular modes
				if (!isVoid[0]) {
					if (index[0] >= connectedNum[0]) {// ensure start on valid input when no void
						index[0] = 0;
					}
					if (indexNext[0] >= connectedNum[0]) {
						updateIndexNext(0);
					}
				}			
				indexPercent = topCross[0] ? (1.0f - lfoVal[0]) : lfoVal[0];
				indexNextPercent = 1.0f - indexPercent;
			}
			else {
				// new ALL mode
				lfoVal[0] *= (float)connectedNum[0];
				index[0] = (int)lfoVal[0];
				indexNext[0] = (index[0] + 1);
				indexNextPercent = lfoVal[0] - (float)index[0];
				indexPercent = 1.0f - indexNextPercent;
				if (index[0] >= connectedNum[0]) index[0] = 0;
				if (indexNext[0] >= connectedNum[0]) indexNext[0] = 0;
				if (isRandom[0])
					srcConnected = connectedRand[0];
			}
			for (int c = 0; c < numChanForPoly[0] ; c++) {
				float currentV = indexPercent * inputs[INA_INPUTS + srcConnected[index[0]]].getVoltage(c);
				float nextV = indexNextPercent * inputs[INA_INPUTS + srcConnected[indexNext[0]]].getVoltage(c);
				outputs[OUTA_OUTPUT].setVoltage(currentV + nextV, c);
			}
			for (int i = 0; i < 8; i++) {
				lights[MIXA_LIGHTS + i].setBrightness(((i == srcConnected[index[0]]) ? indexPercent : 0.0f) + ((i == srcConnected[indexNext[0]]) ? indexNextPercent : 0.0f));
			}
		}
		else {
			outputs[OUTA_OUTPUT].setVoltage(0.0f);
			for (int i = 0; i < 8; i++)
				lights[MIXA_LIGHTS + i].setBrightness(0.0f);
		}


		// Pulsar B
		if (connectedNum[1] > 0) {
			float indexPercent;
			float indexNextPercent;
			int *srcConnected = connected[1];
			if (cvModes[1] < 2) {
				// regular modes
				if (!isVoid[1]) {
					if (index[1] >= connectedNum[1]) {// ensure start on valid input when no void
						index[1] = 0;
					}
					if (indexNext[1] >= connectedNum[1]) {
						updateIndexNext(1);
					}
				}					
				indexPercent = topCross[1] ? (1.0f - lfoVal[1]) : lfoVal[1];
				indexNextPercent = 1.0f - indexPercent;
			}
			else {
				// new ALL mode
				lfoVal[1] *= (float)connectedNum[1];
				index[1] = (int)lfoVal[1];
				indexNext[1] = (index[1] + 1);
				indexNextPercent = lfoVal[1] - (float)index[1];
				indexPercent = 1.0f - indexNextPercent;
				if (index[1] >= connectedNum[1]) index[1] = 0;
				if (indexNext[1] >= connectedNum[1]) indexNext[1] = 0;
				if (isRandom[1])
					srcConnected = connectedRand[1];
			}
			for (int i = 0; i < 8; i++) {
				if (inputs[INB_INPUT].isConnected()) {
					for (int c = 0; c < numChanForPoly[1] ; c++) {
						float currentV = ((i == srcConnected[index[1]]) ? (indexPercent * inputs[INB_INPUT].getVoltage(c)) : 0.0f);
						float nextV = ((i == srcConnected[indexNext[1]]) ? (indexNextPercent * inputs[INB_INPUT].getVoltage(c)) : 0.0f);
						outputs[OUTB_OUTPUTS + i].setVoltage(currentV + nextV, c);
					}
				}
				else {// mutidimensional trick
					for (int c = 0; c < numChanForPoly[1] ; c++) {
						float currentV = ((i == srcConnected[index[1]]) ? (indexPercent * inputs[INA_INPUTS + i].getVoltage(c)) : 0.0f);
						float nextV = ((i == srcConnected[indexNext[1]]) ? (indexNextPercent * inputs[INA_INPUTS + i].getVoltage(c)) : 0.0f);
						outputs[OUTB_OUTPUTS + i].setVoltage(currentV + nextV, c);
					}
				}
				lights[MIXB_LIGHTS + i].setBrightness(((i == srcConnected[index[1]]) ? indexPercent : 0.0f) + ((i == srcConnected[indexNext[1]]) ? indexNextPercent : 0.0f));
			}
		}
		else {
			for (int i = 0; i < 8; i++) {
				outputs[OUTB_OUTPUTS + i].setVoltage(0.0f);
				lights[MIXB_LIGHTS + i].setBrightness(0.0f);
			}
		}

		
		// Pulsar crossovers (LFO detection)
		for (int bnum = 0; bnum < 2; bnum++) {
			if (cvModes[bnum] < 2) {
				if ( (topCross[bnum] && lfoVal[bnum] > (1.0f - epsilon)) || (!topCross[bnum] && lfoVal[bnum] < epsilon) ) {
					topCross[bnum] = !topCross[bnum];// switch to opposite detection
					index[bnum] = indexNext[bnum];
					updateIndexNext(bnum);
					lfoLights[bnum] = 1.0f;
				}
			}
		}

		
		// lights
		if (refresh.processLights()) {
			// Void, Reverse and Random lights
			for (int i = 0; i < 2; i++) {
				lights[VOID_LIGHTS + i].setBrightness(isVoid[i] ? 1.0f : 0.0f);
				lights[REV_LIGHTS + i].setBrightness(isReverse[i] ? 1.0f : 0.0f);
				lights[RND_LIGHTS + i].setBrightness(isRandom[i] ? 1.0f : 0.0f);
			}
			
			// CV mode (cv level) lights
			for (int i = 0; i < 3; i++) {
				lights[CVALEVEL_LIGHTS + i].setBrightness(cvModes[0] == i ? 1.0f : 0.0f);
				lights[CVBLEVEL_LIGHTS + i].setBrightness(cvModes[1] == i ? 1.0f : 0.0f);
			}
			
			// LFO lights
			for (int i = 0; i < 2; i++) {
				lights[LFO_LIGHTS + i].setSmoothBrightness(lfoLights[i], (float)args.sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));
				lfoLights[i] = 0.0f;
			}
			
		}// lightRefreshCounter
		
	}// step()
};


struct PulsarsWidget : ModuleWidget {
	int lastPanelTheme = -1;
	std::shared_ptr<window::Svg> light_svg;
	std::shared_ptr<window::Svg> dark_svg;

	void appendContextMenu(Menu *menu) override {
		Pulsars *module = dynamic_cast<Pulsars*>(this->module);
		assert(module);

		createPanelThemeMenu(menu, &(module->panelTheme));
	}	
	
	PulsarsWidget(Pulsars *module) {
		setModule(module);

		// Main panels from Inkscape
 		light_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/Pulsars-WL.svg"));
		dark_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/Pulsars-DM.svg"));
		int panelTheme = isDark(module ? (&(((Pulsars*)module)->panelTheme)) : NULL) ? 1 : 0;// need this here since step() not called for module browser
		setPanel(panelTheme == 0 ? light_svg : dark_svg);		
		
		// Screws 
		// part of svg panel, no code required
		
		float colRulerCenter = box.size.x / 2.0f;
		static constexpr float rowRulerPulsarA = 127.5;
		static constexpr float rowRulerPulsarB = 261.5f;
		float rowRulerLFOlights = 380.0f - 185.5f;
		static constexpr float offsetLFOlightsX = 25.0f;
		static constexpr float radiusLeds = 23.0f;
		static constexpr float radiusJacks = 46.0f;
		static constexpr float offsetLeds = 17.0f;
		static constexpr float offsetJacks = 33.0f;
		static constexpr float offsetLFO = 24.0f;// used also for void and reverse CV input jacks
		static constexpr float offsetLedX = 13.0f;// adds/subs to offsetLFO for void and reverse lights
		static constexpr float offsetButtonX = 26.0f;// adds/subs to offsetLFO for void and reverse buttons
		static constexpr float offsetLedY = 11.0f;// adds/subs to offsetLFO for void and reverse lights
		static constexpr float offsetButtonY = 18.0f;// adds/subs to offsetLFO for void and reverse buttons
		static constexpr float offsetRndButtonX = 58.0f;// from center of pulsar
		static constexpr float offsetRndButtonY = 24.0f;// from center of pulsar
		static constexpr float offsetRndLedX = 63.0f;// from center of pulsar
		static constexpr float offsetRndLedY = 11.0f;// from center of pulsar


		// PulsarA center output
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerPulsarA), false, module, Pulsars::OUTA_OUTPUT, module ? &module->panelTheme : NULL));
		
		// PulsarA inputs
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerPulsarA - radiusJacks), true, module, Pulsars::INA_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetJacks, rowRulerPulsarA - offsetJacks), true, module, Pulsars::INA_INPUTS + 1, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + radiusJacks, rowRulerPulsarA), true, module, Pulsars::INA_INPUTS + 2, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetJacks, rowRulerPulsarA + offsetJacks), true, module, Pulsars::INA_INPUTS + 3, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerPulsarA + radiusJacks), true, module, Pulsars::INA_INPUTS + 4, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetJacks, rowRulerPulsarA + offsetJacks), true, module, Pulsars::INA_INPUTS + 5, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - radiusJacks, rowRulerPulsarA), true, module, Pulsars::INA_INPUTS + 6, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetJacks, rowRulerPulsarA - offsetJacks), true, module, Pulsars::INA_INPUTS + 7, module ? &module->panelTheme : NULL));

		// PulsarA lights
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter, rowRulerPulsarA - radiusLeds), module, Pulsars::MIXA_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter + offsetLeds, rowRulerPulsarA - offsetLeds), module, Pulsars::MIXA_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter + radiusLeds, rowRulerPulsarA), module, Pulsars::MIXA_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter + offsetLeds, rowRulerPulsarA + offsetLeds), module, Pulsars::MIXA_LIGHTS + 3));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter, rowRulerPulsarA + radiusLeds), module, Pulsars::MIXA_LIGHTS + 4));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - offsetLeds, rowRulerPulsarA + offsetLeds), module, Pulsars::MIXA_LIGHTS + 5));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - radiusLeds, rowRulerPulsarA), module, Pulsars::MIXA_LIGHTS + 6));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - offsetLeds, rowRulerPulsarA - offsetLeds), module, Pulsars::MIXA_LIGHTS + 7));
		
		// PulsarA void (jack, light and button)
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetJacks - offsetLFO, rowRulerPulsarA - offsetJacks - offsetLFO), true, module, Pulsars::VOID_INPUTS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - offsetJacks - offsetLFO + offsetLedX, rowRulerPulsarA - offsetJacks - offsetLFO - offsetLedY), module, Pulsars::VOID_LIGHTS + 0));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - offsetJacks - offsetLFO + offsetButtonX, rowRulerPulsarA - offsetJacks - offsetLFO - offsetButtonY), module, Pulsars::VOID_PARAMS + 0, module ? &module->panelTheme : NULL));

		// PulsarA reverse (jack, light and button)
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetJacks + offsetLFO, rowRulerPulsarA - offsetJacks - offsetLFO), true, module, Pulsars::REV_INPUTS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + offsetJacks + offsetLFO - offsetLedX, rowRulerPulsarA - offsetJacks - offsetLFO - offsetLedY), module, Pulsars::REV_LIGHTS + 0));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + offsetJacks + offsetLFO - offsetButtonX, rowRulerPulsarA - offsetJacks - offsetLFO - offsetButtonY), module, Pulsars::REV_PARAMS + 0, module ? &module->panelTheme : NULL));
		
		// PulsarA random (light and button)
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + offsetRndLedX, rowRulerPulsarA + offsetRndLedY), module, Pulsars::RND_LIGHTS + 0));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + offsetRndButtonX, rowRulerPulsarA + offsetRndButtonY), module, Pulsars::RND_PARAMS + 0, module ? &module->panelTheme : NULL));

		// PulsarA CV level (lights and button)
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - 62.0f, 380.0f - 224.5f), module, Pulsars::CVLEVEL_PARAMS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 74.0f, 380.0f - 220.5f), module, Pulsars::CVALEVEL_LIGHTS + 0));// Bi
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 66.0f, 380.0f - 212.5f), module, Pulsars::CVALEVEL_LIGHTS + 1));// Uni
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 51.0f, 380.0f - 231.5f), module, Pulsars::CVALEVEL_LIGHTS + 2));// Add

		// PulsarA LFO input and light
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - 52.0f, 380.0f - 189.5f), true, module, Pulsars::LFO_INPUTS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - offsetLFOlightsX, rowRulerLFOlights), module, Pulsars::LFO_LIGHTS + 0));
		
		
		// PulsarB center input
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerPulsarB), true, module, Pulsars::INB_INPUT, module ? &module->panelTheme : NULL));
		
		// PulsarB outputs
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerPulsarB - radiusJacks), false, module, Pulsars::OUTB_OUTPUTS + 0, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetJacks, rowRulerPulsarB - offsetJacks), false, module, Pulsars::OUTB_OUTPUTS + 1, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + radiusJacks, rowRulerPulsarB), false, module, Pulsars::OUTB_OUTPUTS + 2, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetJacks, rowRulerPulsarB + offsetJacks), false, module, Pulsars::OUTB_OUTPUTS + 3, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerPulsarB + radiusJacks), false, module, Pulsars::OUTB_OUTPUTS + 4, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetJacks, rowRulerPulsarB + offsetJacks), false, module, Pulsars::OUTB_OUTPUTS + 5, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - radiusJacks, rowRulerPulsarB), false, module, Pulsars::OUTB_OUTPUTS + 6, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetJacks, rowRulerPulsarB - offsetJacks), false, module, Pulsars::OUTB_OUTPUTS + 7, module ? &module->panelTheme : NULL));

		// PulsarB lights
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter, rowRulerPulsarB - radiusLeds), module, Pulsars::MIXB_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter + offsetLeds, rowRulerPulsarB - offsetLeds), module, Pulsars::MIXB_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter + radiusLeds, rowRulerPulsarB), module, Pulsars::MIXB_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter + offsetLeds, rowRulerPulsarB + offsetLeds), module, Pulsars::MIXB_LIGHTS + 3));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter, rowRulerPulsarB + radiusLeds), module, Pulsars::MIXB_LIGHTS + 4));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - offsetLeds, rowRulerPulsarB + offsetLeds), module, Pulsars::MIXB_LIGHTS + 5));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - radiusLeds, rowRulerPulsarB), module, Pulsars::MIXB_LIGHTS + 6));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - offsetLeds, rowRulerPulsarB - offsetLeds), module, Pulsars::MIXB_LIGHTS + 7));
		
		// PulsarB void (jack, light and button)
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetJacks - offsetLFO, rowRulerPulsarB + offsetJacks + offsetLFO), true, module, Pulsars::VOID_INPUTS + 1, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - offsetJacks - offsetLFO + offsetLedX, rowRulerPulsarB + offsetJacks + offsetLFO + offsetLedY), module, Pulsars::VOID_LIGHTS + 1));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - offsetJacks - offsetLFO + offsetButtonX, rowRulerPulsarB + offsetJacks + offsetLFO + offsetButtonY), module, Pulsars::VOID_PARAMS + 1, module ? &module->panelTheme : NULL));

		// PulsarB reverse (jack, light and button)
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetJacks + offsetLFO, rowRulerPulsarB + offsetJacks + offsetLFO), true, module, Pulsars::REV_INPUTS + 1, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + offsetJacks + offsetLFO - offsetLedX, rowRulerPulsarB + offsetJacks + offsetLFO + offsetLedY), module, Pulsars::REV_LIGHTS + 1));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + offsetJacks + offsetLFO - offsetButtonX, rowRulerPulsarB + offsetJacks + offsetLFO + offsetButtonY), module, Pulsars::REV_PARAMS + 1, module ? &module->panelTheme : NULL));
		
		// PulsarB random (light and button)
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - offsetRndLedX, rowRulerPulsarB - offsetRndLedY), module, Pulsars::RND_LIGHTS + 1));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - offsetRndButtonX, rowRulerPulsarB - offsetRndButtonY), module, Pulsars::RND_PARAMS + 1, module ? &module->panelTheme : NULL));
		
		// PulsarB CV level (lights and button)
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + 62.0f, 380.0f - 145.5f), module, Pulsars::CVLEVEL_PARAMS + 1,  module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + 74.0f, 380.0f - 150.5f), module, Pulsars::CVBLEVEL_LIGHTS + 0));// Bi
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + 66.0f, 380.0f - 158.5f), module, Pulsars::CVBLEVEL_LIGHTS + 1));// Uni
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + 51.0f, 380.0f - 138.5f), module, Pulsars::CVBLEVEL_LIGHTS + 2));// All

		// PulsarA LFO input and light
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + 52.0f, 380.0f - 182.5f), true, module, Pulsars::LFO_INPUTS + 1, module ? &module->panelTheme : NULL));		
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + offsetLFOlightsX, rowRulerLFOlights), module, Pulsars::LFO_LIGHTS + 1));
	}
	
	void step() override {
		int panelTheme = isDark(module ? (&(((Pulsars*)module)->panelTheme)) : NULL) ? 1 : 0;
		if (lastPanelTheme != panelTheme) {
			lastPanelTheme = panelTheme;
			SvgPanel* panel = (SvgPanel*)getPanel();
			panel->setBackground(panelTheme == 0 ? light_svg : dark_svg);
			panel->fb->dirty = true;
		}
		Widget::step();
	}
};

Model *modelPulsars = createModel<Pulsars, PulsarsWidget>("Pulsars");