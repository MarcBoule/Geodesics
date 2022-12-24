//***********************************************************************************************
//Relativistic Oscillator module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental plugins by Andrew Belt and graphics  
//  from the Component Library by Wes Milholen. 
//Also based on the BogAudio FM-OP oscillator by Matt Demanett
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//***********************************************************************************************


#include "Geodesics.hpp"
#include "EnergyOsc.hpp"


struct DarkEnergy : Module {
	enum ParamIds {
		ENUMS(PLANCK_PARAMS, 2),// push buttons
		ENUMS(MODTYPE_PARAMS, 2),// push buttons
		ROUTING_PARAM,// push button
		ENUMS(FREQ_PARAMS, 2),// rotary knobs
		FREQ_PARAM,// rotary knobs
		DEPTHCV_PARAM,// rotary knob, FM depth CV aka antigravity CV
		ENUMS(DEPTH_PARAMS, 2),// rotary knobs, FM depth aka antigravity
		MOMENTUMCV_PARAM,// rotary knob, feedback CV
		ENUMS(MOMENTUM_PARAMS, 2),// rotary knobs, feedback
		CROSS_PARAM,
		RESET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(FREQCV_INPUTS, 2), // respectively "mass" and "speed of light"
		FREQCV_INPUT, // main voct input
		MULTIPLY_INPUT,
		ANTIGRAV_INPUT,
		MOMENTUM_INPUT,
		ENUMS(RESET_INPUTS, 2),
		NUM_INPUTS
	};
	enum OutputIds {
		ENERGY_OUTPUT,// main output
		M_OUTPUT,// m oscillator output
		C_OUTPUT,// c oscillator output
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(PLANCK_LIGHTS, 4), // low M (yellow), ratio M (blue), low C (yellow), ratio C (blue)
		ENUMS(ADD_LIGHTS, 2),
		ENUMS(AMP_LIGHTS, 2),
		ENUMS(ROUTING_LIGHTS, 3),
		ENUMS(MOMENTUM_LIGHTS, 2),
		ENUMS(FREQ_ROUTING_LIGHTS, 2 * 2),// room for blue/yellow
		CROSS_LIGHT,
		ENUMS(RESET_LIGHTS, 2),
		NUM_LIGHTS
	};
	
	
	// Constants
	static const int N_POLY = 16;
	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	FMOp oscM[N_POLY];
	FMOp oscC[N_POLY];
	int routing;// routing of knob 1. 
		// 0 is independant (i.e. blue only) (bottom light, light index 0),
		// 1 is control (i.e. blue and yellow) (top light, light index 1),
		// 2 is spread (i.e. blue and inv yellow) (middle, light index 2)
	int plancks[2];// index is left/right, value is: 0 = not quantized, 1 = 5th+octs, 2 = adds -10V offset (LFO)
	int modtypes[2];// index is left/right, value is: {0 to 3} = {bypass, add, amp}
	int cross;// cross momentum active or not
	
	// No need to save, with reset
	int numChan;
	float feedbacks[2][N_POLY];
	float modSignals[2][N_POLY];
	
