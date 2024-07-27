//***********************************************************************************************
//Relativistic time shifting clock module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental and Audible Instruments plugins by Andrew Belt and graphics  
//  from the Component Library. 
//See ./LICENSE.md for all licenses
//See ./res/fonts/ for font licenses
//***********************************************************************************************


#include "Geodesics.hpp"


class Clock {
	// The -1.0 step is used as a reset state every period so that 
	//   lengths can be re-computed; it will stay at -1.0 when a clock is inactive.
	// a clock frame is defined as "length * iterations + syncWait", and
	//   for master, syncWait does not apply and iterations = 1

	
	double step = 0.0;// -1.0 when stopped, [0 to period[ for clock steps 
	double remainder = 0.0;
	double length = 0.0;// period
	double sampleTime = 0.0;
	int iterations = 0;// run this many periods before going into sync if sub-clock
	Clock* syncSrc = nullptr; // only subclocks will have this set to master clock
	static constexpr double guard = 0.0005;// in seconds, region for sync to occur right before end of length of last iteration; sub clocks must be low during this period
	bool *resetClockOutputsHigh = nullptr;
	
	public:
	
	Clock(Clock* clkGiven, bool *resetClockOutputsHighPtr) {
		syncSrc = clkGiven;
		resetClockOutputsHigh = resetClockOutputsHighPtr;
		reset();
	}
	
	void reset(double _remainder = 0.0) {
		step = -1.0;
		remainder = _remainder;
	}
	bool isReset() {
		return step == -1.0;
	}
	double getStep() {
		return step;
	}
	void start() {
		step = remainder;
	}
	
	void setup(double lengthGiven, int iterationsGiven, double sampleTimeGiven) {
		length = lengthGiven;
		iterations = iterationsGiven;
		sampleTime = sampleTimeGiven;
	}

	void stepClock() {// here the clock was output on step "step", this function is called near end of module::process()
		if (step >= 0.0) {// if active clock
			step += sampleTime;
			if ( (syncSrc != nullptr) && (iterations == 1) && (step > (length - guard)) ) {// if in sync region
				if (syncSrc->isReset()) {
					reset();
				}// else nothing needs to be done, just wait and step stays the same
			}
			else {
				if (step >= length) {// reached end iteration
					iterations--;
					step -= length;
					if (iterations <= 0) {
						double newRemainder = (syncSrc == nullptr ? step : 0.0);// don't calc remainders for subclocks since they sync to master
						reset(newRemainder);// frame done
					}
				}
			}
		}
	}
	
	void applyNewLength(double lengthStretchFactor) {
		if (step != -1.0)
			step *= lengthStretchFactor;
		length *= lengthStretchFactor;
	}
	
	int isHigh() {
		if (step >= 0.0) {
			return (step < (length * 0.5)) ? 1 : 0;
		}
		return (*resetClockOutputsHigh) ? 1 : 0;
	}	
};


//*****************************************************************************


struct TwinParadox : Module {
	
	struct BpmParam : ParamQuantity {
		std::string getDisplayValueString() override {
			return module->inputs[BPM_INPUT].isConnected() ? "Ext." : ParamQuantity::getDisplayValueString();
		}
	};
	
	inline bool calcWarningFlash(long count, long countInit) {
		if ( (count > (countInit * 2l / 4l) && count < (countInit * 3l / 4l)) || (count < (countInit * 1l / 4l)) )
			return false;
		return true;
	}	

	enum ParamIds {
		ENUMS(RATIO_PARAMS, 2), // Duration for Main, Duration for Twin
		BPM_PARAM,
		RESET_PARAM,
		RUN_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		RESET_INPUT,
		RUN_INPUT,
		BPM_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		TWIN1_OUTPUT,
		TWIN2_OUTPUT,
		RESET_OUTPUT,
		RUN_OUTPUT,
		SYNC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		RESET_LIGHT,
		RUN_LIGHT,
		ENUMS(SYNCING_LIGHT, 2),// room for GreenRed
		ENUMS(SYNCINMODE_LIGHT, 2),// room for GreenRed
		NUM_LIGHTS
	};
	
	
	// Constants
	static const int bpmMax = 300;
	static const int bpmMin = 30;
	static constexpr float masterLengthMax = 60.0f / bpmMin;// a length is a period
	static constexpr float masterLengthMin = 60.0f / bpmMax;// a length is a period

