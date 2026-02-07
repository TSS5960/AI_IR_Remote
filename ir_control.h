#ifndef IR_CONTROL_H
#define IR_CONTROL_H

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

#include <ir_Daikin.h>
#include <ir_Mitsubishi.h>
#include <ir_Panasonic.h>
#include <ir_Gree.h>
#include <ir_Midea.h>
#include <ir_Haier.h>
#include <ir_Samsung.h>
#include <ir_LG.h>
#include <ir_Fujitsu.h>
#include <ir_Hitachi.h>

#include "config.h"

extern IRsend irsend;
extern IRrecv irrecv;
extern decode_results results;

void initIR();
void sendACState(const ACState& state);
const char* getBrandName(ACBrand brand);

#endif
