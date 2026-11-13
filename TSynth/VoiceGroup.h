#ifndef TSYNTH_VOICE_GROUP_H
#define TSYNTH_VOICE_GROUP_H

#include <Arduino.h>
#include <vector>
#include <stdint.h>
#include <stddef.h>
#include "Voice.h"
#include "MonoNoteHistory.h"
#include "Constants.h"

#define VG_FOR_EACH_OSC(CMD) VG_FOR_EACH_VOICE(voices[i]->patch().CMD)
#define VG_FOR_EACH_VOICE(CMD)                  \
    for (uint8_t i = 0; i < voices.size(); i++) \
    {                                           \
        CMD;                                    \
    }

// These are here because of a Settings.h circular dependency.
// Maximum combined gain of Osc 1 + Osc 2 + XOR into the waveform mixer.
// Band-limited squares peak at ~0.95 FS, so a higher total clips the 16-bit
// mixer and the clipped edges alias badly.
#define OSCMIX_MAX_SUM 1.0f

#define MONOPHONIC_OFF 0
#define MONOPHONIC_LAST 1
#define MONOPHONIC_FIRST 2
#define MONOPHONIC_HIGHEST 3
#define MONOPHONIC_LOWEST 4
// Legato isn't supported, the envelope state from the previous note needs to transfer to the new note
#define MONOPHONIC_LEGATO 5

const static uint32_t WAVEFORM_PARABOLIC = 103;
const static uint32_t WAVEFORM_HARMONIC = 104;

class VoiceGroup
{
private:
    String patchName;
    uint32_t patchIndex;

    // Audio Objects
    PatchShared &shared;
    std::vector<Voice *> voices;

    // Patch Configs
    bool midiClockSignal; // midiCC clock
    bool filterLfoMidiClockSync;
    bool pitchLFOMidiClockSync;

    VoiceParams _params;
    uint8_t unisonNotesOn;
    uint8_t voicesOn;
    uint8_t monoNote;
    uint8_t monophonic;
    uint8_t waveformA;
    uint8_t waveformB;
    float pitchEnvelope;
    uint8_t pwmSource;
    float pwmAmtA;
    float pwmAmtB;
    float pwA;
    float pwB;
    float pwmRate;
    uint8_t oscFX;
    float oscLevelA;
    float oscLevelB;
    float waveformLevels[4] = {1.0f, 1.0f, 1.0f, 0.0f}; // Requested waveform mixer levels (before headroom scaling)
    float noiseContribution = 0.0f;                      // Noise level feeding waveform mixer channel 2
    float oscMixCompensation = 1.0f;                     // Restores loudness lost to headroom scaling, applied after the filter
    float cutoff;
    float resonance;
    float filterMixer;
    float filterEnvelope;
    float filterAttack;
    float filterDecay;
    float filterSustain;
    float filterRelease;
    float ampAttack;
    float ampDecay;
    float ampSustain;
    float ampRelease;
    float filterLfoRetrig;
    float filterLfoRate;
    float filterLfoAmt;
    uint8_t filterLfoWaveform;
    float pinkLevel;
    float whiteLevel;
    uint8_t pitchLfoWaveform;
    bool pitchLfoRetrig;
    float pitchLfoAmount;
    float pitchLfoRate;
    float modWhAmount;
    float effectAmount;
    float effectMix;

    // Used to remember active mono notes.
    MonoNoteHistory noteStack;

public:
    VoiceGroup(PatchShared &shared_) : patchName(""),
                                       patchIndex(0),
                                       shared(shared_),
                                       midiClockSignal(false),
                                       filterLfoMidiClockSync(false),
                                       pitchLFOMidiClockSync(false),
                                       unisonNotesOn(0),
                                       voicesOn(0),
                                       monophonic(0),
                                       waveformA(WAVEFORM_SQUARE),
                                       waveformB(WAVEFORM_SQUARE),
                                       pitchEnvelope(0.0),
                                       pwmSource(PWMSOURCELFO),
                                       pwmAmtA(1.0),
                                       pwmAmtB(1.0),
                                       pwA(0.0),
                                       pwB(0.0),
                                       pwmRate(0.5),
                                       oscFX(0),
                                       oscLevelA(1.0),
                                       oscLevelB(1.0),
                                       cutoff(12000.0),
                                       resonance(1.1),
                                       filterMixer(0.0),
                                       filterEnvelope(0.0),
                                       filterAttack(100.0),
                                       filterDecay(350.0),
                                       filterSustain(0.7),
                                       filterRelease(300.0),
                                       ampAttack(10.0),
                                       ampDecay(35.0),
                                       ampSustain(1.0),
                                       ampRelease(300.0),
                                       filterLfoRetrig(false),
                                       filterLfoRate(2.0),
                                       filterLfoAmt(0.0),
                                       filterLfoWaveform(WAVEFORM_SINE),
                                       pinkLevel(0),
                                       whiteLevel(0),
                                       pitchLfoWaveform(WAVEFORM_SINE),
                                       pitchLfoRetrig(false),
                                       pitchLfoAmount(0),
                                       pitchLfoRate(4.0),
                                       modWhAmount(0.0),
                                       effectAmount(1.0),
                                       effectMix(0.0)
    {
        _params.keytrackingAmount = 0.5; //Half - MIDI CC & settings option
        _params.filterOctave = 4.0f;
        _params.mixerLevel = 0.0;
        _params.prevNote = 48;
        _params.glideSpeed = 0.0;
        _params.unisonMode = 0;
        _params.chordDetune = 0;
        _params.detune = 0;
        _params.oscPitchA = 0;
        _params.oscPitchB = 12;

        shared.noiseMixer.gain(0, 0);
        shared.noiseMixer.gain(1, 0);

        shared.pitchLfo.begin(WAVEFORM_SINE);
        shared.pwmLfoA.amplitude(ONE);
        shared.pwmLfoA.begin(PWMWAVEFORM);
        shared.pwmLfoB.amplitude(ONE);
        shared.pwmLfoB.begin(PWMWAVEFORM);
        shared.pwmLfoB.phase(10.0f); //Off set phase of second osc

        setEffectAmount(effectAmount);
        setEffectMix(effectMix);
    }