	// No need to save, no reset
	RefreshCounter refresh;
	float resetLight0 = 0.0f;
	float resetLight1 = 0.0f;
	Trigger routingTrigger;
	Trigger planckTriggers[2];
	Trigger modtypeTriggers[2];
	Trigger resetTriggers[3];// M inut, C input, button (trigs both)
	Trigger crossTrigger;
	SlewLimiter multiplySlewers[N_POLY];
	
	
	DarkEnergy() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		
		configParam(DEPTHCV_PARAM, -1.0f, 1.0f, 0.0f, "Depth CV");		
		configParam(DEPTH_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Depth M");
		configParam(DEPTH_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Depth C");
		configParam(MOMENTUMCV_PARAM, -1.0f, 1.0f, 0.0f, "Momentum CV");		
		configParam(MOMENTUM_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Momentum M");
		configParam(MOMENTUM_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Momentum C");
		configParam(FREQ_PARAMS + 0, -3.0f, 3.0f, 0.0f, "Freq M");
		configParam(FREQ_PARAMS + 1, -3.0f, 3.0f, 0.0f, "Freq C");
		configParam(FREQ_PARAM, -3.0f, 3.0f, 0.0f, "Freq offset");
		configParam(ROUTING_PARAM, 0.0f, 1.0f, 0.0f, "Routing");
		configParam(PLANCK_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Planck mode M");
		configParam(PLANCK_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Planck mode C");
		configParam(MODTYPE_PARAMS + 0, 0.0f, 1.0f, 0.0f, "CV mod type M");
		configParam(MODTYPE_PARAMS + 1, 0.0f, 1.0f, 0.0f, "CV mod type C");		
		configParam(CROSS_PARAM, 0.0f, 1.0f, 0.0f, "Momentum crossing");
		configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f, "Reset");
		
		configInput(FREQCV_INPUTS + 0, "Mass");
		configInput(FREQCV_INPUTS + 1, "Speed of light");
		configInput(FREQCV_INPUT, "1V/oct");
		configInput(MULTIPLY_INPUT, "Multiply");
		configInput(ANTIGRAV_INPUT, "Anti-gravity (FM depth) CV");
		configInput(MOMENTUM_INPUT, "Momentum (feedback) CV");
		configInput(RESET_INPUTS + 0, "Reset M");
		configInput(RESET_INPUTS + 1, "Reset C");
		
		configOutput(ENERGY_OUTPUT, "Energy");
		configOutput(M_OUTPUT, "M");
		configOutput(C_OUTPUT, "C");
		
		for (int c = 0; c < N_POLY; c++) {
			oscM[c].construct(APP->engine->getSampleRate());
			oscC[c].construct(APP->engine->getSampleRate());
			feedbacks[0][c] = 0.0f;
			feedbacks[1][c] = 0.0f;
		}
		onSampleRateChange();
		onReset();

		panelTheme = (loadDarkAsDefault() ? 1 : 0);
	}
	
	
	void onReset() override {
		for (int c = 0; c < N_POLY; c++) {
			oscM[c].onReset();
			oscC[c].onReset();
		}
		routing = 1;// default is control (i.e. blue and yellow) (top light, light index 1),
		for (int i = 0; i < 2; i++) {
			plancks[i] = 0;
			modtypes[i] = 1;// default is add mode
		}
		cross = 0;
		resetNonJson();
	}
	void resetNonJson() {
		numChan = 1;
		for (int c = 0; c < N_POLY; c++) {
			calcModSignals(c);
			calcFeedbacks(c);
		}			
	}	

	
	void onRandomize() override {
	}
	

	void onSampleRateChange() override {
		float sampleRate = APP->engine->getSampleRate();
		for (int c = 0; c < N_POLY; c++) {
			oscM[c].onSampleRateChange(sampleRate);
			oscC[c].onSampleRateChange(sampleRate);
			multiplySlewers[c].setParams2(sampleRate, 2.5f, 20.0f, 1.0f);
		}
	}
	
	
	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// oscM and oscC
		oscM[0].dataToJson(rootJ, "oscM_");// legacy so do outside loop
		oscC[0].dataToJson(rootJ, "oscC_");// legacy so do outside loop
		for (int c = 1; c < N_POLY; c++) {
			oscM[c].dataToJson(rootJ, string::f("osc%iM_",c));
			oscC[c].dataToJson(rootJ, string::f("osc%iC_",c));
		}

		// routing
		json_object_set_new(rootJ, "routing", json_integer(routing));

		// plancks
		json_object_set_new(rootJ, "planck0", json_integer(plancks[0]));
		json_object_set_new(rootJ, "planck1", json_integer(plancks[1]));

		// modtypes
		json_object_set_new(rootJ, "modtype0", json_integer(modtypes[0]));
		json_object_set_new(rootJ, "modtype1", json_integer(modtypes[1]));
		
		// cross
		json_object_set_new(rootJ, "cross", json_integer(cross));

		return rootJ;
	}

	
	void dataFromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);

		// oscM and oscC
		oscM[0].dataFromJson(rootJ, "oscM_");// legacy so do outside loop
		oscC[0].dataFromJson(rootJ, "oscC_");// legacy so do outside loop
		for (int c = 1; c < N_POLY; c++) {
			oscM[c].dataFromJson(rootJ, string::f("osc%iM_",c));
			oscC[c].dataFromJson(rootJ, string::f("osc%iC_",c));
		}

		// routing
		json_t *routingJ = json_object_get(rootJ, "routing");
		if (routingJ)
			routing = json_integer_value(routingJ);

		// plancks
		json_t *planck0J = json_object_get(rootJ, "planck0");
		if (planck0J)
			plancks[0] = json_integer_value(planck0J);
		json_t *planck1J = json_object_get(rootJ, "planck1");
		if (planck1J)
			plancks[1] = json_integer_value(planck1J);

		// modtypes
		json_t *modtype0J = json_object_get(rootJ, "modtype0");
		if (modtype0J)
			modtypes[0] = json_integer_value(modtype0J);
		json_t *modtype1J = json_object_get(rootJ, "modtype1");
		if (modtype1J)
			modtypes[1] = json_integer_value(modtype1J);

		// cross
		json_t *crossJ = json_object_get(rootJ, "cross");
		if (crossJ)
			cross = json_integer_value(crossJ);
		
		resetNonJson();
	}

	void process(const ProcessArgs &args) override {	
		// user inputs
		if (refresh.processInputs()) {
			numChan = std::max(1, inputs[FREQCV_INPUT].getChannels());
			numChan = std::min(numChan, (int)N_POLY);
			outputs[ENERGY_OUTPUT].setChannels(numChan);
			outputs[M_OUTPUT].setChannels(numChan);
			outputs[C_OUTPUT].setChannels(numChan);

			// routing
			if (routingTrigger.process(params[ROUTING_PARAM].getValue())) {
				if (++routing > 2)
					routing = 0;
			}
			
			// plancks
			for (int i = 0; i < 2; i++) {
				if (planckTriggers[i].process(params[PLANCK_PARAMS + i].getValue())) {
					if (++plancks[i] > 2)
						plancks[i] = 0;
				}
			}
			
			// modtypes
			for (int i = 0; i < 2; i++) {
				if (modtypeTriggers[i].process(params[MODTYPE_PARAMS + i].getValue())) {
					if (++modtypes[i] > 2)
						modtypes[i] = 0;
				}
			}
			
			// cross
			if (crossTrigger.process(params[CROSS_PARAM].getValue())) {
				if (++cross > 1)
					cross = 0;
			}
			
			// reset
			if (resetTriggers[0].process(inputs[RESET_INPUTS + 0].getVoltage())) {
				for (int c = 0; c < N_POLY; c++) {
					oscM[c].onReset();
					resetLight0 = 1.0f;
				}
			}
			if (resetTriggers[1].process(inputs[RESET_INPUTS + 1].getVoltage())) {
				for (int c = 0; c < N_POLY; c++) {
					oscC[c].onReset();
					resetLight1 = 1.0f;
				}
			}
			if (resetTriggers[2].process(params[RESET_PARAM].getValue())) {
				for (int c = 0; c < N_POLY; c++) {
					oscM[c].onReset();
					resetLight0 = 1.0f;
					oscC[c].onReset();
					resetLight1 = 1.0f;
				}
			}
		}// userInputs refresh
		
		
		// main signal flow
		// ----------------		
		for (int c = 0; c < numChan; c++) {
			// pitch modulation and feedbacks
			if ((refresh.refreshCounter & 0x3) == (c & 0x3)) {
				// stagger0 updates channels 0, 4, 8,  12
				// stagger1 updates channels 1, 5, 9,  13
				// stagger2 updates channels 2, 6, 10, 14
				// stagger3 updates channels 3, 7, 11, 15
				calcModSignals(c);// voct modulation, a given channel is updated at sample_rate / 4
				calcFeedbacks(c);// feedback (momentum), a given channel is updated at sample_rate / 4
			}
			
			if (!outputs[ENERGY_OUTPUT].isConnected() && !outputs[M_OUTPUT].isConnected() && !outputs[C_OUTPUT].isConnected()) {// this is placed here such that feedbacks and mod signals of chan 0 are always calculated, since they are used in lights
				break;
			}
			
			// vocts
			float base = inputs[FREQCV_INPUT].getVoltage(c) + params[FREQ_PARAM].getValue();
			float vocts[2] = {base + modSignals[0][c], base + modSignals[1][c]};
			
			// oscillators
			float oscMout = oscM[c].step(vocts[0], feedbacks[0][c] * 0.3f, params[DEPTH_PARAMS + 0].getValue(), oscC[c]._feedbackDelayedSample);
			float oscCout = oscC[c].step(vocts[1], feedbacks[1][c] * 0.3f, params[DEPTH_PARAMS + 1].getValue(), oscM[c]._feedbackDelayedSample);
			
			// multiply 
			float slewInput = 1.0f;
			if (inputs[MULTIPLY_INPUT].isConnected()) {
				int chan = std::min(inputs[MULTIPLY_INPUT].getChannels() - 1, c);
				slewInput = (clamp(inputs[MULTIPLY_INPUT].getVoltage(chan) / 10.0f, 0.0f, 1.0f));
			}
			float multiplySlewValue = multiplySlewers[c].next(slewInput) * 0.2f;
			
			// final attenuverters
			float attv1 = oscCout * oscCout * multiplySlewValue;
			float attv2 = attv1 * oscMout * 0.2f;
			
			// outputs
			outputs[ENERGY_OUTPUT].setVoltage(-attv2, c);// inverted as per spec from Pyer
			outputs[M_OUTPUT].setVoltage(oscMout, c);
			outputs[C_OUTPUT].setVoltage(attv1, c);
		}

		// lights
		if (refresh.processLights()) {
			float deltaTime = args.sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2);
			
			// routing
			for (int i = 0; i < 3; i++)
				lights[ROUTING_LIGHTS + i].setBrightness(routing == i ? 1.0f : 0.0f);
			
			for (int i = 0; i < 2; i++) {
				// plancks
				lights[PLANCK_LIGHTS + i * 2 + 0].setBrightness(plancks[i] == 1 ? 1.0f : 0.0f);// low
				lights[PLANCK_LIGHTS + i * 2 + 1].setBrightness(plancks[i] == 2 ? 1.0f : 0.0f);// ratio
				
				// modtypes
				lights[ADD_LIGHTS + i].setBrightness(modtypes[i] == 1 ? 1.0f : 0.0f);
				lights[AMP_LIGHTS + i].setBrightness(modtypes[i] == 2 ? 1.0f : 0.0f);
				
				// momentum (cross)
				lights[MOMENTUM_LIGHTS + i].setBrightness(feedbacks[i][0]);// lights show first channel only when poly

				// momentum (cross)
				float modSignalLight = modSignals[i][0] / 3.0f;
				lights[FREQ_ROUTING_LIGHTS + 2 * i + 0].setBrightness(modSignalLight);// blue diode
				lights[FREQ_ROUTING_LIGHTS + 2 * i + 1].setBrightness(-modSignalLight);// yellow diode
			}
			
			// cross
			lights[CROSS_LIGHT].setBrightness(cross == 1 ? 1.0f : 0.0f);

			// Reset light
			lights[RESET_LIGHTS + 0].setSmoothBrightness(resetLight0, deltaTime);	
			lights[RESET_LIGHTS + 1].setSmoothBrightness(resetLight1, deltaTime);	
			resetLight0 = 0.0f;	
			resetLight1 = 0.0f;	

		}// lightRefreshCounter
		
	}// step()
	
