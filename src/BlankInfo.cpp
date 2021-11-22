//***********************************************************************************************
//Blank-Panel Info for VCV Rack by Pierre Collard and Marc Boulé
//***********************************************************************************************


#include "Geodesics.hpp"


struct BlankInfo : Module {

	int panelTheme;


	BlankInfo() {
		config(0, 0, 0, 0);
		
		onReset();
		
		panelTheme = (loadDarkAsDefault() ? 1 : 0);
	}

	void onReset() override {
	}

	void onRandomize() override {
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);
	}

	
	void process(const ProcessArgs &args) override {
	}
};


struct BlankInfoWidget : ModuleWidget {
	int lastPanelTheme = -1;
	SvgPanel* darkPanel;

	struct PanelThemeItem : MenuItem {
		BlankInfo *module;
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

		BlankInfo *module = dynamic_cast<BlankInfo*>(this->module);
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


	BlankInfoWidget(BlankInfo *module) {
		setModule(module);

		// Main panels from Inkscape
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/BlankInfo-WL.svg")));
		darkPanel = new SvgPanel();
		darkPanel->setBackground(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/BlankInfo-DM.svg")));
		darkPanel->setVisible(false);
		addChild(darkPanel);
		int panelTheme = isDark(module ? &(((BlankInfo*)module)->panelTheme) : NULL) ? 1 : 0;// need this here since step() not called for module browser
		getPanel()->setVisible(panelTheme == 0);
		darkPanel->setVisible(panelTheme == 1);
		
		// Screws
		// part of svg panel, no code required
	}
	
	void step() override {
		int panelTheme = isDark(module ? (&(((BlankInfo*)module)->panelTheme)) : NULL) ? 1 : 0;
		if (lastPanelTheme != panelTheme) {
			lastPanelTheme = panelTheme;
			Widget* panel = getPanel();
			panel->setVisible(panelTheme == 0);
			darkPanel->setVisible(panelTheme == 1);
		}
		Widget::step();
	}
};

Model *modelBlankInfo = createModel<BlankInfo, BlankInfoWidget>("Blank-PanelInfo");