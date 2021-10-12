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

	p->addModel(modelBlackHoles);
	p->addModel(modelPulsars);
	p->addModel(modelBranes);
	p->addModel(modelIons);
	p->addModel(modelEntropia);
	p->addModel(modelEnergy);
	p->addModel(modelTorus);
	p->addModel(modelFate);
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

void saveDarkAsDefault(bool darkAsDefault) {
	json_t *settingsJ = json_object();
	json_object_set_new(settingsJ, "darkAsDefault", json_boolean(darkAsDefault));
	std::string settingsFilename = asset::user("Geodesics.json");
	FILE *file = fopen(settingsFilename.c_str(), "w");
	if (file) {
		json_dumpf(settingsJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		fclose(file);
	}
	json_decref(settingsJ);
}

bool loadDarkAsDefault() {
	bool ret = false;
	std::string settingsFilename = asset::user("Geodesics.json");
	FILE *file = fopen(settingsFilename.c_str(), "r");
	if (!file) {
		saveDarkAsDefault(false);
		return ret;
	}
	json_error_t error;
	json_t *settingsJ = json_loadf(file, 0, &error);
	if (!settingsJ) {
		// invalid setting json file
		fclose(file);
		saveDarkAsDefault(false);
		return ret;
	}
	json_t *darkAsDefaultJ = json_object_get(settingsJ, "darkAsDefault");
	if (darkAsDefaultJ)
		ret = json_boolean_value(darkAsDefaultJ);
	
	fclose(file);
	json_decref(settingsJ);
	return ret;
}
