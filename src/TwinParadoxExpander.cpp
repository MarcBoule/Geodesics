//***********************************************************************************************
//Relativistic time shifting clock module for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental and Audible Instruments plugins by Andrew Belt and graphics  
//  from the Component Library. 
//See ./LICENSE.md for all licenses
//See ./res/fonts/ for font licenses
//***********************************************************************************************


#include "TwinParadoxCommon.hpp"


struct TwinParadoxExpander : Module {
	enum ParamIds {
		MULTITIME_PARAM,
		PW_PARAM,
		SYNCOUTMODE_PARAM,
		NUM_PARAMS
	};

	enum InputIds {
		MULTITIME_INPUT,
		PWCV_INPUT,
		NUM_INPUTS
	};
	
	enum OutputIds {
		MULTITIME_OUTPUT,
		SYNC_OUTPUT,
		NUM_OUTPUTS
	};

	enum LightIds {
		SYNCOUTMODE_LIGHT,
		KIME1_LIGHT,
		KIME2_LIGHT,
		NUM_LIGHTS
	};
	
	// Expander
	TxFmInterface leftMessages[2];// messages from mother


	// No need to save, no reset
	int panelTheme;


	TwinParadoxExpander() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		
		leftExpander.producerMessage = &leftMessages[0];
		leftExpander.consumerMessage = &leftMessages[1];
		
		configParam(MULTITIME_PARAM, -2.0f, 2.0f, 0.0f, "Multitime");
		configParam(PW_PARAM, 0.0f, 1.0f, 0.5f, "Pulse width");
		configButton(SYNCOUTMODE_PARAM, "Sync output mode");

		configInput(MULTITIME_INPUT, "Multitime CV");
		configInput(PWCV_INPUT, "Master clock pulse width");
		
		configOutput(MULTITIME_OUTPUT, "Multitime");
		configOutput(SYNC_OUTPUT, "Sync clock");
		
		panelTheme = loadDarkAsDefault();
	}


	void process(const ProcessArgs &args) override {		
		bool motherPresent = (leftExpander.module && leftExpander.module->model == modelTwinParadox);
		if (motherPresent) {
			// To Mother
			TmFxInterface *messagesToMother = static_cast<TmFxInterface*>(leftExpander.module->rightExpander.producerMessage);
			messagesToMother->syncOutModeButton = params[SYNCOUTMODE_PARAM].getValue();
			messagesToMother->pulseWidth = params[PW_PARAM].getValue();
			
		float knob = params[MULTITIME_PARAM].getValue();
		knob += inputs[MULTITIME_INPUT].getVoltage() / 5.0f;
		knob = clamp(knob, -2.0f, 2.0f);
			
			messagesToMother->multitimeParam = knob;
			leftExpander.module->rightExpander.messageFlipRequested = true;
			
			// From Mother
			TxFmInterface *messagesFromMother = static_cast<TxFmInterface*>(leftExpander.consumerMessage);			
			outputs[SYNC_OUTPUT].setVoltage(messagesFromMother->syncOutClk);
			lights[SYNCOUTMODE_LIGHT].setBrightness(messagesFromMother->syncOutModeLight);
			outputs[MULTITIME_OUTPUT].setVoltage(messagesFromMother->kimeOut);
			lights[KIME1_LIGHT].setSmoothBrightness(messagesFromMother->k1Light, args.sampleTime / 4.0f);//* (RefreshCounter::displayRefreshStepSkips >> 2));	
			lights[KIME2_LIGHT].setSmoothBrightness(messagesFromMother->k2Light, args.sampleTime / 4.0f);//* (RefreshCounter::displayRefreshStepSkips >> 2));	
			panelTheme = messagesFromMother->panelTheme;
		}		
	}// process()
};


struct TwinParadoxExpanderWidget : ModuleWidget {
	int lastPanelTheme = -1;
	std::shared_ptr<window::Svg> light_svg;
	std::shared_ptr<window::Svg> dark_svg;	

	TwinParadoxExpanderWidget(TwinParadoxExpander *module) {
		setModule(module);

		// Main panels from Inkscape
		light_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/TwinParadox-WL.svg"));
		dark_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/TwinParadox-DM.svg"));
		int panelTheme = isDark(module ? (&((static_cast<TwinParadoxExpander*>(module))->panelTheme)) : NULL) ? 1 : 0;// need this here since step() not called for module browser
		setPanel(panelTheme == 0 ? light_svg : dark_svg);		
		
		// Screws 
		// part of svg panel, no code required


		static const int colX = 290;
		
		//static const int row0 = 58;// reset, run, bpm inputs
		static const int row1 = 95;// reset and run switches, bpm knob
		static const int row2 = 148;// bpm display, display index lights, master clk out
		// static const int row3 = 198;// display and mode buttons
		static const int row4 = 227;// sub clock ratio knobs
		// static const int row5 = 281;// sub clock outs
		//static const int row6 = 328;// reset, run, bpm outputs

		// Multitime output (Kime out)
		addOutput(createDynamicPort<GeoPort>(VecPx(colX, row4 - 30), false, module, TwinParadoxExpander::MULTITIME_OUTPUT, module ? &module->panelTheme : NULL));
		// Multitime knob and CV input
		addParam(createDynamicParam<GeoKnob>(VecPx(colX, row4), module, TwinParadoxExpander::MULTITIME_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(VecPx(colX, row4 + 28.0f), true, module, TwinParadoxExpander::MULTITIME_INPUT, module ? &module->panelTheme : NULL));

		// Sync out jack and mode light
		addOutput(createDynamicPort<GeoPort>(VecPx(colX, row2 + 28.0f), false, module, TwinParadoxExpander::SYNC_OUTPUT, module ? &module->panelTheme : NULL));
		addChild(createLightCentered<SmallLight<GeoWhiteLight>>(VecPx(colX, row2 - 15.0f), module, TwinParadoxExpander::SYNCOUTMODE_LIGHT));
		// sync out mode (button)
		addParam(createDynamicParam<GeoPushButton>(VecPx(colX, row2), module, TwinParadoxExpander::SYNCOUTMODE_PARAM, module ? &module->panelTheme : NULL));

		// Multitime lights (2x)
		addChild(createLightCentered<SmallLight<BlueLight>>(VecPx(colX-15, row4 +15), module, TwinParadoxExpander::KIME1_LIGHT));		
		addChild(createLightCentered<SmallLight<YellowLight>>(VecPx(colX+15, row4 +15), module, TwinParadoxExpander::KIME2_LIGHT));

		// Pulse width
		addParam(createDynamicParam<GeoKnob>(VecPx(colX, row1), module, TwinParadoxExpander::PW_PARAM, module ? &module->panelTheme : NULL));
		

	}
	
	void step() override {
		int panelTheme = isDark(module ? (&((static_cast<TwinParadoxExpander*>(module))->panelTheme)) : NULL) ? 1 : 0;
		if (lastPanelTheme != panelTheme) {
			lastPanelTheme = panelTheme;
			SvgPanel* panel = static_cast<SvgPanel*>(getPanel());
			panel->setBackground(panelTheme == 0 ? light_svg : dark_svg);
			panel->fb->dirty = true;
		}
		Widget::step();
	}
};

Model *modelTwinParadoxExpander = createModel<TwinParadoxExpander, TwinParadoxExpanderWidget>("Twin-Paradox-Expander-Kime");
