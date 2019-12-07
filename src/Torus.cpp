//***********************************************************************************************
//Bi-dimensional multimixer module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental plugins by Andrew Belt and graphics  
//  from the Component Library by Wes Milholen. 
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//***********************************************************************************************


#include "Geodesics.hpp"


struct chanVol {// a mixMap for an output has four of these, for each quadrant that can map to its output
	float vol;// 0.0 to 1.0
	float chan;// channel input number (0 to 15)
	bool inputIsAboveOutput;// true when an input is located above the output, false otherwise
	OnePoleFilter filt;// a lowpass filter (highpass is done using "inval - outval" trick
	
	void writeChan(float _vol, int _chan, bool _inAboveOut, float norm_f_c) {
		vol = _vol;
		chan = _chan;
		inputIsAboveOutput = _inAboveOut;
		filt.setCutoff(norm_f_c);		
	}
	
	float processFilter(float inval) {
		float outval = filt.process(inval);
		return (inputIsAboveOutput ? outval : (inval - outval));// inputIsAboveOutput ? lowpass : highpass		
	}
};


struct mixMapOutput {
	chanVol cvs[4];// an output can have a mix of at most 4 inputs
	int numInputs;// number of inputs that need to be read for this given output
	float sampleRate;

	void init(float _sampleRate) {
		sampleRate = _sampleRate;
		numInputs = 0;
	}
	
	float getScaledInput(int index, float inval) {
		return inval * cvs[index].vol;
	}		

	float getFilteredInput(int index, float inval) {
		return cvs[index].processFilter(inval);
	}

	void insert(int numerator, int denominator, int mixmode, float _chan, bool _inAboveOut) {
		float _vol = (mixmode == 1 ? 1.0f : ((float)numerator / (float)denominator));
		float f_c = (float)calcCutoffFreq(numerator, denominator, _inAboveOut);
		cvs[numInputs].writeChan(_vol, _chan, _inAboveOut, f_c / sampleRate);
		numInputs++;
	}
		
	int calcCutoffFreq(int num, int denum, bool isLowPass) {
		num = denum - num;// complement since distance is complement of volume's fraction in decay mode
		switch (denum) {
			case (3) :
				if (num == 1) return isLowPass ? 3000 : 500;
				return isLowPass ? 1500 : 1000;
			break;
			
			case (4) :
				if (num == 1) return isLowPass ? 4000 : 300;
				if (num == 3) return isLowPass ? 1000 : 1500;
			break;
			
			case (5) :
				if (num == 1) return isLowPass ? 5000 : 250;
				if (num == 2) return isLowPass ? 3000 : 500;
				if (num == 3) return isLowPass ? 1500 : 1000;
				return isLowPass ? 700 : 2000;
			break;
			
			case (6) :
				if (num == 1) return isLowPass ? 8000 : 200;
				if (num == 2) return isLowPass ? 5000 : 500;
				if (num == 4) return isLowPass ? 1000 : 1500;
				if (num == 5) return isLowPass ? 500 : 3000;
			break;
			
			case (7) :
				if (num == 1) return isLowPass ? 12000 : 110;
				if (num == 2) return isLowPass ? 8000 : 350;
				if (num == 3) return isLowPass ? 3000 : 750;
				if (num == 4) return isLowPass ? 1500 : 1500;
				if (num == 5) return isLowPass ? 500 : 2500;
				return isLowPass ? 200 : 4000;
			break;
			
			case (8) :
				if (num == 1) return isLowPass ? 16000 : 60;
				if (num == 2) return isLowPass ? 8000 : 150;
				if (num == 3) return isLowPass ? 4000 : 350;
				if (num == 5) return isLowPass ? 1000 : 1500;
				if (num == 6) return isLowPass ? 400 : 5000;
				if (num == 7) return isLowPass ? 100 : 8000;
			break;
		}
		return isLowPass ? 2000 : 750;
	}
};
	

//*****************************************************************************


struct Torus : Module {
	enum ParamIds {
		GAIN_PARAM,
		MODE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(MIX_INPUTS, 16),
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(MIX_OUTPUTS, 8),
		NUM_OUTPUTS
	};
	enum LightIds {
		DECAY_LIGHT,
		CONSTANT_LIGHT,
		FILTER_LIGHT,
		NUM_LIGHTS
	};
	
	
	// Constants
	// none
	