	static const unsigned int ON_STOP_INT_RST_MSK = 0x1;
	static const unsigned int ON_START_INT_RST_MSK = 0x2;
	static const unsigned int ON_STOP_EXT_RST_MSK = 0x4;
	static const unsigned int ON_START_EXT_RST_MSK = 0x8;
	
	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	bool running;
	unsigned int resetOnStartStop;// see bit masks ON_STOP_INT_RST_MSK, ON_START_EXT_RST_MSK, etc
	int syncInPpqn;// 0 means output BPM CV instead
	int syncOutPpqn;// 0 means output BPM CV instead
	bool resetClockOutputsHigh;
	bool momentaryRunInput;// true = trigger (original rising edge only version), false = level sensitive (emulated with rising and falling detection)
	float bpmInputScale;// -1.0f to 1.0f
	float bpmInputOffset;// -10.0f to 10.0f

	// No need to save, with reset
	double sampleRate;
	double sampleTime;
	std::vector<Clock> clk;// size 3 (REF, TRAV, SYNC_OUT)
	float bufferedKnobs[2];// must be before ratiosDoubled, master is index 1, ratio knobs are 0 to 0
	float bufferedBpm;
	bool syncRatio;
	double ratioTwin;
	int extPulseNumber;// 0 to syncInPpqn - 1
	double extIntervalTime;// also used for auto mode change to P24 (2nd use of this member variable)
	double timeoutTime;
	float newMasterLength;
	float masterLength;
	float clkOutputs[3];
	
	// No need to save, no reset
	bool scheduledReset = false;
	long cantRunWarning = 0l;// 0 when no warning, positive downward step counter timer when warning
	RefreshCounter refresh;
	float resetLight = 0.0f;
	int ratioTwinMainDur = 0;// only set when ratioTwin is set
	int ratioTwinTwinDur = 0;// only set when ratioTwin is set
	Trigger resetTrigger;
	Trigger runButtonTrigger;
	TriggerRiseFall runInputTrigger;
	Trigger bpmDetectTrigger;
	Trigger displayUpTrigger;
	Trigger displayDownTrigger;
	dsp::PulseGenerator resetPulse;
	dsp::PulseGenerator runPulse;

	int getDurationMain() {
		return (int)(bufferedKnobs[0] + 0.5f);
	}
	int getDurationTwin() {
		return (int)(bufferedKnobs[1] + 0.5f);
	}
	
