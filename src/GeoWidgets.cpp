//***********************************************************************************************
//Geodesics: A modular collection for VCV Rack by Pierre Collard and Marc Boulé
//
//See ./LICENSE.txt for all licenses
//***********************************************************************************************


#include "GeoWidgets.hpp"


// ******** Panel Theme management ********

// bool defaultPanelTheme;


// void writeDarkAsDefault() {
	// json_t *settingsJ = json_object();
	// json_object_set_new(settingsJ, "darkAsDefault", json_boolean(defaultPanelTheme));
	// std::string settingsFilename = asset::user("Geodesics.json");
	// FILE *file = fopen(settingsFilename.c_str(), "w");
	// if (file) {
		// json_dumpf(settingsJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		// fclose(file);
	// }
	// json_decref(settingsJ);
// }


// void saveDarkAsDefault(bool darkAsDefault) {
	// defaultPanelTheme = darkAsDefault;
	// writeDarkAsDefault();
// }
int loadDarkAsDefault() {
	return 0x2;//defaultPanelTheme;
}

bool isDark(const int* panelTheme) {
	// if (panelTheme != NULL) {
		// return (*panelTheme != 0);
	// }
	// return defaultPanelTheme;
	
	if (panelTheme != NULL) {
		if ((*panelTheme & 0x2) != 0) {
			return settings::preferDarkPanels;
		}
		return ((*panelTheme & 0x1) != 0);
	}
	return settings::preferDarkPanels;
}


// void readDarkAsDefault() {
	// std::string settingsFilename = asset::user("Geodesics.json");
	// FILE *file = fopen(settingsFilename.c_str(), "r");
	// if (!file) {
		// defaultPanelTheme = false;
		// writeDarkAsDefault();
		// return;
	// }
	// json_error_t error;
	// json_t *settingsJ = json_loadf(file, 0, &error);
	// if (!settingsJ) {
		// invalid setting json file
		// fclose(file);
		// defaultPanelTheme = false;
		// writeDarkAsDefault();
		// return;
	// }
	// json_t *darkAsDefaultJ = json_object_get(settingsJ, "darkAsDefault");
	// if (darkAsDefaultJ) {
		// defaultPanelTheme = json_boolean_value(darkAsDefaultJ);
	// }
	// else {
		// defaultPanelTheme = false;
	// }
	
	// fclose(file);
	// json_decref(settingsJ);
// }

struct ManualThemeItem : MenuItem {
	int* panelTheme;
	int setValue = 0;

	void onAction(const event::Action &e) override {
		*panelTheme = setValue;
	}
	
	void step() override {
		disabled = (*panelTheme & 0x2) != 0;
		rightText = CHECKMARK(*panelTheme == setValue);
		MenuItem::step();
	}
};			


void createPanelThemeMenu(ui::Menu* menu, int* panelTheme) {
	menu->addChild(new MenuSeparator());

	menu->addChild(createMenuLabel("Panel Theme"));
	
	menu->addChild(createCheckMenuItem("Use Rack global theme", "",
		[=]() {return (*panelTheme & 0x2) != 0;},
		[=]() {*panelTheme ^= 0x2;}
	));

	ManualThemeItem *whiteItem = createMenuItem<ManualThemeItem>("White light edition", "");
	whiteItem->panelTheme = panelTheme;
	whiteItem->setValue = 0x0;
	menu->addChild(whiteItem);

	ManualThemeItem *darkItem = createMenuItem<ManualThemeItem>("Dark matter edition", "");
	darkItem->panelTheme = panelTheme;
	darkItem->setValue = 0x1;
	menu->addChild(darkItem);


	// menu->addChild(createCheckMenuItem("White light edition", "",
		// [=]() {return *panelTheme == 0;},
		// [=]() {*panelTheme = 0;}
	// ));
	
	// menu->addChild(createCheckMenuItem("Dark matter edition", "",
		// [=]() {return *panelTheme == 1;},
		// [=]() {*panelTheme = 1;}
	// ));
		
	// menu->addChild(createCheckMenuItem("Dark as default", "",
		// [=]() {return loadDarkAsDefault();},
		// [=]() {saveDarkAsDefault(!loadDarkAsDefault());}
	// ));
}



// Dynamic SVGPort

void DynamicSVGPort::addFrame(std::shared_ptr<Svg> svg) {
    frames.push_back(svg);
    if (frames.size() == 1) {
        SvgPort::setSvg(svg);
	}
}