	// Need to save, no reset
	int panelTheme;
	
	// Need to save, with reset
	int mixmode;// 0 is decay, 1 is constant, 2 is filter
	
	// No need to save, with reset
	mixMapOutput mixMap[7];// 7 outputs
	
	// No need to save, no reset
	RefreshCounter refresh;
	Trigger modeTrigger;
	
	
	Torus() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		
		configParam(Torus::MODE_PARAM, 0.0f, 1.0f, 0.0f, "Mode");
		configParam(Torus::GAIN_PARAM, 0.0f, 2.0f, 1.0f, "Gain");

		onReset();

		panelTheme = (loadDarkAsDefault() ? 1 : 0);
	}

	
	void onReset() override {
		mixmode = 0;
		resetNonJson();
	}
	void resetNonJson() {
		updateMixMap(APP->engine->getSampleRate());
	}


	void onRandomize() override {
		mixmode = random::u32() % 3;
	}
	

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// mixmode
		json_object_set_new(rootJ, "mixmode", json_integer(mixmode));

		return rootJ;
	}

	
	void dataFromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);

		// mixmode
		json_t *mixmodeJ = json_object_get(rootJ, "mixmode");
		if (mixmodeJ)
			mixmode = json_integer_value(mixmodeJ);
		
		resetNonJson();
	}
	

	void process(const ProcessArgs &args) override {		
		// user inputs
		if (refresh.processInputs()) {
			// mixmode
			if (modeTrigger.process(params[MODE_PARAM].getValue())) {
				if (++mixmode > 2)
					mixmode = 0;
			}
			
			updateMixMap(args.sampleRate);
		}// userInputs refresh
		
		
		// mixer code
		for (int outi = 0; outi < 7; outi++) {
			float outValue = 0.0f;
			if (outputs[MIX_OUTPUTS + outi].isConnected()) {
				outValue = clamp(calcOutput(outi) * params[GAIN_PARAM].getValue(), -10.0f, 10.0f);
			}
			outputs[MIX_OUTPUTS + outi].setVoltage(outValue);
		}
		

		// lights
		if (refresh.processLights()) {
			lights[DECAY_LIGHT].setBrightness(mixmode == 0 ? 1.0f : 0.0f);
			lights[CONSTANT_LIGHT].setBrightness(mixmode == 1 ? 1.0f : 0.0f);
			lights[FILTER_LIGHT].setBrightness(mixmode == 2 ? 1.0f : 0.0f);
		}// lightRefreshCounter
	}// step()
	
	
	void updateMixMap(float sampleRate) {
		for (int outi = 0; outi < 7; outi++) {
			mixMap[outi].init(sampleRate);
		}
		
		// scan inputs for upwards flow (input is below output)
		int distanceUL = 1;
		int distanceUR = 1;
		for (int ini = 1; ini < 8; ini++) {
			distanceUL++;
			distanceUR++;
			
			// left side
			if (inputs[MIX_INPUTS + ini].isConnected()) {
				for (int outi = ini - 1 ; outi >= 0; outi--) {
					int numerator = (distanceUL - ini + outi);
					if (numerator == 0) 
						break;
					mixMap[outi].insert(numerator, distanceUL, mixmode, ini, false);// last param is _inAboveOut
				}
				distanceUL = 1;
			}
			
			// right side
			if (inputs[MIX_INPUTS + 8 + ini].isConnected()) {
				for (int outi = ini - 1 ; outi >= 0; outi--) {
					int numerator = (distanceUR - ini + outi);
					if (numerator == 0) 
						break;
					mixMap[outi].insert(numerator, distanceUL, mixmode, 8 + ini, false);// last param is _inAboveOut
				}
				distanceUR = 1;
			}			
		}	

		// scan inputs for downward flow (input is above output)
		int distanceDL = 1;
		int distanceDR = 1;
		for (int ini = 6; ini >= 0; ini--) {
			distanceDL++;
			distanceDR++;
			
			// left side
			if (inputs[MIX_INPUTS + ini].isConnected()) {
				for (int outi = ini ; outi < 7; outi++) {
					int numerator = (distanceDL - 1 + ini - outi);
					if (numerator == 0) 
						break;
					mixMap[outi].insert(numerator, distanceUL, mixmode, ini, true);// last param is _inAboveOut
				}
				distanceDL = 1;
			}
			
			// right side
			if (inputs[MIX_INPUTS + 8 + ini].isConnected()) {
				for (int outi = ini ; outi < 7; outi++) {
					int numerator = (distanceDR - 1 + ini - outi);
					if (numerator == 0) 
						break;
					mixMap[outi].insert(numerator, distanceUL, mixmode, 8 + ini, true);// last param is _inAboveOut
				}
				distanceDR = 1;
			}		
		}	
	}
	
	
	float calcOutput(int outi) {
		float outputValue = 0.0f;
		if (mixmode < 2) {// constant or decay modes	
			for (int i = 0; i < mixMap[outi].numInputs; i++) {
				int chan = mixMap[outi].cvs[i].chan;
				outputValue += mixMap[outi].getScaledInput(i, inputs[MIX_INPUTS + chan].getVoltage());
			}
		}
		else {// filter mode
			for (int i = 0; i < mixMap[outi].numInputs; i++) {
				int chan = mixMap[outi].cvs[i].chan;
				outputValue += mixMap[outi].getFilteredInput(i, inputs[MIX_INPUTS + chan].getVoltage());
			}
		}
		return outputValue;
	}
};


