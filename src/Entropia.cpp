//***********************************************************************************************
//Thermodynamic Evolving Sequencer module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental plugins by Andrew Belt and graphics  
//  from the Component Library by Wes Milholen. 
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//***********************************************************************************************


#include "Geodesics.hpp"


struct Entropia : Module {
	enum ParamIds {
		RUN_PARAM,
		STEPCLOCK_PARAM,// magnetic clock
		RESET_PARAM,
		RESETONRUN_PARAM,
		LENGTH_PARAM,
		ENUMS(CV_PARAMS, 16),// first 8 are blue, last 8 are yellow
		ENUMS(PROB_PARAMS, 8),// prob knobs
		ENUMS(OCT_PARAMS, 2),// energy (range)
		ENUMS(QUANTIZE_PARAMS, 2),// plank
		STATESWITCH_PARAM,// state switch
		SWITCHADD_PARAM,
		ENUMS(FIXEDCV_PARAMS, 2),
		ENUMS(EXTSIG_PARAMS, 2),
		ENUMS(RANDOM_PARAMS, 2),
		GPROB_PARAM,
		CLKSRC_PARAM,
		ENUMS(EXTAUDIO_PARAMS, 2),
		NUM_PARAMS
	};
	enum InputIds {
		CERTAIN_CLK_INPUT,
		UNCERTAIN_CLK_INPUT,
		LENGTH_INPUT,
		RUN_INPUT,
		RESET_INPUT,
		STATESWITCH_INPUT,// state switch
		SWITCHADD_INPUT,
		ENUMS(OCTCV_INPUTS, 2),
		ENUMS(EXTSIG_INPUTS, 2),
		ENUMS(QUANTIZE_INPUTS, 2),
		GPROB_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CV_OUTPUT,// main output
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(STEP_LIGHTS, 16),// first 8 are blue, last 8 are yellow
		ENUMS(CV_LIGHT, 3),// main output (room for Blue-Yellow-White)
		RUN_LIGHT,
		STEPCLOCK_LIGHT,
		RESET_LIGHT,
		RESETONRUN_LIGHT,
		ENUMS(LENGTH_LIGHTS, 8),// all off when len = 8, north-west turns on when len = 7, north-west and west on when len = 6, etc
		STATESWITCH_LIGHT,
		SWITCHADD_LIGHT,
		ADD_LIGHT,
		ENUMS(QUANTIZE_LIGHTS, 2),
		ENUMS(OCT_LIGHTS, 6),// first 3 are blue, last 3 are yellow (symetrical so only 3 instead of 5 declared); 0 is center, 1 is inside mirrors, 2 is outside mirrors
		ENUMS(FIXEDCV_LIGHTS, 2),
		ENUMS(EXTSIG_LIGHTS, 2),
		ENUMS(RANDOM_LIGHTS, 2),
		ENUMS(CLKSRC_LIGHTS, 2),// certain, uncertain
		ENUMS(EXTAUDIO_LIGHTS, 2),
		ENUMS(EXTCV_LIGHTS, 2),
		NUM_LIGHTS
	};
	
	
	// Constants
	enum SourceIds {SRC_CV, SRC_EXT, SRC_RND};
	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	bool running;
	bool resetOnRun;
	int length;
	int quantize;// a.k.a. plank constant, bit0 = blue, bit1 = yellow
	int audio;// bit0 = blue has audio src (else is cv), bit1 = yellow has audio src (else is cv)
	bool addMode;
	int ranges[2];// [0; 2], number of extra octaves to span each side of central octave (which is C4: 0 - 1V) 
	int sources[2];// [0; ], first is blue, 2nd yellow; follows SourceIds
	int stepIndex;
	bool pipeBlue[8];
	float randomCVs[2];// used in SRC_RND
	int clkSource;// which clock to use (0 = both, 1 = certain only, 2 = uncertain only)
	
	// No need to save, with reset
	bool rangeInc[2] = {true, true};// true when 1-3-5 increasing, false when 5-3-1 decreasing
	long clockIgnoreOnReset;
	int stepIndexOld;// when equal to stepIndex, crossfade (antipop) is finished, when not equal, crossfade until counter 0, then set to stepIndex
	long crossFadeStepsToGo;
	
	// No need to save, no reset
	float resetLight = 0.0f;
	float cvLight = 0.0f;
	float stepClockLight = 0.0f;
	float stateSwitchLight = 0.0f;
	RefreshCounter refresh;
	Trigger runningTrigger;
	Trigger plankTriggers[2];
	Trigger lengthTrigger;
	Trigger stateSwitchTrigger;
	Trigger switchAddTrigger;
	Trigger certainClockTrigger;
	Trigger uncertainClockTrigger;
	Trigger octTriggers[2];
	Trigger stepClockTrigger;
	Trigger resetTrigger;
	Trigger resetOnRunTrigger;
	Trigger fixedSrcTriggers[2];
	Trigger rndSrcTriggers[2];
	Trigger extSrcTriggers[2];
	Trigger extAudioTriggers[2];
	Trigger clkSrcTrigger;
	