void DynamicSVGPort::refreshForTheme() {
    int effMode = isDark(mode) ? 1 : 0;
	if (effMode != oldMode) {
        if (effMode > 0 && !frameAltName.empty()) {// JIT loading of alternate skin
			frames.push_back(APP->window->loadSvg(frameAltName));
			frameAltName.clear();// don't reload!
		}
		sw->setSvg(frames[effMode]);
        oldMode = effMode;
        fb->dirty = true;
    }
}


void DynamicSVGPort::step() {
	refreshForTheme();
	PortWidget::step();
}



// Dynamic SVGSwitch

void DynamicSVGSwitch::addFrameAll(std::shared_ptr<Svg> svg) {
    framesAll.push_back(svg);
	if (framesAll.size() == 2) {
		addFrame(framesAll[0]);
		addFrame(framesAll[1]);
	}
}


void DynamicSVGSwitch::refreshForTheme() {
    int effMode = isDark(mode) ? 1 : 0;
	if (effMode != oldMode) {
        if (effMode > 0 && !frameAltName0.empty() && !frameAltName1.empty()) {// JIT loading of alternate skin
			framesAll.push_back(APP->window->loadSvg(frameAltName0));
			framesAll.push_back(APP->window->loadSvg(frameAltName1));
			frameAltName0.clear();// don't reload!
			frameAltName1.clear();// don't reload!
		}
        if (effMode == 0|| framesAll.size() < 4) {
			frames[0]=framesAll[0];
			frames[1]=framesAll[1];
		}
		else {
			frames[0]=framesAll[2];
			frames[1]=framesAll[3];
		}
        oldMode = effMode;
		onChange(*(new event::Change()));// required because of the way SVGSwitch changes images, we only change the frames above.
		fb->dirty = true;// dirty is not sufficient when changing via frames assignments above (i.e. onChange() is required)
    }
}


void DynamicSVGSwitch::step() {
	refreshForTheme();
	SvgSwitch::step();
}



// Dynamic SVGKnob

void DynamicSVGKnob::addFrameAll(std::shared_ptr<Svg> svg) {
    framesAll.push_back(svg);
	if (framesAll.size() == 1) {
		setSvg(svg);
	}
}


void DynamicSVGKnob::addFrameBgAll(std::shared_ptr<Svg> svg) {
    framesBgAll.push_back(svg);
	if (framesBgAll.size() == 1) {
		bg = new widget::SvgWidget;
		fb->addChildBelow(bg, tw);
		bg->setSvg(svg);
	}
}


void DynamicSVGKnob::addFrameFgAll(std::shared_ptr<Svg> svg) {
    framesFgAll.push_back(svg);
	if (framesFgAll.size() == 1) {
		fg = new widget::SvgWidget;
		fb->addChildAbove(fg, tw);
		fg->setSvg(svg);
	}
}


void DynamicSVGKnob::setOrientation(float angle) {
	tw->removeChild(sw);
	TransformWidget *tw2 = new TransformWidget();
	tw2->addChild(sw);
	tw->addChild(tw2);

	Vec center = sw->box.getCenter();
	tw2->translate(center);
	tw2->rotate(angle);
	tw2->translate(center.neg());
}


void DynamicSVGKnob::refreshForTheme() {
    int effMode = isDark(mode) ? 1 : 0;
	if (effMode != oldMode) {
        if (effMode > 0 && !frameAltName.empty()) {// JIT loading of alternate skin
			framesAll.push_back(APP->window->loadSvg(frameAltName));
			frameAltName.clear();// don't reload!
			if (!frameAltBgName.empty()) {
				framesBgAll.push_back(APP->window->loadSvg(frameAltBgName));
			}
			if (!frameAltFgName.empty()) {
				framesFgAll.push_back(APP->window->loadSvg(frameAltFgName));
			}
		}
        if (effMode == 0) {
			setSvg(framesAll[0]);
			if (!frameAltBgName.empty()) {
				bg->setSvg(framesBgAll[0]);
			}
			if (!frameAltFgName.empty()) {
				fg->setSvg(framesFgAll[0]);
			}
		}
		else {
			setSvg(framesAll[1]);
			if (!frameAltBgName.empty()) {
				bg->setSvg(framesBgAll[1]);
			}
			if (!frameAltFgName.empty()) {
				fg->setSvg(framesFgAll[1]);
			}
		}
        oldMode = effMode;
		fb->dirty = true;
    }
}


void DynamicSVGKnob::step() {
	refreshForTheme();
	SvgKnob::step();
}
