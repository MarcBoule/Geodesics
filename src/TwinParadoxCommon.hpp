//***********************************************************************************************
//Mixer module for VCV Rack by Steve Baker and Marc Boulé 
//
//Based on code from the Fundamental plugin by Andrew Belt 
//See ./LICENSE.md for all licenses
//***********************************************************************************************

#pragma once
#include "Geodesics.hpp"


// Communications between clock and expander

struct TxFmInterface {// messages to expander from mother (data is in expander, mother writes into expander)
	float kimeOut;
	float k1Light;
	float k2Light;
	int panelTheme;
};


struct TmFxInterface {// messages to mother from expander (data is in mother, expander writes into mother)
	float pulseWidth;// includes its CV that is local to expander only
	float multitimeParam;// includes its CV that is local to expander only
};

