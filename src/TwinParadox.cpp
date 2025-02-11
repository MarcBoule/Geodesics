//***********************************************************************************************
//Relativistic time shifting clock module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental and Audible Instruments plugins by Andrew Belt and graphics  
//  from the Component Library. 
//See ./LICENSE.md for all licenses
//See ./res/fonts/ for font licenses
//***********************************************************************************************


#include "TwinParadoxCommon.hpp"


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
	float *pulseWidth = nullptr;
	
	public:
	
	Clock(Clock* clkGiven, bool *resetClockOutputsHighPtr, float* pulseWidthPtr) {
		syncSrc = clkGiven;
		resetClockOutputsHigh = resetClockOutputsHighPtr;
		pulseWidth = pulseWidthPtr;
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
		int high = 0;
		if (step >= 0.0) {
			// all following values are in seconds
			float onems = 0.001f;
			float period = (float)length;
			float p2min = onems;
			float p2max = period - onems;
			if (p2max < p2min) {
				p2max = p2min;
			}
			
			//double p1 = 0.0;// implicit, no need 
			float pw = ( (pulseWidth != nullptr) ? (*pulseWidth) : 0.5f );
			double p2 = (double)((p2max - p2min) * pw + p2min);// pulseWidth is [0 : 1]

			if (step <= p2)
				high = 1;
		}
		else if (*resetClockOutputsHigh)
			high = 1;
		return high;	
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
		TAP_PARAM,
		SYNCINMODE_PARAM,
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
		NUM_INPUTS
	};
	enum OutputIds {
		TWIN1_OUTPUT,
		TWIN2_OUTPUT,
		RESET_OUTPUT,
		RUN_OUTPUT,
		MEET_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		RESET_LIGHT,
		RUN_LIGHT,
		SYNCINMODE_LIGHT,
		ENUMS(DURREF_LIGHTS, 8 * 3),// room for GeoBlueYellowWhiteLight
		ENUMS(DURTRAV_LIGHTS, 8 * 3),// room for GeoBlueYellowWhiteLight
		TRAVELMAN_LIGHT,
		TRAVELAUTO_LIGHT,
		ENUMS(TAP_LIGHT, 2),// room for GeoWhiteRedLight
		ENUMS(DIVMULT_LIGHTS, 2 * 2),// room for GeoVioletGreen2Light, 1st pair /*2, 2nd pair /*4
		BPMBEAT_LIGHT,
		TWIN1OUT_LIGHT,
		TWIN2OUT_LIGHT,
		TWIN1TRAVELING_LIGHT,
		TWIN2TRAVELING_LIGHT,
		MEET_LIGHT,
		NUM_LIGHTS
	};
	
	// Expander
	TmFxInterface rightMessages[2];// messages from expander
	
	// Constants
	static const int bpmMax = 300;
	static const int bpmMin = 30;
	static constexpr float masterLengthMax = 60.0f / bpmMin;// a length is a period
	static constexpr float masterLengthMin = 60.0f / bpmMax;// a length is a period
	static constexpr float multitimeGuard = 1e-4;// 100us
	static const int numTapHistory = 4;

	static const unsigned int ON_STOP_INT_RST_MSK = 0x1;
	static const unsigned int ON_START_INT_RST_MSK = 0x2;
	static const unsigned int ON_STOP_EXT_RST_MSK = 0x4;
	static const unsigned int ON_START_EXT_RST_MSK = 0x8;
	
	enum NotifyTypeIds {NOTIFY_SYNCIN, NOTIFY_SYNCOUT, NOTIFY_DIVMULT};
	
	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	bool running;
	unsigned int resetOnStartStop;// see bit masks ON_STOP_INT_RST_MSK, ON_START_EXT_RST_MSK, etc
	int bpmManual;// master bpm from inf knob or tap tempo
	int syncInPpqn;// 0 means output BPM CV instead
	int syncOutPpqn;// 0 means output BPM CV instead
	int divMultInt;// <0 div, >0 mult
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
	int travelingSrc;// 0 when manual trig, 1 when random. only valid when traveling==true
	int multitimeSwitch;// -1 = ref, 1 = trav, 0 = none
	dsp::PulseGenerator multitimeGuardPulse;
	long notifyCounter;// 0 when nothing to notify, downward step counter otherwise
	int notifyType; // see NotifyTypeIds enum
	float pulseWidth;
	
	// No need to save, no reset
	bool scheduledReset = false;
	long cantRunWarning = 0l;// 0 when no warning, positive downward step counter timer when warning
	RefreshCounter refresh;
	float resetLight = 0.0f;
	float tapLight = 0.0f;
	float bpmBeatLight = 0.0f;
	float meetLight = 0.0f;
	float k1Light = 0.0f;
	float k2Light = 0.0f;
	float twin1OutLight = 0.0f;
	float twin2OutLight = 0.0f;
	int bpmKnob = 0;
	int64_t lastTapFrame = 0;
	float tapBpmHistory[numTapHistory] = {};// index 0 is newest, shift right
	Trigger resetTrigger;
	Trigger runButtonTrigger;
	TriggerRiseFall runInputTrigger;
	Trigger bpmDetectTrigger;
	Trigger travelTrigger;
	Trigger tapTrigger;
	Trigger syncInModeTrigger;
	Trigger syncOutModeTrigger;
	Trigger divMultTrigger;
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
	

	bool calcWarningFlash(long count, long countInit) {
		if ( (count > (countInit * 2l / 4l) && count < (countInit * 3l / 4l)) || (count < (countInit * 1l / 4l)) )
			return false;
		return true;
	}	
	int clampBpm(int bpm) {
		return clamp(bpm, bpmMin, bpmMax);
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
	double getDivMult() {
		// this is inverted ratio, so that the period time can be multiplied with return value
		if (divMultInt < 0) {
			return (double)(0x1 << -divMultInt);
		}
		return 1.0 / (double)(0x1 << divMultInt);
	}
	float probMultitime(bool isTwin1, float knob) {
		// knob is [-2.0, +2.0], and has CV included
		if (isTwin1) {
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

		rightExpander.producerMessage = &rightMessages[0];
		rightExpander.consumerMessage = &rightMessages[1];

		configParam(DURREF_PARAM, 1.0f, 8.0f, 1.0f, "Reference time");
		paramQuantities[DURREF_PARAM]->snapEnabled = true;
		configParam(DURTRAV_PARAM, 1.0f, 8.0f, 1.0f, "Travel time");
		paramQuantities[DURTRAV_PARAM]->snapEnabled = true;
		configParam<BpmParamQuantity>(BPM_PARAM, -INFINITY, INFINITY, 0.0f, "Tempo"," BPM");	
		configButton(RESET_PARAM, "Reset");
		configButton(RUN_PARAM, "Run");
		configParam(TRAVPROB_PARAM, 0.0f, 1.0f, 0.0f, "Probability to travel");
		configParam(SWAPPROB_PARAM, 0.0f, 1.0f, 0.0f, "Traveler selection probability");
		configButton(TRAVEL_PARAM, "Travel");
		configButton(DIVMULT_PARAM, "Div/Mult");
		configButton(TAP_PARAM, "Tap tempo");
		configButton(SYNCINMODE_PARAM, "Sync input mode");
		
		configInput(RESET_INPUT, "Reset");
		configInput(RUN_INPUT, "Run");
		configInput(BPM_INPUT, "BPM CV / Ext clock");
		configInput(TRAVEL_INPUT, "Travel");
		configInput(TRAVPROB_INPUT, "Travel probability CV");
		configInput(SWAPPROB_INPUT, "Traveler selection probability CV");
		configInput(DURREF_INPUT, "Reference time CV");
		configInput(DURTRAV_INPUT, "Travel time CV");

		configOutput(TWIN1_OUTPUT, "Twin 1 clock");
		configOutput(TWIN2_OUTPUT, "Twin 2 clock");
		configOutput(RESET_OUTPUT, "Reset");
		configOutput(RUN_OUTPUT, "Run");	
		configOutput(MEET_OUTPUT, "Meet");
		
		configBypass(RESET_INPUT, RESET_OUTPUT);
		configBypass(RUN_INPUT, RUN_OUTPUT);

		clk.reserve(3);
		clk.push_back(Clock(nullptr, &resetClockOutputsHigh, &pulseWidth));// Ref clock
		clk.push_back(Clock(&clk[0], &resetClockOutputsHigh, &pulseWidth));// Traveler clock	
		clk.push_back(Clock(&clk[0], &resetClockOutputsHigh, nullptr));// Sync out clock		

		onReset();
		
		panelTheme = loadDarkAsDefault();
	}
	

	void onReset() override final {
		running = true;
		resetOnStartStop = 0;
		bpmManual = 120;
		syncInPpqn = 0;// must start in CV mode, or else users won't understand why run won't turn on (since it's automatic in ppqn!=0 mode)
		syncOutPpqn = 1;
		divMultInt = 0;// -2=div4, -1=div2, 0=mult1, 1=mult2, 2=mult4
		resetClockOutputsHigh = true;
		momentaryRunInput = true;
		bpmInputScale = 1.0f;
		bpmInputOffset = 0.0f;
		resetNonJson(false);
	}
	void resetNonJson(bool delayed) {// delay thread sensitive parts (i.e. schedule them so that process() will do them)
		notifyCounter = 0l;
		notifyType = NOTIFY_SYNCIN;
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
			newMasterLength = 60.0f / (float)clampBpm(bpmManual);//params[BPM_PARAM].getValue();
			newMasterLength *= getDivMult();
		}
		newMasterLength = clamp(newMasterLength, masterLengthMin, masterLengthMax);
		masterLength = newMasterLength;
		swap = false;
		pendingTravelReq = false;
		traveling = false;
		travelingSrc = 0;
		multitimeSwitch = 0;
		multitimeGuardPulse.reset();
		pulseWidth= 0.5f;
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
		
		// divMultInt
		json_object_set_new(rootJ, "divMultInt", json_integer(divMultInt));
		
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

		// divMultInt
		json_t *divMultIntJ = json_object_get(rootJ, "divMultInt");
		if (divMultIntJ)
			divMultInt = json_integer_value(divMultIntJ);

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

	
	void multitimeSimultaneous(float knob) {
		bool p1 = random::uniform() < probMultitime(true, knob);// true = twin1
		bool p2 = random::uniform() < probMultitime(false, knob);// false = twin2
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
		bool expanderPresent = (rightExpander.module && rightExpander.module->model == modelTwinParadoxExpander);
		TmFxInterface *messagesFromExpander = static_cast<TmFxInterface*>(rightExpander.consumerMessage);// could be invalid pointer when !expanderPresent, so read it only when expanderPresent
		
		pulseWidth = expanderPresent ? messagesFromExpander->pulseWidth : 0.5f; 


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
			// tap tempo
			if (tapTrigger.process(params[TAP_PARAM].getValue())) {
				if (!inputs[BPM_INPUT].isConnected()) {
					int64_t newTapFrame = args.frame;
					int64_t tapPeriodFrames = std::max((int64_t)1, newTapFrame - lastTapFrame);
					float tapBpm = 60.0f / ((float)tapPeriodFrames * args.sampleTime);
					if (tapBpm >= bpmMin && tapBpm <= bpmMax) {
						for (int i = numTapHistory - 1; i > 0; i--) {
							tapBpmHistory[i] = tapBpmHistory[i-1];
						}
						tapBpmHistory[0] = tapBpm;
						
						float bpmSum = 0.0f;
						float bpmN = 0.0f;
						for (int i = 0; i < numTapHistory; i++) {
							bpmSum += tapBpmHistory[i] * (float)(numTapHistory - i + 1);
							if (tapBpmHistory[i] != 0) {
								bpmN += numTapHistory - i + 1;// decayed weighting
							}
						}
						bpmManual = clampBpm(std::round(bpmSum / bpmN));// clamp should not be needed here
					}
					else {
						for (int i = 0; i < numTapHistory; i++) {
							tapBpmHistory[i] = 0.0f;
						}
					}
					lastTapFrame = newTapFrame;
					tapLight = 1.0f;
				}
			}
						
			// bpm knob
			float bpmParamValue = params[BPM_PARAM].getValue();
			int newBpmKnob = (int)std::round(bpmParamValue * 30.0f);
			if (bpmParamValue == 0.0f)// true when constructor or dataFromJson() occured
				bpmKnob = newBpmKnob;
			int deltaBpmKnob = newBpmKnob - bpmKnob;
			if (deltaBpmKnob != 0) {
				if (abs(deltaBpmKnob) <= 3) {// avoid discontinuous 
					bpmManual=clampBpm(bpmManual+deltaBpmKnob);
				}
				bpmKnob = newBpmKnob;
			}	
			
			// travel button
			if (travelTrigger.process(inputs[TRAVEL_INPUT].getVoltage() + params[TRAVEL_PARAM].getValue())) {
				if (!pendingTravelReq) {
					pendingTravelReq = true;
				}
			}
			
			// divMult button
			if (divMultTrigger.process(params[DIVMULT_PARAM].getValue())) {
				// sequence should be 0, -1, -2, 1, 2
				if (divMultInt == 0 || divMultInt == -1) {
					divMultInt--;
				}
				else if (divMultInt == -2) {
					divMultInt = 1;		
				}
				else if (divMultInt == 1) {
					divMultInt++;
				}
				else {
					divMultInt = 0;
				}
				notifyCounter = (long) (3.0 * sampleRate / RefreshCounter::displayRefreshStepSkips);
				notifyType = NOTIFY_DIVMULT;
			}
			
			// syncInMode (values: 0, 24, 48)
			if (syncInModeTrigger.process(params[SYNCINMODE_PARAM].getValue())) {
				if (syncInPpqn == 0) {
					syncInPpqn = 24;
				}
				else if (syncInPpqn == 24) {
					syncInPpqn = 48;
				}
				else {
					syncInPpqn = 0;
				}
				notifyCounter = (long) (3.0 * sampleRate / RefreshCounter::displayRefreshStepSkips);
				notifyType = NOTIFY_SYNCIN;
			}
			// syncOutMode (values: 1, 24, 48)
			if (expanderPresent && syncOutModeTrigger.process(messagesFromExpander->syncOutModeButton)) {
				if (syncOutPpqn == 1) {
					syncOutPpqn = 24;
				}
				else if (syncOutPpqn == 24) {
					syncOutPpqn = 48;
				}
				else if (syncOutPpqn == 48) {
					syncOutPpqn = 0;
				}
				else {// if syncOutPpqn == 0
					syncOutPpqn = 1;
				}
				notifyCounter = (long) (3.0 * sampleRate / RefreshCounter::displayRefreshStepSkips);
				notifyType = NOTIFY_SYNCOUT;
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
							newMasterLength = clamp(clk[0].getStep() + timeLeft, masterLengthMin / 4.0f, masterLengthMax * 4.0f);// extended range for better sync ability (x-x BPM)
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
			newMasterLength = 60.0f / (float)clampBpm(bpmManual);
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
					meetLight = 1.0f;
				}
				
				swap = evalSwap();
				
				int durRef;
				int durTrav;
				double ratioTrav = getRatioTrav(&durRef, &durTrav);
				if (pendingTravelReq ) {// manual is highest priority
					pendingTravelReq = false;
					traveling = true;
					travelingSrc = 0;// manual
				}
				else if (evalTravel()) {// random is lower priority
					traveling = true;
					travelingSrc = 1;// random
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
		int twin1clk = swap ? 1 : 0;
		int twin2clk = swap ? 0 : 1;
		outputs[TWIN1_OUTPUT].setVoltage(clkOutputs[twin1clk]);
		outputs[TWIN2_OUTPUT].setVoltage(clkOutputs[twin2clk]);
		outputs[MEET_OUTPUT].setVoltage(meetPulse.process((float)sampleTime) ? 10.0f : 0.0f);
		
		outputs[RESET_OUTPUT].setVoltage(resetPulse.process((float)sampleTime) ? 10.0f : 0.0f);
		outputs[RUN_OUTPUT].setVoltage(runPulse.process((float)sampleTime) ? 10.0f : 0.0f);
		
		// multitime
		int trigMt1 = multitime1Trigger.process(clk[twin1clk].isHigh() ? 10.0f : 0.0f);
		int trigMt2 = multitime2Trigger.process(clk[twin2clk].isHigh() ? 10.0f : 0.0f);
		float mOut = 0.0f;
		if (expanderPresent) {
			float knob = messagesFromExpander->multitimeParam;
			if ((trigMt1 == -1 && multitimeSwitch == -1) || (trigMt2 == -1 && multitimeSwitch == 1)) {
				multitimeGuardPulse.trigger(multitimeGuard);
				multitimeSwitch = 0;
			}
			
			if (trigMt1 == 1 && running && multitimeSwitch == 0 && multitimeGuardPulse.remaining <= 0.0f) {
				int durThis = clk[twin1clk].getIterationsOrig();
				int durOther = clk[twin2clk].getIterationsOrig();
				int itThis = durThis - clk[twin1clk].getIterations();
				
				if (itThis * durOther % durThis == 0) {
					multitimeSimultaneous(knob);
				}
				else {
					// not simultaneous
					bool p1 = random::uniform() < probMultitime(true, knob);// true = twin1
					if (p1) {
						multitimeSwitch = -1;
					}
					else {
						multitimeSwitch = 0;
						multitimeGuardPulse.trigger(multitimeGuard);
					}				
				}
				if (multitimeSwitch == -1) {
					k1Light = 1.0f;
				}
				else if (multitimeSwitch == 1) {
					k2Light = 1.0f;
				}
			}

			if (trigMt2 == 1 && running && multitimeSwitch == 0 && multitimeGuardPulse.remaining <= 0.0f) {
				int durThis = clk[twin2clk].getIterationsOrig();
				int durOther = clk[twin1clk].getIterationsOrig();
				int itThis = durThis - clk[twin2clk].getIterations();
				
				if (itThis * durOther % durThis == 0) {
					multitimeSimultaneous(knob);
				}
				else {
					// not simultaneous
					bool p2 = random::uniform() < probMultitime(false, knob);// false = twin2
					if (p2) {
						multitimeSwitch = 1;
					}
					else {
						multitimeSwitch = 0;
						multitimeGuardPulse.trigger(multitimeGuard);
					}
				}
				if (multitimeSwitch == -1) {
					k1Light = 1.0f;
				}
				else if (multitimeSwitch == 1) {
					k2Light = 1.0f;
				}
			}			
			
			if (multitimeSwitch == -1 && running) {
				mOut = clk[twin1clk].isHigh() ? 10.0f : 0.0f;
			}
			if (multitimeSwitch == 1 && running) {
				mOut = clk[twin2clk].isHigh() ? 10.0f : 0.0f;
			}
		}
		
		if ((!swap && trigMt1 == 1) || (swap && trigMt2 == 1)) {
			bpmBeatLight = 1.0f;	
		}
		if (trigMt1 == 1) {
			twin1OutLight = 1.0f;
		}
		if (trigMt2 == 1) {
			twin2OutLight = 1.0f;
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
			
			// NOTE: some lights are implemented in the ModuleWidget
			
			// Reset light
			lights[RESET_LIGHT].setSmoothBrightness(resetLight, (float)sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));	
			resetLight = 0.0f;
						
			// BPM mode light (sync in mode)
			bool warningFlashState = true;
			if (cantRunWarning > 0l) 
				warningFlashState = calcWarningFlash(cantRunWarning, (long) (0.7 * sampleRate / RefreshCounter::displayRefreshStepSkips));
			float blight = ((float)syncInPpqn) / 48.0f;
			if (syncInPpqn != 0 && !warningFlashState) {
				blight = 0.0f;
			}
			lights[SYNCINMODE_LIGHT].setBrightness(blight);	

			// BPM Beat light
			lights[BPMBEAT_LIGHT].setSmoothBrightness(bpmBeatLight, (float)sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));	
			bpmBeatLight = 0.0f;

			// Meet light
			lights[MEET_LIGHT].setSmoothBrightness(meetLight, (float)sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));	
			meetLight = 0.0f;
						
			// Tap light
			if (inputs[BPM_INPUT].isConnected()) {
				lights[TAP_LIGHT + 0].setBrightness(0.0f);
				lights[TAP_LIGHT + 1].setBrightness(params[TAP_PARAM].getValue());
			}
			else {
				lights[TAP_LIGHT + 0].setSmoothBrightness(tapLight, (float)sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));
				lights[TAP_LIGHT + 1].setBrightness(0.0f);				
			}
			tapLight = 0.0f;
			
			// Twin1&2 output beat lights
			lights[TWIN1OUT_LIGHT].setSmoothBrightness(twin1OutLight, (float)sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));	
			twin1OutLight = 0.0f;
			lights[TWIN2OUT_LIGHT].setSmoothBrightness(twin2OutLight, (float)sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));	
			twin2OutLight = 0.0f;
			
			
			if (cantRunWarning > 0l)
				cantRunWarning--;
			
			notifyCounter--;
			if (notifyCounter < 0l)
				notifyCounter = 0l;
			
		}// lightRefreshCounter
		


		// To expander
		if (expanderPresent) {
			TxFmInterface *messageToExpander = static_cast<TxFmInterface*>(rightExpander.module->leftExpander.producerMessage);
			
			messageToExpander->syncOutClk = (syncOutPpqn == 0 ? log2f(0.5f / masterLength) : clkOutputs[2]);
			messageToExpander->syncOutModeLight = (syncOutPpqn == 0 ? 0.0f : 1.0f);
			messageToExpander->kimeOut = mOut;
			messageToExpander->k1Light = k1Light;
			k1Light = 0.0f;
			messageToExpander->k2Light = k2Light;
			k2Light = 0.0f;
			messageToExpander->panelTheme = panelTheme;

			rightExpander.module->leftExpander.messageFlipRequested = true;
		}// if (auxExpanderPresent)
			
	}// process()
};


