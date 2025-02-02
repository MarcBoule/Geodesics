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
	int iterationsOrig = 0;// given (original) number of iterations mandated, set by setup()
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
	int getIterations() {
		return iterations;
	}
	int getIterationsOrig() {
		return iterationsOrig;
	}
	void start() {
		step = remainder;
	}
	
	void setup(double lengthGiven, int iterationsGiven, double sampleTimeGiven) {
		length = lengthGiven;
		iterations = iterationsGiven;
		iterationsOrig = iterationsGiven;
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
	
	enum ParamIds {
		DURREF_PARAM,
		DURTRAV_PARAM,
		BPM_PARAM,
		RESET_PARAM,
		RUN_PARAM,
		TRAVPROB_PARAM,
		SWAPPROB_PARAM,
		TRAVEL_PARAM,
		DIVMULT_PARAM,
		MULTITIME_PARAM,
		TAP_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		RESET_INPUT,
		RUN_INPUT,
		BPM_INPUT,
		TRAVEL_INPUT,
		TRAVPROB_INPUT,
		SWAPPROB_INPUT,
		DURREF_INPUT,
		DURTRAV_INPUT,
		MULTITIME_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		TWIN1_OUTPUT,
		TWIN2_OUTPUT,
		RESET_OUTPUT,
		RUN_OUTPUT,
		SYNC_OUTPUT,
		MEET_OUTPUT,
		MULTITIME_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		RESET_LIGHT,
		RUN_LIGHT,
		ENUMS(SYNCINMODE_LIGHT, 2),// room for GreenRed
		ENUMS(DURREF_LIGHTS, 8 * 3),// room for GeoBlueYellowWhiteLight
		ENUMS(DURTRAV_LIGHTS, 8 * 3),// room for GeoBlueYellowWhiteLight
		TRAVEL_LIGHT,
		NUM_LIGHTS
	};
	
	
	// Constants
	static const int bpmMax = 300;
	static const int bpmMin = 30;
	static constexpr float masterLengthMax = 60.0f / bpmMin;// a length is a period
	static constexpr float masterLengthMin = 60.0f / bpmMax;// a length is a period
	static constexpr float multitimeGuard = 1e-4;// 100us

	static const unsigned int ON_STOP_INT_RST_MSK = 0x1;
	static const unsigned int ON_START_INT_RST_MSK = 0x2;
	static const unsigned int ON_STOP_EXT_RST_MSK = 0x4;
	static const unsigned int ON_START_EXT_RST_MSK = 0x8;
	
	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	bool running;
	unsigned int resetOnStartStop;// see bit masks ON_STOP_INT_RST_MSK, ON_START_EXT_RST_MSK, etc
	int bpmManual;// master bpm from inf knob or tap tempo
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
	int extPulseNumber;// 0 to syncInPpqn - 1
	double extIntervalTime;// also used for auto mode change to P24 (2nd use of this member variable)
	double timeoutTime;
	float newMasterLength;
	float masterLength;
	float clkOutputs[3];
	bool swap;// when false, twin1=ref & twin2=trav; when true, twin1=trav & twin2=ref
	bool pendingTravelReq;
	bool traveling;
	int multitimeSwitch;// -1 = ref, 1 = trav, 0 = none
	dsp::PulseGenerator multitimeGuardPulse;
	
	// No need to save, no reset
	bool scheduledReset = false;
	long cantRunWarning = 0l;// 0 when no warning, positive downward step counter timer when warning
	RefreshCounter refresh;
	float resetLight = 0.0f;
	int bpmKnob = 0;
	Trigger resetTrigger;
	Trigger runButtonTrigger;
	TriggerRiseFall runInputTrigger;
	Trigger bpmDetectTrigger;
	Trigger travelTrigger;
	dsp::PulseGenerator resetPulse;
	dsp::PulseGenerator runPulse;
	dsp::PulseGenerator meetPulse;
	TriggerRiseFall multitime1Trigger;
	TriggerRiseFall multitime2Trigger;



	struct BpmParamQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			return module->inputs[BPM_INPUT].isConnected() ? "Ext." : string::f("%d",((TwinParadox*)module)->bpmManual);
		}
	};
	
	struct DivMultParamQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			int val = std::round(getValue());
			if (val < 0) {
				return string::f( "÷%c", '0' + (0x1 << -val) );
			}
			return     string::f( "×%c", '0' + (0x1 <<  val) );
		}
	};


	bool calcWarningFlash(long count, long countInit) {
		if ( (count > (countInit * 2l / 4l) && count < (countInit * 3l / 4l)) || (count < (countInit * 1l / 4l)) )
			return false;
		return true;
	}	
	int getDurationRef() {
		float durValue = params[DURREF_PARAM].getValue();
		durValue += inputs[DURREF_INPUT].getVoltage() / 10.0f * (8.0f - 1.0f);
		return (int)(clamp(durValue, 1.0f, 8.0f) + 0.5f);
	}
	int getDurationTrav() {
		float durValue = params[DURTRAV_PARAM].getValue();
		durValue += inputs[DURTRAV_INPUT].getVoltage() / 10.0f * (8.0f - 1.0f);
		return (int)(clamp(durValue, 1.0f, 8.0f) + 0.5f);	}
	
	double getRatioTrav(int* durationRef, int* durationTrav) {
		*durationRef = getDurationRef();
		*durationTrav = getDurationTrav();
		return (double)*durationTrav / (double)*durationRef;
	}
	
	bool evalTravel() {
		float travelValue = params[TRAVPROB_PARAM].getValue();
		travelValue += inputs[TRAVPROB_INPUT].getVoltage() / 10.0f;
		return (random::uniform() < travelValue); // random::uniform is [0.0, 1.0), see include/util/common.hpp
	}
	bool evalSwap() {
		float swapValue = params[SWAPPROB_PARAM].getValue();
		swapValue += inputs[SWAPPROB_INPUT].getVoltage() / 10.0f;
		return (random::uniform() < swapValue); // random::uniform is [0.0, 1.0), see include/util/common.hpp
	}
	int getDivMultKnob() {
		// this is inverted ratio, so that the period time can be multiplied with return value
		return (int)std::round(params[DIVMULT_PARAM].getValue());
	}	
	double getDivMult() {
		// this is inverted ratio, so that the period time can be multiplied with return value
		int val = getDivMultKnob();
		if (val < 0) {
			return (double)(0x1 << -val);
		}
		return 1.0 / (double)(0x1 << val);
	}
	float getMultitimeValueWithCV() {
		float val = params[MULTITIME_PARAM].getValue();
		val += inputs[MULTITIME_INPUT].getVoltage() / 5.0f;
		return clamp(val, -2.0f, 2.0f);
	}
	float probMultitime(bool isRef) {
		// knob is [-2.0, +2.0]
		float knob = getMultitimeValueWithCV();
		if (isRef) {
			if (knob <= -1.0f) return knob + 2.0f;
			if (knob <= 0.0f) return 1.0f;
			if (knob <= 1.0f) return 1.0f - knob;
			return 0.0f;
		}
		else {
			if (knob <= -1.0f) return 0.0f;
			if (knob <= 0.0f) return knob + 1.0f;
			if (knob <= 1.0f) return 1.0f;
			return 2.0f - knob;
		}
	}

	
	TwinParadox() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(DURREF_PARAM, 1.0f, 8.0f, 1.0f, "Reference time");
		paramQuantities[DURREF_PARAM]->snapEnabled = true;
		configParam(DURTRAV_PARAM, 1.0f, 8.0f, 1.0f, "Travel time");
		paramQuantities[DURTRAV_PARAM]->snapEnabled = true;
		//configParam<BpmParamQuantity>(BPM_PARAM, (float)(bpmMin), (float)(bpmMax), 120.0f, "Tempo", " BPM");// must be a snap knob, code in step() assumes that a rounded value is read from the knob	(chaining considerations vs BPM detect)
		//paramQuantities[BPM_PARAM]->snapEnabled = true;
		configParam<BpmParamQuantity>(BPM_PARAM, -INFINITY, INFINITY, 0.0f, "Tempo"," BPM");	
		configButton(RESET_PARAM, "Reset");
		configButton(RUN_PARAM, "Run");
		configParam(TRAVPROB_PARAM, 0.0f, 1.0f, 0.0f, "Probability to travel");
		configParam(SWAPPROB_PARAM, 0.0f, 1.0f, 0.0f, "Traveler selection probability");
		configButton(TRAVEL_PARAM, "Travel");
		configParam<DivMultParamQuantity>(DIVMULT_PARAM, -3.0f, 3.0f, 0.0f, "Div/Mult");
		paramQuantities[DIVMULT_PARAM]->snapEnabled = true;
		configParam(MULTITIME_PARAM, -2.0f, 2.0f, 0.0f, "Multitime");
		configButton(TAP_PARAM, "Tap tempo");
		
		configInput(RESET_INPUT, "Reset");
		configInput(RUN_INPUT, "Run");
		configInput(BPM_INPUT, "BPM CV / Ext clock");
		configInput(TRAVEL_INPUT, "Travel");
		configInput(TRAVPROB_INPUT, "Travel probability CV");
		configInput(SWAPPROB_INPUT, "Traveler selection probability CV");
		configInput(DURREF_INPUT, "Reference time CV");
		configInput(DURTRAV_INPUT, "Travel time CV");
		configInput(MULTITIME_INPUT, "Multitime CV");

		configOutput(TWIN1_OUTPUT, "Twin 1 clock");
		configOutput(TWIN2_OUTPUT, "Twin 2 clock");
		configOutput(RESET_OUTPUT, "Reset");
		configOutput(RUN_OUTPUT, "Run");
		configOutput(SYNC_OUTPUT, "Sync clock");
		configOutput(MEET_OUTPUT, "Meet");
		configOutput(MULTITIME_OUTPUT, "Multitime");
		
		configBypass(RESET_INPUT, RESET_OUTPUT);
		configBypass(RUN_INPUT, RUN_OUTPUT);
		configBypass(BPM_INPUT, SYNC_OUTPUT);

		clk.reserve(3);
		clk.push_back(Clock(nullptr, &resetClockOutputsHigh));// Ref clock
		clk.push_back(Clock(&clk[0], &resetClockOutputsHigh));// Traveler clock	
		clk.push_back(Clock(&clk[0], &resetClockOutputsHigh));// Sync out clock		

		onReset();
		
		panelTheme = loadDarkAsDefault();
	}
	

	void onReset() override final {
		running = true;
		resetOnStartStop = 0;
		bpmManual = 120;
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
		extPulseNumber = -1;
		extIntervalTime = 0.0;// also used for auto mode change to P24 (2nd use of this member variable)
		timeoutTime = 2.0 / syncInPpqn + 0.1;// worst case. This is a double period at 30 BPM (4s), divided by the expected number of edges in the double period 
									   //   which is 2*syncInPpqn, plus epsilon. This timeoutTime is only used for timingout the 2nd clock edge
		if (inputs[BPM_INPUT].isConnected()) {
			if (syncInPpqn != 0) {
				if (hardReset) {
					newMasterLength = 0.5f;// 120 BPM
					newMasterLength *= getDivMult();
				}
			}
			else {
				newMasterLength = 0.5f / std::pow(2.0f, inputs[BPM_INPUT].getVoltage());// bpm = 120*2^V, T = 60/bpm = 60/(120*2^V) = 0.5/2^V
				newMasterLength *= getDivMult();
			}
		}
		else {
			newMasterLength = 60.0f / (float)bpmManual;//params[BPM_PARAM].getValue();
			newMasterLength *= getDivMult();
		}
		newMasterLength = clamp(newMasterLength, masterLengthMin, masterLengthMax);
		masterLength = newMasterLength;
		swap = false;
		pendingTravelReq = false;
		traveling = false;
		multitimeSwitch = 0;
		multitimeGuardPulse.reset();
	}	
	
	
	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		
		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// running
		json_object_set_new(rootJ, "running", json_boolean(running));
		
		// resetOnStartStop
		json_object_set_new(rootJ, "resetOnStartStop", json_integer(resetOnStartStop));
		
		// bpmManual
		json_object_set_new(rootJ, "bpmManual", json_integer(bpmManual));
		
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

		// bpmManual
		json_t *bpmManualJ = json_object_get(rootJ, "bpmManual");
		if (bpmManualJ)
			bpmManual = json_integer_value(bpmManualJ);

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

	
	void multitimeSimultaneous() {
		bool p1 = random::uniform() < probMultitime(true);// true = REF
		bool p2 = random::uniform() < probMultitime(false);// false = TRAV
		if (p1 && p2) {
			// choose one random
			multitimeSwitch = (random::u32() % 2 == 0) ? -1 : 1;
		}
		else if (p1) {
			multitimeSwitch = -1;
		}
		else if (p2) {
			multitimeSwitch = 1;
		}
		else {
			multitimeSwitch = 0;
			multitimeGuardPulse.trigger(multitimeGuard);
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
			// bpm knob
			float bpmParamValue = params[BPM_PARAM].getValue();
			int newBpmKnob = (int)std::round(bpmParamValue * 30.0f);
			if (bpmParamValue == 0.0f)// true when constructor or dataFromJson() occured
				bpmKnob = newBpmKnob;
			int deltaBpmKnob = newBpmKnob - bpmKnob;
			if (deltaBpmKnob != 0) {
				if (abs(deltaBpmKnob) <= 3) {// avoid discontinuous 
					bpmManual=clamp(bpmManual+deltaBpmKnob, bpmMin, bpmMax);
				}
				bpmKnob = newBpmKnob;
			}	
			
			// travel button
			if (travelTrigger.process(inputs[TRAVEL_INPUT].getVoltage() + params[TRAVEL_PARAM].getValue())) {
				pendingTravelReq = true;
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
						int divMultInt = getDivMultKnob();
						
						int syncInPpqnMultDiv = divMultInt >= 0 ? 
													syncInPpqn / (0x1 << divMultInt) :
													syncInPpqn * (0x1 << -divMultInt);
						
						extPulseNumber++;
						if (extPulseNumber >= syncInPpqnMultDiv)
							extPulseNumber = 0;
						if (extPulseNumber == 0)// if first pulse, start interval timer
							extIntervalTime = 0.0;
						else {
							// all other syncInPpqnMultDiv pulses except the first one. now we have an interval upon which to plan a stretch 
							double timeLeft = extIntervalTime * (double)(syncInPpqnMultDiv - extPulseNumber) / ((double)extPulseNumber);
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
				newMasterLength *= getDivMult();
				
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
			newMasterLength = clamp(60.0f / /*params[BPM_PARAM].getValue()*/(float)bpmManual, masterLengthMin, masterLengthMax);
			newMasterLength *= getDivMult();
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
				clk[1].reset();// force reset (thus refresh) of that sub-clock
				clk[2].reset();// force reset (thus refresh) of that sub-clock
				if (traveling) {
					meetPulse.trigger(0.001f);
				}
				
				swap = evalSwap();
				
				int durRef;
				int durTrav;
				double ratioTrav = getRatioTrav(&durRef, &durTrav);
				if (pendingTravelReq || evalTravel()) {
					pendingTravelReq = false;
					traveling = true;
				}
				else {
					durTrav = durRef;
					ratioTrav = 1.0;
					traveling = false;
				}
				
				// must call setups before starts
				clk[0].setup(masterLength, durRef, sampleTime);
				clk[1].setup(masterLength / ratioTrav, durTrav, sampleTime);
				clk[2].setup(masterLength / ((double)syncOutPpqn), syncOutPpqn * durRef, sampleTime);
				clk[0].start();
				clk[1].start();
				clk[2].start();
			}// if (clk[0].isReset())

			// see further below for clk[i].stepClock();
		}
		
		// outputs
		outputs[TWIN1_OUTPUT].setVoltage(clkOutputs[swap ? 1 : 0]);
		outputs[TWIN2_OUTPUT].setVoltage(clkOutputs[swap ? 0 : 1]);
		outputs[SYNC_OUTPUT].setVoltage(clkOutputs[2]);
		outputs[MEET_OUTPUT].setVoltage(meetPulse.process((float)sampleTime) ? 10.0f : 0.0f);
		
		outputs[RESET_OUTPUT].setVoltage(resetPulse.process((float)sampleTime) ? 10.0f : 0.0f);
		outputs[RUN_OUTPUT].setVoltage(runPulse.process((float)sampleTime) ? 10.0f : 0.0f);
		
		// multitime
		int trigMt1 = multitime1Trigger.process(clk[0].isHigh() ? 10.0f : 0.0f);
		int trigMt2 = multitime2Trigger.process(clk[1].isHigh() ? 10.0f : 0.0f);
		if (outputs[MULTITIME_OUTPUT].isConnected()) {
			if ((trigMt1 == -1 && multitimeSwitch == -1) || (trigMt2 == -1 && multitimeSwitch == 1)) {
				multitimeGuardPulse.trigger(multitimeGuard);
				multitimeSwitch = 0;
			}
			
			if (trigMt1 == 1 && running && multitimeSwitch == 0 && multitimeGuardPulse.remaining <= 0.0f) {
				int durThis = clk[0].getIterationsOrig();
				int durOther = clk[1].getIterationsOrig();
				int itThis = durThis - clk[0].getIterations();
				
				if (itThis * durOther % durThis == 0) {
					multitimeSimultaneous();
				}
				else {
					// not simultaneous
					bool p1 = random::uniform() < probMultitime(true);// true = REF
					if (p1) {
						multitimeSwitch = -1;
					}
					else {
						multitimeSwitch = 0;
						multitimeGuardPulse.trigger(multitimeGuard);
					}				
				}
			}

			if (trigMt2 == 1 && running && multitimeSwitch == 0 && multitimeGuardPulse.remaining <= 0.0f) {
				int durThis = clk[1].getIterationsOrig();
				int durOther = clk[0].getIterationsOrig();
				int itThis = durThis - clk[1].getIterations();
				
				if (itThis * durOther % durThis == 0) {
					multitimeSimultaneous();
				}
				else {
					// not simultaneous
					bool p2 = random::uniform() < probMultitime(false);// false = TRAV
					if (p2) {
						multitimeSwitch = 1;
					}
					else {
						multitimeSwitch = 0;
						multitimeGuardPulse.trigger(multitimeGuard);
					}
				}
			}			
			
			float mOut = 0.0f;
			if (multitimeSwitch == -1 && running) {
				mOut = clk[0].isHigh() ? 10.0f : 0.0f;
			}
			if (multitimeSwitch == 1 && running) {
				mOut = clk[1].isHigh() ? 10.0f : 0.0f;
			}
			outputs[MULTITIME_OUTPUT].setVoltage(mOut);
		}
		else {
			outputs[MULTITIME_OUTPUT].setVoltage(0.0f);
		}
		
		multitimeGuardPulse.process(args.sampleTime);
		if (running) {
			// Step clocks and update clkOutputs[]
			for (int i = 0; i < 3; i++) {
				clkOutputs[i] = clk[i].isHigh() ? 10.0f : 0.0f;		
				clk[i].stepClock();
			}
		}
		
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
			
			if (cantRunWarning > 0l)
				cantRunWarning--;
			
		}// lightRefreshCounter
	}// process()
};