	double getRatioTwin() {
		ratioTwinMainDur = getDurationMain();
		ratioTwinTwinDur = getDurationTwin();
		
		return (double)ratioTwinTwinDur / (double)ratioTwinMainDur;
	}
	
	
	TwinParadox() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f, "Reset");
		configParam(RUN_PARAM, 0.0f, 1.0f, 0.0f, "Run");
		configParam(RATIO_PARAMS + 0, 1.0f, 8.0f, 1.0f, "Main duration");
		paramQuantities[RATIO_PARAMS + 0]->snapEnabled = true;
		configParam(RATIO_PARAMS + 1, 1.0f, 8.0f, 1.0f, "Twin duration");
		paramQuantities[RATIO_PARAMS + 1]->snapEnabled = true;

		configParam<BpmParam>(BPM_PARAM, (float)(bpmMin), (float)(bpmMax), 120.0f, "Master clock", " BPM");// must be a snap knob, code in step() assumes that a rounded value is read from the knob	(chaining considerations vs BPM detect)
		paramQuantities[BPM_PARAM]->snapEnabled = true;
		
		configInput(RESET_INPUT, "Reset");
		configInput(RUN_INPUT, "Run");
		configInput(BPM_INPUT, "BPM CV / Ext clock");

		configOutput(TWIN1_OUTPUT, "Twin 1 clock");
		configOutput(TWIN2_OUTPUT, "Twin 2 clock");
		configOutput(RESET_OUTPUT, "Reset");
		configOutput(RUN_OUTPUT, "Run");
		configOutput(SYNC_OUTPUT, "Sync clock");
		
		configBypass(RESET_INPUT, RESET_OUTPUT);
		configBypass(RUN_INPUT, RUN_OUTPUT);
		configBypass(BPM_INPUT, SYNC_OUTPUT);

		clk.reserve(3);
		clk.push_back(Clock(nullptr, &resetClockOutputsHigh));
		clk.push_back(Clock(&clk[0], &resetClockOutputsHigh));		
		clk.push_back(Clock(&clk[0], &resetClockOutputsHigh));		

		onReset();
		
		panelTheme = loadDarkAsDefault();
	}
	

	void onReset() override final {
		running = true;
		resetOnStartStop = 0;
		syncInPpqn = 0;// must start in CV mode, or else users won't understand why run won't turn on (since it's automatic in ppqn!=0 mode)
		syncOutPpqn = 48;
		resetClockOutputsHigh = true;
		momentaryRunInput = true;
		bpmInputScale = 1.0f;
		bpmInputOffset = 0.0f;
		resetNonJson(false);
	}
	void resetNonJson(bool delayed) {// delay thread sensitive parts (i.e. schedule them so that process() will do them)
		if (delayed) {
			scheduledReset = true;// will be a soft reset
		}
		else {
			resetTwinParadox(true);// hard reset
		}			
	}
	
	void onRandomize() override {
		resetTwinParadox(false);
	}

	
	void resetTwinParadox(bool hardReset) {// set hardReset to true to revert learned BPM to 120 in sync mode, or else when false, learned bmp will stay persistent
		sampleRate = (double)(APP->engine->getSampleRate());
		sampleTime = 1.0 / sampleRate;
		for (int i = 0; i < 3; i++) {
			clk[i].reset();
			clkOutputs[i] = resetClockOutputsHigh ? 10.0f : 0.0f;
		}
		bufferedKnobs[0] = params[RATIO_PARAMS + 0].getValue();// must be done before the getRatioTwin() a few lines down
		bufferedKnobs[1] = params[RATIO_PARAMS + 1].getValue();// must be done before the getRatioTwin() a few lines down
		bufferedBpm = params[BPM_PARAM].getValue();
		syncRatio = false;
		ratioTwin = getRatioTwin();
		extPulseNumber = -1;
		extIntervalTime = 0.0;// also used for auto mode change to P24 (2nd use of this member variable)
		timeoutTime = 2.0 / syncInPpqn + 0.1;// worst case. This is a double period at 30 BPM (4s), divided by the expected number of edges in the double period 
									   //   which is 2*syncInPpqn, plus epsilon. This timeoutTime is only used for timingout the 2nd clock edge
		if (inputs[BPM_INPUT].isConnected()) {
			if (syncInPpqn != 0) {
				if (hardReset) {
					newMasterLength = 0.5f;// 120 BPM
				}
			}
			else {
				newMasterLength = 0.5f / std::pow(2.0f, inputs[BPM_INPUT].getVoltage());// bpm = 120*2^V, T = 60/bpm = 60/(120*2^V) = 0.5/2^V
			}
		}
		else {
			newMasterLength = 60.0f / bufferedBpm;
		}
		newMasterLength = clamp(newMasterLength, masterLengthMin, masterLengthMax);
		masterLength = newMasterLength;
	}	
	
	
	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		
		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// running
		json_object_set_new(rootJ, "running", json_boolean(running));
		
		// resetOnStartStop
		json_object_set_new(rootJ, "resetOnStartStop", json_integer(resetOnStartStop));
		
		// syncInPpqn
		json_object_set_new(rootJ, "syncInPpqn", json_integer(syncInPpqn));
		
		// syncOutPpqn
		json_object_set_new(rootJ, "syncOutPpqn", json_integer(syncOutPpqn));
		
		// resetClockOutputsHigh
		json_object_set_new(rootJ, "resetClockOutputsHigh", json_boolean(resetClockOutputsHigh));
		
		// momentaryRunInput
		json_object_set_new(rootJ, "momentaryRunInput", json_boolean(momentaryRunInput));
		
		// bpmInputScale
		json_object_set_new(rootJ, "bpmInputScale", json_real(bpmInputScale));

		// bpmInputOffset
		json_object_set_new(rootJ, "bpmInputOffset", json_real(bpmInputOffset));

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

		// resetOnStartStop
		json_t *resetOnStartStopJ = json_object_get(rootJ, "resetOnStartStop");
		if (resetOnStartStopJ) {
			resetOnStartStop = json_integer_value(resetOnStartStopJ);
		}

		// syncInPpqn
		json_t *syncInPpqnJ = json_object_get(rootJ, "syncInPpqn");
		if (syncInPpqnJ)
			syncInPpqn = json_integer_value(syncInPpqnJ);

		// syncOutPpqn
		json_t *syncOutPpqnJ = json_object_get(rootJ, "syncOutPpqn");
		if (syncOutPpqnJ)
			syncOutPpqn = json_integer_value(syncOutPpqnJ);

		// resetClockOutputsHigh
		json_t *resetClockOutputsHighJ = json_object_get(rootJ, "resetClockOutputsHigh");
		if (resetClockOutputsHighJ)
			resetClockOutputsHigh = json_is_true(resetClockOutputsHighJ);

		// momentaryRunInput
		json_t *momentaryRunInputJ = json_object_get(rootJ, "momentaryRunInput");
		if (momentaryRunInputJ)
			momentaryRunInput = json_is_true(momentaryRunInputJ);

		// bpmInputScale
		json_t *bpmInputScaleJ = json_object_get(rootJ, "bpmInputScale");
		if (bpmInputScaleJ)
			bpmInputScale = json_number_value(bpmInputScaleJ);

		// bpmInputOffset
		json_t *bpmInputOffsetJ = json_object_get(rootJ, "bpmInputOffset");
		if (bpmInputOffsetJ)
			bpmInputOffset = json_number_value(bpmInputOffsetJ);

		resetNonJson(true);
	}
	

	void toggleRun(void) {
		if (!((syncInPpqn != 0) && inputs[BPM_INPUT].isConnected()) || running) {// toggle when not BPM detect, turn off only when BPM detect (allows turn off faster than timeout if don't want any trailing beats after stoppage). If allow manually start in bpmDetectionMode   the clock will not know which pulse is the 1st of a syncInPpqn set, so only allow stop
			running = !running;
			runPulse.trigger(0.001f);
			if (running) {
				if (resetOnStartStop & ON_START_INT_RST_MSK) {
					resetTwinParadox(false);
				}
				if (resetOnStartStop & ON_START_EXT_RST_MSK) {
					resetPulse.trigger(0.001f);
					resetLight = 1.0f;
				}
			}
			else {
				if (resetOnStartStop & ON_STOP_INT_RST_MSK) {
					resetTwinParadox(false);
				}
				if (resetOnStartStop & ON_STOP_EXT_RST_MSK) {
					resetPulse.trigger(0.001f);
					resetLight = 1.0f;
				}
			}
		}
		else {
			cantRunWarning = (long) (0.7 * sampleRate / RefreshCounter::displayRefreshStepSkips);
		}
	}

	
	void onSampleRateChange() override {
		resetTwinParadox(false);
	}		
	

	void process(const ProcessArgs &args) override {
		// Scheduled reset
		if (scheduledReset) {
			resetTwinParadox(false);		
			scheduledReset = false;
		}
		
		// Run button
		if (runButtonTrigger.process(params[RUN_PARAM].getValue())) {
			toggleRun();
		}
		// Run input
		if (inputs[RUN_INPUT].isConnected()) {
			int state = runInputTrigger.process(inputs[RUN_INPUT].getVoltage());
			if (state != 0) {
				if (momentaryRunInput) {
					if (state == 1) {
						toggleRun();
					}
				}
				else {
					if ( (running && state == -1) || (!running && state == 1) ) {
						toggleRun();
					}
				}
			}
		}


		// Reset (has to be near top because it sets steps to 0, and 0 not a real step (clock section will move to 1 before reaching outputs)
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage() + params[RESET_PARAM].getValue())) {
			resetLight = 1.0f;
			resetPulse.trigger(0.001f);
			resetTwinParadox(false);	
		}	

		if (refresh.processInputs()) {
			// Detect bpm and ratio knob changes and update bufferedKnobs
			for (int i = 0; i < 2; i++) {
				if (bufferedKnobs[i] != params[RATIO_PARAMS + i].getValue()) {
					bufferedKnobs[i] = params[RATIO_PARAMS + i].getValue();
					syncRatio = true;
				}
			}
			if (bufferedBpm != params[BPM_PARAM].getValue()) {
				bufferedBpm = params[BPM_PARAM].getValue();
			}
		}// userInputs refresh
	
		// BPM input and knob
		newMasterLength = masterLength;
		if (inputs[BPM_INPUT].isConnected()) { 
			bool trigBpmInValue = bpmDetectTrigger.process(inputs[BPM_INPUT].getVoltage());
			
			// BPM Detection method
			if (syncInPpqn != 0) {
				// rising edge detect
				if (trigBpmInValue) {
					if (!running) {
						// this must be the only way to start running when in bpmDetectionMode or else
						//   when manually starting, the clock will not know which pulse is the 1st of a syncInPpqn set
						running = true;
						runPulse.trigger(0.001f);
						resetTwinParadox(false);
						if (resetOnStartStop & ON_START_INT_RST_MSK) {
							// resetTwinParadox(false); implicit above
						}
						if (resetOnStartStop & ON_START_EXT_RST_MSK) {
							resetPulse.trigger(0.001f);
							resetLight = 1.0f;
						}
					}
					if (running) {
						extPulseNumber++;
						if (extPulseNumber >= syncInPpqn)
							extPulseNumber = 0;
						if (extPulseNumber == 0)// if first pulse, start interval timer
							extIntervalTime = 0.0;
						else {
							// all other syncInPpqn pulses except the first one. now we have an interval upon which to plan a stretch 
							double timeLeft = extIntervalTime * (double)(syncInPpqn - extPulseNumber) / ((double)extPulseNumber);
							newMasterLength = clamp(clk[0].getStep() + timeLeft, masterLengthMin / 1.5f, masterLengthMax * 1.5f);// extended range for better sync ability (20-450 BPM)
							timeoutTime = extIntervalTime * ((double)(1 + extPulseNumber) / ((double)extPulseNumber)) + 0.1; // when a second or higher clock edge is received, 
							//  the timeout is the predicted next edge (which is extIntervalTime + extIntervalTime / extPulseNumber) plus epsilon
						}
					}
				}
				if (running) {
					extIntervalTime += sampleTime;
					if (extIntervalTime > timeoutTime) {
						running = false;
						runPulse.trigger(0.001f);
						if (resetOnStartStop & ON_STOP_INT_RST_MSK) {
							resetTwinParadox(false);
						}
						if (resetOnStartStop & ON_STOP_EXT_RST_MSK) {
							resetPulse.trigger(0.001f);
							resetLight = 1.0f;
						}
					}
				}
			}
			// BPM CV method
			else {// bpmDetectionMode not active
				float bpmCV = inputs[BPM_INPUT].getVoltage() * bpmInputScale + bpmInputOffset;
				newMasterLength = clamp(0.5f / std::pow(2.0f, bpmCV), masterLengthMin, masterLengthMax);// bpm = 120*2^V, T = 60/bpm = 60/(120*2^V) = 0.5/2^V
				// no need to round since this clocked's master's BPM knob is a snap knob thus already rounded, and with passthru approach, no cumul error
				
				// detect two quick pulses to automatically change the mode to P24
				//   re-uses same variable as in bpmDetectionMode
				if (extIntervalTime != 0.0) {
					extIntervalTime += sampleTime;
				}
				if (trigBpmInValue) {
					// rising edge
					if (extIntervalTime == 0.0) {
						// this is the first pulse, so start interval timer 
						extIntervalTime = sampleTime;
					}
					else {
						// this is a second pulse
						if (extIntervalTime > ((60.0 / 300.0) / 24.0) && extIntervalTime < ((60.0 / 30.0) / 4.0)) {
							// this is the second of two quick successive pulses, so switch to P24
							extIntervalTime = 0.0;
							syncInPpqn = 24;
						}
						else {
							// too long or too short, so restart and treat this as the first pulse for another check
							extIntervalTime = sampleTime;
						}
					}	
				}			
			}
		}
		else {// BPM_INPUT not active
			newMasterLength = clamp(60.0f / bufferedBpm, masterLengthMin, masterLengthMax);
		}
		if (newMasterLength != masterLength) {
			double lengthStretchFactor = ((double)newMasterLength) / ((double)masterLength);
			for (int i = 0; i < 3; i++) {
				clk[i].applyNewLength(lengthStretchFactor);
			}
			masterLength = newMasterLength;
		}
		
		
		// main clock engine
		if (running) {
			// See if clocks finished their prescribed number of iterations of periods (and syncWait for sub) or 
			//    if they were forced reset and if so, recalc and restart them
			
			// Master clock
			if (clk[0].isReset()) {
				// See if ratio knob changed (or uninitialized)
				if (syncRatio) {
					clk[1].reset();// force reset (thus refresh) of that sub-clock
					clk[2].reset();// force reset (thus refresh) of that sub-clock
					ratioTwin = getRatioTwin();
					syncRatio = false;
				}
				clk[0].setup(masterLength, ratioTwinMainDur, sampleTime);// must call setup before start. length = period
				clk[0].start();
			}
			clkOutputs[0] = clk[0].isHigh() ? 10.0f : 0.0f;		
			
			// Sub clocks
			if (clk[1].isReset()) {
				clk[1].setup(masterLength / ratioTwin, ratioTwinTwinDur, sampleTime);
				clk[1].start();
			}
			clkOutputs[1] = clk[1].isHigh() ? 10.0f : 0.0f;
			
			if (clk[2].isReset()) {
				clk[2].setup(masterLength / ((double)syncOutPpqn), syncOutPpqn * ratioTwinMainDur, sampleTime);
				clk[2].start();
			}
			clkOutputs[2] = clk[2].isHigh() ? 10.0f : 0.0f;


			// Step clocks
			for (int i = 0; i < 3; i++)
				clk[i].stepClock();
		}
		
		// outputs
		outputs[TWIN1_OUTPUT].setVoltage(clkOutputs[0]);
		outputs[TWIN2_OUTPUT].setVoltage(clkOutputs[1]);
		outputs[SYNC_OUTPUT].setVoltage(clkOutputs[2]);
		
		outputs[RESET_OUTPUT].setVoltage((resetPulse.process((float)sampleTime) ? 10.0f : 0.0f));
		outputs[RUN_OUTPUT].setVoltage((runPulse.process((float)sampleTime) ? 10.0f : 0.0f));
			
		
		// lights
		if (refresh.processLights()) {
			// Reset light
			lights[RESET_LIGHT].setSmoothBrightness(resetLight, (float)sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));	
			resetLight = 0.0f;
			
			// Run light
			lights[RUN_LIGHT].setBrightness(running ? 1.0f : 0.0f);
			
			// BPM light
			bool warningFlashState = true;
			if (cantRunWarning > 0l) 
				warningFlashState = calcWarningFlash(cantRunWarning, (long) (0.7 * sampleRate / RefreshCounter::displayRefreshStepSkips));
			lights[SYNCINMODE_LIGHT + 0].setBrightness((syncInPpqn != 0 && warningFlashState) ? 1.0f : 0.0f);
			lights[SYNCINMODE_LIGHT + 1].setBrightness((syncInPpqn != 0 && warningFlashState) ? (float)((syncInPpqn - 2)*(syncInPpqn - 2))/440.0f : 0.0f);			
			
			// ratios synched lights
			if (running && syncRatio) {// red
				lights[SYNCING_LIGHT + 0].setBrightness(0.0f);
				lights[SYNCING_LIGHT + 1].setBrightness(1.0f);
			}
			else {// off
				lights[SYNCING_LIGHT + 0].setBrightness(0.0f);
				lights[SYNCING_LIGHT + 1].setBrightness(0.0f);
			}

			if (cantRunWarning > 0l)
				cantRunWarning--;
			
		}// lightRefreshCounter
	}// process()
};