    inline uint8_t size() { return this->voices.size(); }
    inline String getPatchName() { return this->patchName; }
    bool getFilterLfoMidiClockSync() { return filterLfoMidiClockSync; }
    bool getPitchLfoMidiClockSync() { return pitchLFOMidiClockSync; }
    inline uint32_t getPatchIndex() { return this->patchIndex; }
    float getKeytrackingAmount() { return this->_params.keytrackingAmount; }
    uint8_t getMonophonicMode() { return this->monophonic; }
    uint32_t getWaveformA() { return waveformA; }
    uint32_t getWaveformB() { return waveformB; }
    float getPitchEnvelope() { return pitchEnvelope; }
    uint8_t getPwmSource() { return pwmSource; }
    float getPwA() { return pwA; }
    float getPwB() { return pwB; }
    float getPwmAmtA() { return pwmAmtA; }
    float getPwmAmtB() { return pwmAmtB; }
    float getPwmRate() { return pwmRate; }
    uint8_t getOscFX() { return oscFX; }
    float getOscLevelA() { return oscLevelA; }
    float getOscLevelB() { return oscLevelB; }
    float getCutoff() { return cutoff; }
    float getResonance() { return resonance; }
    float getFilterMixer() { return filterMixer; }
    float getFilterEnvelope() { return filterEnvelope; }
    float getFilterAttack() { return filterAttack; }
    float getFilterDecay() { return filterDecay; }
    float getFilterSustain() { return filterSustain; }
    float getFilterRelease() { return filterRelease; }
    float getAmpAttack() { return ampAttack; }
    float getAmpDecay() { return ampDecay; }
    float getAmpSustain() { return ampSustain; }
    float getAmpRelease() { return ampRelease; }
    bool getFilterLfoRetrig() { return filterLfoRetrig; }
    float getFilterLfoRate() { return filterLfoRate; }
    float getFilterLfoAmt() { return filterLfoAmt; }
    uint32_t getFilterLfoWaveform() { return filterLfoWaveform; }
    float getPinkNoiseLevel() { return pinkLevel; }
    float getWhiteNoiseLevel() { return whiteLevel; }
    bool getPitchLfoRetrig() { return pitchLfoRetrig; }
    uint32_t getPitchLfoWaveform() { return pitchLfoWaveform; }
    float getPitchLfoAmount() { return pitchLfoAmount; }
    float getPitchLfoRate() { return pitchLfoRate; }
    float getModWhAmount() { return modWhAmount; }
    float getEffectAmount() { return effectAmount; }
    float getEffectMix() { return effectMix; }

    inline void setPatchName(String name)
    {
        this->patchName = name;
    }

    inline void setPatchIndex(uint32_t index)
    {
        this->patchIndex = index;
    }

    void setWaveformA(uint32_t waveform)
    {
        if (waveformA == waveform)
            return;
        waveformA = waveform;

        int temp = waveform;
        if (waveform == WAVEFORM_PARABOLIC)
        {
            VG_FOR_EACH_OSC(waveformMod_a.arbitraryWaveform(PARABOLIC_WAVE, AWFREQ));
            temp = WAVEFORM_ARBITRARY;
        }
        if (waveform == WAVEFORM_HARMONIC)
        {
            VG_FOR_EACH_OSC(waveformMod_a.arbitraryWaveform(HARMONIC_WAVE, AWFREQ));
            temp = WAVEFORM_ARBITRARY;
        }

        VG_FOR_EACH_OSC(waveformMod_a.begin(temp))
    }

