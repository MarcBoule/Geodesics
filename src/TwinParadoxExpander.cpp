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
		//PW_PARAM,
		NUM_PARAMS
	};

	enum InputIds {
		MULTITIME_INPUT,
		//PWCV_INPUT,
		NUM_INPUTS
	};
	
	enum OutputIds {
		MULTITIME_OUTPUT,
		NUM_OUTPUTS
	};

	enum LightIds {
		KIME1_LIGHT,
		KIME2_LIGHT,
		ENUMS(XPAND_LIGHT, 2),// white-red
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
		//configParam(PW_PARAM, 0.0f, 1.0f, 0.5f, "Pulse width");

		configInput(MULTITIME_INPUT, "Multitime CV");
		//configInput(PWCV_INPUT, "Pulse width CV");
		
		configOutput(MULTITIME_OUTPUT, "Multitime");
		
		
		panelTheme = loadDarkAsDefault();
	}


	void process(const ProcessArgs &args) override {		
		bool motherPresent = (leftExpander.module && leftExpander.module->model == modelTwinParadox);
		if (motherPresent) {
			// To Mother
			TmFxInterface *messagesToMother = static_cast<TmFxInterface*>(leftExpander.module->rightExpander.producerMessage);
			// pulse width with CV
			// float pw = params[PW_PARAM].getValue();
			// pw += inputs[PWCV_INPUT].getVoltage() / 10.0f;
			// pw = clamp(pw, 0.0f, 1.0f);
			// messagesToMother->pulseWidth = pw;
			// multitime knob with CV
			float knob = params[MULTITIME_PARAM].getValue();
			knob += inputs[MULTITIME_INPUT].getVoltage() / 5.0f;
			knob = clamp(knob, -2.0f, 2.0f);
			messagesToMother->multitimeParam = knob;
			
			leftExpander.module->rightExpander.messageFlipRequested = true;
			
			
			// From Mother
			TxFmInterface *messagesFromMother = static_cast<TxFmInterface*>(leftExpander.consumerMessage);			
			outputs[MULTITIME_OUTPUT].setVoltage(messagesFromMother->kimeOut);
			float smoothDeltaTime = args.sampleTime / 4.0f;// args.sampleTime * (RefreshCounter::displayRefreshStepSkips >> 2));
			lights[KIME1_LIGHT].setSmoothBrightness(messagesFromMother->k1Light, smoothDeltaTime);
			lights[KIME2_LIGHT].setSmoothBrightness(messagesFromMother->k2Light, smoothDeltaTime);	
			panelTheme = messagesFromMother->panelTheme;
			lights[XPAND_LIGHT + 0].setBrightness(1.0f);
			lights[XPAND_LIGHT + 1].setBrightness(0.0f);
		}	
		else {
			outputs[MULTITIME_OUTPUT].setVoltage(0.0f);
			lights[KIME1_LIGHT].setBrightness(0.0f);
			lights[KIME2_LIGHT].setBrightness(0.0f);	
			lights[XPAND_LIGHT + 0].setBrightness(0.0f);
			lights[XPAND_LIGHT + 1].setBrightness(1.0f);
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
		light_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/Kime-WL.svg"));
		dark_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/Kime-DM.svg"));
		int panelTheme = isDark(module ? (&((static_cast<TwinParadoxExpander*>(module))->panelTheme)) : NULL) ? 1 : 0;// need this here since step() not called for module browser
		setPanel(panelTheme == 0 ? light_svg : dark_svg);		
		
		// Screws 
		// part of svg panel, no code required


		static const float colX = 10.147f;
		

		// Multitime output (Kime out)
		addOutput(createDynamicPort<GeoPort>(mm2px(Vec(colX, 38.121f)), false, module, TwinParadoxExpander::MULTITIME_OUTPUT, module ? &module->panelTheme : NULL));

		// Multitime lights (2x)
		addChild(createLightCentered<SmallLight<BlueLight>>(mm2px(Vec(2.7055f, 39.2265f)), module, TwinParadoxExpander::KIME1_LIGHT));		
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(17.5845f, 39.2265f)), module, TwinParadoxExpander::KIME2_LIGHT));

		// Multitime knob and CV input
		addParam(createDynamicParam<GeoKnob>(mm2px(Vec(colX, 53.096f)), module, TwinParadoxExpander::MULTITIME_PARAM, module ? &module->panelTheme : NULL));
		addInput(createDynamicPort<GeoPort>(mm2px(Vec(colX, 68.139f)), true, module, TwinParadoxExpander::MULTITIME_INPUT, module ? &module->panelTheme : NULL));

		// Xpand LED
		addChild(createLightCentered<SmallLight<GeoWhiteRedLight>>(mm2px(Vec(colX, 100.4325f)), module, TwinParadoxExpander::XPAND_LIGHT));		


		// Pulse width
		//addParam(createDynamicParam<GeoKnob>(VecPx(colX, row1), module, TwinParadoxExpander::PW_PARAM, module ? &module->panelTheme : NULL));
		//addInput(createDynamicPort<GeoPort>(VecPx(colX, row0), true, module, TwinParadoxExpander::PWCV_INPUT, module ? &module->panelTheme : NULL));
		

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
