/*
   MIT License

  Copyright (c) 2020-21 ElectroTechnique

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ElectroTechnique TSynth - Firmware Rev 2.33
  TEENSY 4.1 - 12 VOICES

  Arduino IDE Tools Settings:
    Board: "Teensy4.1"
    USB Type: "Serial + MIDI + Audio"
    CPU Speed: "600MHz"
    Optimize: "Faster"

  Performance Tests   Max CPU  Mem
  600MHz Faster        80+     59

  Includes code by:
    Dave Benn - Handling MUXs, a few other bits and original inspiration  https://www.notesandvolts.com/2019/01/teensy-synth-part-10-hardware.html
    Alexander Davis / Vince R. Pearson - Stereo ensemble chorus effect https://github.com/quarterturn/teensy3-ensemble-chorus
    Will Winder - Major refactoring and monophonic mode
    Vince R. Pearson - Exponential envelopes & glide
    Github members fab672000 & CDW2000 - General improvements to code
    Mark Tillotson - Special thanks for band-limiting the waveforms in the Audio Library

  Additional libraries:
    Agileware CircularBuffer, Adafruit_GFX (available in Arduino libraries manager)
*/
#include <vector>
#include "Audio.h" //Using local version to override Teensyduino version
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <MIDI.h>
#include <USBHost_t36.h>
#include <TeensyThreads.h>
#include "ThreadSafety.h"
#include "MidiCC.h"
#include "SettingsService.h"
#include "AudioPatching.h"
#include "Constants.h"
#include "Parameters.h"
#include "PatchMgr.h"
#include "HWControls.h"
#include "EepromMgr.h"
#include "Detune.h"
#include "utils.h"
#include "Voice.h"
#include "VoiceGroup.h"

#define PARAMETER 0     // The main page for displaying the current patch and control (parameter) changes
#define RECALL 1        // Patches list
#define SAVE 2          // Save patch page
#define REINITIALISE 3  // Reinitialise message
#define PATCH 4         // Show current patch bypassing PARAMETER
#define PATCHNAMING 5   // Patch naming page
#define DELETE 6        // Delete patch page
#define DELETEMSG 7     // Delete patch message page
#define SETTINGS 8      // Settings page
#define SETTINGSVALUE 9 // Settings page

uint32_t state = PARAMETER;

// Initialize the audio configuration.
Global global{VOICEMIXERLEVEL};
// VoiceGroup voices1{global.SharedAudio[0]};
std::vector<VoiceGroup *> groupvec;
uint8_t activeGroupIndex = 0;
uint8_t loopCount = 0;
#define MIDI_READS_PER_LOOP 16

boolean usbHostPluggedIn = false;
elapsedMillis usb_host_wait_timer;

#include "ST7735Display.h"

// USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);
// MIDIDevice_BigBuffer midi1(myusb); // Try this if your MIDI Compliant controller has problems

// MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

void changeMIDIThruMode()
{
  MIDI.turnThruOn(MIDIThru);
}

#include "Settings.h"

boolean cardStatus = false;
boolean firstPatchLoaded = false;

float previousMillis = millis(); // For MIDI Clk Sync

uint8_t count = 0;            // For MIDI Clk Sync
uint16_t patchNo = 1;         // Current patch no
long earliestTime = millis(); // For voice allocation - initialise to now