    void setWaveformB(uint32_t waveform)
    {
        if (waveformB == waveform)
            return;
        waveformB = waveform;

        int temp = waveform;
        if (waveform == WAVEFORM_PARABOLIC)
        {
            VG_FOR_EACH_OSC(waveformMod_b.arbitraryWaveform(PARABOLIC_WAVE, AWFREQ));
            temp = WAVEFORM_ARBITRARY;
        }
        if (waveform == WAVEFORM_HARMONIC)
        {
            VG_FOR_EACH_OSC(waveformMod_b.arbitraryWaveform(PPG_WAVE, AWFREQ));
            temp = WAVEFORM_ARBITRARY;
        }

        VG_FOR_EACH_OSC(waveformMod_b.begin(temp))
    }

    void setPwmRate(float value)
    {
        pwmRate = value;

        shared.pwmLfoA.frequency(pwmRate);
        shared.pwmLfoB.frequency(pwmRate);

        if (pwmRate == PWMRATE_PW_MODE)
        {
            //Set to fixed PW mode
            this->setPwmMixerALFO(0); //LFO Source off
            this->setPwmMixerBLFO(0);
            this->setPwmMixerAFEnv(0); //Filter Env Source off
            this->setPwmMixerBFEnv(0);
            this->setPwmMixerAPW(1); //Manually adjustable pulse width on
            this->setPwmMixerBPW(1);
        }
        else if (pwmRate == PWMRATE_SOURCE_FILTER_ENV)
        {
            //Set to Filter Env Mod source
            this->setPWMSource(PWMSOURCEFENV);
            this->setPwmMixerAFEnv(this->getPwmAmtA());
            this->setPwmMixerBFEnv(this->getPwmAmtB());
            this->setPwmMixerAPW(0);
            this->setPwmMixerBPW(0);
        }
        else
        {
            this->setPWMSource(PWMSOURCELFO);
            this->setPwmMixerAPW(0);
            this->setPwmMixerBPW(0);
        }
    }

    void setPitchEnvelope(float value)
    {
        pitchEnvelope = value;
        VG_FOR_EACH_OSC(oscModMixer_a.gain(1, value))
        VG_FOR_EACH_OSC(oscModMixer_b.gain(1, value))
    }

    void setPwmMixerALFO(float value)
    {
        VG_FOR_EACH_OSC(pwMixer_a.gain(0, value));
    }

    void setPwmMixerBLFO(float value)
    {
        VG_FOR_EACH_OSC(pwMixer_b.gain(0, value));
    }

    void setPwmMixerAPW(float value)
    {
        VG_FOR_EACH_OSC(pwMixer_a.gain(1, value));
    }

    void setPwmMixerBPW(float value)
    {
        VG_FOR_EACH_OSC(pwMixer_b.gain(1, value));
    }

    void setPwmMixerAFEnv(float value)
    {
        VG_FOR_EACH_OSC(pwMixer_a.gain(2, value));
    }

    void setPwmMixerBFEnv(float value)
    {
        VG_FOR_EACH_OSC(pwMixer_b.gain(2, value));
    }

    // MIDI-CC Only
    void overridePwmAmount(float value)
    {
        pwmAmtA = value;
        pwmAmtB = value;
        pwA = 0;
        pwB = 0;
        this->setPwmMixerALFO(value);
        this->setPwmMixerBLFO(value);
    }

    void setPWA(float valuePwA, float valuePwmAmtA)
    {
        pwA = valuePwA;
        pwmAmtA = valuePwmAmtA;
        if (pwmRate == PWMRATE_PW_MODE)
        {
            //fixed PW is enabled
            this->setPwmMixerALFO(0);
            this->setPwmMixerBLFO(0);
            this->setPwmMixerAFEnv(0);
            this->setPwmMixerBFEnv(0);
            this->setPwmMixerAPW(1);
            this->setPwmMixerBPW(1);
        }
        else
        {
            this->setPwmMixerAPW(0);
            this->setPwmMixerBPW(0);
            if (pwmSource == PWMSOURCELFO)
            {
                //PW alters PWM LFO amount for waveform A
                this->setPwmMixerALFO(pwmAmtA);
            }
            else
            {
                //PW alters PWM Filter Env amount for waveform A
                this->setPwmMixerAFEnv(pwmAmtA);
            }
        }

        //Prevent silence when pw = +/-1.0 on pulse
        float pwA_Adj = pwA;
        if (pwA > 0.98)
            pwA_Adj = 0.98f;
        if (pwA < -0.98)
            pwA_Adj = -0.98f;
        shared.pwa.amplitude(pwA_Adj);
    }