	inline float quantizeCV(float cv) {return std::round(cv * 12.0f) / 12.0f;}
	inline void updatePipeBlue(int step) {
		float effectiveKnob = params[PROB_PARAMS + step].getValue() + -1.0f * (params[GPROB_PARAM].getValue() + inputs[GPROB_INPUT].getVoltage() / 5.0f);
		pipeBlue[step] = effectiveKnob > random::uniform();
	}
	inline void updateRandomCVs() {
		randomCVs[0] = random::uniform();
		randomCVs[1] = random::uniform();
		cvLight = 1.0f;// this could be elsewhere since no relevance to randomCVs, but ok here
	}
	
	
	Entropia() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		char strBuf[32];				
		for (int i = 0; i < 8; i++) {// Blue CV knobs
			snprintf(strBuf, 32, "Blue CV step %i", i + 1);		
			configParam(CV_PARAMS + i, 0.0f, 1.0f, 0.5f, strBuf);
		}
		for (int i = 0; i < 8; i++) {// Yellow CV knobs
			snprintf(strBuf, 32, "Yellow CV step %i", i + 1);		
			configParam(CV_PARAMS + 8 + i, 0.0f, 1.0f, 0.5f, strBuf);
		}
		for (int i = 0; i < 8; i++) {// Prob knobs
			snprintf(strBuf, 32, "Probability step %i", i + 1);		
			configParam(PROB_PARAMS + i, 0.0f, 1.0f, 1.0f, strBuf);
		}

		configParam(LENGTH_PARAM, 0.0f, 1.0f, 0.0f, "Length");
		configParam(CLKSRC_PARAM, 0.0f, 1.0f, 0.0f, "Clock sources");
		