struct TwinParadoxWidget : ModuleWidget {
	int lastPanelTheme = -1;
	std::shared_ptr<window::Svg> light_svg;
	std::shared_ptr<window::Svg> dark_svg;	
	
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
			const int numPpqns = 3;
			const int ppqns[numPpqns] = {0, 24, 48};
			for (int i = 0; i < numPpqns; i++) {
				std::string label = (i == 0 ? "BPM CV" : string::f("%i PPQN",ppqns[i]));
				menu->addChild(createCheckMenuItem(label, "",
					[=]() {return module->syncInPpqn == ppqns[i];},
					[=]() {module->syncInPpqn = ppqns[i]; 
							module->extIntervalTime = 0.0;}// this is for auto mode change to P24
				));
			}
		}));

		menu->addChild(createSubmenuItem("Sync output multiplier", "", [=](Menu* menu) {
			const int numMults = 3;
			const int mults[numMults] = {1, 24, 48};
			for (int i = 0; i < numMults; i++) {
				std::string label = (string::f("×%i",mults[i]));
				menu->addChild(createCheckMenuItem(label, "",
					[=]() {return module->syncOutPpqn == mults[i];},
					[=]() {module->syncOutPpqn = mults[i];}
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
		static const int colR2 = 165;
		
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
		addChild(createLightCentered<SmallLight<GreenRedLight>>(VecPx(colR, row0 - 18.0f), module, TwinParadox::SYNCINMODE_LIGHT));		
		

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

		// 	
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
		addParam(createDynamicParam<GeoPushButton>(VecPx(colC, row2), module, TwinParadox::TRAVEL_PARAM, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colC, row2 - 18.0f), module, TwinParadox::TRAVEL_LIGHT));	
		addInput(createDynamicPort<GeoPort>(VecPx(colC, row2 + 28.0f), true, module, TwinParadox::TRAVEL_INPUT, module ? &module->panelTheme : NULL));
		
		addParam(createDynamicParam<GeoKnob>(VecPx(colR, row2), module, TwinParadox::DIVMULT_PARAM, module ? &module->panelTheme : NULL));
		
		
		
		
		// Row 4 		
		// Ratio knobs
		addParam(createDynamicParam<GeoKnob>(VecPx(colL, row4), module, TwinParadox::DURREF_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colL, row4 + 28.0f), true, module, TwinParadox::DURREF_INPUT, module ? &module->panelTheme : NULL));
		
		addParam(createDynamicParam<GeoKnob>(VecPx(colC, row4), module, TwinParadox::DURTRAV_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colC, row4 + 28.0f), true, module, TwinParadox::DURTRAV_INPUT, module ? &module->panelTheme : NULL));
		
		addParam(createDynamicParam<GeoKnob>(VecPx(colR, row4), module, TwinParadox::TRAVPROB_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colR, row4 + 28.0f), true, module, TwinParadox::TRAVPROB_INPUT, module ? &module->panelTheme : NULL));
		
		addParam(createDynamicParam<GeoKnob>(VecPx(colR2, row4), module, TwinParadox::SWAPPROB_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colR2, row4 + 28.0f), true, module, TwinParadox::SWAPPROB_INPUT, module ? &module->panelTheme : NULL));


		// Row 5
		// Sub-clock outputs


		// Row 6 (last row)
		// Reset out
		addOutput(createDynamicPort<GeoPort>(VecPx(colL, row6), false, module, TwinParadox::RESET_OUTPUT, module ? &module->panelTheme : NULL));
		// Run out
		addOutput(createDynamicPort<GeoPort>(VecPx(colC, row6), false, module, TwinParadox::RUN_OUTPUT, module ? &module->panelTheme : NULL));
		// Sync out
		addOutput(createDynamicPort<GeoPort>(VecPx(colR, row6), false, module, TwinParadox::SYNC_OUTPUT, module ? &module->panelTheme : NULL));
		// Meet out
		addOutput(createDynamicPort<GeoPort>(VecPx(colR2, row6), false, module, TwinParadox::MEET_OUTPUT, module ? &module->panelTheme : NULL));
		
		
		for (int i = 0; i < 8; i++) {
			addChild(createLightCentered<SmallLight<GeoBlueYellowWhiteLight>>(VecPx(200, 50 + 15 * i), module, TwinParadox::DURREF_LIGHTS + i * 3));
			addChild(createLightCentered<SmallLight<GeoBlueYellowWhiteLight>>(VecPx(240, 50 + 15 * i), module, TwinParadox::DURTRAV_LIGHTS + i * 3));
		}
		
		// Multitime
		static const int colM = 220;
		addOutput(createDynamicPort<GeoPort>(VecPx(colM, row4 - 30), false, module, TwinParadox::MULTITIME_OUTPUT, module ? &module->panelTheme : NULL));
		addParam(createDynamicParam<GeoKnob>(VecPx(colM, row4), module, TwinParadox::MULTITIME_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colM, row4 + 28.0f), true, module, TwinParadox::MULTITIME_INPUT, module ? &module->panelTheme : NULL));
		


	}
	
	
	void step() override {
		int panelTheme = isDark(module ? (&((static_cast<TwinParadox*>(module))->panelTheme)) : NULL) ? 1 : 0;
		if (lastPanelTheme != panelTheme) {
			lastPanelTheme = panelTheme;
			SvgPanel* panel = static_cast<SvgPanel*>(getPanel());
			panel->setBackground(panelTheme == 0 ? light_svg : dark_svg);
			panel->fb->dirty = true;
		}
		
		if (module) {
			TwinParadox* m = static_cast<TwinParadox*>(module);
			
			// gate lights done in process() since they look at output[], and when connecting/disconnecting cables the cable sizes are reset (and buffers cleared), which makes the gate lights flicker
			
			m->lights[TwinParadox::TRAVEL_LIGHT].setBrightness(m->pendingTravelReq ? 1.0f : 0.0f);
			
			int durRef = m->getDurationRef();
			int durTrav = m->getDurationTrav();
			int itRef = m->clk[0].getIterationsOrig() - std::max(1, m->clk[0].getIterations());
			int itTrav = m->clk[1].getIterationsOrig() - std::max(1, m->clk[1].getIterations());
			bool traveling = m->traveling; 
			bool swap = m->swap;
			// duration REF lights
			for (int i = 0; i < 8; i++) {
				float light = 0.0f;
				if ((i <= itRef && m->running) || (i < durRef && !m->running)) {
					light = 1.0f;
				}
				else if (i < durRef) {
					light = 0.3f;
				}
				bool wantWhite = (light < 0.5f || !traveling);
				float blueL =   !wantWhite && !swap ? light : 0.0f;// twin 1
				float yellowL = !wantWhite && swap  ? light : 0.0f;// twin 2
				float whiteL =   wantWhite          ? light : 0.0f;
				m->lights[TwinParadox::DURREF_LIGHTS + i * 3 + 0].setBrightness(blueL);
				m->lights[TwinParadox::DURREF_LIGHTS + i * 3 + 1].setBrightness(yellowL);
				m->lights[TwinParadox::DURREF_LIGHTS + i * 3 + 2].setBrightness(whiteL);
			}
			// duration TRAV lights
			for (int i = 0; i < 8; i++) {
				float light = 0.0f;
				if (traveling && ((i <= itTrav && m->running) || (i < durTrav && !m->running))) {
					light = 1.0f;
				}
				else if (i < durTrav) {
					light = 0.3f;
				}
				bool wantWhite = (light < 0.5f || !traveling);
				float blueL =   !wantWhite && swap  ? light : 0.0f;// twin 2
				float yellowL = !wantWhite && !swap ? light : 0.0f;// twin 1
				float whiteL =   wantWhite          ? light : 0.0f;
				m->lights[TwinParadox::DURTRAV_LIGHTS + i * 3 + 0].setBrightness(blueL);
				m->lights[TwinParadox::DURTRAV_LIGHTS + i * 3 + 1].setBrightness(yellowL);
				m->lights[TwinParadox::DURTRAV_LIGHTS + i * 3 + 2].setBrightness(whiteL);
			}
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