struct TwinParadoxWidget : ModuleWidget {
	int lastPanelTheme = -1;
	std::shared_ptr<window::Svg> light_svg;
	std::shared_ptr<window::Svg> dark_svg;	

	// display code below adapted from VCVRack's Fundamental code
	struct DigitalDisplay : Widget {
		std::string fontPath;
		std::string bgText;
		std::string text;
		float fontSize;
		NVGcolor bgColor = nvgRGB(0x46,0x46, 0x46);
		NVGcolor fgColor = nvgRGB(0xda,0xda, 0xda);
		Vec textPos;

		void prepareFont(const DrawArgs& args) {
			// Get font
			std::shared_ptr<Font> font = APP->window->loadFont(fontPath);
			if (!font)
				return;
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, fontSize);
			nvgTextLetterSpacing(args.vg, 0.0);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT);
		}

		void draw(const DrawArgs& args) override {
			// Background
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2);
			nvgFillColor(args.vg, nvgRGB(0x19, 0x19, 0x19));
			nvgFill(args.vg);

			prepareFont(args);

			// Background text
			//nvgFillColor(args.vg, bgColor);
			//nvgText(args.vg, textPos.x, textPos.y, bgText.c_str(), NULL);
		}

		void drawLayer(const DrawArgs& args, int layer) override {
			if (layer == 1) {
				prepareFont(args);

				// Foreground text
				nvgFillColor(args.vg, fgColor);
				nvgText(args.vg, textPos.x, textPos.y, text.c_str(), NULL);
			}
			Widget::drawLayer(args, layer);
		}
	};

	struct BpmDisplay : DigitalDisplay {
		TwinParadox* module;
		
		BpmDisplay() {
			fontPath = asset::system("res/fonts/Nunito-Bold.ttf");
			textPos = Vec(41, 19);
			//bgText = "888";
			fontSize = 24;
		}
		
		void step() override {
			if (!module) {
				text = "120";
			}
			else {
				if (module->notifyCounter == 0l) {
					int bpm = (unsigned)((60.0f / (module->masterLength / module->getDivMult())) + 0.5f);
					bpm = module->clampBpm(bpm);
					text = string::f("%d", bpm);
				}
				else if (module->notifyType == TwinParadox::NOTIFY_SYNCIN) {
					if (module->syncInPpqn == 0) {
						text = " CV";
					}
					else {
						text = string::f("P%d", module->syncInPpqn);
					}
				}
				else if (module->notifyType == TwinParadox::NOTIFY_SYNCOUT) {
					if (module->syncOutPpqn == 0) {
						text = string::f("CV");
					}
					else {
						text = string::f("×%d", module->syncOutPpqn);
					}
				}
				else {// if (module->notifyType == TwinParadox::NOTIFY_DIVMULT) {
					if (module->divMultInt < 0) {
						text = string::f( "÷%d", 0x1<<(-module->divMultInt) );
					}
					else {
						text = string::f( "×%d", 0x1<<(module->divMultInt) );
					}
				}
			}
		}	
	};
	
	
	struct BpmKnob : GeoKnobInf {
		BpmKnob() {};		
		void onDoubleClick(const event::DoubleClick &e) override {
			ParamQuantity* paramQuantity = getParamQuantity();
			if (paramQuantity) {
				TwinParadox* module = static_cast<TwinParadox*>(paramQuantity->module);
				module->bpmManual=120;
			}
			ParamWidget::onDoubleClick(e);
		}
	};
	
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
		
		menu->addChild(createBoolMenuItem("Run CV input is level sensitive", "",
			[=]() {return !module->momentaryRunInput;},
			[=](bool loop) {module->momentaryRunInput = !module->momentaryRunInput;}
		));
		
		/*menu->addChild(createSubmenuItem("Sync input mode", "", [=](Menu* menu) {
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
		}));*/

		/*menu->addChild(createSubmenuItem("Sync output multiplier", "", [=](Menu* menu) {
			const int numMults = 3;
			const int mults[numMults] = {1, 24, 48};
			for (int i = 0; i < numMults; i++) {
				std::string label = (string::f("×%i",mults[i]));
				menu->addChild(createCheckMenuItem(label, "",
					[=]() {return module->syncOutPpqn == mults[i];},
					[=]() {module->syncOutPpqn = mults[i];}
				));
			}
		}));*/

		//createBPMCVInputMenu(menu, &module->bpmInputScale, &module->bpmInputOffset);

		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("Actions"));
		
		InstantiateExpanderItem *expItem = createMenuItem<InstantiateExpanderItem>("Add expander (4HP right side)", "");
		expItem->module = module;
		expItem->model = modelTwinParadoxExpander;
		expItem->posit = box.pos.plus(math::Vec(box.size.x,0));
		menu->addChild(expItem);	

	}
	


	TwinParadoxWidget(TwinParadox *module) {
		setModule(module);

		// Main panels from Inkscape
		light_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/TwinParadox-WL.svg"));
		dark_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/TwinParadox-DM.svg"));
		int panelTheme = isDark(module ? (&((static_cast<TwinParadox*>(module))->panelTheme)) : NULL) ? 1 : 0;// need this here since step() not called for module browser
		setPanel(panelTheme == 0 ? light_svg : dark_svg);		
		
		// Screws 
		// part of svg panel, no code required


		static const int colL = 30;
		static const int colC = 75;
		static const int colR = 120;
		static const int colR2 = 165;
		static const int colM = 220;
		
		static const int row0 = 58;// reset, run, bpm inputs
		static const int row1 = 95;// reset and run switches, bpm knob
		static const int row2 = 148;// bpm display, display index lights, master clk out
		// static const int row3 = 198;// display and mode buttons
		static const int row4 = 227;// sub clock ratio knobs
		// static const int row5 = 281;// sub clock outs
		static const int row6 = 328;// reset, run, bpm outputs


		// Reset button, light and input
		addParam(createParamCentered<GeoPushButton>(VecPx(colL, row1), module, TwinParadox::RESET_PARAM));
		addChild(createLightCentered<SmallLight<WhiteLight>>(VecPx(colL, row1 - 15.0f), module, TwinParadox::RESET_LIGHT));
		addInput(createDynamicPort<GeoPort>(VecPx(colL, row0), true, module, TwinParadox::RESET_INPUT, module ? &module->panelTheme : NULL));
		
		
		// Run button, light and input
		addParam(createParamCentered<GeoPushButton>(VecPx(colC, row1), module, TwinParadox::RUN_PARAM));
		addChild(createLightCentered<SmallLight<WhiteLight>>(VecPx(colC, row1 - 15.0f), module, TwinParadox::RUN_LIGHT));
		addInput(createDynamicPort<GeoPort>(VecPx(colC, row0), true, module, TwinParadox::RUN_INPUT, module ? &module->panelTheme : NULL));

		
		// Master BPM knob
		addParam(createDynamicParam<BpmKnob>(VecPx(colR, row1), module, TwinParadox::BPM_PARAM, module ? &module->panelTheme : NULL));
		
		// Tap tempo + light
		addParam(createDynamicParam<GeoPushButton>(VecPx(colR2, row1), module, TwinParadox::TAP_PARAM, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteRedLight>>(VecPx(colR2, row1 - 15.0f), module, TwinParadox::TAP_LIGHT));

		// Twin1 clock outputs and lights
		addOutput(createDynamicPort<GeoPort>(VecPx(colL, row2), false, module, TwinParadox::TWIN1_OUTPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<BlueLight>>(VecPx(colL-15, row2 -5), module, TwinParadox::TWIN1OUT_LIGHT));		
		addChild(createLightCentered<SmallLight<BlueLight>>(VecPx(colL-15, row2 +5), module, TwinParadox::TWIN1TRAVELING_LIGHT));
		// Twin2 clock outputs and lights
		addOutput(createDynamicPort<GeoPort>(VecPx(colL, row2 + 28.0f), false, module, TwinParadox::TWIN2_OUTPUT, module ? &module->panelTheme : NULL));	
		addChild(createLightCentered<SmallLight<YellowLight>>(VecPx(colL-15, row2 + 28.0f -5), module, TwinParadox::TWIN2OUT_LIGHT));
		addChild(createLightCentered<SmallLight<YellowLight>>(VecPx(colL-15, row2 + 28.0f +5), module, TwinParadox::TWIN2TRAVELING_LIGHT));



		// BPM display and beat light
		BpmDisplay* display = createWidget<BpmDisplay>(VecPx(colM, row0-30));
		display->box.size = mm2px(Vec(2.0*8.197, 8.197));
		display->box.pos = display->box.pos.minus(display->box.size.div(2));
		display->module = module;
		addChild(display);
		addChild(createLightCentered<SmallLight<WhiteLight>>(VecPx(colM, row0-45), module, TwinParadox::BPMBEAT_LIGHT));


		// Manual Travel button, light and input
		addParam(createDynamicParam<GeoPushButton>(VecPx(colC, row2), module, TwinParadox::TRAVEL_PARAM, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(colC, row2 - 18.0f), module, TwinParadox::TRAVELMAN_LIGHT));	
		addInput(createDynamicPort<GeoPort>(VecPx(colC, row2 + 28.0f), true, module, TwinParadox::TRAVEL_INPUT, module ? &module->panelTheme : NULL));
		
		// Auto Travel knob, light and input
		addParam(createDynamicParam<GeoKnob>(VecPx(colR, row4), module, TwinParadox::TRAVPROB_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colR, row4 + 28.0f), true, module, TwinParadox::TRAVPROB_INPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoRedLight>>(VecPx(colR, row4 - 18.0f), module, TwinParadox::TRAVELAUTO_LIGHT));	

		
		// Div/Mult button and lights
		addParam(createDynamicParam<GeoPushButton>(VecPx(colR, row2), module, TwinParadox::DIVMULT_PARAM, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoVioletGreen2Light>>(VecPx(colR - 5.0f, row2 - 18.0f), module, TwinParadox::DIVMULT_LIGHTS + 0));	// /*2
		addChild(createLightCentered<SmallLight<GeoVioletGreen2Light>>(VecPx(colR + 5.0f, row2 - 18.0f), module, TwinParadox::DIVMULT_LIGHTS + 2));	// /*4
		
		
		// sync in mode (button and light)
		addParam(createDynamicParam<GeoPushButton>(VecPx(colR2, row2), module, TwinParadox::SYNCINMODE_PARAM, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colR2, row2 - 15.0f), module, TwinParadox::SYNCINMODE_LIGHT));
		// Bpm/sync input jack
		addInput(createDynamicPort<GeoPort>(VecPx(colR2, row2 + 28.0f), true, module, TwinParadox::BPM_INPUT, module ? &module->panelTheme : NULL));
		
		
		
		// Row 4 		
		// Ratio knobs
		addParam(createDynamicParam<GeoKnob>(VecPx(colL, row4), module, TwinParadox::DURREF_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colL, row4 + 28.0f), true, module, TwinParadox::DURREF_INPUT, module ? &module->panelTheme : NULL));
		
		addParam(createDynamicParam<GeoKnob>(VecPx(colC, row4), module, TwinParadox::DURTRAV_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colC, row4 + 28.0f), true, module, TwinParadox::DURTRAV_INPUT, module ? &module->panelTheme : NULL));
				
		addParam(createDynamicParam<GeoKnob>(VecPx(colR2, row4), module, TwinParadox::SWAPPROB_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colR2, row4 + 28.0f), true, module, TwinParadox::SWAPPROB_INPUT, module ? &module->panelTheme : NULL));


		// Row 5
		// Sub-clock outputs


		// Row 6 (last row)
		// Reset out
		addOutput(createDynamicPort<GeoPort>(VecPx(colL, row6), false, module, TwinParadox::RESET_OUTPUT, module ? &module->panelTheme : NULL));
		// Run out
		addOutput(createDynamicPort<GeoPort>(VecPx(colC, row6), false, module, TwinParadox::RUN_OUTPUT, module ? &module->panelTheme : NULL));
		
		// Meet output and light
		addOutput(createDynamicPort<GeoPort>(VecPx(colR2, row6), false, module, TwinParadox::MEET_OUTPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colR2, row6 - 15.0f), module, TwinParadox::MEET_LIGHT));
		
		
		for (int i = 0; i < 8; i++) {
			addChild(createLightCentered<SmallLight<GeoBlueYellowWhiteLight>>(VecPx(200, 50 + 15 * i), module, TwinParadox::DURREF_LIGHTS + i * 3));
			addChild(createLightCentered<SmallLight<GeoBlueYellowWhiteLight>>(VecPx(240, 50 + 15 * i), module, TwinParadox::DURTRAV_LIGHTS + i * 3));
		}
		

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
			
			// DIVMULT_LIGHTS, violet/green2, 1st pair is /*2, 2nd pair is /*4
			m->lights[TwinParadox::DIVMULT_LIGHTS + 0].setBrightness(m->divMultInt ==  1 ? 1.0f : 0.0f);
			m->lights[TwinParadox::DIVMULT_LIGHTS + 1].setBrightness(m->divMultInt == -1 ? 1.0f : 0.0f);
			m->lights[TwinParadox::DIVMULT_LIGHTS + 2].setBrightness(m->divMultInt ==  2 ? 1.0f : 0.0f);
			m->lights[TwinParadox::DIVMULT_LIGHTS + 3].setBrightness(m->divMultInt == -2 ? 1.0f : 0.0f);

			// Travel lights
			m->lights[TwinParadox::TRAVELMAN_LIGHT].setBrightness((m->traveling || m->pendingTravelReq) && m->travelingSrc == 0 ? 1.0f : 0.0f);
			m->lights[TwinParadox::TRAVELAUTO_LIGHT].setBrightness(m->traveling && m->travelingSrc == 1 ? 1.0f : 0.0f);
			
			// Separate twin traveling lights
			m->lights[TwinParadox::TWIN1TRAVELING_LIGHT].setBrightness((m->traveling && !m->swap) ? 1.0f : 0.0f);
			m->lights[TwinParadox::TWIN2TRAVELING_LIGHT].setBrightness((m->traveling && m->swap) ? 1.0f : 0.0f);
						
			// Run light
			m->lights[TwinParadox::RUN_LIGHT].setBrightness(m->running ? 1.0f : 0.0f);
			
			
			// main step lights (2x8 lights)
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