		configParam(SWITCHADD_PARAM, 0.0f, 1.0f, 0.0f, "Add");
		configParam(STATESWITCH_PARAM, 0.0f, 1.0f, 0.0f, "Invert microstate");
		configParam(QUANTIZE_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Quantize (Planck) blue");
		configParam(QUANTIZE_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Quantize (Planck) yellow");
		
		configParam(OCT_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Octaves blue");
		configParam(OCT_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Octaves yellow");
		configParam(GPROB_PARAM, -1.0f, 1.0f, 0.0f, "Global probability");
		
		configParam(EXTSIG_PARAMS + 0, 0.0f, 1.0f, 0.0f, "External signal blue");
		configParam(RANDOM_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Random blue");
		configParam(FIXEDCV_PARAMS + 0, 0.0f, 1.0f, 0.0f, "Fixed CV blue");
		configParam(EXTAUDIO_PARAMS + 0, 0.0f, 1.0f, 0.0f, "CV/audio blue");
		configParam(EXTSIG_PARAMS + 1, 0.0f, 1.0f, 0.0f, "External signal yellow");
		configParam(RANDOM_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Random yellow");
		configParam(FIXEDCV_PARAMS + 1, 0.0f, 1.0f, 0.0f, "Fixed CV yellow");
		configParam(EXTAUDIO_PARAMS + 1, 0.0f, 1.0f, 0.0f, "CV/audio yellow");
		configParam(RUN_PARAM, 0.0f, 1.0f, 0.0f, "Run");	
		configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f, "Reset");	
		configParam(STEPCLOCK_PARAM, 0.0f, 1.0f, 0.0f, "Magnetic clock");			
		configParam(RESETONRUN_PARAM, 0.0f, 1.0f, 0.0f, "Reset on run");				
						
		onReset();

		panelTheme = (loadDarkAsDefault() ? 1 : 0);
	}

	
	void onReset() override {
		running = true;
		resetOnRun = false;
		length = 8;
		quantize = 3;
		audio = 0;
		addMode = false;
		for (int i = 0; i < 2; i++) {
			ranges[i] = 1;	
			sources[i] = SRC_CV;
		}
		// stepIndex done in resetNonJson(true) -> initRun(true)
		// pipeBlue[] done in resetNonJson(true) -> initRun(true)
		// randomCVs[] done in resetNonJson(true) -> initRun(true)
		clkSource = 0;
		resetNonJson(true);
	}
	void resetNonJson(bool hard) {
		rangeInc[0] = true;
		rangeInc[1] = true;
		initRun(hard);
	}
	void initRun(bool hard) {
		clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * APP->engine->getSampleRate());
		if (hard) {
			stepIndex = 0;
			for (int i = 0; i < 8; i++)
				updatePipeBlue(i);
			updateRandomCVs();
		}
		stepIndexOld = stepIndex;
		crossFadeStepsToGo = 0;
	}
	
	
	void onRandomize() override {
		initRun(true);
	}


	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// running
		json_object_set_new(rootJ, "running", json_boolean(running));

		// resetOnRun
		json_object_set_new(rootJ, "resetOnRun", json_boolean(resetOnRun));

		// length
		json_object_set_new(rootJ, "length", json_integer(length));

		// quantize
		json_object_set_new(rootJ, "quantize", json_integer(quantize));

		// audio
		json_object_set_new(rootJ, "audio", json_integer(audio));

		// ranges
		json_object_set_new(rootJ, "ranges0", json_integer(ranges[0]));
		json_object_set_new(rootJ, "ranges1", json_integer(ranges[1]));

		// addMode
		json_object_set_new(rootJ, "addMode", json_boolean(addMode));

		// sources
		json_object_set_new(rootJ, "sources0", json_integer(sources[0]));
		json_object_set_new(rootJ, "sources1", json_integer(sources[1]));

		// stepIndex
		json_object_set_new(rootJ, "stepIndex", json_integer(stepIndex));

		// pipeBlue (only need to save the one corresponding to stepIndex, since others will get regenerated when moving to those steps)
		json_object_set_new(rootJ, "pipeBlue", json_boolean(pipeBlue[stepIndex]));
		
		// randomCVs (only need to save the one corresponding to stepIndex, since others will get regenerated when moving to those steps)
		json_object_set_new(rootJ, "randomCVs0", json_real(randomCVs[0]));
		json_object_set_new(rootJ, "randomCVs1", json_real(randomCVs[1]));

		// clkSource
		json_object_set_new(rootJ, "clkSource", json_integer(clkSource));

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

		// length
		json_t *lengthJ = json_object_get(rootJ, "length");
		if (lengthJ)
			length = json_integer_value(lengthJ);

		// quantize
		json_t *quantizeJ = json_object_get(rootJ, "quantize");
		if (quantizeJ)
			quantize = json_integer_value(quantizeJ);

		// audio
		json_t *audioJ = json_object_get(rootJ, "audio");
		if (audioJ)
			audio = json_integer_value(audioJ);

		// ranges
		json_t *ranges0J = json_object_get(rootJ, "ranges0");
		if (ranges0J)
			ranges[0] = json_integer_value(ranges0J);
		json_t *ranges1J = json_object_get(rootJ, "ranges1");
		if (ranges1J)
			ranges[1] = json_integer_value(ranges1J);

		// addMode
		json_t *addModeJ = json_object_get(rootJ, "addMode");
		if (addModeJ)
			addMode = json_is_true(addModeJ);

		// sources
		json_t *sources0J = json_object_get(rootJ, "sources0");
		if (sources0J)
			sources[0] = json_integer_value(sources0J);
		json_t *sources1J = json_object_get(rootJ, "sources1");
		if (sources1J)
			sources[1] = json_integer_value(sources1J);

		// stepIndex
		json_t *stepIndexJ = json_object_get(rootJ, "stepIndex");
		if (stepIndexJ)
			stepIndex = json_integer_value(stepIndexJ);

		// pipeBlue (only saved the one corresponding to stepIndex, since others will get regenerated when moving to those steps)
		json_t *pipeBlueJ = json_object_get(rootJ, "pipeBlue");
		if (pipeBlueJ)
			pipeBlue[stepIndex] = json_is_true(pipeBlueJ);

		// randomCVs (only saved the one corresponding to stepIndex, since others will get regenerated when moving to those steps)
		json_t *randomCVs0J = json_object_get(rootJ, "randomCVs0");
		if (randomCVs0J)
			randomCVs[0] = json_number_value(randomCVs0J);
		json_t *randomCVs1J = json_object_get(rootJ, "randomCVs1");
		if (randomCVs1J)
			randomCVs[1] = json_number_value(randomCVs1J);

		// clkSource
		json_t *clkSourceJ = json_object_get(rootJ, "clkSource");
		if (clkSourceJ)
			clkSource = json_integer_value(clkSourceJ);

		resetNonJson(false);// soft init, don't want to init stepIndex, pipeBlue nor randomCVs
	}

	
	void process(const ProcessArgs &args) override {
		float crossFadeTime = 0.005f;
	
		//********** Buttons, knobs, switches and inputs **********
	
		// Run button
		if (runningTrigger.process(params[RUN_PARAM].getValue() + inputs[RUN_INPUT].getVoltage())) {// no input refresh here, don't want to introduce startup skew
			running = !running;
			if (running) {
				if (resetOnRun) {
					initRun(true);
				}
			}
		}
		
		if (refresh.processInputs()) {
			// Length button and input
			bool lengthTrig = lengthTrigger.process(params[LENGTH_PARAM].getValue());
			if (inputs[LENGTH_INPUT].isConnected()) {
				length = clamp(8 - (int)(inputs[LENGTH_INPUT].getVoltage() * 7.0f / 10.0f + 0.5f)  , 1, 8);
			}
			else if (lengthTrig) {
				if (length > 1) length--;
				else length = 8;
			}

			// Plank buttons (quantize)
			if (plankTriggers[0].process(params[QUANTIZE_PARAMS + 0].getValue()))
				quantize ^= 0x1;
			if (plankTriggers[1].process(params[QUANTIZE_PARAMS + 1].getValue()))
				quantize ^= 0x2;

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
			
			// Source buttons (fixedCV, random, ext)
			for (int i = 0; i < 2; i++) {
				if (rndSrcTriggers[i].process(params[RANDOM_PARAMS + i].getValue()))
					sources[i] = SRC_RND;
				if (extSrcTriggers[i].process(params[EXTSIG_PARAMS + i].getValue()))
					sources[i] = SRC_EXT;
				if (fixedSrcTriggers[i].process(params[FIXEDCV_PARAMS + i].getValue()))
					sources[i] = SRC_CV;
				if (extAudioTriggers[i].process(params[EXTAUDIO_PARAMS + i].getValue()))
					audio ^= (1 << i);
			}
			
			// addMode
			if (switchAddTrigger.process(params[SWITCHADD_PARAM].getValue() + inputs[SWITCHADD_INPUT].getVoltage())) {
				addMode = !addMode;
			}		
		
			// StateSwitch
			if (stateSwitchTrigger.process(params[STATESWITCH_PARAM].getValue() + inputs[STATESWITCH_INPUT].getVoltage())) {
				pipeBlue[stepIndex] = !pipeBlue[stepIndex];
				stateSwitchLight = 1.0f;
			}		
		
			// Reset on Run button
			if (resetOnRunTrigger.process(params[RESETONRUN_PARAM].getValue())) {
				resetOnRun = !resetOnRun;
			}	

			if (clkSrcTrigger.process(params[CLKSRC_PARAM].getValue())) {
				if (++clkSource > 2)
					clkSource = 0;
			}
		}// userInputs refresh
		

		//********** Clock and reset **********
		
		// External clocks
		if (running && clockIgnoreOnReset == 0l) {
			bool certainClockTrig = certainClockTrigger.process(inputs[CERTAIN_CLK_INPUT].getVoltage());
			bool uncertainClockTrig = uncertainClockTrigger.process(inputs[UNCERTAIN_CLK_INPUT].getVoltage());
			certainClockTrig &= (clkSource < 2);
			if (certainClockTrig) {
				stepIndex++;
			}
			uncertainClockTrig &= ((clkSource & 0x1) == 0);
			if (uncertainClockTrig) {
				stepIndex += getWeighted1to8random();
			}
			if (certainClockTrig || uncertainClockTrig) {
				stepIndex %= length;
				crossFadeStepsToGo = (long)(crossFadeTime * args.sampleRate);;
				updatePipeBlue(stepIndex);
				updateRandomCVs();
			}
		}				
		// Magnetic clock (manual step clock)
		if (stepClockTrigger.process(params[STEPCLOCK_PARAM].getValue())) {
			if (++stepIndex >= length) stepIndex = 0;
			crossFadeStepsToGo = (long)(crossFadeTime * args.sampleRate);
			updatePipeBlue(stepIndex);
			updateRandomCVs();
			stepClockLight = 1.0f;
		}
		
		// Reset
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage() + params[RESET_PARAM].getValue())) {
			initRun(true);
			resetLight = 1.0f;
			certainClockTrigger.reset();
			uncertainClockTrigger.reset();
		}
		
		
		//********** Outputs and lights **********

		// Output
		int crossFadeActive = audio;
		if (sources[0] != SRC_EXT) crossFadeActive &= ~0x1;
		if (sources[1] != SRC_EXT) crossFadeActive &= ~0x2;
		if (crossFadeStepsToGo > 0 && crossFadeActive != 0)
		{
			long crossFadeStepsToGoInit = (long)(crossFadeTime * args.sampleRate);
			float fadeRatio = ((float)crossFadeStepsToGo) / ((float)crossFadeStepsToGoInit);
			outputs[CV_OUTPUT].setVoltage(calcOutput(stepIndexOld) * fadeRatio + calcOutput(stepIndex) * (1.0f - fadeRatio));
			crossFadeStepsToGo--;
			if (crossFadeStepsToGo == 0)
				stepIndexOld = stepIndex;
		}
		else
			outputs[CV_OUTPUT].setVoltage(calcOutput(stepIndex));
		
		// lights
		if (refresh.processLights()) {
			float deltaTime = args.sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2);

			// Reset light
			lights[RESET_LIGHT].setSmoothBrightness(resetLight, deltaTime);	
			resetLight = 0.0f;	
			
			// Run light
			lights[RUN_LIGHT].setBrightness(running ? 1.0f : 0.0f);
			lights[RESETONRUN_LIGHT].setBrightness(resetOnRun ? 1.0f : 0.0f);
			
			// Length lights
			for (int i = 0; i < 8; i++)
				lights[LENGTH_LIGHTS + i].setBrightness(i < length ? 0.0f : 1.0f);
			
			// Plank
			lights[QUANTIZE_LIGHTS + 0].setBrightness((quantize & 0x1) ? 1.0f : 0.0f);// Blue
			lights[QUANTIZE_LIGHTS + 1].setBrightness((quantize & 0x2) ? 1.0f : 0.0f);// Yellow

			// step and main output lights (GeoBlueYellowWhiteLight)
			lights[CV_LIGHT + 0].setSmoothBrightness((pipeBlue[stepIndex])              ? cvLight : 0.0f, deltaTime);
			lights[CV_LIGHT + 1].setSmoothBrightness((!pipeBlue[stepIndex] && !addMode) ? cvLight : 0.0f, deltaTime);
			lights[CV_LIGHT + 2].setSmoothBrightness((!pipeBlue[stepIndex] && addMode)  ? cvLight : 0.0f, deltaTime);
			cvLight = 0.0f;	
			for (int i = 0; i < 8; i++) {
				lights[STEP_LIGHTS + i].setBrightness( ((pipeBlue[i] || addMode) && stepIndex == i) ? 1.0f : 0.0f );
				lights[STEP_LIGHTS + 8 + i].setBrightness( ((!pipeBlue[i]) && stepIndex == i) ? 1.0f : 0.0f );
			}
			
			// Range (energy) lights
			for (int i = 0; i < 3; i++) {
				lights[OCT_LIGHTS + i].setBrightness(i <= ranges[0] ? 1.0f : 0.0f);
				lights[OCT_LIGHTS + 3 + i].setBrightness(i <= ranges[1] ? 1.0f : 0.0f);
			}
				
			// Step clocks light
			lights[STEPCLOCK_LIGHT].setSmoothBrightness(stepClockLight, deltaTime);
			stepClockLight = 0.0f;

			// Swtich add light
			lights[SWITCHADD_LIGHT].setBrightness(addMode ? 0.0f : 1.0f);
			lights[ADD_LIGHT].setBrightness(addMode ? 1.0f : 0.0f);
			
			// State switch light
			lights[STATESWITCH_LIGHT].setSmoothBrightness(stateSwitchLight, deltaTime);
			stateSwitchLight = 0.0f;
			
			for (int i = 0; i < 2; i++) {
				// Sources lights
				lights[RANDOM_LIGHTS + i].setBrightness((sources[i] == SRC_RND) ? 1.0f : 0.0f);
				lights[EXTSIG_LIGHTS + i].setBrightness((sources[i] == SRC_EXT) ? 1.0f : 0.0f);
				lights[FIXEDCV_LIGHTS + i].setBrightness((sources[i] == SRC_CV) ? 1.0f : 0.0f);
				
				// Audio lights
				lights[EXTAUDIO_LIGHTS + i].setBrightness(((audio & (1 << i)) != 0) ? 1.0f : 0.0f);
				lights[EXTCV_LIGHTS + i].setBrightness(((audio & (1 << i)) == 0) ? 1.0f : 0.0f);
			}
			
			
			
			// Clock source lights
			lights[CLKSRC_LIGHTS + 0].setBrightness((clkSource < 2) ? 1.0f : 0.0f);
			lights[CLKSRC_LIGHTS + 1].setBrightness(((clkSource & 0x1) == 0) ? 1.0f : 0.0f);
			
		}// lightRefreshCounter
		
