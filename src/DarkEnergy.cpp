//***********************************************************************************************
//Complex Oscillator module for VCV Rack by Pierre Collard and Marc Boulé
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
		ENUMS(FREQ_PARAMS, 2),// rotary knobs
		FREQ_PARAM,// rotary knobs
		DEPTHCV_PARAM,// rotary knob, FM depth CV aka antigravity CV
		ENUMS(DEPTH_PARAMS, 2),// rotary knobs, FM depth aka antigravity
		MOMENTUMCV_PARAM,// rotary knob, feedback CV
		ENUMS(MOMENTUM_PARAMS, 2),// rotary knobs, feedback
		MODE_PARAM,
		MULTEN_PARAM,
		MULTDECAY_PARAM,
		MULTDEST_PARAM,
		RESET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(FREQCV_INPUTS, 2), // respectively "mass" and "speed of light"
		FREQCV_INPUT, // main voct input
		MULTIPLY_INPUT,
		MULTDECAY_INPUT,
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
		ENUMS(MOMENTUM_LIGHTS, 2),
		ENUMS(ANTIGRAV_LIGHTS, 2),
		ENUMS(FREQ_LIGHTS, 2 * 2),// room for blue/yellow
		ENUMS(MODE_LIGHTS, 2),// index 0 is fmDepth, index 1 is feedback
		ENUMS(DEST_LIGHTS, 2),// index 0 is fmDepth, index 1 is feedback
		ENUMS(RESET_LIGHTS, 2),
		MULTEN_LIGHT,
		MULTDECAY_LIGHT,
		NUM_LIGHTS
	};
	
	
	// Constants
	static const int N_POLY = 16;
	static constexpr float MULTSLEW_RISETIME = 2.5f;
	static constexpr float DECAY_MIN = 2.0f;// ms
	static constexpr float DECAY_MAX = 2000.0f;// ms
	static constexpr float DECAY_DEF = 20.0f;// ms
	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	FMOp oscM[N_POLY];
	FMOp oscC[N_POLY];
	int plancks[2];// index is left/right, value is: 0 = not quantized, 1 = 5th+octs, 2 = adds -10V offset (LFO)
	int mode;// main center modulation modes (bit 0 is fmDepth mode, bit 1 is feedback mode; a 0 bit means both sides CV modulated the same, a 1 bit means pos attenuverter mods right side only, neg atten means mod left side only (but still a positive attenuverter value though!))
	int dest;// mult destination (bit 0 is fmDepth mode, bit 1 is feedback mode; a 0 bit means both sides CV modulated the same, a 1 bit means pos attenuverter mods right side only, neg atten means mod left side only (but still a positive attenuverter value though!))
	int multEnable;
	
	// No need to save, with reset
	int numChan;
	float feedbacks[2][N_POLY];
	float depths[2][N_POLY];// fm depth
	float modSignals[2][N_POLY];
	float lastVocts[N_POLY];
	
	// No need to save, no reset
	RefreshCounter refresh;
	float resetLight0 = 0.0f;
	float resetLight1 = 0.0f;
	Trigger planckTriggers[2];
	Trigger resetTriggers[3];// M inut, C input, button (trigs both)
	Trigger modeTrigger;
	Trigger multEnableTrigger;
	Trigger multDestTrigger;
	SlewLimiter multiplySignalSlewers[N_POLY];
	SlewLimiter multiplyOnSlewer;
	dsp::PulseGenerator multiplyPulses[N_POLY];// for cv delta to trig
	
	
	float getDecayTime(int chan) {
		float decay = params[MULTDECAY_PARAM].getValue();// in ms
		if (inputs[MULTDECAY_INPUT].isConnected()) {
			int chanIn = std::min(inputs[MULTDECAY_INPUT].getChannels() - 1, chan);
			float decaycv = inputs[MULTDECAY_INPUT].getVoltage(chanIn) * 0.1f * (DECAY_MAX - DECAY_MIN);// assumes decay input is 0-10V CV
			decay = clamp(decay + decaycv, DECAY_MIN, DECAY_MAX);
		}
		return decay;
	}
	
	
	DarkEnergy() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		
		configParam(DEPTHCV_PARAM, -1.0f, 1.0f, 0.0f, "Cross mod CV");		
		configParam(DEPTH_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Cross mod M");
		configParam(DEPTH_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Cross mod C");
		configParam(MOMENTUMCV_PARAM, -1.0f, 1.0f, 0.0f, "Self mod CV");		
		configParam(MOMENTUM_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Self mod M");
		configParam(MOMENTUM_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Self mod C");
		configParam(FREQ_PARAMS + 0, -3.0f, 3.0f, 0.0f, "Freq M");
		configParam(FREQ_PARAMS + 1, -3.0f, 3.0f, 0.0f, "Freq C");
		configParam(FREQ_PARAM, -3.0f, 3.0f, 0.0f, "Freq offset");
		configParam(PLANCK_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Freq mode M");
		configParam(PLANCK_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Freq mode C");
		configParam(MODE_PARAM, 0.0f, 1.0f, 0.0f, "Cross mod and self mod CV mode");
		configParam(MULTEN_PARAM, 0.0f, 1.0f, 0.0f, "Enable extra mod VCA");
		configParam(MULTDECAY_PARAM, DECAY_MIN, DECAY_MAX, DECAY_DEF, "Extra mod decay", " ms");
		configParam(MULTDEST_PARAM, 0.0f, 1.0f, 0.0f, "Extra mod destination");
		configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f, "Reset");
		
		configInput(FREQCV_INPUTS + 0, "M freq CV");
		configInput(FREQCV_INPUTS + 1, "C freq CV");
		configInput(FREQCV_INPUT, "1V/oct");
		configInput(MULTIPLY_INPUT, "Extra mod");
		configInput(MULTDECAY_INPUT, "Extra mod decay CV");
		configInput(ANTIGRAV_INPUT, "Cross mod CV");
		configInput(MOMENTUM_INPUT, "Self mod CV");
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
			depths[0][c] = 0.0f;
			depths[1][c] = 0.0f;
		}
		onSampleRateChange();
		onReset();

		panelTheme = loadDarkAsDefault();
	}
	
	
	void onReset() override final {
		for (int c = 0; c < N_POLY; c++) {
			oscM[c].onReset();
			oscC[c].onReset();
		}
		for (int i = 0; i < 2; i++) {
			plancks[i] = 0;
		}
		mode = 0x0;
		dest = 0x0;
		multEnable = 0x0;
		resetNonJson();
	}
	void resetNonJson() {
		numChan = 1;
		for (int c = 0; c < N_POLY; c++) {
			calcModSignals(c);
			calcFeedbacks(c);
			lastVocts[c] = inputs[FREQCV_INPUT].getVoltage(c);
		}	
	}	

	
	void onRandomize() override {
	}

	void onSampleRateChange() override final {
		float sampleRate = APP->engine->getSampleRate();
		for (int c = 0; c < N_POLY; c++) {
			oscM[c].onSampleRateChange(sampleRate);
			oscC[c].onSampleRateChange(sampleRate);
			multiplySignalSlewers[c].setParams2(sampleRate, MULTSLEW_RISETIME, getDecayTime(c), 1.0f);
			multiplyOnSlewer.setParams(sampleRate, MULTSLEW_RISETIME, 1.0f);
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

		// plancks
		json_object_set_new(rootJ, "planck0", json_integer(plancks[0]));
		json_object_set_new(rootJ, "planck1", json_integer(plancks[1]));
		
		// mode
		json_object_set_new(rootJ, "mode", json_integer(mode));

		// dest
		json_object_set_new(rootJ, "dest", json_integer(dest));

		// multEnable
		json_object_set_new(rootJ, "multEnable", json_integer(multEnable));

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

		// plancks
		json_t *planck0J = json_object_get(rootJ, "planck0");
		if (planck0J)
			plancks[0] = json_integer_value(planck0J);
		json_t *planck1J = json_object_get(rootJ, "planck1");
		if (planck1J)
			plancks[1] = json_integer_value(planck1J);

		// mode
		json_t *modeJ = json_object_get(rootJ, "mode");
		if (modeJ)
			mode = json_integer_value(modeJ);
		
		// dest
		json_t *destJ = json_object_get(rootJ, "dest");
		if (destJ)
			dest = json_integer_value(destJ);
		
		// multEnable
		json_t *multEnableJ = json_object_get(rootJ, "multEnable");
		if (multEnableJ)
			multEnable = json_integer_value(multEnableJ);
		
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

			// plancks
			for (int i = 0; i < 2; i++) {
				if (planckTriggers[i].process(params[PLANCK_PARAMS + i].getValue())) {
					if (++plancks[i] > 2)
						plancks[i] = 0;
				}
			}
						
			// mode
			if (modeTrigger.process(params[MODE_PARAM].getValue())) {
				if (++mode > 0x3)
					mode = 0x0;
			}
			
			// dest (aka mult dest)
			if (multDestTrigger.process(params[MULTDEST_PARAM].getValue())) {
				if (++dest > 0x3)
					dest = 0x0;
			}

			// multEnable
			if (multEnableTrigger.process(params[MULTEN_PARAM].getValue())) {
				multEnable ^= 0x1;
			}
		
			// refresh multslewers fall time (aka mult decay)
			for (int c = 0; c < numChan; c++) {
				multiplySignalSlewers[c].setParams2(args.sampleRate, MULTSLEW_RISETIME, getDecayTime(c), 1.0f);
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
		float multiplyOnSlewed = multiplyOnSlewer.next(multEnable != 0 ? 1.0f : 0.0f);
		for (int c = 0; c < numChan; c++) {
			// lastVocts
			if (lastVocts[c] != inputs[FREQCV_INPUT].getVoltage(c)) {
				lastVocts[c] = inputs[FREQCV_INPUT].getVoltage(c);
				multiplyPulses[c].trigger(0.01f);// 10 ms
			}
			
			// multiply 
			float slewInput;
			if (inputs[MULTIPLY_INPUT].isConnected()) {
				int chan = std::min(inputs[MULTIPLY_INPUT].getChannels() - 1, c);
				slewInput = (clamp(inputs[MULTIPLY_INPUT].getVoltage(chan) / 10.0f, 0.0f, 1.0f));
			}
			else {
				slewInput = multiplyPulses[c].process(args.sampleTime) ? 1.0f : 0.0f;
			}
			float multiplySignalSlewed = multiplySignalSlewers[c].next(slewInput);
			
			
			// pitch modulation, feedbacks and depths (some use multiplySignalSlewers[c]._last)
			if ((refresh.refreshCounter & 0x3) == (c & 0x3)) {
				// stagger0 updates channels 0, 4, 8,  12
				// stagger1 updates channels 1, 5, 9,  13
				// stagger2 updates channels 2, 6, 10, 14
				// stagger3 updates channels 3, 7, 11, 15
				calcModSignals(c);// voct modulation, a given channel is updated at sample_rate / 4
				calcFeedbacks(c);// feedback (momentum), a given channel is updated at sample_rate / 4
				calcDepths(c);// fmDepth (anti-gravity), a given channel is updated at sample_rate / 4
			}
			
			// vocts
			float base = inputs[FREQCV_INPUT].getVoltage(c);
			const float vocts[2] = {base + modSignals[0][c], base + modSignals[1][c]};
			
			// oscillators
			float oscMout = oscM[c].step(vocts[0], feedbacks[0][c] * 0.3f, depths[0][c], oscC[c]._feedbackDelayedSample);
			float oscCout = oscC[c].step(vocts[1], feedbacks[1][c] * 0.3f, depths[1][c], oscM[c]._feedbackDelayedSample);
						
			// final signals
			float attv1 = oscCout * oscCout * 0.2f * crossfade(1.0f, multiplySignalSlewed, multiplyOnSlewed);// C^2 is done here, with multiply
			float attv2 = attv1 * oscMout * 0.2f;// ring mod is here
			
			// outputs
			outputs[ENERGY_OUTPUT].setVoltage(-attv2, c);// inverted as per spec from Pyer
			outputs[M_OUTPUT].setVoltage(oscMout, c);
			outputs[C_OUTPUT].setVoltage(attv1, c);
		}

		// lights
		if (refresh.processLights()) {
			float deltaTime = args.sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2);
						
			for (int i = 0; i < 2; i++) {
				// plancks
				lights[PLANCK_LIGHTS + i * 2 + 0].setBrightness(plancks[i] == 2 ? 1.0f : 0.0f);// low (LFO)
				lights[PLANCK_LIGHTS + i * 2 + 1].setBrightness(plancks[i] == 1 ? 1.0f : 0.0f);// ratio
								
				// momentum (feedback) and anti-gravity (fmDepth)
				lights[MOMENTUM_LIGHTS + i].setBrightness(feedbacks[i][0]);// lights show first channel only when poly
				lights[ANTIGRAV_LIGHTS + i].setBrightness(depths[i][0]);// lights show first channel only when poly

				// freqs
				float modSignalLight = modSignals[i][0] / 3.0f;
				lights[FREQ_LIGHTS + 2 * i + 0].setBrightness(modSignalLight);// blue diode
				lights[FREQ_LIGHTS + 2 * i + 1].setBrightness(-modSignalLight);// yellow diode
			}
			
			// mult enable and decay
			lights[MULTEN_LIGHT].setBrightness(multiplyOnSlewer._last);
			lights[MULTDECAY_LIGHT].setBrightness(multiplySignalSlewers[0]._last);
			
			// mode
			lights[MODE_LIGHTS + 0].setBrightness((mode & 0x1) != 0 ? 1.0f : 0.0f);
			lights[MODE_LIGHTS + 1].setBrightness((mode & 0x2) != 0 ? 1.0f : 0.0f);

			// dest
			lights[DEST_LIGHTS + 0].setBrightness((dest & 0x1) != 0 ? 1.0f : 0.0f);
			lights[DEST_LIGHTS + 1].setBrightness((dest & 0x2) != 0 ? 1.0f : 0.0f);

			// Reset light
			lights[RESET_LIGHTS + 0].setSmoothBrightness(resetLight0, deltaTime);	
			lights[RESET_LIGHTS + 1].setSmoothBrightness(resetLight1, deltaTime);	
			resetLight0 = 0.0f;	
			resetLight1 = 0.0f;	

		}// lightRefreshCounter
		
	}// step()
	
	float calcFreqKnob(int osci) {
		if (plancks[osci] == 0)// off (smooth)
			return params[FREQ_PARAMS + osci].getValue();
		if (plancks[osci] == 2)// -10V offset
			return params[FREQ_PARAMS + osci].getValue() - 10.0f;
		// 5ths and octs (plancks[osci] == 1)
		int retcv = (int)std::round((params[FREQ_PARAMS + osci].getValue() + 3.0f) * 2.0f);
		if ((retcv & 0x1) != 0)
			return (float)(retcv)/2.0f - 3.0f + 0.08333333333f;
		return (float)(retcv)/2.0f - 3.0f;
	}
	
	void calcModSignals(int chan) {
		for (int osci = 0; osci < 2; osci++) {
			float freqValue = calcFreqKnob(osci) + params[FREQ_PARAM].getValue();
			if (inputs[FREQCV_INPUTS + osci].isConnected()) {
				int chanIn = std::min(inputs[FREQCV_INPUTS + osci].getChannels() - 1, chan);
				freqValue += inputs[FREQCV_INPUTS + osci].getVoltage(chanIn);
			}	
			modSignals[osci][chan] = freqValue;
		}
	}
	
	void calcFeedbacks(int chan) {
		float modIn = params[MOMENTUMCV_PARAM].getValue();
		float cvIn = 0.0f;
		bool hasCvIn = false;
		if ((dest & 0x2) != 0) {
			cvIn += multiplySignalSlewers[chan]._last;
			hasCvIn = true;
		}
		if (inputs[MOMENTUM_INPUT].isConnected()) {
			int chanIn = std::min(inputs[MOMENTUM_INPUT].getChannels() - 1, chan);
			cvIn += inputs[MOMENTUM_INPUT].getVoltage(chanIn) * 0.1f;
			hasCvIn = true;
		}
		if (hasCvIn) {
			modIn *= cvIn;
		}
			
		for (int osci = 0; osci < 2; osci++) {
			feedbacks[osci][chan] = params[MOMENTUM_PARAMS + osci].getValue();
		}
		
		if ((mode & 0x2) != 0) {
			if (modIn > 0.0f) {
				// modulate feedback of right side only
				feedbacks[1][chan] += modIn;
			}
			else {
				// modulate feedback of left side only
				feedbacks[0][chan] -= modIn;// this has to modulate positively but modIn is negative, so correct for this
			}
		}
		else {
			// modulate both feedbacks the same
			feedbacks[0][chan] += modIn;
			feedbacks[1][chan] += modIn;
		}
		
		feedbacks[0][chan] = clamp(feedbacks[0][chan], 0.0f, 1.0f);
		feedbacks[1][chan] = clamp(feedbacks[1][chan], 0.0f, 1.0f);
	}	
	
	void calcDepths(int chan) { 
		float modIn = params[DEPTHCV_PARAM].getValue();
		float cvIn = 0.0f;
		bool hasCvIn = false;
		if ((dest & 0x1) != 0) {
			cvIn += multiplySignalSlewers[chan]._last;
			hasCvIn = true;
		}
		if (inputs[ANTIGRAV_INPUT].isConnected()) {
			int chanIn = std::min(inputs[ANTIGRAV_INPUT].getChannels() - 1, chan);
			cvIn += inputs[ANTIGRAV_INPUT].getVoltage(chanIn) * 0.1f;
			hasCvIn = true;
		}
		if (hasCvIn) {
			modIn *= cvIn;
		}
			
		for (int osci = 0; osci < 2; osci++) {
			depths[osci][chan] = params[DEPTH_PARAMS + osci].getValue();
		}
		
		if ((mode & 0x1) != 0) {
			if (modIn > 0.0f) {
				// modulate depth of right side only
				depths[1][chan] += modIn;
			}
			else {
				// modulate depth of left side only
				depths[0][chan] -= modIn;// this has to modulate positively but modIn is negative, so correct for this
			}
		}
		else {
			// modulate both depths the same
			depths[0][chan] += modIn;
			depths[1][chan] += modIn;
		}
		
		depths[0][chan] = clamp(depths[0][chan], 0.0f, 1.0f);
		depths[1][chan] = clamp(depths[1][chan], 0.0f, 1.0f);
	}
};


struct DarkEnergyWidget : ModuleWidget {
	int lastPanelTheme = -1;
	std::shared_ptr<window::Svg> light_svg;
	std::shared_ptr<window::Svg> dark_svg;

	void appendContextMenu(Menu *menu) override {
		DarkEnergy *module = static_cast<DarkEnergy*>(this->module);
		assert(module);

		createPanelThemeMenu(menu, &(module->panelTheme));
	}	
	
	DarkEnergyWidget(DarkEnergy *module) {
		setModule(module);

		// Main panels from Inkscape
 		light_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/DarkEnergy-WL.svg"));
		dark_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/DarkEnergy-DM.svg"));
		int panelTheme = isDark(module ? (&((static_cast<DarkEnergy*>(module))->panelTheme)) : NULL) ? 1 : 0;// need this here since step() not called for module browser
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
		
		// mult dest button and dest lights
		addParam(createDynamicParam<GeoPushButton>(mm2px(Vec(6.28f, 16.10f)), module, DarkEnergy::MULTDEST_PARAM, module ? &module->panelTheme : NULL));		
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(23.20f, 58.19f)), module, DarkEnergy::DEST_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(32.69f, 69.36f)), module, DarkEnergy::DEST_LIGHTS + 1));

		// mult enable button and light
		addParam(createDynamicParam<GeoPushButton>(mm2px(Vec(36.75f, 38.57f)), module, DarkEnergy::MULTEN_PARAM, module ? &module->panelTheme : NULL));		
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(41.49f, 38.57f)), module, DarkEnergy::MULTEN_LIGHT));

		// mult decay knob, input and light
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC, 32.81f)), module, DarkEnergy::MULTDECAY_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(colC - oX1, 40.42f)), true, module, DarkEnergy::MULTDECAY_INPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(colC, 40.42f)), module, DarkEnergy::MULTDECAY_LIGHT));

		
		// ANTIGRAVITY (FM DEPTH)
		// depth CV knob 
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC, 50.43f)), module, DarkEnergy::DEPTHCV_PARAM, module ? &module->panelTheme : NULL));
		// depth knobs
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC - oX2, 54.80f)), module, DarkEnergy::DEPTH_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colC + oX2, 54.80f)), module, DarkEnergy::DEPTH_PARAMS + 1, module ? &module->panelTheme : NULL));		

		// depth lights
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(colC - oX2, 62.74f)), module, DarkEnergy::ANTIGRAV_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(colC + oX2, 62.74f)), module, DarkEnergy::ANTIGRAV_LIGHTS + 1));

		// depth and momentum inputs
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(colC - oX1, 63.75f)), true, module, DarkEnergy::ANTIGRAV_INPUT, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(colC + oX1, 63.75f)), true, module, DarkEnergy::MOMENTUM_INPUT, module ? &module->panelTheme : NULL));
		
		// mode button and lights
		addParam(createDynamicParam<GeoPushButton>(mm2px(Vec(colC, 63.75f)), module, DarkEnergy::MODE_PARAM, module ? &module->panelTheme : NULL));		
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(colC, 58.19f)), module, DarkEnergy::MODE_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(colC, 69.36f)), module, DarkEnergy::MODE_LIGHTS + 1));

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
		
		
		// momentum lights
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(colC - oX2, 84.21f)), module, DarkEnergy::MOMENTUM_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(mm2px(Vec(colC + oX2, 84.21f)), module, DarkEnergy::MOMENTUM_LIGHTS + 1));

		// FREQ
		// freq lights (below momentum lights)
		addChild(createLightCentered<SmallLight<GeoBlueYellowLight>>(mm2px(Vec(colC - oX2, 87.25f)), module, DarkEnergy::FREQ_LIGHTS + 2 * 0));
		addChild(createLightCentered<SmallLight<GeoBlueYellowLight>>(mm2px(Vec(colC + oX2, 87.25f)), module, DarkEnergy::FREQ_LIGHTS + 2 * 1));		
		
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

	}
	
	void step() override {
		int panelTheme = isDark(module ? (&((static_cast<DarkEnergy*>(module))->panelTheme)) : NULL) ? 1 : 0;
		if (lastPanelTheme != panelTheme) {
			lastPanelTheme = panelTheme;
			SvgPanel* panel = static_cast<SvgPanel*>(getPanel());
			panel->setBackground(panelTheme == 0 ? light_svg : dark_svg);
			panel->fb->dirty = true;
		}
		Widget::step();
	}
};

Model *modelDarkEnergy = createModel<DarkEnergy, DarkEnergyWidget>("DarkEnergy");
