#pragma once

#include <string>
#include "Gui.h"

class GuiController {
public:
	void setup(Gui*, const std::string&) {}
	unsigned int addSlider(const std::string&, float = 0.5f, float = 0.0f, float = 1.0f, float = 0.001f) { return 0; }
	float getSliderValue(unsigned int) { return 0.0f; }
};