struct TorusWidget : ModuleWidget {
	SvgPanel* darkPanel;

	struct PanelThemeItem : MenuItem {
		Torus *module;
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

		Torus *module = dynamic_cast<Torus*>(this->module);
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
	
	TorusWidget(Torus *module) {
		setModule(module);

		// Main panels from Inkscape
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/Torus-WL.svg")));
        if (module) {
			darkPanel = new SvgPanel();
			darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/Torus-DM.svg")));
			darkPanel->visible = false;
			addChild(darkPanel);
		}

		// Screws 
		// part of svg panel, no code required
		
		float colRulerCenter = box.size.x / 2.0f;

		// mixmode button
		addParam(createDynamicParam<GeoPushButton>(Vec(colRulerCenter, 380.0f - 329.5f), module, Torus::MODE_PARAM, module ? &module->panelTheme : NULL));
		
		// decay and constant lights
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(colRulerCenter, 380.0f - 343.5f), module, Torus::FILTER_LIGHT));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(colRulerCenter - 12.5f, 380.0f - 322.5f), module, Torus::DECAY_LIGHT));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(Vec(colRulerCenter + 12.5f, 380.0f - 322.5f), module, Torus::CONSTANT_LIGHT));

		// gain knob
		addParam(createDynamicParam<GeoKnob>(Vec(colRulerCenter, 380 - 294), module, Torus::GAIN_PARAM, module ? &module->panelTheme : NULL));
		
		// inputs
		static const int offsetY = 34;
		for (int i = 0; i < 8; i++) {
			addInput(createDynamicPort<GeoPort>(Vec(colRulerCenter - 22.5f, 380 - (270 - offsetY * i)), true, module, Torus::MIX_INPUTS + i, module ? &module->panelTheme : NULL));
			addInput(createDynamicPort<GeoPort>(Vec(colRulerCenter + 22.5f, 380 - (270 - offsetY * i)), true, module, Torus::MIX_INPUTS + 8 + i, module ? &module->panelTheme : NULL));
		}
		
		// mix outputs
		for (int i = 0; i < 7; i++) {
			addOutput(createDynamicPort<GeoPort>(Vec(colRulerCenter, 380 - (253 - offsetY * i)), false, module, Torus::MIX_OUTPUTS + i, module ? &module->panelTheme : NULL));
		}
	}
	
	void step() override {
		if (module) {
			panel->visible = ((((Torus*)module)->panelTheme) == 0);
			darkPanel->visible  = ((((Torus*)module)->panelTheme) == 1);
		}
		Widget::step();
	}
};

Model *modelTorus = createModel<Torus, TorusWidget>("Torus");

/*CHANGE LOG

*/