#include "VoiceGroup.h"
#include "SettingsService.h"

void settingsMIDICh(int index, const char * value);
void settingsVelocitySens(int index, const char * value);
void settingsKeyTracking(int index, const char * value);
void settingsPitchBend(int index, const char * value);
void settingsModWheelDepth(int index, const char * value);
void settingsMIDIOutCh(int index, const char * value);
void settingsMIDIThru(int index, const char * value);
void settingsEncoderDir(int index, const char * value);
void settingsPickupEnable(int index, const char * value);
void settingsBassEnhanceEnable(int index, const char * value);
void settingsScopeEnable(int index, const char * value);
void settingsVUEnable(int index, const char * value);
void settingsMonophonic(int index, const char * value);
void settingsAmpEnv(int index, const char *value);
void settingsFiltEnv(int index, const char *value);
void settingsGlideShape(int index, const char *value);

int currentIndexMIDICh();
int currentIndexVelocitySens();
int currentIndexKeyTracking();
int currentIndexPitchBend();
int currentIndexModWheelDepth();
int currentIndexMIDIOutCh();
int currentIndexMIDIThru();
int currentIndexEncoderDir();
int currentIndexPickupEnable();
int currentIndexBassEnhanceEnable();
int currentIndexScopeEnable();
int currentIndexVUEnable();
int currentIndexMonophonicMode();
int currentIndexAmpEnv();
int currentIndexFiltEnv();
int currentIndexGlideShape();

FLASHMEM int currentIndexGlideShape() {
  return glideShape;
}

FLASHMEM void settingsGlideShape(int index, const char * value) {
  if (strcmp(value, "Lin") == 0) glideShape = 0;
  else if (strcmp(value, "Exp") == 0) glideShape = 1;
  else glideShape = 1;
  for (uint8_t i = 0; i < global.maxVoices(); i++) {
    global.Oscillators[i].glide_.setMode(glideShape);
  }
  storeGlideShape(glideShape); 
}

FLASHMEM int currentIndexAmpEnv() {
  if((envTypeAmp>=-8) && (envTypeAmp<=8))return envTypeAmp+9;
  else return 8;
}

FLASHMEM int currentIndexFiltEnv() {
  if((envTypeFilt>=-8) && (envTypeFilt<=8))return envTypeFilt+9;
  else return 8;
}

FLASHMEM void settingsAmpEnv(int index, const char * value) {
  if (strcmp(value, "Lin") == 0) envTypeAmp = -128;
  else if (strcmp(value, "Exp -8") == 0)  envTypeAmp = -8;
  else if (strcmp(value, "Exp -7") == 0)  envTypeAmp = -7;
  else if (strcmp(value, "Exp -6") == 0)  envTypeAmp = -6;
  else if (strcmp(value, "Exp -5") == 0)  envTypeAmp = -5;
  else if (strcmp(value, "Exp -4") == 0)  envTypeAmp = -4;
  else if (strcmp(value, "Exp -3") == 0)  envTypeAmp = -3;
  else if (strcmp(value, "Exp -2") == 0)  envTypeAmp = -2;
  else if (strcmp(value, "Exp -1") == 0)  envTypeAmp = -1;
  else if (strcmp(value, "Exp 0") == 0)  envTypeAmp = 0;
  else if (strcmp(value, "Exp +1") == 0)  envTypeAmp = 1;
  else if (strcmp(value, "Exp +2") == 0)  envTypeAmp = 2;
  else if (strcmp(value, "Exp +3") == 0)  envTypeAmp = 3;
  else if (strcmp(value, "Exp +4") == 0)  envTypeAmp = 4;
  else if (strcmp(value, "Exp +5") == 0)  envTypeAmp = 5;
  else if (strcmp(value, "Exp +6") == 0)  envTypeAmp = 6;
  else if (strcmp(value, "Exp +7") == 0)  envTypeAmp = 7;
  else if (strcmp(value, "Exp +8") == 0)  envTypeAmp = 8;
  else envTypeAmp = -128;
  for (uint8_t i = 0; i < global.maxVoices(); i++) {
    global.Oscillators[i].ampEnvelope_.setEnvType(envTypeAmp);
  }
  storeAmpEnv(envTypeAmp);
}