		if (clockIgnoreOnReset > 0l)
			clockIgnoreOnReset--;
	}// step()
	
	inline float calcOutput(int stepIdx) {
		if (addMode) 
			return getStepCV(stepIdx, true) + (pipeBlue[stepIdx] ? 0.0f : getStepCV(stepIdx, false));
		return getStepCV(stepIdx, pipeBlue[stepIdx]);
	}
	
	float getStepCV(int step, bool blue) {
		int colorIndex = blue ? 0 : 1;
		float knobVal = params[CV_PARAMS + (colorIndex << 3) + step].getValue();
		float cv = 0.0f;
		
		if (sources[colorIndex] == SRC_RND) {
			cv = randomCVs[colorIndex] * (knobVal * 10.0f - 5.0f);
		}
		else if (sources[colorIndex] == SRC_EXT) {
			float extOffset = ((audio & (1 << colorIndex)) != 0) ? 0.0f : -1.0f;
			cv = clamp(inputs[EXTSIG_INPUTS + colorIndex].getVoltage() * (knobVal * 2.0f + extOffset), -10.0f, 10.0f);
		}
		else {// SRC_CV
			int range = ranges[colorIndex];
			if ( (blue && (quantize & 0x1) != 0) || (!blue && (quantize > 1)) ) {
				cv = (knobVal * (float)(range * 2 + 1) - (float)range);
				cv = quantizeCV(cv);
			}
			else {
				int maxCV = (range == 0 ? 1 : (range * 5));// maxCV is [1, 5, 10]
				cv = knobVal * (float)(maxCV * 2) - (float)maxCV;
			}
		}
		
		return cv;
	}
	
};


