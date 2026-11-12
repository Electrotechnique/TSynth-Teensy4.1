#ifndef TSYNTH_PICKUP_H
#define TSYNTH_PICKUP_H

// Knob pick-up: after a patch is recalled (or a parameter is changed by MIDI), a panel knob
// does nothing until it reaches the stored value, so turning it doesn't make the sound jump.
//
// Each knob has its own state. The stored value is converted to the knob's own range
// (0-127, or 0-255 for cutoff), as a band of positions for stepped controls such as
// waveform selectors. The knob picks up when it reaches or crosses that band, however
// fast it is turned. While waiting, the display shows which way to turn.
// MIDI CCs always apply, they are not gated.
//
// Included from TSynth.ino after the update functions it uses.

#define PU_KNOBS 32
#define PU_KNOB(mux, input) ((mux) * MUXCHANNELS + (input))
#define PU_CUTOFF PU_KNOB(1, MUX2_cutoff)

struct PickupState
{
  bool picked = true;       // knob is in control of its parameter
  bool targetValid = false; // lo/hi are up to date
  bool lastValid = false;   // lastPos holds a reading
  uint8_t lastPos = 0;      // previous knob position
  uint8_t lo = 0, hi = 0;   // knob positions matching the stored value
};

PickupState pickupState[PU_KNOBS];
int8_t pickupDirection = 0; // for the display: 1 turn up, -1 turn down, 0 picked up
bool controlFromPanel = false;

inline uint8_t pickupMaxPos(uint8_t k) { return k == PU_CUTOFF ? 255 : 127; }

// The parameter value a knob position gives, using the same mapping as myControlChange.
// Stepped controls return their step (e.g. waveform), compared for equality.
float pickupKnobValue(uint8_t k, uint8_t pos)
{
  VoiceGroup *g = groupvec[activeGroupIndex];
  switch (k)
  {
  case PU_KNOB(0, MUX1_noiseLevel):
    return LINEARCENTREZERO[pos];
  case PU_KNOB(0, MUX1_pitchLfoRate):
    return g->getPitchLfoMidiClockSync() ? lfoSyncFreq * LFOTEMPO[pos] : LFOMAXRATE * POWER[pos];
  case PU_KNOB(0, MUX1_pitchLfoWaveform):
    return getLFOWaveform(pos);
  case PU_KNOB(0, MUX1_pitchLfoAmount):
    return POWER[pos];
  case PU_KNOB(0, MUX1_detune):
    return 1.0f - (MAXDETUNE * POWER[pos]);
  case PU_KNOB(0, MUX1_oscMix):
    return LINEAR[OSCMIXB[pos]] - LINEAR[OSCMIXA[pos]];
  case PU_KNOB(0, MUX1_filterAttack):
  case PU_KNOB(0, MUX1_filterDecay):
  case PU_KNOB(1, MUX2_filterRelease):
  case PU_KNOB(1, MUX2_attack):
  case PU_KNOB(1, MUX2_decay):
  case PU_KNOB(1, MUX2_release):
    return ENVTIMES[pos];
  case PU_KNOB(0, MUX1_pwmAmountA):
  case PU_KNOB(0, MUX1_pwmAmountB):
    return LINEARCENTREZERO[pos];
  case PU_KNOB(0, MUX1_waveformA):
    return getWaveformA(pos);
  case PU_KNOB(0, MUX1_waveformB):
    return getWaveformB(pos);
  case PU_KNOB(0, MUX1_pitchA):
  case PU_KNOB(0, MUX1_pitchB):
    return PITCH[pos];
  case PU_KNOB(0, MUX1_pwmRate):
    return PWMRATE[pos];
  case PU_KNOB(0, MUX1_pitchEnv):
    return LINEARCENTREZERO[pos] * OSCMODMIXERMAX;
  case PU_KNOB(1, MUX2_sustain):
  case PU_KNOB(1, MUX2_filterSustain):
  case PU_KNOB(1, MUX2_FXMix):
    return LINEAR[pos];
  case PU_KNOB(1, MUX2_filterLFOAmount):
    return LINEAR[pos] * FILTERMODMIXERMAX;
  case PU_KNOB(1, MUX2_FXAmount):
    return ENSEMBLE_LFO[pos];
  case PU_KNOB(1, MUX2_glide):
    return POWER[pos];
  case PU_KNOB(1, MUX2_filterEnv):
    return LINEARCENTREZERO[pos] * FILTERMODMIXERMAX;
  case PU_KNOB(1, MUX2_filterType):
    return LINEAR_FILTERMIXER[pos];
  case PU_KNOB(1, MUX2_resonance):
    return (14.29f * POWER[pos]) + 0.71f;
  case PU_CUTOFF:
    return FILTERFREQS256[pos];
  case PU_KNOB(1, MUX2_filterLFORate):
    return g->getFilterLfoMidiClockSync() ? lfoSyncFreq * LFOTEMPO[pos] : LFOMAXRATE * POWER[pos];
  case PU_KNOB(1, MUX2_filterLFOWaveform):
    return getLFOWaveform(pos);
  }
  return 0.0f;
}