FLASHMEM void settingsFiltEnv(int index, const char * value) {
  if (strcmp(value, "Lin") == 0) envTypeFilt = -128;
  else if (strcmp(value, "Exp -8") == 0)  envTypeFilt = -8;
  else if (strcmp(value, "Exp -7") == 0)  envTypeFilt = -7;
  else if (strcmp(value, "Exp -6") == 0)  envTypeFilt = -6;
  else if (strcmp(value, "Exp -5") == 0)  envTypeFilt = -5;
  else if (strcmp(value, "Exp -4") == 0)  envTypeFilt = -4;
  else if (strcmp(value, "Exp -3") == 0)  envTypeFilt = -3;
  else if (strcmp(value, "Exp -2") == 0)  envTypeFilt = -2;
  else if (strcmp(value, "Exp -1") == 0)  envTypeFilt = -1;
  else if (strcmp(value, "Exp 0") == 0)  envTypeFilt = 0;
  else if (strcmp(value, "Exp +1") == 0)  envTypeFilt = 1;
  else if (strcmp(value, "Exp +2") == 0)  envTypeFilt = 2;
  else if (strcmp(value, "Exp +3") == 0)  envTypeFilt = 3;
  else if (strcmp(value, "Exp +4") == 0)  envTypeFilt = 4;
  else if (strcmp(value, "Exp +5") == 0)  envTypeFilt = 5;
  else if (strcmp(value, "Exp +6") == 0)  envTypeFilt = 6;
  else if (strcmp(value, "Exp +7") == 0)  envTypeFilt = 7;
  else if (strcmp(value, "Exp +8") == 0)  envTypeFilt = 8;
  else envTypeFilt = -128;
  for (uint8_t i = 0; i < global.maxVoices(); i++) {
    global.Oscillators[i].filterEnvelope_.setEnvType(envTypeFilt);
  }
  storeFiltEnv(envTypeFilt);
}						

FLASHMEM void reloadGlideShape(){
  glideShape = getGlideShape();
  for (uint8_t i = 0; i < global.maxVoices(); i++) {
    global.Oscillators[i].glide_.setMode(glideShape);
  }
}

FLASHMEM void reloadAmpEnv(){
  envTypeAmp = getAmpEnv();
  for (uint8_t i = 0; i < global.maxVoices(); i++) {
    global.Oscillators[i].ampEnvelope_.setEnvType(envTypeAmp);
  }
}

FLASHMEM void reloadFiltEnv(){
  envTypeFilt = getFiltEnv();
  for (uint8_t i = 0; i < global.maxVoices(); i++) {
    global.Oscillators[i].filterEnvelope_.setEnvType(envTypeFilt);
  }
}

FLASHMEM void settingsMIDICh(int index, const char * value) {
  if (strcmp(value, "ALL") == 0) {
    midiChannel = MIDI_CHANNEL_OMNI;
  } else {
    midiChannel = atoi(value);
  }
  storeMidiChannel(midiChannel);
}

FLASHMEM void settingsVelocitySens(int index, const char * value) {
  if (strcmp(value, "Off") == 0) {
    velocitySens = 0;
  } else {
    velocitySens = atoi(value);
  }
}

FLASHMEM void settingsKeyTracking(int index, const char * value) {
  if (strcmp(value, "None") == 0) updateKeyTracking(0);
  if (strcmp(value, "Half") == 0) updateKeyTracking(0.5);
  if (strcmp(value, "Full") == 0) updateKeyTracking(1.0);
}

FLASHMEM void settingsPitchBend(int index, const char * value) {
  pitchBendRange = atoi(value);
  storePitchBendRange(pitchBendRange);
}

FLASHMEM void settingsModWheelDepth(int index, const char * value) {
  modWheelDepth = atoi(value) / 10.0f;
  storeModWheelDepth(modWheelDepth);
}