    void setPWB(float valuePwA, float valuePwmAmtA)
    {
        pwB = valuePwA;
        pwmAmtB = valuePwmAmtA;
        if (pwmRate == PWMRATE_PW_MODE)
        {
            //fixed PW is enabled
            this->setPwmMixerALFO(0);
            this->setPwmMixerBLFO(0);
            this->setPwmMixerAFEnv(0);
            this->setPwmMixerBFEnv(0);
            this->setPwmMixerAPW(1);
            this->setPwmMixerBPW(1);
        }
        else
        {
            this->setPwmMixerAPW(0);
            this->setPwmMixerBPW(0);
            if (pwmSource == PWMSOURCELFO)
            {
                //PW alters PWM LFO amount for waveform B
                this->setPwmMixerBLFO(pwmAmtB);
            }
            else
            {
                //PW alters PWM Filter Env amount for waveform B
                this->setPwmMixerBFEnv(pwmAmtB);
            }
        }

        //Prevent silence when pw = +/-1.0 on pulse
        float pwB_Adj = pwB;
        if (pwB > 0.98)
            pwB_Adj = 0.98f;
        if (pwB < -0.98)
            pwB_Adj = -0.98f;
        shared.pwb.amplitude(pwB_Adj);
    }

    void setPWMSource(uint8_t value)
    {
        pwmSource = value;
        if (value == PWMSOURCELFO)
        {
            //Set filter mod to zero
            this->setPwmMixerAFEnv(0);
            this->setPwmMixerBFEnv(0);

            //Set LFO mod
            if (pwmRate > PWMRATE_SOURCE_FILTER_ENV)
            {
                this->setPwmMixerALFO(pwmAmtA); //Set LFO mod
                this->setPwmMixerBLFO(pwmAmtB); //Set LFO mod
            }
        }
        else
        {
            this->setPwmMixerALFO(0); //Set LFO mod to zero
            this->setPwmMixerBLFO(0); //Set LFO mod to zero
            if (pwmRate > PWMRATE_SOURCE_FILTER_ENV)
            {
                this->setPwmMixerAFEnv(pwmAmtA); //Set filter mod
                this->setPwmMixerBFEnv(pwmAmtB); //Set filter mod
            }
        }
    }

    void setWaveformMixerLevel(int channel, float level)
    {
        waveformLevels[channel] = level;
        applyWaveformMixerLevels();
    }

    // Scale the oscillator and noise channels together so their sum can't clip the mixer
    void applyWaveformMixerLevels()
    {
        float sum = waveformLevels[0] + waveformLevels[1] + waveformLevels[3] + waveformLevels[2] * noiseContribution;
        float scale = sum > OSCMIX_MAX_SUM ? OSCMIX_MAX_SUM / sum : 1.0f;
        // Uncorrelated sources add by power, so compensate by sqrt to keep perceived level
        oscMixCompensation = 1.0f / sqrtf(scale);
        VG_FOR_EACH_OSC(waveformMixer_.gain(0, waveformLevels[0] * scale))
        VG_FOR_EACH_OSC(waveformMixer_.gain(1, waveformLevels[1] * scale))
        VG_FOR_EACH_OSC(waveformMixer_.gain(2, waveformLevels[2] * scale))
        VG_FOR_EACH_OSC(waveformMixer_.gain(3, waveformLevels[3] * scale))
    }

    void updateNoiseContribution(float gain)
    {
        noiseContribution = (pinkLevel + whiteLevel) * gain;
        applyWaveformMixerLevels();
    }

    // Call regularly from the main loop: starts note-ons deferred for stolen voices
    void serviceVoices()
    {
        for (uint8_t i = 0; i < voices.size(); i++)
            voices[i]->service();
    }

    void setOscModMixerA(int channel, float level)
    {
        VG_FOR_EACH_OSC(oscModMixer_a.gain(channel, level))
    }

    void setOscModMixerB(int channel, float level)
    {
        VG_FOR_EACH_OSC(oscModMixer_b.gain(channel, level))
    }

    void setOscFXCombineMode(AudioEffectDigitalCombine::combineMode mode)
    {
        VG_FOR_EACH_OSC(oscFX_.setCombineMode(mode))
    }

    void setOscLevelA(float value)
    {
        oscLevelA = value;

        switch (oscFX)
        {
        case 1:                                                       //XOR
            setWaveformMixerLevel(0, oscLevelA);                      //Osc 1 (A)
            setWaveformMixerLevel(3, (oscLevelA + oscLevelB) / 2.0f); //oscFX XOR level
            break;
        case 2: //XMod
            //osc A sounds with increasing osc B mod
            if (oscLevelA == 1.0f && oscLevelB <= 1.0f)
            {
                setOscModMixerA(3, 1 - oscLevelB); //Feed from Osc 2 (B)
                setWaveformMixerLevel(0, ONE);     //Osc 1 (A)
                setWaveformMixerLevel(1, 0);       //Osc 2 (B)
            }
            break;
        case 0:                                  //None
            setOscModMixerA(3, 0);               //Feed from Osc 2 (B)
            setWaveformMixerLevel(0, oscLevelA); //Osc 1 (A)
            setWaveformMixerLevel(3, 0);         //XOR
            break;
        }
    }

