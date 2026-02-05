#ifndef DISPLAY_H
#define DISPLAY_H

#include <TFT_eSPI.h>
#include "config.h"

extern TFT_eSPI tft;

void initDisplay();
void showBootScreen();
void updateDisplay(const struct ACState& state);
void showStatusIndicator(const char* text, uint16_t color);

// Multi-screen support
void updateScreenDisplay();
void drawVolumeScreen();
void drawClockScreen();
void drawNetworkScreen();
void drawACScreen();
void drawIRLearnScreen();
void drawSensorsScreen();
void drawAlarmScreen();

#endif // DISPLAY_H