FLASHMEM void settingsMIDIOutCh(int index, const char * value) {
  if (strcmp(value, "Off") == 0) {
    midiOutCh = 0;
  } else {
    midiOutCh = atoi(value);
  }
  storeMidiOutCh(midiOutCh);
}

FLASHMEM void settingsMIDIThru(int index, const char * value) {
  if (strcmp(value, "Off") == 0) MIDIThru = midi::Thru::Off;
  if (strcmp(value, "Full") == 0)  MIDIThru =  midi::Thru::Full;
  if (strcmp(value, "Same Ch.") == 0) MIDIThru =  midi::Thru::SameChannel;
  if (strcmp(value, "Diff. Ch.") == 0) MIDIThru =  midi::Thru::DifferentChannel;
  changeMIDIThruMode();
  storeMidiThru(MIDIThru);
}

FLASHMEM void settingsEncoderDir(int index, const char * value) {
  if (strcmp(value, "Type 1") == 0) {
    encCW = true;
  } else {
    encCW =  false;
  }
  storeEncoderDir(encCW ? 1 : 0);
}

FLASHMEM void settingsPickupEnable(int index, const char * value) {
  if (strcmp(value, "Off") == 0) {
    pickUp = false;
  } else {
    pickUp =  true;
    pickupRearmAll();
  }
  storePickupEnable(pickUp ? 1 : 0);
}

FLASHMEM void settingsBassEnhanceEnable(int index, const char * value) {
  if (strcmp(value, "Off") == 0) {
    global.sgtl5000_1.enhanceBassDisable();
    storeBassEnhanceEnable(0);
  } else {
    global.sgtl5000_1.enhanceBassEnable();
    storeBassEnhanceEnable(1);
  }
}

FLASHMEM void settingsMonophonic(int index, const char * value) {
  uint8_t monophonic;
  if (strcmp(value, "Off") == 0)     monophonic = MONOPHONIC_OFF;
  if (strcmp(value, "Last") == 0)    monophonic = MONOPHONIC_LAST;
  if (strcmp(value, "First") == 0)   monophonic = MONOPHONIC_FIRST;
  if (strcmp(value, "Highest") == 0) monophonic = MONOPHONIC_HIGHEST;
  if (strcmp(value, "Lowest") == 0)  monophonic = MONOPHONIC_LOWEST;
  if (strcmp(value, "Legato") == 0)  monophonic = MONOPHONIC_LEGATO;
  groupvec[activeGroupIndex]->setMonophonic(monophonic);
}

FLASHMEM void settingsScopeEnable(int index, const char * value) {
  if (strcmp(value, "Off") == 0) {
    enableScope(false);
    storeScopeEnable(0);
  } else {
    enableScope(true);
    storeScopeEnable(1);
  }
}

FLASHMEM void settingsVUEnable(int index, const char * value) {
  if (strcmp(value, "Off") == 0) {
    vuMeter = false;
    storeVUEnable(0);
  } else {
    vuMeter = true;
    storeVUEnable(1);
  }
}

FLASHMEM int currentIndexMIDICh() {
  return getMIDIChannel();
}

FLASHMEM int currentIndexVelocitySens() {
  return velocitySens;
}

FLASHMEM int currentIndexKeyTracking() {
  if (groupvec[activeGroupIndex]->getKeytrackingAmount() == 0.0f) return 0;
  if (groupvec[activeGroupIndex]->getKeytrackingAmount() == 0.5f) return 1;
  if (groupvec[activeGroupIndex]->getKeytrackingAmount() == 1.0f) return 2;
  return 0;
}

FLASHMEM int currentIndexPitchBend() {
  return  getPitchBendRange() - 1;
}

FLASHMEM int currentIndexModWheelDepth() {
  return (getModWheelDepth() * 10) - 1;
}

FLASHMEM int currentIndexMIDIOutCh() {
  return getMIDIOutCh();
}

FLASHMEM int currentIndexMIDIThru() {
  if (MIDIThru == midi::Thru::Off) return 0;
  if (MIDIThru == midi::Thru::Full)  return 1;
  if (MIDIThru == midi::Thru::SameChannel) return 2;
  if (MIDIThru == midi::Thru::DifferentChannel) return 3;
  return 0;
}