    void setOscLevelB(float value)
    {
        oscLevelB = value;

        switch (oscFX)
        {
        case 1:                                                       //XOR
            setWaveformMixerLevel(1, oscLevelB);                      //Osc 2 (B)
            setWaveformMixerLevel(3, (oscLevelA + oscLevelB) / 2.0f); //oscFX XOR level
            break;
        case 2: //XMod
            //osc B sounds with increasing osc A mod
            if (oscLevelB == 1.0f && oscLevelA < 1.0f)
            {
                setOscModMixerB(3, 1 - oscLevelA); //Feed from Osc 1 (A)
                setWaveformMixerLevel(0, 0);       //Osc 1 (A)
                setWaveformMixerLevel(1, ONE);     //Osc 2 (B)
            }
            break;
        case 0:                                  //None
            setOscModMixerB(3, 0);               //Feed from Osc 1 (A)
            setWaveformMixerLevel(1, oscLevelB); //Osc 2 (B)
            setWaveformMixerLevel(3, 0);         //XOR
            break;
        }
    }

    void setOscFX(uint8_t value)
    {
        oscFX = value;

        if (oscFX == 2)
        {
            if (oscLevelA == 1.0f && oscLevelB <= 1.0f)
            {
                setOscModMixerA(3, 1 - oscLevelB); //Feed from Osc 2 (B)
                setWaveformMixerLevel(0, ONE);     //Osc 1 (A)
                setWaveformMixerLevel(1, 0);       //Osc 2 (B)
            }
            else
            {
                setOscModMixerB(3, 1 - oscLevelA); //Feed from Osc 1 (A)
                setWaveformMixerLevel(0, 0);       //Osc 1 (A)
                setWaveformMixerLevel(1, ONE);     //Osc 2 (B)
            }
            //Set XOR type off
            setOscFXCombineMode(AudioEffectDigitalCombine::OFF);
            setWaveformMixerLevel(3, 0); //XOR
        }
        else if (oscFX == 1)
        {
            setOscModMixerA(3, 0); //XMod off
            setOscModMixerB(3, 0); //XMod off
            //XOR 'Ring Mod' type effect
            setOscFXCombineMode(AudioEffectDigitalCombine::XOR);
            setWaveformMixerLevel(3, (oscLevelA + oscLevelB) / 2.0f); //XOR on
        }
        else
        {
            setOscModMixerA(3, 0);                               //XMod off
            setOscModMixerB(3, 0);                               //XMod off
            setOscFXCombineMode(AudioEffectDigitalCombine::OFF); //Set XOR type off
            setWaveformMixerLevel(0, oscLevelA);                 //Osc 1 (A)
            setWaveformMixerLevel(1, oscLevelB);                 //Osc 2 (B)
            setWaveformMixerLevel(3, 0);                         //XOR off
        }
    }

    void setCutoff(float value)
    {
        this->cutoff = value;

        VG_FOR_EACH_OSC(filter_.frequency(value))

        float filterOctave = 0.0;
        //Altering filterOctave to give more cutoff width for deeper bass, but sharper cuttoff at higher frequncies
        if (value <= 2000)
        {
            filterOctave = 4.0f + ((2000.0f - value) / 710.0f); //More bass
        }
        else if (value > 2000 && value <= 3500)
        {
            filterOctave = 3.0f + ((3500.0f - value) / 1500.0f); //Sharper cutoff
        }
        else if (value > 3500 && value <= 7000)
        {
            filterOctave = 2.0f + ((7000.0f - value) / 4000.0f); //Sharper cutoff
        }
        else
        {
            filterOctave = 1.0f + ((12000.0f - value) / 5100.0f); //Sharper cutoff
        }

        VG_FOR_EACH_OSC(filter_.octaveControl(filterOctave))

        // Key tracking depends on the octave range, keep held notes in step
        _params.filterOctave = filterOctave;
        VG_FOR_EACH_VOICE(voices[i]->updateKeytracking(_params))
    }

    void setResonance(float value)
    {
        resonance = value;
        VG_FOR_EACH_OSC(filter_.resonance(value))
    }

    void setFilterMixer(float value)
    {
        filterMixer = value;

        float LP = 1.0f;
        float BP = 0;
        float HP = 0;

        if (value == BANDPASS)
        {
            //BP mode
            LP = 0;
            BP = 1.0f;
            HP = 0;
        }
        else
        {
            //LP-HP mix mode - a notch filter
            LP = 1.0f - value;
            BP = 0;
            HP = value;
        }

        VG_FOR_EACH_VOICE(
            voices[i]->patch().filterMixer_.gain(0, LP);
            voices[i]->patch().filterMixer_.gain(1, BP);
            voices[i]->patch().filterMixer_.gain(2, HP);)
    }

    void setFilterModMixer(int channel, float level)
    {
        VG_FOR_EACH_OSC(filterModMixer_.gain(channel, level))
    }

    void setFilterEnvelope(float value)
    {
        filterEnvelope = value;
        this->setFilterModMixer(0, filterEnvelope);
    }