// The current (stored) value of the parameter a knob controls
float pickupCurrentValue(uint8_t k)
{
  VoiceGroup *g = groupvec[activeGroupIndex];
  switch (k)
  {
  case PU_KNOB(0, MUX1_noiseLevel):
    return g->getPinkNoiseLevel() - g->getWhiteNoiseLevel();
  case PU_KNOB(0, MUX1_pitchLfoRate):
    return g->getPitchLfoRate();
  case PU_KNOB(0, MUX1_pitchLfoWaveform):
    return g->getPitchLfoWaveform();
  case PU_KNOB(0, MUX1_pitchLfoAmount):
    return g->getPitchLfoAmount();
  case PU_KNOB(0, MUX1_detune):
    return g->params().detune;
  case PU_KNOB(0, MUX1_oscMix):
    return g->getOscLevelB() - g->getOscLevelA();
  case PU_KNOB(0, MUX1_filterAttack):
    return g->getFilterAttack();
  case PU_KNOB(0, MUX1_filterDecay):
    return g->getFilterDecay();
  case PU_KNOB(0, MUX1_pwmAmountA):
    return g->getPwA();
  case PU_KNOB(0, MUX1_waveformA):
    return g->getWaveformA();
  case PU_KNOB(0, MUX1_pitchA):
    return g->params().oscPitchA;
  case PU_KNOB(0, MUX1_pwmAmountB):
    return g->getPwB();
  case PU_KNOB(0, MUX1_waveformB):
    return g->getWaveformB();
  case PU_KNOB(0, MUX1_pitchB):
    return g->params().oscPitchB;
  case PU_KNOB(0, MUX1_pwmRate):
    return g->getPwmRate();
  case PU_KNOB(0, MUX1_pitchEnv):
    return g->getPitchEnvelope();
  case PU_KNOB(1, MUX2_release):
    return g->getAmpRelease();
  case PU_KNOB(1, MUX2_sustain):
    return g->getAmpSustain();
  case PU_KNOB(1, MUX2_decay):
    return g->getAmpDecay();
  case PU_KNOB(1, MUX2_attack):
    return g->getAmpAttack();
  case PU_KNOB(1, MUX2_filterLFOAmount):
    return g->getFilterLfoAmt();
  case PU_KNOB(1, MUX2_FXMix):
    return g->getEffectMix();
  case PU_KNOB(1, MUX2_FXAmount):
    return g->getEffectAmount();
  case PU_KNOB(1, MUX2_glide):
    return g->params().glideSpeed;
  case PU_KNOB(1, MUX2_filterEnv):
    return g->getFilterEnvelope();
  case PU_KNOB(1, MUX2_filterRelease):
    return g->getFilterRelease();
  case PU_KNOB(1, MUX2_filterSustain):
    return g->getFilterSustain();
  case PU_KNOB(1, MUX2_filterType):
    return g->getFilterMixer();
  case PU_KNOB(1, MUX2_resonance):
    return g->getResonance();
  case PU_CUTOFF:
    return g->getCutoff();
  case PU_KNOB(1, MUX2_filterLFORate):
    return g->getFilterLfoRate();
  case PU_KNOB(1, MUX2_filterLFOWaveform):
    return g->getFilterLfoWaveform();
  }
  return 0.0f;
}