struct TwinParadoxWidget : ModuleWidget {
	int lastPanelTheme = -1;
	std::shared_ptr<window::Svg> light_svg;
	std::shared_ptr<window::Svg> dark_svg;

	/*struct BpmRatioDisplayWidget : TransparentWidget {
		TwinParadox *module = nullptr;
		std::shared_ptr<Font> font;
		std::string fontPath;
		char displayStr[16] = {};
		const NVGcolor displayColOn = nvgRGB(0xff, 0xff, 0xff);
		
		BpmRatioDisplayWidget() {
			// fontPath = std::string(asset::plugin(pluginInstance, "res/fonts/Segment14.ttf"));
			fontPath = asset::system("res/fonts/DSEG7ClassicMini-BoldItalic.ttf");
		}
		
		void drawLayer(const DrawArgs &args, int layer) override {
			if (layer == 1) {
				if (!(font = APP->window->loadFont(fontPath))) {
					return;
				}
				nvgFontSize(args.vg, 18);
				nvgFontFaceId(args.vg, font->handle);
				//nvgTextLetterSpacing(args.vg, 2.5);
				nvgTextAlign(args.vg, NVG_ALIGN_RIGHT);

				Vec textPos = VecPx(6, 24);
				nvgFillColor(args.vg, nvgTransRGBA(displayColOn, 23));
				nvgText(args.vg, textPos.x, textPos.y, "~~~", NULL);
				
				nvgFillColor(args.vg, displayColOn);
				if (module == NULL) {
					snprintf(displayStr, 4, "120");
				}
				else if (module->editingBpmMode != 0l) {// BPM mode to display
					if (!module->bpmDetectionMode)
						snprintf(displayStr, 4, " CV");
					else
						snprintf(displayStr, 4, "P%2u", (unsigned) module->ppqn);
				}
				else {// BPM to display
					int bpm = (unsigned)((60.0f / module->masterLength) + 0.5f);
					snprintf(displayStr, 4, "%3u", bpm);
				}
				displayStr[3] = 0;// more safety
				nvgText(args.vg, textPos.x, textPos.y, displayStr, NULL);
			}
		}
	};*/		
	
