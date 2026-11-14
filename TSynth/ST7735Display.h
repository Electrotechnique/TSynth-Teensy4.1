#ifndef TSYNTH_ST7735_DISPLAY_H
#define TSYNTH_ST7735_DISPLAY_H

#define sclk 27
#define mosi 26
#define cs 2
#define dc 3
#define rst 9
#define DISPLAYTIMEOUT 700
#define DISPLAY_FRAME_MS 30      // Minimum time between screen refreshes (~33fps max)
#define DISPLAY_THREAD_STACK 4096 // bytes; the TeensyThreads default of 1024 is tight for the render code
#define DISPLAY_THREAD_TICKS 1   // Display thread time slice in 1ms ticks. Main loop (MIDI) waits at most this long

#include <Adafruit_GFX.h>
#include "ST7735_t3.h" // Local copy from TD1.48 that works for 0.96" IPS 160x80 display
#include "Voice.h"

#include "Fonts/Org_01.h"
#include "Yeysk16pt7b.h"
#include "Fonts/FreeSansBold18pt7b.h"
#include "Fonts/FreeSans12pt7b.h"
#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/FreeSansOblique24pt7b.h"
#include "Fonts/FreeSansBoldOblique24pt7b.h"

#define PULSE 1
#define VAR_TRI 2
#define FILTER_ENV 3
#define AMP_ENV 4

ST7735_t3 tft = ST7735_t3(cs, dc, mosi, sclk, rst);

String currentParameter = "";
String currentValue = "";
float currentFloatValue = 0.0;
String currentPgmNum = "";
String currentPatchName = "";
String newPatchName = "";
const char * currentSettingsOption = "";
const char * currentSettingsValue = "";
uint32_t currentSettingsPart = SETTINGS;
uint32_t paramType = PARAMETER;

boolean MIDIClkSignal = false;
uint32_t peakCount = 0;
uint16_t prevLen = 0;

uint32_t colourPriority[5] = {ST7735_BLACK, ST7735_BLUE, ST7735_YELLOW, ST77XX_ORANGE, ST77XX_DARKRED};

unsigned long timer = 0;

void startTimer() {
  if (state == PARAMETER)
  {
    timer = millis();
  }
}

FLASHMEM void setMIDIClkSignal(bool val) {
  MIDIClkSignal = val;
}

FLASHMEM bool getMIDIClkSignal() {
  return MIDIClkSignal;
}

FLASHMEM void renderBootUpPage()
{
  tft.fillScreen(ST7735_BLACK);
  tft.drawRect(42, 30, 46, 11, ST7735_WHITE);
  tft.fillRect(88, 30, 61, 11, ST7735_WHITE);
  tft.setCursor(45, 31);
  tft.setFont(&Org_01);
  tft.setTextSize(1);
  tft.setTextColor(ST7735_WHITE);
  tft.println(F("ELECTRO"));
  tft.setTextColor(ST7735_BLACK);
  tft.setCursor(91, 37);
  tft.println(F("TECHNIQUE"));
  tft.setTextColor(ST7735_YELLOW);
  tft.setFont(&Yeysk16pt7b);
  tft.setCursor(5, 70);
  tft.setTextSize(1);
  tft.println(F("TSynth"));
  tft.setTextColor(ST7735_RED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(110, 95);
  tft.println(VERSION);
}

FLASHMEM void renderPeak() {
  if (vuMeter && global.peak.available()) {
    uint16_t len = 0;
    if (peakCount > 1) {
      len = (int)(global.peak.read() * 75.0f);
      prevLen = len;
      peakCount = 0;
    } else {
      len = prevLen;
      peakCount++;
    }
    tft.drawFastVLine(158, 103 - len, len ,  len > 72 ? ST77XX_RED : ST77XX_GREEN);
    tft.drawFastVLine(159, 103 - len, len ,  len > 72 ? ST77XX_RED : ST77XX_GREEN);
  }
}

FLASHMEM void renderCurrentPatchPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(5, 53);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println(currentPgmNum);

  tft.setTextColor(ST7735_BLACK);
  tft.setFont(&Org_01);

  if (MIDIClkSignal) {
    tft.fillRect(100, 27, 14, 7, ST77XX_ORANGE);
    tft.setCursor(101, 32);
    tft.println(F("CK"));
  }
  renderPeak();

  
  uint8_t fillColour[global.maxVoices()] = {};
  uint8_t borderColour[global.maxVoices()] = {};
  // Select colours based on voice state.
  uint8_t i = 0;
  for (uint8_t group = 0; group < groupvec.size(); group++) {
    for (uint8_t voice = 0; voice  < groupvec[group]->size(); voice++) {
      borderColour[i] = group + 1;
      if ((*groupvec[group])[voice]->on()) fillColour[i] = (*groupvec[group])[voice]->noteId() + 1;
      else fillColour[i] = 0;
      i++;
    }
  }

  // Draw rectangles to represent each voice.
  uint8_t max_rows = 3;
  uint8_t x_step = 10;
  uint8_t y_step = 10;
  uint8_t x_end = 147;
  uint8_t x_start = x_end - (x_step * ceil(global.maxVoices() / float(max_rows))) + x_step;
  uint8_t y_start = 27;
  uint8_t y_end = 47;
  uint8_t idx = 0;
  for (uint8_t y = y_start; y <= y_end; y += y_step) {
    for (uint8_t x = x_start; x <= x_end; x += x_step) {
      // Always draw border to indicate timbre.
      tft.fillRect(x, y, 8, 8, colourPriority[fillColour[idx]]);
      tft.drawRect(x, y, 8, 8, colourPriority[borderColour[idx]]);
      idx++;
      if (idx >= global.maxVoices()) {
        break;
      }
    }
  }

  tft.drawFastHLine(10, 63, tft.width() - 20, ST7735_RED);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(1, 90);
  tft.setTextColor(ST7735_WHITE);
  tft.println(currentPatchName);
}