    void setFilterAttack(float value)
    {
        filterAttack = value;
        VG_FOR_EACH_OSC(filterEnvelope_.attack(value))
    }

    void setFilterDecay(float value)
    {
        filterDecay = value;
        VG_FOR_EACH_OSC(filterEnvelope_.decay(value))
    }

    void setFilterSustain(float value)
    {
        filterSustain = value;
        VG_FOR_EACH_OSC(filterEnvelope_.sustain(value))
    }

    void setFilterRelease(float value)
    {
        filterRelease = value;
        VG_FOR_EACH_OSC(filterEnvelope_.release(value))
    }

    void setAmpAttack(float value)
    {
        ampAttack = value;
        VG_FOR_EACH_OSC(ampEnvelope_.attack(value))
    }

    void setAmpDecay(float value)
    {
        ampDecay = value;
        VG_FOR_EACH_OSC(ampEnvelope_.decay(value))
    }

    void setAmpSustain(float value)
    {
        ampSustain = value;
        VG_FOR_EACH_OSC(ampEnvelope_.sustain(value))
    }

    void setAmpRelease(float value)
    {
        ampRelease = value;
        VG_FOR_EACH_OSC(ampEnvelope_.release(value))
    }

    void setKeytracking(float value)
    {
        // TODO: Move this out of params to avoid setting it directly without updating the mixer.
        _params.keytrackingAmount = value;
        setFilterModMixer(2, 1.0f); // The amount is applied in the per-voice DC level, not here as well
        VG_FOR_EACH_VOICE(voices[i]->updateKeytracking(_params))
    }

    void setFilterLfoRetrig(bool value)
    {
        filterLfoRetrig = value;
        shared.filterLfo.sync();
    }

    void setFilterLfoRate(float value)
    {
        filterLfoRate = value;
        shared.filterLfo.frequency(value);
    }

    void setFilterLfoAmt(float value)
    {
        filterLfoAmt = value;
        shared.filterLfo.amplitude(value);
    }

    void setFilterLfoWaveform(int waveform)
    {
        if (filterLfoWaveform == waveform)
            return;
        filterLfoWaveform = waveform;
        shared.filterLfo.begin(filterLfoWaveform);
    }

    void setPinkNoiseLevel(float value)
    {
        pinkLevel = value;
        float gain;
        if (_params.unisonMode == 0)
            gain = 1.0;
        else
            gain = UNISONNOISEMIXERLEVEL;
        shared.noiseMixer.gain(0, pinkLevel * gain);
        updateNoiseContribution(gain);
    }

    void setWhiteNoiseLevel(float value)
    {
        whiteLevel = value;
        float gain;
        if (_params.unisonMode == 0)
            gain = 1.0;
        else
            gain = UNISONNOISEMIXERLEVEL;
        shared.noiseMixer.gain(1, whiteLevel * gain);
        updateNoiseContribution(gain);
    }

    void setPitchLfoRetrig(bool value)
    {
        pitchLfoRetrig = value;
        shared.pitchLfo.sync();
    }

    void setPitchLfoWaveform(uint32_t waveform)
    {
        if (pitchLfoWaveform == waveform)
            return;
        pitchLfoWaveform = waveform;
        shared.pitchLfo.begin(waveform);
    }

    void setPitchLfoAmount(float value)
    {
        pitchLfoAmount = value;
        shared.pitchLfo.amplitude(value + modWhAmount);
    }

    void setPitchLfoRate(float value)
    {
        pitchLfoRate = value;
        shared.pitchLfo.frequency(value);
    }

    void setModWhAmount(float value)
    {
        modWhAmount = value;
        shared.pitchLfo.amplitude(value + pitchLfoAmount);
    }

    void setEffectAmount(float value)
    {
        effectAmount = value;
        shared.ensemble.lfoRate(effectAmount);
    }

    void setEffectMix(float value)
    {
        effectMix = value;
        shared.effectMixerL.gain(0, 1.0f - effectMix); //Dry
        shared.effectMixerL.gain(1, effectMix);        //Wet
        shared.effectMixerR.gain(0, 1.0f - effectMix); //Dry
        shared.effectMixerR.gain(1, effectMix);        //Wet
    }

    inline void setMonophonic(uint8_t mode)
    {
        if (mode != this->monophonic)
        {
            noteStack.clear();
        }
        this->monophonic = mode;
    }

    void setUnisonMode(uint8_t mode)
    {
        if (mode == 0)
            allNotesOff();

        this->_params.unisonMode = mode;
        this->unisonNotesOn = 0;

        // Update noise gain
        setPinkNoiseLevel(pinkLevel);
        setWhiteNoiseLevel(whiteLevel);
    }

    void setFilterLfoMidiClockSync(bool value)
    {
        filterLfoMidiClockSync = value;
    }

    void setPitchLfoMidiClockSync(bool value)
    {
        pitchLFOMidiClockSync = value;
    }

    inline uint8_t unisonNotes()
    {
        return this->unisonNotesOn;
    }