FLASHMEM void setup()
{
  // Initialize the voice groups.
  uint8_t total = 0;
  while (total < global.maxVoices())
  {
    VoiceGroup *currentGroup = new VoiceGroup{global.SharedAudio[groupvec.size()]};

    for (uint8_t i = 0; total < global.maxVoices() && i < global.maxVoicesPerGroup(); i++)
    {
      Voice *v = new Voice(global.Oscillators[i], i);
      currentGroup->add(v);
      total++;
    }

    groupvec.push_back(currentGroup);
  }

  setupDisplay();
  setUpSettings();
  setupHardware();

  AudioMemory(60);
  global.sgtl5000_1.enable();
  global.sgtl5000_1.volume(0.5 * SGTL_MAXVOLUME);
  global.sgtl5000_1.dacVolumeRamp();
  global.sgtl5000_1.muteHeadphone();
  global.sgtl5000_1.muteLineout();
  global.sgtl5000_1.audioPostProcessorEnable();
  global.sgtl5000_1.enhanceBass(0.85, 0.87, 0, 4); // Normal level, bass level, HPF bypass (1 - on), bass cutoff freq
  global.sgtl5000_1.enhanceBassDisable();          // Turned on from EEPROM

  cardStatus = SD.begin(BUILTIN_SDCARD);
  if (cardStatus)
  {
    Serial.println(F("SD card is connected"));
    // Get patch numbers and names from SD card
    loadPatches();
    if (patches.size() == 0)
    {
      // save an initialised patch to SD card
      savePatch("1", INITPATCH);
      loadPatches();
    }
  }
  else
  {
    Serial.println(F("SD card is not connected or unusable"));
    reinitialiseToPanel();
    showPatchPage(F("No SD"), F("conn'd / usable"));
  }

  // Read MIDI Channel from EEPROM
  midiChannel = getMIDIChannel();
  Serial.println(F("MIDI In Ch:") + String(midiChannel) + F(" (0 is Omni On)"));

  // USB HOST MIDI Class Compliant
  delay(200); // Wait to turn on USB Host
  myusb.begin();
  midi1.setHandleControlChange(usbHostControlChange);
  midi1.setHandleNoteOff(myNoteOff);
  midi1.setHandleNoteOn(myNoteOn);
  midi1.setHandlePitchChange(myPitchBend);
  midi1.setHandleProgramChange(myProgramChange);
  midi1.setHandleClock(myMIDIClock);
  midi1.setHandleStart(myMIDIClockStart);
  midi1.setHandleStop(myMIDIClockStop);
  Serial.println(F("USB HOST MIDI Class Compliant Listening"));

  // USB Client MIDI
  usbMIDI.setHandleControlChange(myControlChange);
  usbMIDI.setHandleNoteOff(myNoteOff);
  usbMIDI.setHandleNoteOn(myNoteOn);
  usbMIDI.setHandlePitchChange(myPitchBend);
  usbMIDI.setHandleProgramChange(myProgramChange);
  usbMIDI.setHandleClock(myMIDIClock);
  usbMIDI.setHandleStart(myMIDIClockStart);
  usbMIDI.setHandleStop(myMIDIClockStop);
  Serial.println(F("USB Client MIDI Listening"));

  // MIDI 5 Pin DIN
  MIDI.begin();
  MIDI.setHandleNoteOn(myNoteOn);
  MIDI.setHandleNoteOff(myNoteOff);
  MIDI.setHandlePitchBend(myPitchBend);
  MIDI.setHandleControlChange(myControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  MIDI.setHandleClock(myMIDIClock);
  MIDI.setHandleStart(myMIDIClockStart);
  MIDI.setHandleStop(myMIDIClockStop);
  Serial.println(F("MIDI In DIN Listening"));

  volumePrevious = RE_READ; // Force volume control to be read and set to current

  // Read Pitch Bend Range from EEPROM
  pitchBendRange = getPitchBendRange();
  // Read Mod Wheel Depth from EEPROM
  modWheelDepth = getModWheelDepth();
  // Read MIDI Out Channel from EEPROM
  midiOutCh = getMIDIOutCh();
  // Read MIDI Thru mode from EEPROM
  MIDIThru = getMidiThru();
  changeMIDIThruMode();
  // Read Encoder Direction from EEPROM
  encCW = getEncoderDir();
  // Read Pick-up enable from EEPROM - experimental feature
  pickUp = getPickupEnable();
  // Read bass enhance enable from EEPROM
  if (getBassEnhanceEnable())
    global.sgtl5000_1.enhanceBassEnable();
  // Read oscilloscope enable from EEPROM
  enableScope(getScopeEnable());
  // Read VU enable from EEPROM
  vuMeter = getVUEnable();
  // Read Filter and Amp Envelope shapes
  reloadFiltEnv();
  reloadAmpEnv();
  reloadGlideShape();

  // If the audio processing ever overruns, drop a voice rather than hang
  global.cpuGuard.onOverload = []() { groupvec[activeGroupIndex]->shedVoice(); };
}

// Set while flushing MIDI input on Panic: incoming messages are read and dropped
volatile boolean midiDiscard = false;

void myNoteOn(byte channel, byte note, byte velocity)
{
  if (midiDiscard)
    return;
  // Check for out of range notes
  if (note + groupvec[activeGroupIndex]->params().oscPitchA < 0 || note + groupvec[activeGroupIndex]->params().oscPitchA > 127 || note + groupvec[activeGroupIndex]->params().oscPitchB < 0 || note + groupvec[activeGroupIndex]->params().oscPitchB > 127)
    return;

  // Apply envelope triggers and pitch changes within the same audio block
  AudioNoInterrupts();
  groupvec[activeGroupIndex]->noteOn(note, velocity);
  AudioInterrupts();
}

void myNoteOff(byte channel, byte note, byte velocity)
{
  if (midiDiscard)
    return;
  AudioNoInterrupts();
  groupvec[activeGroupIndex]->noteOff(note);
  AudioInterrupts();
}

int getLFOWaveform(int value)
{
  if (value >= 0 && value < 8)
  {
    return WAVEFORM_SINE;
  }
  else if (value >= 8 && value < 30)
  {
    return WAVEFORM_TRIANGLE;
  }
  else if (value >= 30 && value < 63)
  {
    return WAVEFORM_SAWTOOTH_REVERSE;
  }
  else if (value >= 63 && value < 92)
  {
    return WAVEFORM_SAWTOOTH;
  }
  else if (value >= 92 && value < 111)
  {
    return WAVEFORM_SQUARE;
  }
  else
  {
    return WAVEFORM_SAMPLE_HOLD;
  }
}

FLASHMEM String getWaveformStr(int value)
{
  switch (value)
  {
  case WAVEFORM_SILENT:
    return F("Off");
  case WAVEFORM_SAMPLE_HOLD:
    return F("Sample & Hold");
  case WAVEFORM_SINE:
    return F("Sine");
  case WAVEFORM_BANDLIMIT_SQUARE:
  case WAVEFORM_SQUARE:
    return F("Square");
  case WAVEFORM_TRIANGLE:
    return F("Triangle");
  case WAVEFORM_BANDLIMIT_SAWTOOTH:
  case WAVEFORM_SAWTOOTH:
    return F("Sawtooth");
  case WAVEFORM_SAWTOOTH_REVERSE:
    return F("Ramp");
  case WAVEFORM_BANDLIMIT_PULSE:
    return F("Var. Pulse");
  case WAVEFORM_TRIANGLE_VARIABLE:
    return F("Var. Triangle");
  case WAVEFORM_PARABOLIC:
    return F("Parabolic");
  case WAVEFORM_HARMONIC:
    return F("Harmonic");
  default:
    return F("ERR_WAVE");
  }
}

FLASHMEM int getWaveformA(int value)
{
  if (value >= 0 && value < 7)
  {
    // This will turn the osc off
    return WAVEFORM_SILENT;
  }
  else if (value >= 7 && value < 23)
  {
    return WAVEFORM_TRIANGLE;
  }
  else if (value >= 23 && value < 40)
  {
    return WAVEFORM_BANDLIMIT_SQUARE;
  }
  else if (value >= 40 && value < 60)
  {
    return WAVEFORM_BANDLIMIT_SAWTOOTH;
  }
  else if (value >= 60 && value < 80)
  {
    return WAVEFORM_BANDLIMIT_PULSE;
  }
  else if (value >= 80 && value < 100)
  {
    return WAVEFORM_TRIANGLE_VARIABLE;
  }
  else if (value >= 100 && value < 120)
  {
    return WAVEFORM_PARABOLIC;
  }
  else
  {
    return WAVEFORM_HARMONIC;
  }
}

FLASHMEM int getWaveformB(int value)
{
  if (value >= 0 && value < 7)
  {
    // This will turn the osc off
    return WAVEFORM_SILENT;
  }
  else if (value >= 7 && value < 23)
  {
    return WAVEFORM_SAMPLE_HOLD;
  }
  else if (value >= 23 && value < 40)
  {
    return WAVEFORM_BANDLIMIT_SQUARE;
  }
  else if (value >= 40 && value < 60)
  {
    return WAVEFORM_BANDLIMIT_SAWTOOTH;
  }
  else if (value >= 60 && value < 80)
  {
    return WAVEFORM_BANDLIMIT_PULSE;
  }
  else if (value >= 80 && value < 100)
  {
    return WAVEFORM_TRIANGLE_VARIABLE;
  }
  else if (value >= 100 && value < 120)
  {
    return WAVEFORM_PARABOLIC;
  }
  else
  {
    return WAVEFORM_HARMONIC;
  }
}

FLASHMEM void updateUnison(uint8_t unison)
{
  groupvec[activeGroupIndex]->setUnisonMode(unison);

  if (unison == 0)
  {
    showCurrentParameterPage(F("Unison"), F("Off"));
    pinMode(UNISON_LED, OUTPUT);
    digitalWriteFast(UNISON_LED, LOW); // LED off
  }
  else if (unison == 1)
  {
    showCurrentParameterPage(F("Dyn. Unison"), F("On"));
    pinMode(UNISON_LED, OUTPUT);
    digitalWriteFast(UNISON_LED, HIGH); // LED on
  }
  else
  {
    showCurrentParameterPage(F("Chd. Unison"), F("On"));
    analogWriteFrequency(UNISON_LED, 1); // This is to make the LED flash using PWM rather than some thread
    analogWrite(UNISON_LED, 127);
  }
}

// Volume knob taper. The SGTL5000 headphone volume moves in 0.5dB steps, so mapping the knob
// straight onto it (as before) was linear in dB across ~64dB: half way was 29dB down, about
// an eighth as loud. This is an audio taper (amplitude = knob^2): half way is -12dB, about
// half as loud, quarter way -24dB. The maximum is unchanged.
#define VOLUME_MAX_DB 6.0f    // codec headphone gain at full volume (as with volume 0.9 before)
#define VOLUME_TAPER_DB 40.0f // dB per decade of knob travel

FLASHMEM void setOutputVolume(float vol)
{
  // Headphone volume register: 0x00 = +12dB ... 0x7F = -51.5dB; volume(n) writes 0x80 - n*129
  float reg = vol > 0.0f ? (12.0f - (VOLUME_MAX_DB + VOLUME_TAPER_DB * log10f(vol))) * 2.0f : 1000.0f;
  if (reg > 127.0f)
  {
    global.sgtl5000_1.volume(0.0f); // below the codec's range: mute
    return;
  }
  if (reg < 0.0f)
    reg = 0.0f;
  int n = 128 - (int)(reg + 0.5f);
  global.sgtl5000_1.volume(n / 129.0f);
}

FLASHMEM void updateVolume(float vol)
{
  setOutputVolume(vol);
  showCurrentParameterPage(F("Volume"), vol);
}

FLASHMEM void updateGlide(float glideSpeed)
{
  groupvec[activeGroupIndex]->params().glideSpeed = glideSpeed;
  showCurrentParameterPage(F("Glide"), milliToString(glideSpeed * GLIDEFACTOR));
}

FLASHMEM void updateWaveformA(uint32_t waveform)
{
  groupvec[activeGroupIndex]->setWaveformA(waveform);
  showCurrentParameterPage(F("1. Waveform"), getWaveformStr(waveform));
}

FLASHMEM void updateWaveformB(uint32_t waveform)
{
  groupvec[activeGroupIndex]->setWaveformB(waveform);
  showCurrentParameterPage(F("2. Waveform"), getWaveformStr(waveform));
}

FLASHMEM void updatePitchA(int pitch)
{
  groupvec[activeGroupIndex]->params().oscPitchA = pitch;
  groupvec[activeGroupIndex]->updateVoices();
  showCurrentParameterPage("1. Semitones", (pitch > 0 ? "+" : "") + String(pitch));
}

FLASHMEM void updatePitchB(int pitch)
{
  groupvec[activeGroupIndex]->params().oscPitchB = pitch;
  groupvec[activeGroupIndex]->updateVoices();
  showCurrentParameterPage(F("2. Semitones"), (pitch > 0 ? "+" : "") + String(pitch));
}

FLASHMEM void updateDetune(float detune, uint32_t chordDetune)
{
  groupvec[activeGroupIndex]->params().detune = detune;
  groupvec[activeGroupIndex]->params().chordDetune = chordDetune;
  groupvec[activeGroupIndex]->updateVoices();

  if (groupvec[activeGroupIndex]->params().unisonMode == 2)
  {
    showCurrentParameterPage(F("Chord"), CDT_STR[chordDetune]);
  }
  else
  {
    showCurrentParameterPage(F("Detune"), String((1 - detune) * 100) + F(" %"));
  }
}

FLASHMEM void updatePWMSource(uint8_t source)
{
  groupvec[activeGroupIndex]->setPWMSource(source);

  if (source == PWMSOURCELFO)
  {
    showCurrentParameterPage(F("PWM Source"), F("LFO")); // Only shown when updated via MIDI
  }
  else
  {
    showCurrentParameterPage(F("PWM Source"), F("Filter Env"));
  }
}

FLASHMEM void updatePWMRate(float value)
{
  groupvec[activeGroupIndex]->setPwmRate(value);

  if (value == PWMRATE_PW_MODE)
  {
    // Set to fixed PW mode
    showCurrentParameterPage(F("PW Mode"), F("On"));
  }
  else if (value == PWMRATE_SOURCE_FILTER_ENV)
  {
    // Set to Filter Env Mod source
    showCurrentParameterPage(F("PWM Source"), F("Filter Env"));
  }
  else
  {
    showCurrentParameterPage(F("PWM Rate"), String(2 * value) + F(" Hz")); // PWM goes through mid to maximum, sounding effectively twice as fast
  }
}

FLASHMEM void updatePWMAmount(float value)
{
  // MIDI only - sets both osc PWM
  groupvec[activeGroupIndex]->overridePwmAmount(value);
  showCurrentParameterPage(F("PWM Amt"), String(value) + F(" : ") + String(value));
}

FLASHMEM void updatePWA(float valuePwA, float valuePwmAmtA)
{
  groupvec[activeGroupIndex]->setPWA(valuePwA, valuePwmAmtA);

  if (groupvec[activeGroupIndex]->getPwmRate() == PWMRATE_PW_MODE)
  {
    if (groupvec[activeGroupIndex]->getWaveformA() == WAVEFORM_TRIANGLE_VARIABLE)
    {
      showCurrentParameterPage(F("1. PW Amt"), VAR_TRI, groupvec[activeGroupIndex]->getPwA());
    }
    else
    {
      showCurrentParameterPage(F("1. PW Amt"), PULSE, groupvec[activeGroupIndex]->getPwA());
    }
  }
  else
  {
    if (groupvec[activeGroupIndex]->getPwmSource() == PWMSOURCELFO)
    {
      // PW alters PWM LFO amount for waveform A
      showCurrentParameterPage(F("1. PWM Amt"), F("LFO ") + String(groupvec[activeGroupIndex]->getPwmAmtA()));
    }
    else
    {
      // PW alters PWM Filter Env amount for waveform A
      showCurrentParameterPage(F("1. PWM Amt"), F("F. Env ") + String(groupvec[activeGroupIndex]->getPwmAmtA()));
    }
  }
}

FLASHMEM void updatePWB(float valuePwB, float valuePwmAmtB)
{
  groupvec[activeGroupIndex]->setPWB(valuePwB, valuePwmAmtB);

  if (groupvec[activeGroupIndex]->getPwmRate() == PWMRATE_PW_MODE)
  {
    if (groupvec[activeGroupIndex]->getWaveformB() == WAVEFORM_TRIANGLE_VARIABLE)
    {
      showCurrentParameterPage(F("2. PW Amt"), VAR_TRI, groupvec[activeGroupIndex]->getPwB());
    }
    else
    {
      showCurrentParameterPage(F("2. PW Amt"), PULSE, groupvec[activeGroupIndex]->getPwB());
    }
  }
  else
  {
    if (groupvec[activeGroupIndex]->getPwmSource() == PWMSOURCELFO)
    {
      // PW alters PWM LFO amount for waveform B
      showCurrentParameterPage(F("2. PWM Amt"), F("LFO ") + String(groupvec[activeGroupIndex]->getPwmAmtB()));
    }
    else
    {
      // PW alters PWM Filter Env amount for waveform B
      showCurrentParameterPage(F("2. PWM Amt"), F("F. Env ") + String(groupvec[activeGroupIndex]->getPwmAmtB()));
    }
  }
}

FLASHMEM void updateOscLevelA(float value)
{
  groupvec[activeGroupIndex]->setOscLevelA(value);

  switch (groupvec[activeGroupIndex]->getOscFX())
  {
  case 1: // XOR
    showCurrentParameterPage(F("Osc Mix 1:2"), F("   ") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(" : ") + String(groupvec[activeGroupIndex]->getOscLevelB()));
    break;
  case 2: // XMod
    // osc A sounds with increasing osc B mod
    if (groupvec[activeGroupIndex]->getOscLevelA() == 1.0f && groupvec[activeGroupIndex]->getOscLevelB() <= 1.0f)
    {
      showCurrentParameterPage(F("XMod Osc 1"), F("Osc 2: ") + String(1 - groupvec[activeGroupIndex]->getOscLevelB()));
    }
    break;
  case 0: // None
    showCurrentParameterPage(F("Osc Mix 1:2"), F("   ") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(" : ") + String(groupvec[activeGroupIndex]->getOscLevelB()));
    break;
  }
}

FLASHMEM void updateOscLevelB(float value)
{
  groupvec[activeGroupIndex]->setOscLevelB(value);

  switch (groupvec[activeGroupIndex]->getOscFX())
  {
  case 1: // XOR
    showCurrentParameterPage(F("Osc Mix 1:2"), F("   ") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(" : ") + String(groupvec[activeGroupIndex]->getOscLevelB()));
    break;
  case 2: // XMod
    // osc B sounds with increasing osc A mod
    if (groupvec[activeGroupIndex]->getOscLevelB() == 1.0f && groupvec[activeGroupIndex]->getOscLevelA() < 1.0f)
    {
      showCurrentParameterPage(F("XMod Osc 2"), F("Osc 1: ") + String(1 - groupvec[activeGroupIndex]->getOscLevelA()));
    }
    break;
  case 0: // None
    showCurrentParameterPage(F("Osc Mix 1:2"), F("   ") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(" : ") + String(groupvec[activeGroupIndex]->getOscLevelB()));
    break;
  }
}

FLASHMEM void updateNoiseLevel(float value)
{
  float pink = 0.0;
  float white = 0.0;
  if (value > 0)
  {
    pink = value;
  }
  else if (value < 0)
  {
    white = abs(value);
  }

  groupvec[activeGroupIndex]->setPinkNoiseLevel(pink);
  groupvec[activeGroupIndex]->setWhiteNoiseLevel(white);

  if (value > 0)
  {
    showCurrentParameterPage(F("Noise Level"), F("Pink ") + String(value));
  }
  else if (value < 0)
  {
    showCurrentParameterPage(F("Noise Level"), F("White ") + String(abs(value)));
  }
  else
  {
    showCurrentParameterPage(F("Noise Level"), F("Off"));
  }
}

FLASHMEM void updateFilterFreq(float value)
{
  groupvec[activeGroupIndex]->setCutoff(value);
  showCurrentParameterPage(F("Cutoff"), String(int(value)) + F(" Hz"));
}

FLASHMEM void updateFilterRes(float value)
{
  groupvec[activeGroupIndex]->setResonance(value);
  showCurrentParameterPage(F("Resonance"), value);
}

FLASHMEM void updateFilterMixer(float value)
{
  groupvec[activeGroupIndex]->setFilterMixer(value);

  String filterStr;
  if (value == BANDPASS || value >= 2.0f)
  {
    filterStr = F("Band Pass");
  }
  else if (value > 1.0f)
  {
    filterStr = F("HP ") + String(100 - int(100 * (value - 1.0f) + 0.5f)) + F(" - ") + String(int(100 * (value - 1.0f) + 0.5f)) + F(" BP");
  }
  else
  {
    // LP-HP mix mode - a notch filter
    if (value == LOWPASS)
    {
      filterStr = F("Low Pass");
    }
    else if (value == HIGHPASS)
    {
      filterStr = F("High Pass");
    }
    else
    {
      filterStr = F("LP ") + String(100 - int(100 * value)) + F(" - ") + String(int(100 * value)) + F(" HP");
    }
  }

  showCurrentParameterPage(F("Filter Type"), filterStr);
}

FLASHMEM void updateFilterEnv(float value)
{
  groupvec[activeGroupIndex]->setFilterEnvelope(value);
  showCurrentParameterPage(F("Filter Env."), String(value));
}

FLASHMEM void updatePitchEnv(float value)
{
  groupvec[activeGroupIndex]->setPitchEnvelope(value);
  showCurrentParameterPage(F("Pitch Env Amt"), String(value));
}

FLASHMEM void updateKeyTracking(float value)
{
  groupvec[activeGroupIndex]->setKeytracking(value);
  showCurrentParameterPage(F("Key Tracking"), String(value));
}

FLASHMEM void updatePitchLFOAmt(float value)
{
  groupvec[activeGroupIndex]->setPitchLfoAmount(value);
  char buf[10];
  showCurrentParameterPage(F("LFO Amount"), dtostrf(value, 4, 3, buf));
}

FLASHMEM void updateModWheel(float value)
{
  groupvec[activeGroupIndex]->setModWhAmount(value);
}

FLASHMEM void updatePitchLFORate(float value)
{
  groupvec[activeGroupIndex]->setPitchLfoRate(value);
  showCurrentParameterPage(F("LFO Rate"), String(value) + F(" Hz"));
}

FLASHMEM void updatePitchLFOWaveform(uint32_t waveform)
{
  groupvec[activeGroupIndex]->setPitchLfoWaveform(waveform);
  showCurrentParameterPage(F("Pitch LFO"), getWaveformStr(waveform));
}

// MIDI CC only
FLASHMEM void updatePitchLFOMidiClkSync(bool value)
{
  groupvec[activeGroupIndex]->setPitchLfoMidiClockSync(value);
  showCurrentParameterPage(F("P. LFO Sync"), value ? F("On") : F("Off"));
}

FLASHMEM void updateFilterLfoRate(float value, String timeDivStr)
{
  groupvec[activeGroupIndex]->setFilterLfoRate(value);

  if (timeDivStr.length() > 0)
  {
    showCurrentParameterPage(F("LFO Time Div"), timeDivStr);
  }
  else
  {
    showCurrentParameterPage(F("F. LFO Rate"), String(value) + F(" Hz"));
  }
}

FLASHMEM void updateFilterLfoAmt(float value)
{
  groupvec[activeGroupIndex]->setFilterLfoAmt(value);
  showCurrentParameterPage(F("F. LFO Amt"), String(value));
}

FLASHMEM void updateFilterLFOWaveform(uint32_t waveform)
{
  groupvec[activeGroupIndex]->setFilterLfoWaveform(waveform);
  showCurrentParameterPage(F("Filter LFO"), getWaveformStr(waveform));
}

FLASHMEM void updatePitchLFORetrig(bool value)
{
  groupvec[activeGroupIndex]->setPitchLfoRetrig(value);
  showCurrentParameterPage(F("P. LFO Retrig"), value ? F("On") : F("Off"));
}

FLASHMEM void updateFilterLFORetrig(bool value)
{
  groupvec[activeGroupIndex]->setFilterLfoRetrig(value);
  showCurrentParameterPage(F("F. LFO Retrig"), groupvec[activeGroupIndex]->getFilterLfoRetrig() ? F("On") : F("Off"));
  digitalWriteFast(RETRIG_LED, groupvec[activeGroupIndex]->getFilterLfoRetrig() ? HIGH : LOW); // LED
}

FLASHMEM void updateFilterLFOMidiClkSync(bool value)
{
  groupvec[activeGroupIndex]->setFilterLfoMidiClockSync(value);
  showCurrentParameterPage(F("Tempo Sync"), value ? F("On") : F("Off"));
  digitalWriteFast(TEMPO_LED, value ? HIGH : LOW); // LED
}

FLASHMEM void updateFilterAttack(float value)
{
  groupvec[activeGroupIndex]->setFilterAttack(value);
  showCurrentParameterPage(F("Filter Attack"), milliToString(value), FILTER_ENV);
}

FLASHMEM void updateFilterDecay(float value)
{
  groupvec[activeGroupIndex]->setFilterDecay(value);
  showCurrentParameterPage("Filter Decay", milliToString(value), FILTER_ENV);
}

FLASHMEM void updateFilterSustain(float value)
{
  groupvec[activeGroupIndex]->setFilterSustain(value);
  showCurrentParameterPage(F("Filter Sustain"), String(value), FILTER_ENV);
}

FLASHMEM void updateFilterRelease(float value)
{
  groupvec[activeGroupIndex]->setFilterRelease(value);
  showCurrentParameterPage(F("Filter Release"), milliToString(value), FILTER_ENV);
}

FLASHMEM void updateAttack(float value)
{
  groupvec[activeGroupIndex]->setAmpAttack(value);
  showCurrentParameterPage(F("Attack"), milliToString(value), AMP_ENV);
}

FLASHMEM void updateDecay(float value)
{
  groupvec[activeGroupIndex]->setAmpDecay(value);
  showCurrentParameterPage(F("Decay"), milliToString(value), AMP_ENV);
}

FLASHMEM void updateSustain(float value)
{
  groupvec[activeGroupIndex]->setAmpSustain(value);
  showCurrentParameterPage(F("Sustain"), String(value), AMP_ENV);
}

FLASHMEM void updateRelease(float value)
{
  groupvec[activeGroupIndex]->setAmpRelease(value);
  showCurrentParameterPage(F("Release"), milliToString(value), AMP_ENV);
}

FLASHMEM void updateOscFX(uint8_t value)
{
  groupvec[activeGroupIndex]->setOscFX(value);
  if (value == 2)
  {
    showCurrentParameterPage(F("Osc FX"), F("On - X Mod"));
    analogWriteFrequency(OSC_FX_LED, 1); // This is to make the LED flash using PWM rather than some thread
    analogWrite(OSC_FX_LED, 127);
  }
  else if (value == 1)
  {
    showCurrentParameterPage(F("Osc FX"), F("On - XOR"));
    pinMode(OSC_FX_LED, OUTPUT);
    digitalWriteFast(OSC_FX_LED, HIGH); // LED on
  }
  else
  {
    showCurrentParameterPage(F("Osc FX"), F("Off"));
    pinMode(OSC_FX_LED, OUTPUT);
    digitalWriteFast(OSC_FX_LED, LOW); // LED off
  }
}

FLASHMEM void updateEffectAmt(float value)
{
  groupvec[activeGroupIndex]->setEffectAmount(value);
  showCurrentParameterPage(F("Effect Amt"), String(value) + F(" Hz"));
}

FLASHMEM void updateEffectMix(float value)
{
  groupvec[activeGroupIndex]->setEffectMix(value);
  showCurrentParameterPage(F("Effect Mix"), String(value));
}

FLASHMEM void updatePatch(String name, uint32_t index)
{
  groupvec[activeGroupIndex]->setPatchName(name);
  groupvec[activeGroupIndex]->setPatchIndex(index);
  showPatchPage(String(index), name);
}

void myPitchBend(byte channel, int bend)
{
  if (midiDiscard)
    return;
  // 0.5 to give 1oct max - spread of mod is 2oct
  groupvec[activeGroupIndex]->pitchBend(bend * 0.5f * pitchBendRange * DIV12 * DIV8192);
}

#include "PickUp.h"

void myControlChange(byte channel, byte control, byte value)
{
  if (midiDiscard)
    return;
  if (!controlFromPanel)
  {
    // Changed by MIDI: the knob no longer matches, so it has to pick up again. MIDI is never gated.
    pickupRearmCC(control);
    pickupDirection = 0;
  }
  switch (control)
  {
  case CCvolume:
    updateVolume(LINEAR[value]);
    break;
  case CCunison:
    updateUnison(inRangeOrDefault<int>(value, 2, 0, 2));
    break;

  case CCglide:
    updateGlide(POWER[value]);
    break;

  case CCpitchenv:
    updatePitchEnv(LINEARCENTREZERO[value] * OSCMODMIXERMAX);
    break;

  case CCoscwaveformA:
    updateWaveformA(getWaveformA(value));
    break;

  case CCoscwaveformB:
    updateWaveformB(getWaveformB(value));
    break;

  case CCpitchA:
    updatePitchA(PITCH[value]);
    break;

  case CCpitchB:
    updatePitchB(PITCH[value]);
    break;

  case CCdetune:
    updateDetune(1.0f - (MAXDETUNE * POWER[value]), value);
    break;

  case CCpwmSource:
    updatePWMSource(value > 0 ? PWMSOURCEFENV : PWMSOURCELFO);
    break;

  case CCpwmRate:
    // Uses combination of PWMRate, PWa and PWb
    updatePWMRate(PWMRATE[value]);
    break;

  case CCpwmAmt:
    // NO FRONT PANEL CONTROL - MIDI CC ONLY
    // Total PWM amount for both oscillators
    updatePWMAmount(LINEAR[value]);
    break;

  case CCpwA:
    updatePWA(LINEARCENTREZERO[value], LINEAR[value]);
    break;

  case CCpwB:
    updatePWB(LINEARCENTREZERO[value], LINEAR[value]);
    break;

  case CCoscLevelA:
    updateOscLevelA(LINEAR[value]);
    break;

  case CCoscLevelB:
    updateOscLevelB(LINEAR[value]);
    break;

  case CCnoiseLevel:
    updateNoiseLevel(LINEARCENTREZERO[value]);
    break;

  case CCfilterfreq:

    // MIDI is 7 bit, 128 values and needs to choose alternate filterfreqs(8 bit) by multiplying by 2
    updateFilterFreq(FILTERFREQS256[value * 2]);
    break;

  case CCfilterres:

    // If <1.1 there is noise at high cutoff freq
    updateFilterRes((14.29f * POWER[value]) + 0.71f);
    break;

  case CCfiltermixer:

    updateFilterMixer(LINEAR_FILTERMIXER[value]);
    break;

  case CCfilterenv:
    updateFilterEnv(LINEARCENTREZERO[value] * FILTERMODMIXERMAX);
    break;

  case CCkeytracking:
    updateKeyTracking(KEYTRACKINGAMT[value]);
    break;

  case CCmodwheel:
    // Variable LFO amount from mod wheel - Settings Option
    updateModWheel(POWER[value] * modWheelDepth);
    break;

  case CCosclfoamt:

    updatePitchLFOAmt(POWER[value]);
    break;

  case CCoscLfoRate:
  {

    float rate = 0.0;
    if (groupvec[activeGroupIndex]->getPitchLfoMidiClockSync())
    {
      // TODO: MIDI Tempo stuff remains global?
      lfoTempoValue = LFOTEMPO[value];
      oscLFOTimeDivStr = LFOTEMPOSTR[value];
      rate = lfoSyncFreq * LFOTEMPO[value];
    }
    else
    {
      rate = LFOMAXRATE * POWER[value];
    }
    updatePitchLFORate(rate);
    break;
  }

  case CCoscLfoWaveform:
    updatePitchLFOWaveform(getLFOWaveform(value));
    break;

  case CCosclforetrig:
    updatePitchLFORetrig(value > 0);
    break;

  case CCfilterLFOMidiClkSync:
    updateFilterLFOMidiClkSync(value > 0);
    break;

  case CCfilterlforate:
  {

    float rate;
    String timeDivStr = "";
    if (groupvec[activeGroupIndex]->getFilterLfoMidiClockSync())
    {
      lfoTempoValue = LFOTEMPO[value];
      rate = lfoSyncFreq * LFOTEMPO[value];
      timeDivStr = LFOTEMPOSTR[value];
    }
    else
    {
      rate = LFOMAXRATE * POWER[value];
    }

    updateFilterLfoRate(rate, timeDivStr);
    break;
  }

  case CCfilterlfoamt:

    updateFilterLfoAmt(LINEAR[value] * FILTERMODMIXERMAX);
    break;

  case CCfilterlfowaveform:
    updateFilterLFOWaveform(getLFOWaveform(value));
    break;

  case CCfilterlforetrig:
    updateFilterLFORetrig(value > 0);
    break;

  // MIDI Only
  case CCoscLFOMidiClkSync:
    updatePitchLFOMidiClkSync(value > 0);
    break;

  case CCfilterattack:
    updateFilterAttack(ENVTIMES[value]);
    break;

  case CCfilterdecay:
    updateFilterDecay(ENVTIMES[value]);
    break;

  case CCfiltersustain:
    updateFilterSustain(LINEAR[value]);
    break;

  case CCfilterrelease:
    updateFilterRelease(ENVTIMES[value]);
    break;

  case CCampattack:
    updateAttack(ENVTIMES[value]);
    break;

  case CCampdecay:
    updateDecay(ENVTIMES[value]);
    break;

  case CCampsustain:
    updateSustain(LINEAR[value]);
    break;

  case CCamprelease:
    updateRelease(ENVTIMES[value]);
    break;

  case CCoscfx:
    updateOscFX(inRangeOrDefault<int>(value, 2, 0, 2));
    break;

  case CCfxamt:
    updateEffectAmt(ENSEMBLE_LFO[value]);
    break;

  case CCfxmix:
    updateEffectMix(LINEAR[value]);
    break;

  case CCallnotesoff:
    groupvec[activeGroupIndex]->allNotesOff();
    break;
  }
}

FLASHMEM void myProgramChange(byte channel, byte program)
{
  if (midiDiscard)
    return;
  state = PATCH;
  patchNo = program + 1;
  recallPatch(patchNo);
  Serial.print(F("MIDI Pgm Change:"));
  Serial.println(patchNo);
  state = PARAMETER;
}

FLASHMEM void myMIDIClockStart()
{
  if (midiDiscard)
    return;
  setMIDIClkSignal(true);
  // Resync LFOs when MIDI Clock starts.
  // When there's a jump to a different
  // part of a track, such as in a DAW, the DAW must have same
  // rhythmic quantisation as Tempo Div.

  // TODO: Apply to all groupvec[activeGroupIndex]-> Maybe check channel?
  groupvec[activeGroupIndex]->midiClockStart();
}

FLASHMEM void myMIDIClockStop()
{
  if (midiDiscard)
    return;
  setMIDIClkSignal(false);
}

void myMIDIClock()
{
  if (midiDiscard)
    return;
  // This recalculates the LFO frequencies if the tempo changes (MIDI cLock is 24ppq)
  if (count > 23)
  {
    // TODO: Most of this needs to move into the VoiceGroup

    setMIDIClkSignal(!getMIDIClkSignal());
    long timeNow = millis();
    midiClkTimeInterval = (timeNow - previousMillis);
    lfoSyncFreq = 1000.0f / midiClkTimeInterval;
    previousMillis = timeNow;
    groupvec[activeGroupIndex]->midiClock(lfoSyncFreq * lfoTempoValue);
    count = 0;
  }

  count++;
}

FLASHMEM void recallPatch(int patchNo)
{
  LockGuard lock(displayLock); // Don't render a half loaded patch
  groupvec[activeGroupIndex]->allNotesOff();
  groupvec[activeGroupIndex]->closeEnvelopes();
  File patchFile = SD.open(String(patchNo).c_str());
  if (!patchFile)
  {
    Serial.println(F("File not found"));
  }
  else
  {
    String data[NO_OF_PARAMS]; // Array of data read in
    recallPatchData(patchFile, data);
    setCurrentPatchData(data);
    patchFile.close();
  }
}

FLASHMEM void setCurrentPatchData(String data[])
{
  updatePatch(data[0], patchNo);
  updateOscLevelA(data[1].toFloat());
  updateOscLevelB(data[2].toFloat());
  updateNoiseLevel(data[3].toFloat());
  updateUnison(data[4].toInt());
  updateOscFX(data[5].toInt());
  updateDetune(data[6].toFloat(), data[48].toInt());
  // Why is this MIDI Clock stuff part of the patch??
  lfoSyncFreq = data[7].toInt();
  midiClkTimeInterval = data[8].toInt();
  lfoTempoValue = data[9].toFloat();
  updateKeyTracking(data[10].toFloat());
  updateGlide(data[11].toFloat());
  updatePitchA(data[12].toFloat());
  updatePitchB(data[13].toFloat());
  updateWaveformA(data[14].toInt());
  updateWaveformB(data[15].toInt());
  updatePWMSource(data[16].toInt());
  updatePWA(data[20].toFloat(), data[17].toFloat());
  updatePWB(data[21].toFloat(), data[18].toFloat());
  updatePWMRate(data[19].toFloat());
  updateFilterRes(data[22].toFloat());
  updateFilterFreq(data[23].toFloat());
  updateFilterMixer(data[24].toFloat());
  updateFilterEnv(data[25].toFloat());
  updatePitchLFOAmt(data[26].toFloat());
  updatePitchLFORate(data[27].toFloat());
  updatePitchLFOWaveform(data[28].toInt());
  updatePitchLFORetrig(data[29].toInt() > 0);
  updatePitchLFOMidiClkSync(data[30].toInt() > 0); // MIDI CC Only
  updateFilterLfoRate(data[31].toFloat(), "");
  updateFilterLFORetrig(data[32].toInt() > 0);
  updateFilterLFOMidiClkSync(data[33].toInt() > 0);
  updateFilterLfoAmt(data[34].toFloat());
  updateFilterLFOWaveform(data[35].toFloat());
  updateFilterAttack(data[36].toFloat());
  updateFilterDecay(data[37].toFloat());
  updateFilterSustain(data[38].toFloat());
  updateFilterRelease(data[39].toFloat());
  updateAttack(data[40].toFloat());
  updateDecay(data[41].toFloat());
  updateSustain(data[42].toFloat());
  updateRelease(data[43].toFloat());
  updateEffectAmt(data[44].toFloat());
  updateEffectMix(data[45].toFloat());
  updatePitchEnv(data[46].toFloat());
  velocitySens = data[47].toFloat();
  groupvec[activeGroupIndex]->setMonophonic(data[49].toInt());
  pickupRearmAll(); // Knobs pick up the recalled values
  //  SPARE1 = data[50].toFloat();
  //  SPARE2 = data[51].toFloat();

  Serial.print(F("Set Patch: "));
  Serial.println(data[0]);
}

FLASHMEM String getCurrentPatchData()
{
  auto p = groupvec[activeGroupIndex]->params();
  return patchName + F(",") + String(groupvec[activeGroupIndex]->getOscLevelA()) + F(",") + String(groupvec[activeGroupIndex]->getOscLevelB()) + F(",") + String(groupvec[activeGroupIndex]->getPinkNoiseLevel() - groupvec[activeGroupIndex]->getWhiteNoiseLevel()) + F(",") + String(p.unisonMode) + F(",") + String(groupvec[activeGroupIndex]->getOscFX()) + F(",") + String(p.detune, 5) + F(",") + String(lfoSyncFreq) + F(",") + String(midiClkTimeInterval) + F(",") + String(lfoTempoValue) + F(",") + String(groupvec[activeGroupIndex]->getKeytrackingAmount()) + F(",") + String(p.glideSpeed, 5) + F(",") + String(p.oscPitchA) + F(",") + String(p.oscPitchB) + F(",") + String(groupvec[activeGroupIndex]->getWaveformA()) + F(",") + String(groupvec[activeGroupIndex]->getWaveformB()) + F(",") +
         String(groupvec[activeGroupIndex]->getPwmSource()) + F(",") + String(groupvec[activeGroupIndex]->getPwmAmtA()) + F(",") + String(groupvec[activeGroupIndex]->getPwmAmtB()) + F(",") + String(groupvec[activeGroupIndex]->getPwmRate()) + F(",") + String(groupvec[activeGroupIndex]->getPwA()) + F(",") + String(groupvec[activeGroupIndex]->getPwB()) + F(",") + String(groupvec[activeGroupIndex]->getResonance()) + F(",") + String(groupvec[activeGroupIndex]->getCutoff()) + F(",") + String(groupvec[activeGroupIndex]->getFilterMixer()) + F(",") + String(groupvec[activeGroupIndex]->getFilterEnvelope()) + F(",") + String(groupvec[activeGroupIndex]->getPitchLfoAmount(), 5) + F(",") + String(groupvec[activeGroupIndex]->getPitchLfoRate(), 5) + F(",") + String(groupvec[activeGroupIndex]->getPitchLfoWaveform()) + F(",") + String(int(groupvec[activeGroupIndex]->getPitchLfoRetrig())) + F(",") + String(int(groupvec[activeGroupIndex]->getPitchLfoMidiClockSync())) + F(",") + String(groupvec[activeGroupIndex]->getFilterLfoRate(), 5) + F(",") +
         groupvec[activeGroupIndex]->getFilterLfoRetrig() + F(",") + groupvec[activeGroupIndex]->getFilterLfoMidiClockSync() + F(",") + groupvec[activeGroupIndex]->getFilterLfoAmt() + F(",") + groupvec[activeGroupIndex]->getFilterLfoWaveform() + F(",") + groupvec[activeGroupIndex]->getFilterAttack() + F(",") + groupvec[activeGroupIndex]->getFilterDecay() + F(",") + groupvec[activeGroupIndex]->getFilterSustain() + F(",") + groupvec[activeGroupIndex]->getFilterRelease() + F(",") + groupvec[activeGroupIndex]->getAmpAttack() + F(",") + groupvec[activeGroupIndex]->getAmpDecay() + F(",") + groupvec[activeGroupIndex]->getAmpSustain() + F(",") + groupvec[activeGroupIndex]->getAmpRelease() + F(",") +
         String(groupvec[activeGroupIndex]->getEffectAmount()) + F(",") + String(groupvec[activeGroupIndex]->getEffectMix()) + F(",") + String(groupvec[activeGroupIndex]->getPitchEnvelope()) + F(",") + String(velocitySens) + F(",") + String(p.chordDetune) + F(",") + String(groupvec[activeGroupIndex]->getMonophonicMode()) + F(",") + String(0.0f) + F(",") + String(0.0f);
}

void checkMux()
{
  mux1Read = adc->adc1->analogRead(MUX1_S);
  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux1ValuesPrev[muxInput] = mux1Read;
    mux1Read = (mux1Read >> 5); // Change range to 0-127
    if (pickupCheck(PU_KNOB(0, muxInput), mux1Read))
    {
    controlFromPanel = true;
    switch (muxInput)
    {
    case MUX1_noiseLevel:
      midiCCOut(CCnoiseLevel, mux1Read);
      myControlChange(midiChannel, CCnoiseLevel, mux1Read);
      break;
    case MUX1_pitchLfoRate:
      midiCCOut(CCoscLfoRate, mux1Read);
      myControlChange(midiChannel, CCoscLfoRate, mux1Read);
      break;
    case MUX1_pitchLfoWaveform:
      midiCCOut(CCoscLfoWaveform, mux1Read);
      myControlChange(midiChannel, CCoscLfoWaveform, mux1Read);
      break;
    case MUX1_pitchLfoAmount:
      midiCCOut(CCosclfoamt, mux1Read);
      myControlChange(midiChannel, CCosclfoamt, mux1Read);
      break;
    case MUX1_detune:
      midiCCOut(CCdetune, mux1Read);
      myControlChange(midiChannel, CCdetune, mux1Read);
      break;
    case MUX1_oscMix:
      midiCCOut(CCoscLevelA, mux1Read);
      midiCCOut(CCoscLevelB, mux1Read);
      myControlChange(midiChannel, CCoscLevelA, OSCMIXA[mux1Read]);
      myControlChange(midiChannel, CCoscLevelB, OSCMIXB[mux1Read]);
      break;
    case MUX1_filterAttack:
      midiCCOut(CCfilterattack, mux1Read);
      myControlChange(midiChannel, CCfilterattack, mux1Read);
      break;
    case MUX1_filterDecay:
      midiCCOut(CCfilterdecay, mux1Read);
      myControlChange(midiChannel, CCfilterdecay, mux1Read);
      break;
    case MUX1_pwmAmountA:
      midiCCOut(CCpwA, mux1Read);
      myControlChange(midiChannel, CCpwA, mux1Read);
      break;
    case MUX1_waveformA:
      midiCCOut(CCoscwaveformA, mux1Read);
      myControlChange(midiChannel, CCoscwaveformA, mux1Read);
      break;
    case MUX1_pitchA:
      midiCCOut(CCpitchA, mux1Read);
      myControlChange(midiChannel, CCpitchA, mux1Read);
      break;
    case MUX1_pwmAmountB:
      midiCCOut(CCpwB, mux1Read);
      myControlChange(midiChannel, CCpwB, mux1Read);
      break;
    case MUX1_waveformB:
      midiCCOut(CCoscwaveformB, mux1Read);
      myControlChange(midiChannel, CCoscwaveformB, mux1Read);
      break;
    case MUX1_pitchB:
      midiCCOut(CCpitchB, mux1Read);
      myControlChange(midiChannel, CCpitchB, mux1Read);
      break;
    case MUX1_pwmRate:
      midiCCOut(CCpwmRate, mux1Read);
      myControlChange(midiChannel, CCpwmRate, mux1Read);
      break;
    case MUX1_pitchEnv:
      midiCCOut(CCpitchenv, mux1Read);
      myControlChange(midiChannel, CCpitchenv, mux1Read);
      break;
    }
    controlFromPanel = false;
    }
  }
  mux2Read = adc->adc1->analogRead(MUX2_S);
  if (mux2Read > (mux2ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux2Read < (mux2ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux2ValuesPrev[muxInput] = mux2Read;
    // Change range to 0-127, except Filter Cutoff which is 8 bit, 256 values for smoother changes
    mux2Read = muxInput == MUX2_cutoff ? (mux2Read >> 4) : (mux2Read >> 5);
    if (pickupCheck(PU_KNOB(1, muxInput), mux2Read))
    {
    controlFromPanel = true;
    switch (muxInput)
    {
    case MUX2_attack:
      midiCCOut(CCampattack, mux2Read);
      myControlChange(midiChannel, CCampattack, mux2Read);
      break;
    case MUX2_decay:
      midiCCOut(CCampdecay, mux2Read);
      myControlChange(midiChannel, CCampdecay, mux2Read);
      break;
    case MUX2_sustain:
      midiCCOut(CCampsustain, mux2Read);
      myControlChange(midiChannel, CCampsustain, mux2Read);
      break;
    case MUX2_release:
      midiCCOut(CCamprelease, mux2Read);
      myControlChange(midiChannel, CCamprelease, mux2Read);
      break;
    case MUX2_filterLFOAmount:
      midiCCOut(CCfilterlfoamt, mux2Read);
      myControlChange(midiChannel, CCfilterlfoamt, mux2Read);
      break;
    case MUX2_FXMix:
      midiCCOut(CCfxmix, mux2Read);
      myControlChange(midiChannel, CCfxmix, mux2Read);
      break;
    case MUX2_FXAmount:
      midiCCOut(CCfxamt, mux2Read);
      myControlChange(midiChannel, CCfxamt, mux2Read);
      break;
    case MUX2_glide:
      midiCCOut(CCglide, mux2Read);
      myControlChange(midiChannel, CCglide, mux2Read);
      break;
    case MUX2_filterEnv:
      midiCCOut(CCfilterenv, mux2Read);
      myControlChange(midiChannel, CCfilterenv, mux2Read);
      break;
    case MUX2_filterRelease:
      midiCCOut(CCfilterrelease, mux2Read);
      myControlChange(midiChannel, CCfilterrelease, mux2Read);
      break;
    case MUX2_filterSustain:
      midiCCOut(CCfiltersustain, mux2Read);
      myControlChange(midiChannel, CCfiltersustain, mux2Read);
      break;
    case MUX2_filterType:
      midiCCOut(CCfiltermixer, mux2Read);
      myControlChange(midiChannel, CCfiltermixer, mux2Read);
      break;
    case MUX2_resonance:
      midiCCOut(CCfilterres, mux2Read);
      myControlChange(midiChannel, CCfilterres, mux2Read);
      break;
    case MUX2_cutoff:
      updateFilterFreq(FILTERFREQS256[mux2Read]);
      midiCCOut(CCfilterfreq, mux2Read >> 1);
      break;
    case MUX2_filterLFORate:
      midiCCOut(CCfilterlforate, mux2Read);
      myControlChange(midiChannel, CCfilterlforate, mux2Read);
      break;
    case MUX2_filterLFOWaveform:
      midiCCOut(CCfilterlfowaveform, mux2Read);
      myControlChange(midiChannel, CCfilterlfowaveform, mux2Read);
      break;
    }
    controlFromPanel = false;
    }
  }
  muxInput++;
  if (muxInput >= MUXCHANNELS)
  {
    muxInput = 0;
    checkVolumePot(); // Check here
    if (!firstPatchLoaded)
    {
      recallPatch(patchNo); // Load first patch after all controls read
      firstPatchLoaded = true;
      global.sgtl5000_1.unmuteHeadphone();
      global.sgtl5000_1.unmuteLineout();
    }
  }
  digitalWriteFast(MUX_0, muxInput & B0001);
  digitalWriteFast(MUX_1, muxInput & B0010);
  digitalWriteFast(MUX_2, muxInput & B0100);
  digitalWriteFast(MUX_3, muxInput & B1000);
}

void checkVolumePot()
{
  volumeRead = adc->adc0->analogRead(VOLUME_POT);
  if (volumeRead > (volumePrevious + QUANTISE_FACTOR) || volumeRead < (volumePrevious - QUANTISE_FACTOR))
  {
    volumePrevious = volumeRead;
    volumeRead = (volumeRead >> 5); // Change range to 0-127
    myControlChange(midiChannel, CCvolume, volumeRead);
  }
}

void showSettingsPage()
{
  showSettingsPage(settings::current_setting(), settings::current_setting_value(), state);
}

// Read and drop everything waiting in the MIDI inputs, so a backlog from a flood of MIDI
// doesn't keep playing after Panic. Bounded in time in case the sender keeps going.
#define MIDI_FLUSH_MAX_MS 50
void flushMidiInput()
{
  midiDiscard = true;
  elapsedMillis t;
  bool more = true;
  while (more && t < MIDI_FLUSH_MAX_MS)
  {
    more = false;
    myusb.Task();
    if (midi1.read())   // USB host
      more = true;
    if (usbMIDI.read()) // USB device
      more = true;
  }
  // 5 pin DIN: drop the unread bytes without parsing them, so they aren't sent to MIDI thru
  Serial1.clear();
  midiDiscard = false;
}

void panic()
{
  flushMidiInput();
  AudioNoInterrupts();
  groupvec[activeGroupIndex]->panic();
  AudioInterrupts();
  showCurrentParameterPage(F("Panic"), F("All notes off"));
}

void checkSwitches()
{
  unisonSwitch.update();
  if (unisonSwitch.numClicks() == 1)
  {
    // Cycle through each option
    midiCCOut(CCunison, groupvec[activeGroupIndex]->params().unisonMode == 2 ? 0 : groupvec[activeGroupIndex]->params().unisonMode + 1);
    myControlChange(midiChannel, CCunison, groupvec[activeGroupIndex]->params().unisonMode == 2 ? 0 : groupvec[activeGroupIndex]->params().unisonMode + 1);
  }

  oscFXSwitch.update();
  if (oscFXSwitch.numClicks() == 1)
  {
    // Cycle through each option
    midiCCOut(CCoscfx, groupvec[activeGroupIndex]->getOscFX() == 2 ? 0 : groupvec[activeGroupIndex]->getOscFX() + 1);
    myControlChange(midiChannel, CCoscfx, groupvec[activeGroupIndex]->getOscFX() == 2 ? 0 : groupvec[activeGroupIndex]->getOscFX() + 1);
  }

  filterLFORetrigSwitch.update();
  if (filterLFORetrigSwitch.numClicks() == 1)
  {
    bool value = !groupvec[activeGroupIndex]->getFilterLfoRetrig();
    midiCCOut(CCfilterlforetrig, value);
    myControlChange(midiChannel, CCfilterlforetrig, value);
  }

  tempoSwitch.update();
  if (tempoSwitch.numClicks() == 1)
  {
    bool value = !groupvec[activeGroupIndex]->getFilterLfoMidiClockSync();
    midiCCOut(CCfilterLFOMidiClkSync, value);
    myControlChange(midiChannel, CCfilterLFOMidiClkSync, value);
  }

  saveButton.update();
  if (saveButton.held())
  {
    switch (state)
    {
    case PARAMETER:
    case PATCH:
      state = DELETE;
      break;
    }
  }
  else if (saveButton.numClicks() == 1)
  {
    switch (state)
    {
    case PARAMETER:
      if (patches.size() < PATCHES_LIMIT)
      {
        LockGuard lock(displayLock); // The display renders from the patch list
        resetPatchesOrdering(); // Reset order of patches from first patch
        patches.push({patches.size() + 1, INITPATCHNAME});
        state = SAVE;
      }
      break;
    case SAVE:
      // Save as new patch with INITIALPATCH name or overwrite existing keeping name - bypassing patch renaming
      patchName = patches.last().patchName;
      state = PATCH;
      savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
      showPatchPage(patches.last().patchNo, patches.last().patchName);
      patchNo = patches.last().patchNo;
      loadPatches(); // Get rid of pushed patch if it wasn't saved
      setPatchesOrdering(patchNo);
      renamedPatch = "";
      state = PARAMETER;
      break;
    case PATCHNAMING:
      if (renamedPatch.length() > 0)
        patchName = renamedPatch; // Prevent empty strings
      state = PATCH;
      savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
      showPatchPage(patches.last().patchNo, patchName);
      patchNo = patches.last().patchNo;
      loadPatches(); // Get rid of pushed patch if it wasn't saved
      setPatchesOrdering(patchNo);
      renamedPatch = "";
      state = PARAMETER;
      break;
    }
  }

  settingsButton.update();
  if (settingsButton.held())
  {
    // If recall held, set current patch to match current hardware state
    // Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
  }
  else if (settingsButton.numClicks() == 1)
  {
    switch (state)
    {
    case PARAMETER:
      state = SETTINGS;
      showSettingsPage();
      break;
    case SETTINGS:
      showSettingsPage();
    case SETTINGSVALUE:
      settings::save_current_value();
      state = SETTINGS;
      showSettingsPage();
      break;
    }
  }

  backButton.update();
  if (backButton.held())
  {
    // If Back button held, Panic: drop any MIDI waiting to be read, then silence all voices
    panic();
  }
  else if (backButton.numClicks() == 1)
  {
    switch (state)
    {
    case RECALL:
      setPatchesOrdering(patchNo);
      state = PARAMETER;
      break;
    case SAVE:
      renamedPatch = "";
      state = PARAMETER;
      loadPatches(); // Remove patch that was to be saved
      setPatchesOrdering(patchNo);
      break;
    case PATCHNAMING:
      charIndex = 0;
      renamedPatch = "";
      state = SAVE;
      break;
    case DELETE:
      setPatchesOrdering(patchNo);
      state = PARAMETER;
      break;
    case SETTINGS:
      state = PARAMETER;
      break;
    case SETTINGSVALUE:
      state = SETTINGS;
      showSettingsPage();
      break;
    }
  }

  // Encoder switch
  recallButton.update();
  if (recallButton.held())
  {
    // If Recall button held, return to current patch setting
    // which clears any changes made
    state = PATCH;
    // Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(patchNo);
    state = PARAMETER;
  }
  else if (recallButton.numClicks() == 1)
  {
    switch (state)
    {
    case PARAMETER:
      state = RECALL; // show patch list
      break;
    case RECALL:
      state = PATCH;
      // Recall the current patch
      patchNo = patches.first().patchNo;
      recallPatch(patchNo);
      state = PARAMETER;
      break;
    case SAVE:
      showRenamingPage(patches.last().patchName);
      patchName = patches.last().patchName;
      state = PATCHNAMING;
      break;
    case PATCHNAMING:
      if (renamedPatch.length() < 12) // actually 12 chars
      {
        renamedPatch.concat(String(currentCharacter));
        charIndex = 0;
        currentCharacter = CHARACTERS[charIndex];
        showRenamingPage(renamedPatch);
      }
      break;
    case DELETE:
      // Don't delete final patch
      if (patches.size() > 1)
      {
        LockGuard lock(displayLock);          // The display renders from the patch list
        state = DELETEMSG;
        patchNo = patches.first().patchNo;    // PatchNo to delete from SD card
        patches.shift();                      // Remove patch from circular buffer
        deletePatch(String(patchNo).c_str()); // Delete from SD card
        loadPatches();                        // Repopulate circular buffer to start from lowest Patch No
        renumberPatchesOnSD();
        loadPatches();                     // Repopulate circular buffer again after delete
        patchNo = patches.first().patchNo; // Go back to 1
        recallPatch(patchNo);              // Load first patch
      }
      state = PARAMETER;
      break;
    case SETTINGS:
      state = SETTINGSVALUE;
      showSettingsPage();
      break;
    case SETTINGSVALUE:
      settings::save_current_value();
      state = SETTINGS;
      showSettingsPage();
      break;
    }
  }
}

FLASHMEM void reinitialiseToPanel()
{
  // This sets the current patch to be the same as the current hardware panel state - all the pots
  // The four button controls stay the same state
  // This reinialises the previous hardware values to force a re-read
  muxInput = 0;
  for (int i = 0; i < MUXCHANNELS; i++)
  {
    mux1ValuesPrev[i] = RE_READ;
    mux2ValuesPrev[i] = RE_READ;
  }
  volumePrevious = RE_READ;
  pickupReleaseAll(); // Every knob sets its parameter
  patchName = INITPATCHNAME;
  showPatchPage(F("Initial"), F("Panel Settings"));
}

// Turning the encoder on the patch page only selects and shows the patch. It is recalled
// once the encoder has been still for PATCH_RECALL_DELAY_MS, rather than on every detent:
// each recall reads the SD card and sets every parameter, which a fast turn used to do
// dozens of times a second.
#define PATCH_RECALL_DELAY_MS 150
boolean patchRecallPending = false;
elapsedMillis patchRecallTimer;

void selectPatchForRecall()
{
  patchNo = patches.first().patchNo;
  showPatchPage(String(patchNo), patches.first().patchName);
  state = PATCH;
  patchRecallPending = true;
  patchRecallTimer = 0;
}

void checkPendingPatchRecall()
{
  if (!patchRecallPending || patchRecallTimer < PATCH_RECALL_DELAY_MS)
    return;
  patchRecallPending = false;
  recallPatch(patchNo);
  if (state == PATCH)
    state = PARAMETER;
  // Make sure the current setting value is refreshed.
  settings::increment_setting();
  settings::decrement_setting();
}

void checkEncoder()
{
  // Encoder works with relative inc and dec values
  // Detent encoder goes up in 4 steps, hence +/-3
  long encRead = encoder.read();
  if ((encCW && encRead > encPrevious + 3) || (!encCW && encRead < encPrevious - 3))
  {
    LockGuard lock(displayLock); // The display renders from the patch list
    switch (state)
    {
    case PARAMETER:
    case PATCH: // Still turning, recall not done yet
      patches.push(patches.shift());
      selectPatchForRecall();
      break;
    case RECALL:
      patches.push(patches.shift());
      break;
    case SAVE:
      patches.push(patches.shift());
      break;
    case PATCHNAMING:
      if (charIndex == TOTALCHARS)
        charIndex = 0; // Wrap around
      currentCharacter = CHARACTERS[charIndex++];
      showRenamingPage(renamedPatch + currentCharacter);
      break;
    case DELETE:
      patches.push(patches.shift());
      break;
    case SETTINGS:
      settings::increment_setting();
      showSettingsPage();
      break;
    case SETTINGSVALUE:
      settings::increment_setting_value();
      showSettingsPage();
      break;
    }
    encPrevious = encRead;
  }
  else if ((encCW && encRead < encPrevious - 3) || (!encCW && encRead > encPrevious + 3))
  {
    LockGuard lock(displayLock); // The display renders from the patch list
    switch (state)
    {
    case PARAMETER:
    case PATCH: // Still turning, recall not done yet
      patches.unshift(patches.pop());
      selectPatchForRecall();
      break;
    case RECALL:
      patches.unshift(patches.pop());
      break;
    case SAVE:
      patches.unshift(patches.pop());
      break;
    case PATCHNAMING:
      if (charIndex == -1)
        charIndex = TOTALCHARS - 1;
      currentCharacter = CHARACTERS[charIndex--];
      showRenamingPage(renamedPatch + currentCharacter);
      break;
    case DELETE:
      patches.unshift(patches.pop());
      break;
    case SETTINGS:
      settings::decrement_setting();
      showSettingsPage();
      break;
    case SETTINGSVALUE:
      settings::decrement_setting_value();
      showSettingsPage();
      break;
    }
    encPrevious = encRead;
  }
}

void midiCCOut(byte cc, byte value)
{
  if (midiOutCh > 0)
  {
    usbMIDI.sendControlChange(cc, value, midiOutCh);
    midi1.sendControlChange(cc, value, midiOutCh);
    if (MIDIThru == midi::Thru::Off)
      MIDI.sendControlChange(cc, value, midiOutCh); // MIDI DIN is set to Out
  }
}

// Ignores CC messages for four seconds after USB Host is plugged in
// Some MIDI controllers send data when connected that changes synth settings
FLASHMEM void usbHostControlChange(byte channel, byte control, byte value)
{
  if (usbHostPluggedIn && usb_host_wait_timer > 4000) // wait 4s
  {
    // When waiting time has expired, all CC messages go through to the normal handler
    myControlChange(channel, control, value);
  }
}

FLASHMEM void checkUSBHostStatus()
{
  if (midi1 && !usbHostPluggedIn)
  {
    usb_host_wait_timer = 0;
    usbHostPluggedIn = true;
  }
  else if (!midi1 && usbHostPluggedIn)
  {
    usbHostPluggedIn = false;
  }
}

// Audio CPU use, once a second: total, then by group of audio objects (current / max over
// the last second, in % of the time available). Uncomment the call in loop() to use.
struct CpuGroup
{
  float now = 0, max = 0;
  void add(AudioStream &s)
  {
    now += s.processorUsage();
    max += s.processorUsageMax();
    s.processorUsageMaxReset();
  }
  void print(const __FlashStringHelper *name)
  {
    Serial.print(F("  "));
    Serial.print(name);
    Serial.print(F(" "));
    Serial.print(now, 1);
    Serial.print(F("/"));
    Serial.print(max, 1);
  }
};

void CPUMonitor()
{
  static elapsedMillis sinceLast;
  if (sinceLast < 1000)
    return;
  sinceLast = 0;

  CpuGroup cg_osc, cg_filt, cg_env, cg_vmix, cg_fx, cg_dc, cg_lfo, cg_smix, cg_ens, cg_dcf, cg_noise, cg_out;
  for (uint8_t i = 0; i < global.maxVoices(); i++)
  {
    Patch &p = global.Oscillators[i];
    cg_osc.add(p.waveformMod_a);
    cg_osc.add(p.waveformMod_b);
    cg_filt.add(p.filter_);
    cg_env.add(p.ampEnvelope_);
    cg_env.add(p.filterEnvelope_);
    cg_vmix.add(p.pwMixer_a);
    cg_vmix.add(p.pwMixer_b);
    cg_vmix.add(p.oscModMixer_a);
    cg_vmix.add(p.oscModMixer_b);
    cg_vmix.add(p.waveformMixer_);
    cg_vmix.add(p.filterModMixer_);
    cg_fx.add(p.oscFX_);
    cg_dc.add(p.glide_);
    cg_dc.add(p.keytracking_);
  }
  PatchShared &s = global.SharedAudio[0];
  cg_lfo.add(s.pitchLfo);
  cg_lfo.add(s.pwmLfoA);
  cg_lfo.add(s.pwmLfoB);
  cg_lfo.add(s.filterLfo);
  cg_lfo.add(s.pitchBend);
  cg_lfo.add(s.pwa);
  cg_lfo.add(s.pwb);
  cg_smix.add(s.pitchMixer);
  cg_smix.add(s.noiseMixer);
  for (uint8_t i = 0; i < 3; i++)
    cg_smix.add(s.voiceMixer[i]);
  cg_smix.add(s.voiceMixerM);
  cg_smix.add(s.volumeMixer);
  cg_smix.add(s.effectMixerL);
  cg_smix.add(s.effectMixerR);
  cg_ens.add(s.ensemble);
  cg_dcf.add(s.dcOffsetFilter);
  cg_noise.add(global.pink);
  cg_noise.add(global.white);
  cg_out.add(global.i2s);
  cg_out.add(global.usbAudio);
  cg_out.add(global.peak);
  cg_out.add(global.scope);
  cg_out.add(global.cpuGuard);
  cg_out.add(global.constant1Dc);
  for (uint8_t i = 0; i < 3; i++)
  {
    cg_out.add(global.effectMixerL[i]);
    cg_out.add(global.effectMixerR[i]);
  }
  cg_out.add(global.effectMixerLM);
  cg_out.add(global.effectMixerRM);

  Serial.print(F("CPU "));
  Serial.print(AudioProcessorUsage(), 1);
  Serial.print(F("/"));
  Serial.print(AudioProcessorUsageMax(), 1);
  Serial.print(F("% MEM "));
  Serial.print(AudioMemoryUsageMax());
  Serial.print(F(" |"));
  cg_osc.print(F("osc"));
  cg_filt.print(F("filter"));
  cg_env.print(F("env"));
  cg_vmix.print(F("vmix"));
  cg_fx.print(F("oscfx"));
  cg_dc.print(F("dc"));
  cg_lfo.print(F("lfo"));
  cg_smix.print(F("smix"));
  cg_ens.print(F("ensemble"));
  cg_dcf.print(F("dcfilt"));
  cg_noise.print(F("noise"));
  cg_out.print(F("out"));
  Serial.println();
  AudioProcessorUsageMaxReset();
}

void loop()
{
  // USB HOST MIDI Class Compliant
  myusb.Task();
  // Drain pending MIDI messages (bounded) so notes aren't queued behind clock/CC traffic
  for (uint8_t i = 0; i < MIDI_READS_PER_LOOP && midi1.read(midiChannel); i++)
    ;
  // USB Client MIDI
  for (uint8_t i = 0; i < MIDI_READS_PER_LOOP && usbMIDI.read(midiChannel); i++)
    ;
  // MIDI 5 Pin DIN
  for (uint8_t i = 0; i < MIDI_READS_PER_LOOP && MIDI.read(midiChannel); i++)
    ;
  // Start note-ons that were waiting for a stolen voice to fade out, and suspend the
  // oscillators of voices that have stopped sounding
  groupvec[activeGroupIndex]->serviceVoices();
  static uint32_t overloadsReported = 0;
  if (global.cpuGuard.overloads != overloadsReported)
  {
    overloadsReported = global.cpuGuard.overloads;
    Serial.println(F("Audio CPU overload: a voice was dropped"));
  }
  if (loopCount++ > 9)
  {
    checkUSBHostStatus();
    checkMux();
    checkSwitches();
    checkEncoder();
    checkPendingPatchRecall();
    // CPUMonitor();
    loopCount = 0;
  }
}