FLASHMEM void renderPulseWidth(float value) {
  tft.drawFastHLine(108, 74, 15 + (value * 13), ST7735_CYAN);
  tft.drawFastVLine(123 + (value * 13), 74, 20, ST7735_CYAN);
  tft.drawFastHLine(123 + (value * 13), 94, 16 - (value * 13), ST7735_CYAN);
  if (value < 0) {
    tft.drawFastVLine(108, 74, 21, ST7735_CYAN);
  } else {
    tft.drawFastVLine(138, 74, 21, ST7735_CYAN);
  }
}

FLASHMEM void renderVarTriangle(float value) {
  tft.drawLine(110, 94, 123 + (value * 13), 74, ST7735_CYAN);
  tft.drawLine(123 + (value * 13), 74, 136, 94, ST7735_CYAN);
}

FLASHMEM void renderEnv(float att, float dec, float sus, float rel) {
  tft.drawLine(100, 94, 100 + (att * 15), 74, ST7735_CYAN);
  tft.drawLine(100 + (att * 15), 74.0, 100 + ((att + dec) * 15), 94 - (sus * 20), ST7735_CYAN);
  tft.drawFastHLine(100 + ((att + dec) * 15), 94 - (sus * 20), 40 - ((att + dec) * 15), ST7735_CYAN);
  tft.drawLine(139, 94 - (sus * 20), 139 + (rel * 13), 94, ST7735_CYAN);
}

FLASHMEM void renderCurrentParameterPage() {
  switch (state) {
    case PARAMETER:
      tft.fillScreen(ST7735_BLACK);
      tft.setFont(&FreeSans12pt7b);
      tft.setCursor(0, 53);
      tft.setTextColor(ST7735_YELLOW);
      tft.setTextSize(1);
      tft.println(currentParameter);

      // Not a necessary feature perhaps
      //      if (midiOutCh > 0) {
      //        tft.setTextColor(ST77XX_ORANGE);
      //        tft.setFont(&Org_01);
      //        tft.setTextSize(2);
      //        tft.setCursor(140, 35);
      //        tft.println(midiOutCh);
      //        tft.setFont(&FreeSans12pt7b);
      //        tft.setTextSize(1);
      //      }
      renderPeak();
      tft.drawFastHLine(10, 63, tft.width() - 20, ST7735_RED);
      tft.setCursor(1, 90);
      tft.setTextColor(ST7735_WHITE);
      tft.println(currentValue);
      if (pickUpActive) {
        tft.fillCircle(150, 70, 5, ST77XX_DARKGREY);
        // Knob waiting to pick up: small arrow inside the indicator showing which way to turn
        if (pickupDirection > 0) {
          tft.fillTriangle(147, 72, 153, 72, 150, 67, ST7735_WHITE);
        } else if (pickupDirection < 0) {
          tft.fillTriangle(147, 68, 153, 68, 150, 73, ST7735_WHITE);
        } else {
          tft.drawFastHLine(146, 70, 4, ST7735_WHITE);
        }
      }
      switch (paramType) {
        case PULSE:
          renderPulseWidth(currentFloatValue);
          break;
        case VAR_TRI:
          renderVarTriangle(currentFloatValue);
          break;
        case FILTER_ENV:
          renderEnv(groupvec[activeGroupIndex]->getFilterAttack() * 0.0001f, groupvec[activeGroupIndex]->getFilterDecay() * 0.0001f, groupvec[activeGroupIndex]->getFilterSustain(), groupvec[activeGroupIndex]->getFilterRelease() * 0.0001f);
          break;
        case AMP_ENV:
          renderEnv(groupvec[activeGroupIndex]->getAmpAttack() * 0.0001f, groupvec[activeGroupIndex]->getAmpDecay() * 0.0001f, groupvec[activeGroupIndex]->getAmpSustain(), groupvec[activeGroupIndex]->getAmpRelease() * 0.0001f);
          break;
      }
      break;
  }
}

