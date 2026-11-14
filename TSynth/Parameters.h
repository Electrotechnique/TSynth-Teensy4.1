#pragma once
#include <stdint.h>
#include <MIDI.h>

// Globals used for OLED Settings
extern byte midiChannel;
extern byte midiOutCh;
extern midi::Thru::Mode MIDIThru;
extern String patchName;
extern boolean encCW;
extern boolean vuMeter;

// Global patch modifiers
extern float lfoSyncFreq;
extern long midiClkTimeInterval;
extern float lfoTempoValue;
extern int pitchBendRange;
extern float modWheelDepth;
extern String oscLFOTimeDivStr;
extern int velocitySens;
// Exponential envelopes
extern int8_t envTypeAmp;
extern int8_t envTypeFilt;
//Glide shape
extern int8_t glideShape;

//Pick-up - Experimental feature
//Control will only start changing when the Knob/MIDI control reaches the current parameter value
//Prevents jumps in value when the patch parameter and control are different values
extern boolean pickUp;
extern boolean pickUpActive;
// Knob pick-up, see PickUp.h
extern int8_t pickupDirection;
void pickupRearmAll();
void pickupReleaseAll();
