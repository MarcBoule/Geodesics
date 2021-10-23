//***********************************************************************************************
//Geodesics: A modular collection for VCV Rack by Pierre Collard and Marc Boulé
//
//See ./LICENSE.txt for all licenses
//***********************************************************************************************


#include "GeoWidgets.hpp"


// Dynamic SVGPort

void DynamicSVGPort::addFrame(std::shared_ptr<Svg> svg) {
    frames.push_back(svg);
    if(frames.size() == 1) {
        SvgPort::setSvg(svg);
	}
}


void DynamicSVGPort::step() {
    if(mode != NULL && *mode != oldMode) {
        if (*mode > 0 && !frameAltName.empty()) {// JIT loading of alternate skin
			frames.push_back(APP->window->loadSvg(frameAltName));
			frameAltName.clear();// don't reload!
		}
		sw->setSvg(frames[*mode]);
        oldMode = *mode;
        fb->dirty = true;
    }
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

void DynamicSVGSwitch::step() {
    if(mode != NULL && *mode != oldMode) {
        if (*mode > 0 && !frameAltName0.empty() && !frameAltName1.empty()) {// JIT loading of alternate skin
			framesAll.push_back(APP->window->loadSvg(frameAltName0));
			framesAll.push_back(APP->window->loadSvg(frameAltName1));
			frameAltName0.clear();// don't reload!
			frameAltName1.clear();// don't reload!
		}
        if ((*mode) == 0|| framesAll.size() < 4) {
			frames[0]=framesAll[0];
			frames[1]=framesAll[1];
		}
		else {
			frames[0]=framesAll[2];
			frames[1]=framesAll[3];
		}
        oldMode = *mode;
		onChange(*(new event::Change()));// required because of the way SVGSwitch changes images, we only change the frames above.
		fb->dirty = true;// dirty is not sufficient when changing via frames assignments above (i.e. onChange() is required)
    }
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

void DynamicSVGKnob::step() {
    if(mode != NULL && *mode != oldMode) {
        if (*mode > 0 && !frameAltName.empty()) {// JIT loading of alternate skin
			framesAll.push_back(APP->window->loadSvg(frameAltName));
			frameAltName.clear();// don't reload!
			if (!frameAltBgName.empty()) {
				framesBgAll.push_back(APP->window->loadSvg(frameAltBgName));
			}
			if (!frameAltFgName.empty()) {
				framesFgAll.push_back(APP->window->loadSvg(frameAltFgName));
			}
		}
        if ((*mode) == 0) {
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
        oldMode = *mode;
		fb->dirty = true;
    }
	SvgKnob::step();
}