	inline float calcFreqKnob(int osci) {
		if (plancks[osci] == 0)// off (smooth)
			return params[FREQ_PARAMS + osci].getValue();
		if (plancks[osci] == 1)// -10V offset
			return params[FREQ_PARAMS + osci].getValue() - 10.0f;
		// 5ths and octs (plancks[osci] == 2)
		int retcv = (int)std::round((params[FREQ_PARAMS + osci].getValue() + 3.0f) * 2.0f);
		if ((retcv & 0x1) != 0)
			return (float)(retcv)/2.0f - 3.0f + 0.08333333333f;
		return (float)(retcv)/2.0f - 3.0f;
	}
	
	inline void calcModSignals(int chan) {
		for (int osci = 0; osci < 2; osci++) {
			float freqValue = calcFreqKnob(osci);
			if (modtypes[osci] == 0 || !inputs[FREQCV_INPUTS + osci].isConnected()) {// bypass
				modSignals[osci][chan] = freqValue;
			}
			else {
				int chanIn = std::min(inputs[FREQCV_INPUTS + osci].getChannels() - 1, chan);
				if (modtypes[osci] == 1) {// add
					modSignals[osci][chan] = freqValue + inputs[FREQCV_INPUTS + osci].getVoltage(chanIn);
				}
				else {// amp
					modSignals[osci][chan] = freqValue * (clamp(inputs[FREQCV_INPUTS + osci].getVoltage(chanIn), 0.0f, 10.0f) / 10.0f);
				}
			}
		}
		if (routing == 1) {
			modSignals[1][chan] += modSignals[0][chan];
		}
		else if (routing == 2) {
			modSignals[1][chan] -= modSignals[0][chan];
		}
	}
	
