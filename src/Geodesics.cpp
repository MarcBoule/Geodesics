//***********************************************************************************************
//Geodesics: A modular collection for VCV Rack by Pierre Collard and Marc Boulé
//
//Based on code from the Fundamental plugins by Andrew Belt 
//  and graphics from the Component Library by Wes Milholen 
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//***********************************************************************************************


#include "Geodesics.hpp"


Plugin *pluginInstance;


void init(Plugin *p) {
	pluginInstance = p;
	
	//readDarkAsDefault();

	p->addModel(modelBlackHoles);
	p->addModel(modelPulsars);
	p->addModel(modelBranes);
	p->addModel(modelIons);
	p->addModel(modelEntropia);
	p->addModel(modelEnergy);
	p->addModel(modelDarkEnergy);
	p->addModel(modelTorus);
	p->addModel(modelFate);
	p->addModel(modelTwinParadox);
	p->addModel(modelTwinParadoxExpander);
	p->addModel(modelBlankLogo);
	p->addModel(modelBlankInfo);
}



// other

int getWeighted1to8random() {
	int	prob = random::u32() % 1000;
	if (prob < 175)
		return 1;
	else if (prob < 330) // 175 + 155
		return 2;
	else if (prob < 475) // 175 + 155 + 145
		return 3;
	else if (prob < 610) // 175 + 155 + 145 + 135
		return 4;
	else if (prob < 725) // 175 + 155 + 145 + 135 + 115
		return 5;
	else if (prob < 830) // 175 + 155 + 145 + 135 + 115 + 105
		return 6;
	else if (prob < 925) // 175 + 155 + 145 + 135 + 115 + 105 + 95
		return 7;
	return 8;
}

void InstantiateExpanderItem::onAction(const event::Action &e) {
	// Create Module and ModuleWidget
	module = model->createModule();
	APP->engine->addModule(module);

	ModuleWidget* mw = model->createModuleWidget(module);
	// APP->scene->rack->addModuleAtMouse(mw);
	
	// ModuleWidget *mw = model->createModuleWidget();
	if (mw) {
		APP->scene->rack->setModulePosNearest(mw, posit);
		APP->scene->rack->addModule(mw);
		history::ModuleAdd *h = new history::ModuleAdd;
		h->name = "create expander module";
		h->setModule(mw);
		APP->history->push(h);
	}
}