	void appendContextMenu(Menu *menu) override {
		TwinParadox *module = static_cast<TwinParadox*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator());
		
		createPanelThemeMenu(menu, &(module->panelTheme));

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Settings"));
		
		menu->addChild(createSubmenuItem("On Start", "", [=](Menu* menu) {
			menu->addChild(createCheckMenuItem("Do internal reset", "",
				[=]() {return module->resetOnStartStop & TwinParadox::ON_START_INT_RST_MSK;},
				[=]() {module->resetOnStartStop ^= TwinParadox::ON_START_INT_RST_MSK;}
			));
			menu->addChild(createCheckMenuItem("Send reset pulse", "",
				[=]() {return module->resetOnStartStop & TwinParadox::ON_START_EXT_RST_MSK;},
				[=]() {module->resetOnStartStop ^= TwinParadox::ON_START_EXT_RST_MSK;}
			));
		}));	
		
		menu->addChild(createSubmenuItem("On Stop", "", [=](Menu* menu) {
			menu->addChild(createCheckMenuItem("Do internal reset", "",
				[=]() {return module->resetOnStartStop & TwinParadox::ON_STOP_INT_RST_MSK;},
				[=]() {module->resetOnStartStop ^= TwinParadox::ON_STOP_INT_RST_MSK;}
			));
			menu->addChild(createCheckMenuItem("Send reset pulse", "",
				[=]() {return module->resetOnStartStop & TwinParadox::ON_STOP_EXT_RST_MSK;},
				[=]() {module->resetOnStartStop ^= TwinParadox::ON_STOP_EXT_RST_MSK;}
			));
		}));	