	inline void calcFeedbacks(int chan) {
		float moIn[2]; 	
		for (int osci = 0; osci < 2; osci++) {
			moIn[osci] = 0.0f;
			if (inputs[MOMENTUM_INPUT].isConnected()) {
				int chanIn = std::min(inputs[MOMENTUM_INPUT].getChannels() - 1, chan);
				moIn[osci] = inputs[MOMENTUM_INPUT].getVoltage(chanIn);
			}
			feedbacks[osci][chan] = params[MOMENTUM_PARAMS + osci].getValue();
		}
		
		if (cross == 0) {
			feedbacks[0][chan] += moIn[0] * 0.1f;
			feedbacks[1][chan] += moIn[1] * 0.1f;
		}
		else {// cross momentum
			if (moIn[0] > 0)
				feedbacks[0][chan] += moIn[0] * 0.2f;
			else 
				feedbacks[1][chan] += moIn[0] * -0.2f;
			if (moIn[1] > 0)
				feedbacks[1][chan] += moIn[1] * 0.2f;
			else 
				feedbacks[0][chan] += moIn[1] * -0.2f;
		}
		feedbacks[0][chan] = clamp(feedbacks[0][chan], 0.0f, 1.0f);
		feedbacks[1][chan] = clamp(feedbacks[1][chan], 0.0f, 1.0f);
	}
};