struct EntropiaWidget : ModuleWidget {
	SvgPanel* darkPanel;

	struct PanelThemeItem : MenuItem {
		Entropia *module;
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

		Entropia *module = dynamic_cast<Entropia*>(this->module);
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
	
	EntropiaWidget(Entropia *module) {
		setModule(module);

		// Main panels from Inkscape
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/Entropia-WL.svg")));
        if (module) {
			darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/Entropia-DM.svg")));
			darkPanel->visible = false;
			addChild(darkPanel);
		}
		
		// Screws 
		// part of svg panel, no code required
		
		static constexpr float colRulerCenter = 157.0f;//box.size.x / 2.0f;
		static constexpr float rowRulerOutput = 380.0f - 155.5f;
		static constexpr float radius1 = 50.0f;
		static constexpr float offset1 = 35.5f;
		static constexpr float radius3 = 105.0f;
		static constexpr float offset3 = 74.5f;
		static constexpr float offset2b = 74.5f;// big
		static constexpr float offset2s = 27.5f;// small
		
		
		// CV out and light 
		addOutput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerOutput), false, module, Entropia::CV_OUTPUT, module ? &module->panelTheme : NULL));		
		addChild(createLightCentered<SmallLight<GeoBlueYellowWhiteLight>>(VecPx(colRulerCenter, rowRulerOutput - 21.5f), module, Entropia::CV_LIGHT));
		
		// Blue CV knobs
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter, rowRulerOutput - radius1), module, Entropia::CV_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter + offset1, rowRulerOutput - offset1), module, Entropia::CV_PARAMS + 1, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter + radius1, rowRulerOutput), module, Entropia::CV_PARAMS + 2, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter + offset1, rowRulerOutput + offset1), module, Entropia::CV_PARAMS + 3, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter, rowRulerOutput + radius1), module, Entropia::CV_PARAMS + 4, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter - offset1, rowRulerOutput + offset1), module, Entropia::CV_PARAMS + 5, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter - radius1, rowRulerOutput), module, Entropia::CV_PARAMS + 6, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter - offset1, rowRulerOutput - offset1), module, Entropia::CV_PARAMS + 7, module ? &module->panelTheme : NULL));

		// Yellow CV knobs
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter, rowRulerOutput - radius3), module, Entropia::CV_PARAMS + 8 + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter + offset3, rowRulerOutput - offset3), module, Entropia::CV_PARAMS + 8 + 1, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter + radius3, rowRulerOutput), module, Entropia::CV_PARAMS + 8 + 2, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter + offset3, rowRulerOutput + offset3), module, Entropia::CV_PARAMS + 8 + 3, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter, rowRulerOutput + radius3), module, Entropia::CV_PARAMS + 8 + 4, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter - offset3, rowRulerOutput + offset3), module, Entropia::CV_PARAMS + 8 + 5, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter - radius3, rowRulerOutput), module, Entropia::CV_PARAMS + 8 + 6, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter - offset3, rowRulerOutput - offset3), module, Entropia::CV_PARAMS + 8 + 7, module ? &module->panelTheme : NULL));
		
		// Prob CV knobs
		addParam(createDynamicParam<GeoKnobRight>(VecPx(colRulerCenter + offset2s, rowRulerOutput - offset2b - 3), module, Entropia::PROB_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnobBotRight>(VecPx(colRulerCenter + offset2b, rowRulerOutput - offset2s - 8), module, Entropia::PROB_PARAMS + 1, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnobBottom>(VecPx(colRulerCenter + offset2b + 3, rowRulerOutput + offset2s), module, Entropia::PROB_PARAMS + 2, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnobBotLeft>(VecPx(colRulerCenter + offset2s + 8, rowRulerOutput + offset2b), module, Entropia::PROB_PARAMS + 3, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnobLeft>(VecPx(colRulerCenter - offset2s, rowRulerOutput + offset2b + 3), module, Entropia::PROB_PARAMS + 4, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnobTopLeft>(VecPx(colRulerCenter - offset2b, rowRulerOutput + offset2s + 8), module, Entropia::PROB_PARAMS + 5, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter - offset2b - 3, rowRulerOutput - offset2s), module, Entropia::PROB_PARAMS + 6, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnobTopRight>(VecPx(colRulerCenter - offset2s - 7.5f, rowRulerOutput - offset2b + 1.0f), module, Entropia::PROB_PARAMS + 7, module ? &module->panelTheme : NULL));
		
		// Blue step lights	
		float radiusBL = 228.5f - 155.5f;// radius blue lights
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter, rowRulerOutput - radiusBL), module, Entropia::STEP_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter + radiusBL * 0.707f, rowRulerOutput - radiusBL * 0.707f), module, Entropia::STEP_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter + radiusBL, rowRulerOutput), module, Entropia::STEP_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter + radiusBL * 0.707f, rowRulerOutput + radiusBL * 0.707f), module, Entropia::STEP_LIGHTS + 3));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter, rowRulerOutput + radiusBL), module, Entropia::STEP_LIGHTS + 4));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - radiusBL * 0.707f, rowRulerOutput + radiusBL * 0.707f), module, Entropia::STEP_LIGHTS + 5));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - radiusBL, rowRulerOutput), module, Entropia::STEP_LIGHTS + 6));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - radiusBL * 0.707f, rowRulerOutput - radiusBL * 0.707f), module, Entropia::STEP_LIGHTS + 7));
		radiusBL += 9.0f;
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter, rowRulerOutput - radiusBL), module, Entropia::STEP_LIGHTS + 8 + 0));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + radiusBL * 0.707f, rowRulerOutput - radiusBL * 0.707f), module, Entropia::STEP_LIGHTS + 8 + 1));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + radiusBL, rowRulerOutput), module, Entropia::STEP_LIGHTS + 8 + 2));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + radiusBL * 0.707f, rowRulerOutput + radiusBL * 0.707f), module, Entropia::STEP_LIGHTS + 8 + 3));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter, rowRulerOutput + radiusBL), module, Entropia::STEP_LIGHTS + 8 + 4));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter - radiusBL * 0.707f, rowRulerOutput + radiusBL * 0.707f), module, Entropia::STEP_LIGHTS + 8 + 5));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter - radiusBL, rowRulerOutput), module, Entropia::STEP_LIGHTS + 8 + 6));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter - radiusBL * 0.707f, rowRulerOutput - radiusBL * 0.707f), module, Entropia::STEP_LIGHTS + 8 + 7));
	
	
		// Length jack, button and lights
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + 116.5f, rowRulerOutput + 70.0f), true, module, Entropia::LENGTH_INPUT, module ? &module->panelTheme : NULL));
		static float lenButtonX = colRulerCenter + 130.5f;
		static float lenButtonY = rowRulerOutput + 36.5f;
		addParam(createDynamicParam<GeoPushButton>(VecPx(lenButtonX, lenButtonY), module, Entropia::LENGTH_PARAM, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(lenButtonX        , lenButtonY - 14.5f), module, Entropia::LENGTH_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(lenButtonX + 10.5f, lenButtonY - 10.5f), module, Entropia::LENGTH_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(lenButtonX + 14.5f, lenButtonY        ), module, Entropia::LENGTH_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(lenButtonX + 10.5f, lenButtonY + 10.5f), module, Entropia::LENGTH_LIGHTS + 3));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(lenButtonX        , lenButtonY + 14.5f), module, Entropia::LENGTH_LIGHTS + 4));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(lenButtonX - 10.5f, lenButtonY + 10.5f), module, Entropia::LENGTH_LIGHTS + 5));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(lenButtonX - 14.5f, lenButtonY        ), module, Entropia::LENGTH_LIGHTS + 6));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(lenButtonX - 10.5f, lenButtonY - 10.5f), module, Entropia::LENGTH_LIGHTS + 7));
		
		// Clock inputs
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - 130.5f, rowRulerOutput + 36.5f), true, module, Entropia::CERTAIN_CLK_INPUT, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - 116.5f, rowRulerOutput + 70.0f), true, module, Entropia::UNCERTAIN_CLK_INPUT, module ? &module->panelTheme : NULL));
		// Clock source button and LEDs
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(43.0f, 256.5f), module, Entropia::CLKSRC_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(55.0f, 284.5f), module, Entropia::CLKSRC_LIGHTS + 1));
		addParam(createDynamicParam<GeoPushButton>(VecPx(46.0f, 272.5f), module, Entropia::CLKSRC_PARAM, module ? &module->panelTheme : NULL));
		
		
		// Switch, add, state (jacks, buttons, ligths)
		// left side
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - 130.5f, rowRulerOutput - 36.0f), true, module, Entropia::SWITCHADD_INPUT, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - 115.5f, rowRulerOutput - 69.0f), module, Entropia::SWITCHADD_PARAM, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 115.5f - 7.0f, rowRulerOutput - 69.0f + 13.0f), module, Entropia::SWITCHADD_LIGHT));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - 115.5f + 3.0f, rowRulerOutput - 69.0f + 14.0f), module, Entropia::ADD_LIGHT));
		// right side
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + 130.5f, rowRulerOutput - 36.0f), true, module, Entropia::STATESWITCH_INPUT, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + 115.5f, rowRulerOutput - 69.0f), module, Entropia::STATESWITCH_PARAM, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + 115.5f + 7.0f, rowRulerOutput - 69.0f + 13.0f), module, Entropia::STATESWITCH_LIGHT));
		
		// Plank constant (jack, light and button)
		// left side (blue)
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - 96.0f, rowRulerOutput - 96.0f), true, module, Entropia::OCTCV_INPUTS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - 96.0f - 13.0f, rowRulerOutput - 96.0f - 13.0f), module, Entropia::QUANTIZE_LIGHTS + 0));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - 96.0f - 23.0f, rowRulerOutput - 96.0f - 23.0f), module, Entropia::QUANTIZE_PARAMS + 0, module ? &module->panelTheme : NULL));
		// right side (yellow)
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + 96.0f, rowRulerOutput - 96.0f), true, module, Entropia::OCTCV_INPUTS + 1, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + 96.0f + 13.0f, rowRulerOutput - 96.0f - 13.0f), module, Entropia::QUANTIZE_LIGHTS + 1));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + 96.0f + 23.0f, rowRulerOutput - 96.0f - 23.0f), module, Entropia::QUANTIZE_PARAMS + 1, module ? &module->panelTheme : NULL));
		
		// Energy (button and lights)
		// left side (blue)
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - 69.5f, rowRulerOutput - 116.0f), module, Entropia::OCT_PARAMS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - 69.5f - 12.0f, rowRulerOutput - 116.0f + 9.0f), module, Entropia::OCT_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - 69.5f - 15.0f, rowRulerOutput - 116.0f - 1.0f), module, Entropia::OCT_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - 69.5f - 3.0f, rowRulerOutput - 116.0f + 14.0f), module, Entropia::OCT_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - 69.5f - 10.0f, rowRulerOutput - 116.0f - 11.0f), module, Entropia::OCT_LIGHTS + 2));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - 69.5f + 7.0f, rowRulerOutput - 116.0f + 12.0f), module, Entropia::OCT_LIGHTS + 2));
		// right side (yellow)
		// left side (blue)
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + 69.5f, rowRulerOutput - 116.0f), module, Entropia::OCT_PARAMS + 1, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + 69.5f + 12.0f, rowRulerOutput - 116.0f + 9.0f), module, Entropia::OCT_LIGHTS + 3 + 0));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + 69.5f + 15.0f, rowRulerOutput - 116.0f - 1.0f), module, Entropia::OCT_LIGHTS + 3 + 1));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + 69.5f + 3.0f, rowRulerOutput - 116.0f + 14.0f), module, Entropia::OCT_LIGHTS + 3 + 1));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + 69.5f + 10.0f, rowRulerOutput - 116.0f - 11.0f), module, Entropia::OCT_LIGHTS + 3 + 2));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + 69.5f - 7.0f, rowRulerOutput - 116.0f + 12.0f), module, Entropia::OCT_LIGHTS + 3 + 2));
		
		
		// Top portion
		static constexpr float rowRulerTop = rowRulerOutput - 150.0f;
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter, rowRulerTop - 30.5f), true, module, Entropia::GPROB_INPUT, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colRulerCenter, rowRulerTop), module, Entropia::GPROB_PARAM, module ? &module->panelTheme : NULL));
		
		// Left side top
		// ext
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - 77.5f, rowRulerTop), true, module, Entropia::EXTSIG_INPUTS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - 41.5f, rowRulerTop), module, Entropia::EXTSIG_PARAMS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - 26.5f, rowRulerTop), module, Entropia::EXTSIG_LIGHTS + 0));
		// random
		static constexpr float buttonOffsetX = 35.5f;// button
		static constexpr float buttonOffsetY = 20.5f;// button
		static constexpr float lightOffsetX = 22.5f;// light
		static constexpr float lightOffsetY = 12.5f;// light
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - buttonOffsetX, rowRulerTop - buttonOffsetY), module, Entropia::RANDOM_PARAMS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - lightOffsetX, rowRulerTop - lightOffsetY), module, Entropia::RANDOM_LIGHTS + 0));
		// fixed cv
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - buttonOffsetX, rowRulerTop + buttonOffsetY), module, Entropia::FIXEDCV_PARAMS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoBlueLight>>(VecPx(colRulerCenter - lightOffsetX, rowRulerTop + lightOffsetY), module, Entropia::FIXEDCV_LIGHTS + 0));
		// audio
		addParam(createDynamicParam<GeoPushButton>(VecPx(38.5f, 380.0f - 325.5f), module, Entropia::EXTAUDIO_PARAMS + 0, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(40.0f, 380.0f - 311.5f), module, Entropia::EXTAUDIO_LIGHTS + 0));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(48.5f, 380.0f - 315.5f), module, Entropia::EXTCV_LIGHTS + 0));
		
		
		// Right side top
		// ext
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + 77.5f, rowRulerTop), true, module, Entropia::EXTSIG_INPUTS + 1, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + 41.5f, rowRulerTop), module, Entropia::EXTSIG_PARAMS + 1, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + 26.5f, rowRulerTop), module, Entropia::EXTSIG_LIGHTS + 1));
		// random
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + buttonOffsetX, rowRulerTop - buttonOffsetY), module, Entropia::RANDOM_PARAMS + 1, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + lightOffsetX, rowRulerTop - lightOffsetY), module, Entropia::RANDOM_LIGHTS + 1));
		// fixed cv
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + buttonOffsetX, rowRulerTop + buttonOffsetY), module, Entropia::FIXEDCV_PARAMS + 1, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoYellowLight>>(VecPx(colRulerCenter + lightOffsetX, rowRulerTop + lightOffsetY), module, Entropia::FIXEDCV_LIGHTS + 1));
		// audio
		addParam(createDynamicParam<GeoPushButton>(VecPx(315.0f - 38.5f, 380.0f - 325.5f), module, Entropia::EXTAUDIO_PARAMS + 1, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(315.0f - 40.0f, 380.0f - 311.5f), module, Entropia::EXTAUDIO_LIGHTS + 1));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(315.0f - 48.5f, 380.0f - 315.5f), module, Entropia::EXTCV_LIGHTS + 1));
		
		
		
		// Bottom row
		
		// Run jack, light and button
		static constexpr float rowRulerRunJack = 380.0f - 32.5f;
		static constexpr float offsetRunJackX = 119.5f;
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter - offsetRunJackX, rowRulerRunJack), true, module, Entropia::RUN_INPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - offsetRunJackX + 18.0f, rowRulerRunJack), module, Entropia::RUN_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - offsetRunJackX + 33.0f, rowRulerRunJack), module, Entropia::RUN_PARAM, module ? &module->panelTheme : NULL));	
		
		// Reset jack, light and button
		addInput(createDynamicPort<GeoPort>(VecPx(colRulerCenter + offsetRunJackX, rowRulerRunJack), true, module, Entropia::RESET_INPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + offsetRunJackX - 18.0f, rowRulerRunJack), module, Entropia::RESET_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + offsetRunJackX - 33.0f, rowRulerRunJack), module, Entropia::RESET_PARAM, module ? &module->panelTheme : NULL));	
	
		static constexpr float offsetMagneticButton = 42.5f;
		// Magnetic clock (step clocks)
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter - offsetMagneticButton - 15.0f, rowRulerRunJack), module, Entropia::STEPCLOCK_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter - offsetMagneticButton, rowRulerRunJack), module, Entropia::STEPCLOCK_PARAM, module ? &module->panelTheme : NULL));			
		// Reset on Run light and button
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colRulerCenter + offsetMagneticButton + 15.0f, rowRulerRunJack), module, Entropia::RESETONRUN_LIGHT));
		addParam(createDynamicParam<GeoPushButton>(VecPx(colRulerCenter + offsetMagneticButton, rowRulerRunJack), module, Entropia::RESETONRUN_PARAM, module ? &module->panelTheme : NULL));	
	}
	
	void step() override {
		if (module) {
			panel->visible = ((((Entropia*)module)->panelTheme) == 0);
			darkPanel->visible  = ((((Entropia*)module)->panelTheme) == 1);
		}
		Widget::step();
	}
};

Model *modelEntropia = createModel<Entropia, EntropiaWidget>("Entropia");