		menu->addChild(createCheckMenuItem("Outputs high on reset when not running", "",
			[=]() {return module->resetClockOutputsHigh;},
			[=]() {module->resetClockOutputsHigh = !module->resetClockOutputsHigh;
				   module->resetTwinParadox(true);}
		));
		
		menu->addChild(createBoolMenuItem("Play CV input is level sensitive", "",
			[=]() {return !module->momentaryRunInput;},
			[=](bool loop) {module->momentaryRunInput = !module->momentaryRunInput;}
		));
		
		menu->addChild(createSubmenuItem("Sync input mode", "", [=](Menu* menu) {
			const int ppqns[7] = {0, 2, 4, 8, 12, 16, 24};
			for (int i = 0; i < 7; i++) {
				std::string label = (i == 0 ? "BPM CV" : string::f("%i PPQN",ppqns[i]));
				menu->addChild(createCheckMenuItem(label, "",
					[=]() {return module->syncInPpqn == ppqns[i];},
					[=]() {module->syncInPpqn = ppqns[i]; 
							module->extIntervalTime = 0.0;}// this is for auto mode change to P24
				));
			}
		}));

		menu->addChild(createSubmenuItem("Sync output mode", "", [=](Menu* menu) {
			const int ppqns2[4] = {1, 12, 24, 48};
			for (int i = 0; i < 4; i++) {
				std::string label = (string::f("×%i",ppqns2[i]));
				menu->addChild(createCheckMenuItem(label, "",
					[=]() {return module->syncOutPpqn == ppqns2[i];},
					[=]() {module->syncOutPpqn = ppqns2[i];}
				));
			}
		}));