struct DarkEnergyWidget : ModuleWidget {
	int lastPanelTheme = -1;
	std::shared_ptr<window::Svg> light_svg;
	std::shared_ptr<window::Svg> dark_svg;

	void appendContextMenu(Menu *menu) override {
		DarkEnergy *module = dynamic_cast<DarkEnergy*>(this->module);
		assert(module);

		createPanelThemeMenu(menu, &(module->panelTheme));
	}	
	
	DarkEnergyWidget(DarkEnergy *module) {
		setModule(module);

		// Main panels from Inkscape
 		light_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/DarkEnergy-WL.svg"));
		dark_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/DarkEnergy-DM.svg"));
		int panelTheme = isDark(module ? (&(((DarkEnergy*)module)->panelTheme)) : NULL) ? 1 : 0;// need this here since step() not called for module browser
		setPanel(panelTheme == 0 ? light_svg : dark_svg);		

		// Screws 
		// part of svg panel, no code required
		
		static const float colC = 55.88f / 2.0f;// = 27.94f
		static const float oX1 = 8.46f;// 27.94f - 19.48f
		static const float oX2 = 17.44f;

		// main outputs
		addOutput(createDynamicPort<GeoPort>(mm2px(Vec(colC, 16.07f)), false, module, DarkEnergy::ENERGY_OUTPUT, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(mm2px(Vec(colC - oX2, 27.57f)), false, module, DarkEnergy::M_OUTPUT, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(mm2px(Vec(46.23f, 27.57f)), false, module, DarkEnergy::C_OUTPUT, module ? &module->panelTheme : NULL));
		
		// multiply input
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(49.61f, 16.07f)), true, module, DarkEnergy::MULTIPLY_INPUT, module ? &module->panelTheme : NULL));
		
		// ANTIGRAVITY (FM DEPTH)
		// depth CV knob 
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC, 50.43f)), module, DarkEnergy::DEPTHCV_PARAM, module ? &module->panelTheme : NULL));
		// depth knobs
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC - oX2, 54.80f)), module, DarkEnergy::DEPTH_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC + oX2, 54.80f)), module, DarkEnergy::DEPTH_PARAMS + 1, module ? &module->panelTheme : NULL));		

		// depth and momentum inputs
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(colC - oX1, 63.75f)), true, module, DarkEnergy::ANTIGRAV_INPUT, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(colC + oX1, 63.75f)), true, module, DarkEnergy::MOMENTUM_INPUT, module ? &module->panelTheme : NULL));

		// MOMENTUM (FEEDBACK)
		// momentum knobs
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC - oX2, 72.72f)), module, DarkEnergy::MOMENTUM_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC + oX2, 72.72f)), module, DarkEnergy::MOMENTUM_PARAMS + 1, module ? &module->panelTheme : NULL));
		// momentum CV knob
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC, 77.12f)), module, DarkEnergy::MOMENTUMCV_PARAM, module ? &module->panelTheme : NULL));

		// RESET
		// reset inputs
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(colC - oX1, 85.74f)), true, module, DarkEnergy::RESET_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(colC + oX1, 85.74f)), true, module, DarkEnergy::RESET_INPUTS + 1, module ? &module->panelTheme : NULL));
		// reset lights and button
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(22.86f, 90.46f)), module, DarkEnergy::RESET_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(55.88f - 22.86f, 90.46f)), module, DarkEnergy::RESET_LIGHTS + 1));
		addParam(createDynamicParam<GeoPushButton>(mm2px(Vec(colC, 90.46f)), module, DarkEnergy::RESET_PARAM, module ? &module->panelTheme : NULL));


		
		// freq knobs
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC - oX2, 95.37f)), module, DarkEnergy::FREQ_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC + oX2, 95.37f)), module, DarkEnergy::FREQ_PARAMS + 1, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC, 100.10f)), module, DarkEnergy::FREQ_PARAM, module ? &module->panelTheme : NULL));
		
		// planck lights: low M (yellow), ratio M (blue), low C (yellow), ratio C (blue)
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(mm2px(Vec(13.39f, 102.97f)), module, DarkEnergy::PLANCK_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(mm2px(Vec(11.68f, 106.36f)), module, DarkEnergy::PLANCK_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(mm2px(Vec(55.88f - 13.39f, 102.97f)), module, DarkEnergy::PLANCK_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(mm2px(Vec(55.88f - 11.68f, 106.36f)), module, DarkEnergy::PLANCK_LIGHTS + 3));
		
		// planck buttons
		addParam(createDynamicParam<GeoPushButton>(mm2px(Vec(16.43f, 107.05f)), module, DarkEnergy::PLANCK_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoPushButton>(mm2px(Vec(55.88f - 16.43f, 107.05f)), module, DarkEnergy::PLANCK_PARAMS + 1, module ? &module->panelTheme : NULL));		
		
		// freq inputs (V/oct, M and C)
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(colC, 113.14f)), true, module, DarkEnergy::FREQCV_INPUT, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(13.39f, 117.87f)), true, module, DarkEnergy::FREQCV_INPUTS + 0, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(42.51f, 117.87f)), true, module, DarkEnergy::FREQCV_INPUTS + 1, module ? &module->panelTheme : NULL));

		
		// cross button and light