// Name and stored value for the display while a knob is waiting to pick up
FLASHMEM String pickupName(uint8_t k)
{
  switch (k)
  {
  case PU_KNOB(0, MUX1_noiseLevel): return F("Noise Level");
  case PU_KNOB(0, MUX1_pitchLfoRate): return F("LFO Rate");
  case PU_KNOB(0, MUX1_pitchLfoWaveform): return F("Pitch LFO");
  case PU_KNOB(0, MUX1_pitchLfoAmount): return F("LFO Amount");
  case PU_KNOB(0, MUX1_detune): return F("Detune");
  case PU_KNOB(0, MUX1_oscMix): return F("Osc Mix 1:2");
  case PU_KNOB(0, MUX1_filterAttack): return F("Filter Attack");
  case PU_KNOB(0, MUX1_filterDecay): return F("Filter Decay");
  case PU_KNOB(0, MUX1_pwmAmountA): return F("PW/PWM 1");
  case PU_KNOB(0, MUX1_waveformA): return F("Waveform 1");
  case PU_KNOB(0, MUX1_pitchA): return F("Pitch 1");
  case PU_KNOB(0, MUX1_pwmAmountB): return F("PW/PWM 2");
  case PU_KNOB(0, MUX1_waveformB): return F("Waveform 2");
  case PU_KNOB(0, MUX1_pitchB): return F("Pitch 2");
  case PU_KNOB(0, MUX1_pwmRate): return F("PWM Rate");
  case PU_KNOB(0, MUX1_pitchEnv): return F("Pitch Env Amt");
  case PU_KNOB(1, MUX2_release): return F("Release");
  case PU_KNOB(1, MUX2_sustain): return F("Sustain");
  case PU_KNOB(1, MUX2_decay): return F("Decay");
  case PU_KNOB(1, MUX2_attack): return F("Attack");
  case PU_KNOB(1, MUX2_filterLFOAmount): return F("LFO Amount");
  case PU_KNOB(1, MUX2_FXMix): return F("Effect Mix");
  case PU_KNOB(1, MUX2_FXAmount): return F("Effect Amt");
  case PU_KNOB(1, MUX2_glide): return F("Glide");
  case PU_KNOB(1, MUX2_filterEnv): return F("Filter Env.");
  case PU_KNOB(1, MUX2_filterRelease): return F("Filter Release");
  case PU_KNOB(1, MUX2_filterSustain): return F("Filter Sustain");
  case PU_KNOB(1, MUX2_filterType): return F("Filter Type");
  case PU_KNOB(1, MUX2_resonance): return F("Resonance");
  case PU_CUTOFF: return F("Cutoff");
  case PU_KNOB(1, MUX2_filterLFORate): return F("LFO Rate");
  case PU_KNOB(1, MUX2_filterLFOWaveform): return F("Filter LFO");
  }
  return F("");
}

FLASHMEM String pickupValueStr(uint8_t k)
{
  float v = pickupCurrentValue(k);
  switch (k)
  {
  case PU_KNOB(0, MUX1_waveformA):
  case PU_KNOB(0, MUX1_waveformB):
  case PU_KNOB(0, MUX1_pitchLfoWaveform):
  case PU_KNOB(1, MUX2_filterLFOWaveform):
    return getWaveformStr((int)v);
  case PU_CUTOFF:
    return String(int(v)) + F(" Hz");
  case PU_KNOB(0, MUX1_filterAttack):
  case PU_KNOB(0, MUX1_filterDecay):
  case PU_KNOB(1, MUX2_filterRelease):
  case PU_KNOB(1, MUX2_attack):
  case PU_KNOB(1, MUX2_decay):
  case PU_KNOB(1, MUX2_release):
    return String(int(v)) + F(" ms");
  case PU_KNOB(0, MUX1_pitchA):
  case PU_KNOB(0, MUX1_pitchB):
    return String(int(v));
  }
  return String(v);
}