FLASHMEM void renderDeletePatchPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(5, 53);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println(F("Delete?"));
  tft.drawFastHLine(10, 60, tft.width() - 20, ST7735_RED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(0, 78);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.last().patchNo);
  tft.setCursor(35, 78);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.last().patchName);
  tft.fillRect(0, 85, tft.width(), 23, ST77XX_DARKRED);
  tft.setCursor(0, 98);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.first().patchNo);
  tft.setCursor(35, 98);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.first().patchName);
}

FLASHMEM void renderDeleteMessagePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(2, 53);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println(F("Renumbering"));
  tft.setCursor(10, 90);
  tft.println("SD Card");
}

FLASHMEM void renderSavePage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(5, 53);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.println(F("Save?"));
  tft.drawFastHLine(10, 60, tft.width() - 20, ST7735_RED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(0, 78);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches[patches.size() - 2].patchNo);
  tft.setCursor(35, 78);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches[patches.size() - 2].patchName);
  tft.fillRect(0, 85, tft.width(), 23, ST77XX_DARKRED);
  tft.setCursor(0, 98);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.last().patchNo);
  tft.setCursor(35, 98);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.last().patchName);
}

FLASHMEM void renderReinitialisePage()
{
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(5, 53);
  tft.println(F("Initialise to"));
  tft.setCursor(5, 90);
  tft.println("panel setting");
}

FLASHMEM void renderPatchNamingPage()
{
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(0, 53);
  tft.println(F("Rename Patch"));
  tft.drawFastHLine(10, 63, tft.width() - 20, ST7735_RED);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 90);
  tft.println(newPatchName);
}

FLASHMEM void renderRecallPage()
{
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(0, 45);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.last().patchNo);
  tft.setCursor(35, 45);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.last().patchName);

  tft.fillRect(0, 56, tft.width(), 23, 0xA000);
  tft.setCursor(0, 72);
  tft.setTextColor(ST7735_YELLOW);
  tft.println(patches.first().patchNo);
  tft.setCursor(35, 72);
  tft.setTextColor(ST7735_WHITE);
  tft.println(patches.first().patchName);

  tft.setCursor(0, 98);
  tft.setTextColor(ST7735_YELLOW);
  patches.size() > 1 ? tft.println(patches[1].patchNo) : tft.println(patches.last().patchNo);
  tft.setCursor(35, 98);
  tft.setTextColor(ST7735_WHITE);
  patches.size() > 1 ? tft.println(patches[1].patchName) : tft.println(patches.last().patchName);
}

FLASHMEM void showRenamingPage(String newName) {
  LockGuard lock(displayLock);
  newPatchName = newName;
}

FLASHMEM void renderUpDown(uint16_t  x, uint16_t  y, uint16_t  colour) {
  //Produces up/down indicator glyph at x,y
  tft.setCursor(x, y);
  tft.fillTriangle(x, y, x + 8, y - 8, x + 16, y, colour);
  tft.fillTriangle(x, y + 4, x + 8, y + 12, x + 16, y + 4, colour);
}

FLASHMEM void renderSettingsPage() {
  tft.fillScreen(ST7735_BLACK);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(ST7735_YELLOW);
  tft.setTextSize(1);
  tft.setCursor(0, 53);
  tft.println(currentSettingsOption);
  if (currentSettingsPart == SETTINGS) renderUpDown(140, 42, ST7735_YELLOW);
  tft.drawFastHLine(10, 63, tft.width() - 20, ST7735_RED);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 90);
  tft.println(currentSettingsValue);
  if (currentSettingsPart == SETTINGSVALUE) renderUpDown(140, 80, ST7735_WHITE);
}

FLASHMEM void showCurrentParameterPage(String param, int pType, float val) {
  LockGuard lock(displayLock);
  currentParameter = param;
  currentValue = String(val);
  currentFloatValue = val;
  paramType = pType;
  startTimer();
}

FLASHMEM void showCurrentParameterPage(String param, String val, int pType) {
  LockGuard lock(displayLock);
  if (state == SETTINGS || state == SETTINGSVALUE)state = PARAMETER;//Exit settings page if showing
  currentParameter = param;
  currentValue = val;
  paramType = pType;
  startTimer();
}

FLASHMEM void showCurrentParameterPage(String param, String val) {
  showCurrentParameterPage(param, val, PARAMETER);
}

