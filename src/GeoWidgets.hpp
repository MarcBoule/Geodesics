//***********************************************************************************************
//Geodesics: A modular collection for VCV Rack by Pierre Collard and Marc Boulé
//
//See ./LICENSE.txt for all licenses
//***********************************************************************************************


#pragma once

#include "rack.hpp"

using namespace rack;



// ******** Panel Theme management ********


// void saveDarkAsDefault(bool darkAsDefault);
int loadDarkAsDefault();

bool isDark(int* panelTheme);

// void readDarkAsDefault();

void createPanelThemeMenu(ui::Menu* menu, int* panelTheme);


// ******** Dynamic Ports ********

// General Dynamic Port creation
template <class TDynamicPort>
TDynamicPort* createDynamicPort(Vec pos, bool isInput, Module *module, int portId, int* mode) {
	TDynamicPort *dynPort = isInput ? 
		createInputCentered<TDynamicPort>(pos, module, portId) :
		createOutputCentered<TDynamicPort>(pos, module, portId);
	dynPort->mode = mode;
	dynPort->refreshForTheme();// all TDynamicPort must have this
	return dynPort;
}

struct DynamicSVGPort : SvgPort {
    int* mode = NULL;
    int oldMode = -1;
    std::vector<std::shared_ptr<Svg>> frames;
	std::string frameAltName;

    void addFrame(std::shared_ptr<Svg> svg);
    void addFrameAlt(std::string filename) {frameAltName = filename;}
	void refreshForTheme();
    void step() override;
};



// ******** Dynamic Params ********

template <class TDynamicParam>
TDynamicParam* createDynamicParam(Vec pos, Module *module, int paramId, int* mode) {
	TDynamicParam *dynParam = createParamCentered<TDynamicParam>(pos, module, paramId);
	dynParam->mode = mode;
	dynParam->refreshForTheme();// all TDynamicParam must have this
	return dynParam;
}

struct DynamicSVGSwitch : SvgSwitch {
    int* mode = NULL;
    int oldMode = -1;
	std::vector<std::shared_ptr<Svg>> framesAll;
	std::string frameAltName0;
	std::string frameAltName1;
	
	void addFrameAll(std::shared_ptr<Svg> svg);
    void addFrameAlt0(std::string filename) {frameAltName0 = filename;}
    void addFrameAlt1(std::string filename) {frameAltName1 = filename;}
	void refreshForTheme();
	void step() override;
};

struct DynamicSVGKnob : SvgKnob {
    int* mode = NULL;
    int oldMode = -1;
	std::vector<std::shared_ptr<Svg>> framesAll;
	std::vector<std::shared_ptr<Svg>> framesBgAll;
	std::vector<std::shared_ptr<Svg>> framesFgAll;
	std::string frameAltName;
	std::string frameAltBgName;
	std::string frameAltFgName;
	widget::SvgWidget* bg = NULL;
	widget::SvgWidget* fg = NULL;
	
	void setOrientation(float angle);
	void addFrameAll(std::shared_ptr<Svg> svg);
    void addFrameAlt(std::string filename) {frameAltName = filename;}
	void addFrameBgAll(std::shared_ptr<Svg> svg);
    void addFrameBgAlt(std::string filename) {frameAltBgName = filename;}
	void addFrameFgAll(std::shared_ptr<Svg> svg);
    void addFrameFgAlt(std::string filename) {frameAltFgName = filename;}
	void refreshForTheme();
    void step() override;
};