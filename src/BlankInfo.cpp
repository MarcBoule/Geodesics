//***********************************************************************************************
//Blank-Panel Info for VCV Rack by Pierre Collard and Marc Boulé
//***********************************************************************************************


#include "Geodesics.hpp"


struct BlankInfo : Module {

	int panelTheme;


	BlankInfo() {
		config(0, 0, 0, 0);
		
		onReset();
		
		panelTheme = loadDarkAsDefault();
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
	std::shared_ptr<window::Svg> light_svg;
	std::shared_ptr<window::Svg> dark_svg;

	void appendContextMenu(Menu *menu) override {
		BlankInfo *module = dynamic_cast<BlankInfo*>(this->module);
		assert(module);

		createPanelThemeMenu(menu, &(module->panelTheme));
	}	


	BlankInfoWidget(BlankInfo *module) {
		setModule(module);

		// Main panels from Inkscape
 		light_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/WhiteLight/BlankInfo-WL.svg"));
		dark_svg = APP->window->loadSvg(asset::plugin(pluginInstance, "res/DarkMatter/BlankInfo-DM.svg"));
		int panelTheme = isDark(module ? (&(((BlankInfo*)module)->panelTheme)) : NULL) ? 1 : 0;// need this here since step() not called for module browser
		setPanel(panelTheme == 0 ? light_svg : dark_svg);		
		
		// Screws
		// part of svg panel, no code required
	}
	
	void step() override {
		int panelTheme = isDark(module ? (&(((BlankInfo*)module)->panelTheme)) : NULL) ? 1 : 0;
		if (lastPanelTheme != panelTheme) {
			lastPanelTheme = panelTheme;
			SvgPanel* panel = (SvgPanel*)getPanel();
			panel->setBackground(panelTheme == 0 ? light_svg : dark_svg);
			panel->fb->dirty = true;
		}
		Widget::step();
	}
};

Model *modelBlankInfo = createModel<BlankInfo, BlankInfoWidget>("Blank-PanelInfo");