    //
    // Configure the group
    //
    VoiceParams &params()
    {
        return _params;
    }

    //
    // Modify the group
    //

    // Splits out num voices from the current group into a new group.
    Voice *pop()
    {
        if (voices.size() == 0)
        {
            return nullptr;
        }
        Voice *result = voices.back();
        voices.pop_back();
        return result;
    }

    void add(Voice *v)
    {
        Mixer *m = v->patch().connectTo(shared, voices.size());
        v->setMixer(m);
        voices.push_back(v);
    }

    // Merges the other VoiceGroup into this one, making additional voices
    // available to this group.
    void merge(VoiceGroup other)
    {
    }

    //
    // Use the group
    //

    void noteOn(uint8_t note, uint8_t velocity)
    {
        if (this->monophonic)
        {
            handleMonophonicNoteOn(note, velocity);
            return;
        }
        noteOn(note, velocity, false);
    }

    void allNotesOn(uint8_t note, int velocity, uint8_t id)
    {
        for (uint8_t i = 0; i < voices.size(); i++)
        {
            voices[i]->noteOn(note, velocity, this->_params, unisonNotesOn, id);
        }
    }

    void allNotesOff()
    {
        this->unisonNotesOn = 0;
        for (uint8_t i = 0; i < voices.size(); i++)
        {
            voices[i]->noteOff();
        }
        // Keep the voice count and mono note history in step, or unison voice allocation
        // and mono retriggering work from stale notes
        this->voicesOn = 0;
        noteStack.clear();
    }

    // Silence everything at once: notes off and envelopes closed
    void panic()
    {
        allNotesOff();
        closeEnvelopes();
    }

    void updateVoices()
    {
        for (uint8_t i = 0; i < voices.size(); i++)
        {
            voices[i]->updateVoice(this->_params, unisonNotesOn);
        }
    }

    void noteOff(uint8_t note)
    {
        if (this->monophonic)
        {
            handleMonophonicNoteOff(note);
            return;
        }

        uint8_t num = 0;
        switch (this->_params.unisonMode)
        {
        case 0:
            num = noteOff(note, false); // changed for experiment
            break;
        default:
            num = noteOff(note, true);
            break;
        }

        // Decrement unison note count if noteOff modified a voice.
        if (this->unisonNotesOn > 0 && num > 0)
            this->unisonNotesOn--;
    }

    void pitchBend(float amount)
    {
        shared.pitchBend.amplitude(amount);
    }

    void midiClockStart()
    {
        midiClockSignal = true;
        shared.filterLfo.sync();
    }

    void midiClockStop()
    {
        midiClockSignal = false;
    }

    void midiClock(float frequency)
    {
        midiClockSignal = true;

        if (filterLfoMidiClockSync)
        {
            shared.filterLfo.frequency(frequency);
        }

        if (pitchLFOMidiClockSync)
        {
            shared.pitchLfo.frequency(frequency);
        }
    }

    void closeEnvelopes()
    {
        VG_FOR_EACH_VOICE(
            voices[i]->patch().filterEnvelope_.close();
            voices[i]->patch().ampEnvelope_.close();)
    }

    // TODO: This helps during refactoring, maybe it will be removed later.
    Voice *operator[](int i) const { return voices[i]; }

private:
    void handleMonophonicNoteOn(uint8_t note, uint8_t velocity)
    {
        noteStack.push(note, velocity);
        if (monophonic)
        {
            MonoNoteHistory::Element nextNote{note, velocity};
            // If there is more than 1 note check for retrigger.
            if (noteStack.size() > 1)
            {
                switch (monophonic)
                {
                case MONOPHONIC_FIRST:
                    // Exit since there is already a note playing.
                    return;
                    break;
                case MONOPHONIC_LEGATO:
                case MONOPHONIC_LAST:
                    // Always play the last note.
                    break;
                case MONOPHONIC_HIGHEST:
                {
                    // Play if higher than active note.
                    if (note < monoNote)
                    {
                        return;
                    }
                    break;
                }
                case MONOPHONIC_LOWEST:
                {
                    if (note > monoNote)
                    {
                        return;
                    }
                    break;
                }
                }
            }

            // Make sure any active note is turned off.
            for (uint8_t i = 0; i < voices.size(); i++)
            {
                voices[i]->noteOff();
            }
            this->monoNote = nextNote.note;
            noteOn(nextNote.note, nextNote.velocity, false);
        }
        else
        {
            noteOn(note, velocity, false);
        }
    }