/*		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter, 380.0f - 205.5f), module, DarkEnergy::CROSS_PARAM, module ? &module->panelTheme : NULL));		
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 7.5f, 380.0f - 219.5f), module, DarkEnergy::CROSS_LIGHT));
		
		// routing lights
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(39, 380.0f - 141.5f), module, DarkEnergy::ROUTING_LIGHTS + 0));// bottom
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(51, 380.0f - 154.5f), module, DarkEnergy::ROUTING_LIGHTS + 1));// top
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(45, 380.0f - 148.5f), module, DarkEnergy::ROUTING_LIGHTS + 2));// middle
		
		// momentum lights
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - offsetX, 380.0f - 186.0f), module, DarkEnergy::MOMENTUM_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + offsetX, 380.0f - 186.0f), module, DarkEnergy::MOMENTUM_LIGHTS + 1));

		// freq routing lights (below momentum lights)
		addChild(createLightCentered<SmallLight<GeoBlueYellowLight>>(VecPx(colRulerCenter - offsetX, 380.0f - 177.0f), module, DarkEnergy::FREQ_ROUTING_LIGHTS + 2 * 0));
		addChild(createLightCentered<SmallLight<GeoBlueYellowLight>>(VecPx(colRulerCenter + offsetX, 380.0f - 177.0f), module, DarkEnergy::FREQ_ROUTING_LIGHTS + 2 * 1));

		// routing button
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter, 380.0f - 113.5f), module, DarkEnergy::ROUTING_PARAM, module ? &module->panelTheme : NULL));
		
				
		// mod type buttons
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - offsetX - 0.5f, 380.0f - 57.5f), module, DarkEnergy::MODTYPE_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + offsetX + 0.5f, 380.0f - 57.5f), module, DarkEnergy::MODTYPE_PARAMS + 1, module ? &module->panelTheme : NULL));
		
		// mod type lights
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 17.5f, 380.0f - 62.5f), module, DarkEnergy::ADD_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + 17.5f, 380.0f - 62.5f), module, DarkEnergy::ADD_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 41.5f, 380.0f - 47.5f), module, DarkEnergy::AMP_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + 41.5f, 380.0f - 47.5f), module, DarkEnergy::AMP_LIGHTS + 1));
		
		*/
	}
	
	void step() override {
		int panelTheme = isDark(module ? (&(((DarkEnergy*)module)->panelTheme)) : NULL) ? 1 : 0;
		if (lastPanelTheme != panelTheme) {
			lastPanelTheme = panelTheme;
			SvgPanel* panel = (SvgPanel*)getPanel();
			panel->setBackground(panelTheme == 0 ? light_svg : dark_svg);
			panel->fb->dirty = true;
		}
		Widget::step();
	}
};

Model *modelDarkEnergy = createModel<DarkEnergy, DarkEnergyWidget>("DarkEnergy");