		//createBPMCVInputMenu(menu, &module->bpmInputScale, &module->bpmInputOffset);
	}
	


	TwinParadoxWidget(TwinParadox *module) {
		setModule(module);

		// Main panels from Inkscape
		light_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/TwinParadox-WL.svg"));
		dark_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/TwinParadox-WL.svg"));
		int panelTheme = isDark(module ? (&((static_cast<TwinParadox*>(module))->panelTheme)) : NULL) ? 1 : 0;// need this here since step() not called for module browser
		setPanel(panelTheme == 0 ? light_svg : dark_svg);		
		
		// Screws 
		// part of svg panel, no code required


		static const int colL = 30;
		static const int colC = 75;
		static const int colR = 120;
		
		static const int row0 = 58;// reset, run, bpm inputs
		static const int row1 = 95;// reset and run switches, bpm knob
		static const int row2 = 148;// bpm display, display index lights, master clk out
		// static const int row3 = 198;// display and mode buttons
		static const int row4 = 227;// sub clock ratio knobs
		// static const int row5 = 281;// sub clock outs
		static const int row6 = 328;// reset, run, bpm outputs


		// Row 0
		// Reset input
		addInput(createDynamicPort<GeoPort>(VecPx(colL, row0), true, module, TwinParadox::RESET_INPUT, module ? &module->panelTheme : NULL));
		// Run input
		addInput(createDynamicPort<GeoPort>(VecPx(colC, row0), true, module, TwinParadox::RUN_INPUT, module ? &module->panelTheme : NULL));
		// Bpm input
		addInput(createDynamicPort<GeoPort>(VecPx(colR, row0), true, module, TwinParadox::BPM_INPUT, module ? &module->panelTheme : NULL));
		// BPM mode light
		addChild(createLightCentered<SmallLight<GreenRedLight>>(VecPx(colR, row0 - 15.0f), module, TwinParadox::SYNCINMODE_LIGHT));		
		

		// Row 1
		// Reset LED bezel and light
		addParam(createParamCentered<LEDBezel>(VecPx(colL, row1), module, TwinParadox::RESET_PARAM));
		addChild(createLightCentered<LEDBezelLight<GeoWhiteLight>>(VecPx(colL, row1), module, TwinParadox::RESET_LIGHT));
		// Run LED bezel and light
		addParam(createParamCentered<LEDBezel>(VecPx(colC, row1), module, TwinParadox::RUN_PARAM));
		addChild(createLightCentered<LEDBezelLight<GeoWhiteLight>>(VecPx(colC, row1), module, TwinParadox::RUN_LIGHT));
		// Master BPM knob
		addParam(createDynamicParam<GeoKnob>(VecPx(colR, row1), module, TwinParadox::BPM_PARAM, module ? &module->panelTheme : NULL));


		// Row 2
		// Clock master out
		addOutput(createDynamicPort<GeoPort>(VecPx(colL, row2), false, module, TwinParadox::TWIN1_OUTPUT, module ? &module->panelTheme : NULL));
		addOutput(createDynamicPort<GeoPort>(VecPx(colL, row2 + 28.0f), false, module, TwinParadox::TWIN2_OUTPUT, module ? &module->panelTheme : NULL));	

		// Display index lights
		static const int delY = 10;
		addChild(createLightCentered<SmallLight<GreenRedLight>>(VecPx(colC - 10.5f, row2  -2 * delY - 4 ), module, TwinParadox::SYNCING_LIGHT + 0 * 2));		
		// BPM display
		/*BpmRatioDisplayWidget *bpmRatioDisplay = new BpmRatioDisplayWidget();
		bpmRatioDisplay->box.size = VecPx(55, 30);// 3 characters
		bpmRatioDisplay->box.pos = VecPx((colR + colC) / 2.0f + 9, row2).minus(bpmRatioDisplay->box.size.div(2));
		bpmRatioDisplay->module = module;
		addChild(bpmRatioDisplay);*/
		

		// Row 3
		// static const int bspaceX = 24;
		// BPM mode buttons
		// addParam(createDynamicParam<GeoPushButton>(VecPx(colR - bspaceX + 4, row3), module, TwinParadox::BPMMODE_DOWN_PARAM, module ? &module->panelTheme : NULL));
		// addParam(createDynamicParam<GeoPushButton>(VecPx(colR + 4, row3), module, TwinParadox::BPMMODE_UP_PARAM, module ? &module->panelTheme : NULL));

		
		// Row 4 		
		// Ratio knobs
		addParam(createDynamicParam<GeoKnob>(VecPx(colL, row4), module, TwinParadox::RATIO_PARAMS + 0, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colC, row4), module, TwinParadox::RATIO_PARAMS + 1, module ? &module->panelTheme : NULL));


		// Row 5
		// Sub-clock outputs


		// Row 6 (last row)
		// Reset out
		addOutput(createDynamicPort<GeoPort>(VecPx(colL, row6), false, module, TwinParadox::RESET_OUTPUT, module ? &module->panelTheme : NULL));
		// Run out
		addOutput(createDynamicPort<GeoPort>(VecPx(colC, row6), false, module, TwinParadox::RUN_OUTPUT, module ? &module->panelTheme : NULL));
		// Sync out
		addOutput(createDynamicPort<GeoPort>(VecPx(colR, row6), false, module, TwinParadox::SYNC_OUTPUT, module ? &module->panelTheme : NULL));

	}
	
	
	void step() override {
		int panelTheme = isDark(module ? (&((static_cast<TwinParadox*>(module))->panelTheme)) : NULL) ? 1 : 0;
		if (lastPanelTheme != panelTheme) {
			lastPanelTheme = panelTheme;
			SvgPanel* panel = static_cast<SvgPanel*>(getPanel());
			panel->setBackground(panelTheme == 0 ? light_svg : dark_svg);
			panel->fb->dirty = true;
		}
		Widget::step();
	}


	void onHoverKey(const event::HoverKey& e) override {
		if (e.action == GLFW_PRESS) {
			if ( e.key == GLFW_KEY_SPACE && ((e.mods & RACK_MOD_MASK) == 0) ) {
				TwinParadox *module = static_cast<TwinParadox*>(this->module);
				module->toggleRun();
				e.consume(this);
				return;
			}
		}
		ModuleWidget::onHoverKey(e); 
	}
};

Model *modelTwinParadox = createModel<TwinParadox, TwinParadoxWidget>("Twin-Paradox");