// Knob controlling a MIDI CC, or -1
int pickupKnobForCC(byte control)
{
  switch (control)
  {
  case CCnoiseLevel: return PU_KNOB(0, MUX1_noiseLevel);
  case CCoscLfoRate: return PU_KNOB(0, MUX1_pitchLfoRate);
  case CCoscLfoWaveform: return PU_KNOB(0, MUX1_pitchLfoWaveform);
  case CCosclfoamt: return PU_KNOB(0, MUX1_pitchLfoAmount);
  case CCdetune: return PU_KNOB(0, MUX1_detune);
  case CCoscLevelA:
  case CCoscLevelB: return PU_KNOB(0, MUX1_oscMix);
  case CCfilterattack: return PU_KNOB(0, MUX1_filterAttack);
  case CCfilterdecay: return PU_KNOB(0, MUX1_filterDecay);
  case CCpwA: return PU_KNOB(0, MUX1_pwmAmountA);
  case CCoscwaveformA: return PU_KNOB(0, MUX1_waveformA);
  case CCpitchA: return PU_KNOB(0, MUX1_pitchA);
  case CCpwB: return PU_KNOB(0, MUX1_pwmAmountB);
  case CCoscwaveformB: return PU_KNOB(0, MUX1_waveformB);
  case CCpitchB: return PU_KNOB(0, MUX1_pitchB);
  case CCpwmRate: return PU_KNOB(0, MUX1_pwmRate);
  case CCpitchenv: return PU_KNOB(0, MUX1_pitchEnv);
  case CCamprelease: return PU_KNOB(1, MUX2_release);
  case CCampsustain: return PU_KNOB(1, MUX2_sustain);
  case CCampdecay: return PU_KNOB(1, MUX2_decay);
  case CCampattack: return PU_KNOB(1, MUX2_attack);
  case CCfilterlfoamt: return PU_KNOB(1, MUX2_filterLFOAmount);
  case CCfxmix: return PU_KNOB(1, MUX2_FXMix);
  case CCfxamt: return PU_KNOB(1, MUX2_FXAmount);
  case CCglide: return PU_KNOB(1, MUX2_glide);
  case CCfilterenv: return PU_KNOB(1, MUX2_filterEnv);
  case CCfilterrelease: return PU_KNOB(1, MUX2_filterRelease);
  case CCfiltersustain: return PU_KNOB(1, MUX2_filterSustain);
  case CCfiltermixer: return PU_KNOB(1, MUX2_filterType);
  case CCfilterres: return PU_KNOB(1, MUX2_resonance);
  case CCfilterfreq: return PU_CUTOFF;
  case CCfilterlforate: return PU_KNOB(1, MUX2_filterLFORate);
  case CCfilterlfowaveform: return PU_KNOB(1, MUX2_filterLFOWaveform);
  }
  return -1;
}

// Knobs must reach the stored values again (patch recalled, pick-up turned on)
void pickupRearmAll()
{
  for (uint8_t k = 0; k < PU_KNOBS; k++)
  {
    pickupState[k].picked = false;
    pickupState[k].targetValid = false;
  }
  pickupDirection = 0;
}

// A parameter was changed by MIDI, so its knob no longer matches it
void pickupRearmCC(byte control)
{
  int k = pickupKnobForCC(control);
  if (k >= 0)
  {
    pickupState[k].picked = false;
    pickupState[k].targetValid = false;
  }
}

// All knobs control their parameters straight away (reinitialise to panel)
void pickupReleaseAll()
{
  for (uint8_t k = 0; k < PU_KNOBS; k++)
    pickupState[k].picked = true;
  pickupDirection = 0;
}

// Knob positions matching the stored value: the nearest position, widened to every
// position giving the same value (steps of stepped controls, flat parts of tables)
void pickupFindTarget(uint8_t k)
{
  PickupState &s = pickupState[k];
  const uint8_t maxPos = pickupMaxPos(k);
  const float v = pickupCurrentValue(k);
  uint8_t best = 0;
  float bestErr = 3.4e38f;
  for (uint16_t p = 0; p <= maxPos; p++)
  {
    float e = fabsf(pickupKnobValue(k, p) - v);
    if (e < bestErr)
    {
      bestErr = e;
      best = p;
    }
  }
  const float bestVal = pickupKnobValue(k, best);
  const float same = 1e-5f * fmaxf(1.0f, fabsf(bestVal));
  uint8_t lo = best, hi = best;
  while (lo > 0 && fabsf(pickupKnobValue(k, lo - 1) - bestVal) <= same)
    lo--;
  while (hi < maxPos && fabsf(pickupKnobValue(k, hi + 1) - bestVal) <= same)
    hi++;
  s.lo = lo;
  s.hi = hi;
  s.targetValid = true;
}

// Called for every panel knob movement. Returns true if the knob should change its parameter.
bool pickupCheck(uint8_t k, uint8_t pos)
{
  PickupState &s = pickupState[k];
  const uint8_t prev = s.lastValid ? s.lastPos : pos;
  s.lastPos = pos;
  s.lastValid = true;

  if (!pickUp || s.picked)
  {
    pickupDirection = 0;
    return true;
  }
  if (!s.targetValid)
    pickupFindTarget(k);

  // Picks up when the movement since the last reading reaches or crosses the target
  const uint8_t from = min(prev, pos), to = max(prev, pos);
  if (to >= s.lo && from <= s.hi)
  {
    s.picked = true;
    pickupDirection = 0;
    return true;
  }
  pickupDirection = pos < s.lo ? 1 : -1;
  showCurrentParameterPage(pickupName(k), pickupValueStr(k));
  return false;
}

#endif