FLASHMEM int currentIndexEncoderDir() {
  return getEncoderDir() ? 0 : 1;
}

FLASHMEM int currentIndexPickupEnable() {
  return getPickupEnable() ? 1 : 0;
}

FLASHMEM int currentIndexBassEnhanceEnable() {
  return getBassEnhanceEnable() ? 1 : 0;
}

FLASHMEM int currentIndexMonophonicMode() {
  return groupvec[activeGroupIndex]->getMonophonicMode();
}

FLASHMEM int currentIndexScopeEnable() {
  return getScopeEnable() ? 1 : 0;
}

FLASHMEM int currentIndexVUEnable() {
  return getVUEnable() ? 1 : 0;
}

// add settings to the circular buffer
FLASHMEM void setUpSettings() {
  settings::append(settings::SettingsOption{"MIDI In Ch.", {"All", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "\0"}, settingsMIDICh, currentIndexMIDICh});
  settings::append(settings::SettingsOption{"Key Tracking", {"None", "Half", "Full", "\0"}, settingsKeyTracking, currentIndexKeyTracking});
  settings::append(settings::SettingsOption{"Vel. Sens.", {"Off", "1", "2", "3", "4", "\0"}, settingsVelocitySens, currentIndexVelocitySens});
  settings::append(settings::SettingsOption{"Monophonic", {"Off", "Last", "First", "Highest", "Lowest"/* , "Legato"*/, "\0"}, settingsMonophonic, currentIndexMonophonicMode});
  settings::append(settings::SettingsOption{"Pitch Bend", {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "\0"}, settingsPitchBend, currentIndexPitchBend});
  settings::append(settings::SettingsOption{"MW Depth", {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "\0"}, settingsModWheelDepth, currentIndexModWheelDepth});
  settings::append(settings::SettingsOption{"MIDI Out Ch.", {"Off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "\0"}, settingsMIDIOutCh, currentIndexMIDIOutCh});
  settings::append(settings::SettingsOption{"MIDI Thru", {"Off", "Full", "Same Ch.", "Diff. Ch.", "\0"}, settingsMIDIThru, currentIndexMIDIThru});
  settings::append(settings::SettingsOption{"Amp. Env.", {"Lin", "Exp -8", "Exp -7", "Exp -6", "Exp -5", "Exp -4", "Exp -3", "Exp -2", "Exp -1", "Exp 0", "Exp +1", "Exp +2", "Exp +3", "Exp +4", "Exp +5", "Exp +6", "Exp +7", "Exp +8", "\0"}, settingsAmpEnv, currentIndexAmpEnv});
  settings::append(settings::SettingsOption{"Filter Env.", {"Lin", "Exp -8", "Exp -7", "Exp -6", "Exp -5", "Exp -4", "Exp -3", "Exp -2", "Exp -1", "Exp 0", "Exp +1", "Exp +2", "Exp +3", "Exp +4", "Exp +5", "Exp +6", "Exp +7", "Exp +8", "\0"}, settingsFiltEnv, currentIndexFiltEnv});
  settings::append(settings::SettingsOption{"Glide Shape", {"Lin", "Exp", "\0"}, settingsGlideShape, currentIndexGlideShape});
  settings::append(settings::SettingsOption{"Pick-up", {"Off", "On", "\0"}, settingsPickupEnable, currentIndexPickupEnable});
  settings::append(settings::SettingsOption{"Encoder", {"Type 1", "Type 2", "\0"}, settingsEncoderDir, currentIndexEncoderDir});
  settings::append(settings::SettingsOption{"Oscilloscope", {"Off", "On", "\0"}, settingsScopeEnable, currentIndexScopeEnable});
  settings::append(settings::SettingsOption{"VU Meter", {"Off", "On", "\0"}, settingsVUEnable, currentIndexVUEnable});
  settings::append(settings::SettingsOption{"Bass Enh.", {"Off", "On", "\0"}, settingsBassEnhanceEnable, currentIndexBassEnhanceEnable});
}