    void handleMonophonicNoteOff(uint8_t note)
    {
        if (!this->monophonic)
            return;

        // Remove turned-off note
        noteStack.erase(note);
        bool activeNoteTurnedOff = 0 != noteOff(note, true);

        // Active note still active, or no more notes.
        if (!activeNoteTurnedOff || noteStack.size() == 0)
            return;

        MonoNoteHistory::Element nextNote;
        switch (this->monophonic)
        {
        case MONOPHONIC_LAST:
            nextNote = noteStack.getLast();
            break;
        case MONOPHONIC_FIRST:
            nextNote = noteStack.getFirst();
            break;
        case MONOPHONIC_LOWEST:
            nextNote = noteStack.getLowest();
            break;
        case MONOPHONIC_HIGHEST:
            nextNote = noteStack.getHighest();
            break;
        case MONOPHONIC_LEGATO:
        default:
            // No retriggering.
            return;
            break;
        }
        this->monoNote = nextNote.note;
        noteOn(nextNote.note, nextNote.velocity, true);
        return;
    }

    // Get the oldest free voice, of none free get the oldest active voice.
    // Prefer a silent voice, then the oldest released voice (still in its release tail),
    // then steal the oldest held note. Using silent voices first avoids cutting off release tails.
    Voice *getVoice()
    {
        // Mono modes retrigger the same voice rather than overlapping the previous note's release
        if (this->monophonic)
        {
            for (uint8_t i = 0; i < voices.size(); i++)
                if (!voices[i]->on())
                    return voices[i];
        }

        Voice *released = nullptr;
        Voice *oldest = nullptr;
        for (uint8_t i = 0; i < voices.size(); i++)
        {
            Voice *v = voices[i];
            if (!v->on())
            {
                if (!v->sounding())
                    return v;
                if (released == nullptr || v->timeOn() < released->timeOn())
                    released = v;
            }
            else if (oldest == nullptr || v->timeOn() < oldest->timeOn())
            {
                oldest = v;
            }
        }
        return released != nullptr ? released : oldest;
    }

    // Turn off one or more notes, return the number of notes turned off.
    int noteOff(uint8_t note, bool all)
    {
        int num = 0;
        for (uint8_t i = 0; i < voices.size(); i++)
        {
            if (voices[i]->note() == note && voices[i]->on() == true)
            {
                num++;
                voices[i]->noteOff();
                this->voicesOn--;
                if (!all)
                {
                    return 1;
                }
            }
        }
        return num;
    }

    void noteOn(uint8_t note, uint8_t velocity, bool monoRetrigger)
    {

        if (filterLfoRetrig)
        {
            shared.filterLfo.sync();
        }

        switch (this->_params.unisonMode)
        {
        case 0:
        {
            this->_params.mixerLevel = VOICEMIXERLEVEL * oscMixCompensation;
            Voice *v = this->getVoice();
            if (!v->on())
                this->voicesOn++;
            v->noteOn(note, velocity, this->_params, unisonNotesOn, 0);
            break;
        }
        case 1:
        {
            if (!this->monophonic)
                this->unisonNotesOn++;

            this->_params.mixerLevel = UNISONVOICEMIXERLEVEL * oscMixCompensation;

            uint8_t maxUnison = voices.size() / MINUNISONVOICES;
            uint8_t tally[maxUnison] = {};
            uint8_t oldestVoiceIndex = 0;

            // Figure out which note id to use.
            for (uint8_t i = 0; i < this->voices.size(); i++)
            {
                if (voices[i]->on())
                {
                    tally[voices[i]->noteId()]++;
                    if (voices[i]->timeOn() < voices[oldestVoiceIndex]->timeOn())
                    {
                        oldestVoiceIndex = i;
                    }
                }
            }

            uint8_t id = 0;
            for (uint8_t i = 0; i < maxUnison; i++)
            {
                if (tally[i] == 0)
                {
                    id = i;
                    break;
                }
            }

            // Replace oldest note if too many are playing.
            if (this->unisonNotesOn > maxUnison)
            {
                id = voices[oldestVoiceIndex]->noteId();
                noteOff(voices[oldestVoiceIndex]->note());
            }

            // Fill gaps if there are any.
            if (this->unisonNotesOn != 1 && this->voicesOn != this->voices.size())
            {
                for (uint8_t i = 0; i < this->voices.size(); i++)
                {
                    if (!voices[i]->on())
                    {
                        voices[i]->noteOn(note, velocity, this->_params, unisonNotesOn, id);
                        this->voicesOn++;
                    }
                }
                return;
            }

            // Start all voices or...
            // Steal voices until each has the right amount.
            uint8_t max = this->voices.size() / unisonNotesOn;
            for (uint8_t i = 0; i < voices.size(); i++)
            {
                if (!voices[i]->on() || tally[voices[i]->noteId()] > max)
                {
                    // underflow here when starting first unison note, but it still works.
                    tally[voices[i]->noteId()]--;
                    voices[i]->noteOn(note, velocity, this->_params, unisonNotesOn, id);
                }
            }
            voicesOn = voices.size();

            break;
        }
        case 2:
            this->_params.mixerLevel = UNISONVOICEMIXERLEVEL * oscMixCompensation;
            allNotesOn(note, velocity, 0);
            break;
        }
        this->_params.prevNote = note;
    }
};

#endif