FLASHMEM void showPatchPage(String number, String patchName) {
  LockGuard lock(displayLock);
  currentPgmNum = number;
  currentPatchName = patchName;
}

FLASHMEM void showSettingsPage(const char *  option, const char * value, int settingsPart) {
  LockGuard lock(displayLock);
  currentSettingsOption = option;
  currentSettingsValue = value;
  currentSettingsPart = settingsPart;
}

FLASHMEM void enableScope(boolean enable) {
  enable ? global.scope.ScreenSetup(&tft) : global.scope.ScreenSetup(NULL);
}

// Send the frame buffer to the display using DMA so the CPU is free during the
// SPI transfer. Falls back to a blocking update if DMA can't be started.
// Period in samples of the lowest held note's audible oscillator, 0 if no notes are held.
// Locks the scope timebase to the waveform.
float scopePeriodSamples()
{
  VoiceGroup *g = groupvec[activeGroupIndex];
  int lowest = 128;
  for (uint8_t i = 0; i < g->size(); i++)
  {
    Voice *v = (*g)[i];
    if (v->on() && v->note() < lowest)
      lowest = v->note();
  }
  if (lowest > 127)
    return 0.0f;
  VoiceParams &p = g->params();
  int noteA = lowest + p.oscPitchA;
  int noteB = lowest + p.oscPitchB;
  float freq = 0.0f;
  if (g->getOscLevelA() > 0.01f && noteA >= 0 && noteA < 128)
    freq = NOTEFREQS[noteA];
  if (g->getOscLevelB() > 0.01f && noteB >= 0 && noteB < 128 && (freq == 0.0f || NOTEFREQS[noteB] < freq))
    freq = NOTEFREQS[noteB];
  if (freq == 0.0f)
    freq = NOTEFREQS[lowest];
  return AUDIO_SAMPLE_RATE_EXACT / freq;
}

void updateScreenDMA() {
  global.scope.setPeriod(scopePeriodSamples());
  global.scope.Display(); // Draws only if the scope is enabled
  // One DMA transfer for the whole frame (~14ms), polled. See ST7735_t3::oneShotUpdate().
  if (tft.oneShotUpdate()) {
    elapsedMillis wait;
    while (tft.oneShotUpdateActive()) {
      if (wait > 200) {
        // Should never happen; recover rather than freeze the display
        Serial.println(F("Display: DMA screen update timed out, reset"));
        tft.oneShotUpdateAbort();
        break;
      }
      threads.yield();
    }
  } else {
    tft.updateScreen();
  }
}

void displayThread() {
  threads.delay(2000); //Give bootup page chance to display
  elapsedMillis frameTime;
  while (1) {
    frameTime = 0;
    bool reinitPause = false;
    // Render into the frame buffer while holding the lock, so the main loop can't change
    // the Strings and patch list being drawn. The SPI transfer below runs without it.
    displayLock.lock();
    switch (state) {
      case PARAMETER:
        if ((millis() - timer) > DISPLAYTIMEOUT) {
          pickUpActive = false;
          renderCurrentPatchPage();
        } else {
          pickUpActive = pickUp;
          renderCurrentParameterPage();
        }
        break;
      case RECALL:
        renderRecallPage();
        break;
      case SAVE:
        renderSavePage();
        break;
      case REINITIALISE:
        renderReinitialisePage();
        reinitPause = true; // Shown for a second, below
        break;
      case PATCHNAMING:
        renderPatchNamingPage();
        break;
      case PATCH:
        renderCurrentPatchPage();
        break;
      case DELETE:
        renderDeletePatchPage();
        break;
      case DELETEMSG:
        renderDeleteMessagePage();
        break;
      case SETTINGS:
      case SETTINGSVALUE:
        renderSettingsPage();
        break;
    }
    displayLock.unlock();
    updateScreenDMA();
    if (reinitPause) {
      threads.delay(1000);
      state = PARAMETER;
    }
    // Yield the CPU back to the main loop (MIDI handling) for the rest of the frame
    if (frameTime < DISPLAY_FRAME_MS) threads.delay(DISPLAY_FRAME_MS - frameTime);
  }
}

void setupDisplay() {
  tft.initR(INITR_GREENTAB);
  
  tft.useFrameBuffer(true);
  tft.setRotation(3);
  tft.invertDisplay(true);
  renderBootUpPage();
  tft.updateScreen();
  int displayThreadId = threads.addThread(displayThread, 0, DISPLAY_THREAD_STACK);
  // Default slice is 10ms, which could hold off MIDI note handling by up to 10ms
  if (displayThreadId > 0) threads.setTimeSlice(displayThreadId, DISPLAY_THREAD_TICKS);
}

#endif
