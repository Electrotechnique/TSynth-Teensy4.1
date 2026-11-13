#ifndef TSYNTH_AUDIO_PATCHING_H
#define TSYNTH_AUDIO_PATCHING_H

#include <vector>
#include "Constants.h"

//waveformX      -->   waveformMixerX   -->   voiceMixer1-3   -->   voiceMixerM  --> volumeMixer
//WAVEFORMLEVEL        oscA/BLevel             VELOCITY    VOICEMIXERLEVEL/UNISONVOICEMIXERLEVEL    volume

class Mixer {
    private:
    AudioMixer4TS& mixer;
    uint8_t index;

    public:
    Mixer(AudioMixer4TS& mixer_, uint8_t index_): mixer(mixer_), index(index_) {}

    void gain(float value) {
        mixer.gain(index, value);
    }
};

struct PatchShared {
    AudioSynthWaveformDcTS pitchBend;
    AudioSynthWaveformTS pitchLfo;
    AudioMixer4TS pitchMixer;
    AudioSynthWaveformTS pwmLfoA;
    AudioSynthWaveformTS pwmLfoB;
    AudioSynthWaveformTS filterLfo;
    AudioSynthWaveformDcTS pwa;
    AudioSynthWaveformDcTS pwb;
    AudioMixer4TS noiseMixer;

    AudioMixer4TS voiceMixer[3];
    AudioMixer4TS voiceMixerM;

    AudioEffectEnsemble ensemble;
    AudioFilterStateVariableTS dcOffsetFilter;
    AudioMixer4TS volumeMixer;
    AudioMixer4TS effectMixerL;
    AudioMixer4TS effectMixerR;

    AudioConnection connections[12] = {
        {pitchBend, 0, pitchMixer, 0},
        {pitchLfo, 0, pitchMixer, 1},
        {voiceMixer[0], 0, voiceMixerM, 0},
        {voiceMixer[1], 0, voiceMixerM, 1},
        {voiceMixer[2], 0, voiceMixerM, 2},
        {voiceMixerM, 0, dcOffsetFilter, 0},
        {dcOffsetFilter, 2, volumeMixer, 0},
        {volumeMixer, 0, ensemble, 0},
        {ensemble, 0, effectMixerL, 1},
        {ensemble, 1, effectMixerR, 1},
        {volumeMixer, 0, effectMixerL, 0},
        {volumeMixer, 0, effectMixerR, 0},
    };

    private:
    AudioConnection *pinkNoiseConnection = nullptr;
    AudioConnection *whiteNoiseConnection = nullptr;
    AudioConnection *outputLConnection = nullptr;
    AudioConnection *outputRConnection = nullptr;

    public:
    
    void connectNoise(AudioSynthNoisePink& pink, AudioSynthNoiseWhite& white) {
        delete pinkNoiseConnection;
        delete whiteNoiseConnection;
        pinkNoiseConnection = new AudioConnection(pink, 0, noiseMixer, 0);
        whiteNoiseConnection = new AudioConnection(white, 0, noiseMixer, 1);
    }

    void connectOutput(AudioMixer4TS& left, AudioMixer4TS& right, uint8_t index) {
        delete outputLConnection;
        delete outputRConnection;
        outputLConnection = new AudioConnection(effectMixerL, 0, left, index);
        outputRConnection = new AudioConnection(effectMixerR, 0, right, index);
    }
};

// Oscillator configurations.
struct Patch {
    AudioEffectEnvelopeTS filterEnvelope_;

    AudioMixer4TS pwMixer_a;
    AudioMixer4TS pwMixer_b;

    AudioSynthWaveformDcTS glide_;

    AudioSynthWaveformDcTS keytracking_;

    AudioMixer4TS oscModMixer_a;
    AudioMixer4TS oscModMixer_b;

    AudioSynthWaveformModulatedTS waveformMod_a;
    AudioSynthWaveformModulatedTS waveformMod_b;

    AudioEffectDigitalCombine oscFX_;

    AudioMixer4TS waveformMixer_;

    AudioMixer4TS filterModMixer_;

    AudioFilterStateVariableTS filter_;

    AudioMixer4TS filterMixer_;

    AudioEffectEnvelopeTS ampEnvelope_;

    AudioConnection connections[25] = {
        {keytracking_, 0, filterModMixer_, 2},
        {pwMixer_a, 0, waveformMod_a, 1},
        {pwMixer_b, 0, waveformMod_b, 1},
        {waveformMod_a, 0, waveformMixer_, 0},
        {waveformMod_a, 0, oscFX_, 0},
        {waveformMod_b, 0, waveformMixer_, 1},
        {waveformMod_b, 0, oscFX_, 1},
        {filterEnvelope_, 0, filterModMixer_, 0},
        {filterEnvelope_, 0, pwMixer_a, 2},
        {filterEnvelope_, 0, pwMixer_b, 2},
        {oscFX_, 0, waveformMixer_, 3},
        {waveformMixer_, 0, filter_, 0},
        {filterModMixer_, 0, filter_, 1},
        {filter_, 0, filterMixer_, 0},
        {filter_, 1, filterMixer_, 1},
        {filter_, 2, filterMixer_, 2},
        {filterMixer_, ampEnvelope_},
        // Mod sources
        {oscModMixer_a, 0, waveformMod_a, 0},
        {oscModMixer_b, 0, waveformMod_b, 0},
        // Pitch env
        {filterEnvelope_, 0, oscModMixer_a, 1},
        {filterEnvelope_, 0, oscModMixer_b, 1},
        // Glide
        {glide_, 0, oscModMixer_a, 2},
        {glide_, 0, oscModMixer_b, 2},
        // X Mod
        {waveformMod_a, 0, oscModMixer_b, 3},
        {waveformMod_b, 0, oscModMixer_a, 3}
    };

    private:
    // When added to a voice group, connect PWA/PWB.
    AudioConnection *pitchMixerAConnection = nullptr;
    AudioConnection *pitchMixerBConnection = nullptr;
    AudioConnection *pwmLfoAConnection = nullptr;
    AudioConnection *pwmLfoBConnection = nullptr;
    AudioConnection *filterLfoConnection = nullptr;
    AudioConnection *pwaConnection = nullptr;
    AudioConnection *pwbConnection = nullptr;
    AudioConnection *noiseMixerConnection = nullptr;
    AudioConnection *ampConnection = nullptr;

    public:
    // Connect the shared audio objects to the per-voice audio objects.
    Mixer* connectTo(PatchShared& shared, uint8_t index) {
        delete pitchMixerAConnection;
        delete pitchMixerBConnection;
        delete pwmLfoAConnection;
        delete pwmLfoBConnection;
        delete filterLfoConnection;
        delete pwaConnection;
        delete pwbConnection;
        delete noiseMixerConnection;
        delete ampConnection;

        pitchMixerAConnection = new AudioConnection(shared.pitchMixer, 0, oscModMixer_a, 0);
        pitchMixerBConnection = new AudioConnection(shared.pitchMixer, 0, oscModMixer_b, 0);
        pwmLfoAConnection = new AudioConnection(shared.pwmLfoA, 0, pwMixer_a, 0);
        pwmLfoBConnection = new AudioConnection(shared.pwmLfoB, 0, pwMixer_b, 0);
        filterLfoConnection = new AudioConnection(shared.filterLfo, 0, filterModMixer_, 1);
        pwaConnection = new AudioConnection(shared.pwa, 0, pwMixer_a, 1);
        pwbConnection = new AudioConnection(shared.pwb, 0, pwMixer_b, 1);
        noiseMixerConnection = new AudioConnection(shared.noiseMixer, 0, waveformMixer_, 2);

        uint8_t voiceMixerIndex = 0;
        uint8_t indexMod4 = index % 4;
        if (index != 0) voiceMixerIndex = index / 4;
        ampConnection = new AudioConnection(ampEnvelope_, 0, shared.voiceMixer[voiceMixerIndex], indexMod4);
        return new Mixer{shared.voiceMixer[voiceMixerIndex], indexMod4};
    }
};

struct Global {
    private:
    // Only one voice group (12 voices) is ever created, using SharedAudio[0]. A second set of
    // shared objects (LFOs, mixers, ensemble, DC filter...) used to be built and ran every
    // block producing silence.
    static const uint8_t MAX_NO_TIMBER = 1;
    static const uint8_t MAX_NO_VOICE = 12;

    public:
    AudioOutputUSB           usbAudio;
    AudioSynthWaveformDcTS     constant1Dc;
    AudioSynthNoisePink      pink;
    AudioSynthNoiseWhite     white;
    AudioAnalyzePeak         peak;
    Oscilloscope             scope;
    AudioCpuGuard            cpuGuard;
    AudioMixer4TS              effectMixerR[3];
    AudioMixer4TS              effectMixerRM;
    AudioMixer4TS              effectMixerL[3];
    AudioMixer4TS              effectMixerLM;
    AudioOutputI2S           i2s;

    PatchShared SharedAudio[MAX_NO_TIMBER];
    Patch Oscillators[MAX_NO_VOICE];

    AudioControlSGTL5000     sgtl5000_1;

    AudioConnection connectionsArray[13] = {
        {effectMixerL[0], 0, effectMixerLM, 0},
        {effectMixerL[1], 0, effectMixerLM, 1},
        {effectMixerL[2], 0, effectMixerLM, 2},
        {effectMixerR[0], 0, effectMixerRM, 0},
        {effectMixerR[1], 0, effectMixerRM, 1},
        {effectMixerR[2], 0, effectMixerRM, 2},
        // Scope taps the voices before the effects (DC removed, before volume) for a clean trace
        {SharedAudio[0].dcOffsetFilter, 2, scope, 0},
        {effectMixerLM, 0, peak, 0},
        {effectMixerRM, 0, usbAudio, 1},
        {effectMixerRM, 0, i2s, 1},
        {effectMixerLM, 0, i2s, 0},
        {effectMixerLM, 0, usbAudio, 0},
        {effectMixerLM, 0, cpuGuard, 0} // only so it runs every update
    };

    std::vector<AudioConnection*> connections;

    Global(float mixerLevel) {
        for (uint8_t i = 0; i < MAX_NO_VOICE; i++) {
            connections.push_back(new AudioConnection{constant1Dc, Oscillators[i].filterEnvelope_});
        }

        for (uint8_t i = 0; i < MAX_NO_TIMBER; i++) {
            SharedAudio[i].connectNoise(pink, white);

            uint8_t mixerIdx = 0;
            if (i > 0) mixerIdx = i / 12;
            SharedAudio[i].connectOutput(effectMixerL[mixerIdx], effectMixerR[mixerIdx], i % 4);

            SharedAudio[i].voiceMixerM.gain(0, mixerLevel);
            SharedAudio[i].voiceMixerM.gain(1, mixerLevel);
            SharedAudio[i].voiceMixerM.gain(2, mixerLevel);
            SharedAudio[i].voiceMixerM.gain(3, mixerLevel);

            SharedAudio[i].volumeMixer.gain(0, 1.6f);
            SharedAudio[i].volumeMixer.gain(1, 0);
            SharedAudio[i].volumeMixer.gain(2, 0);
            SharedAudio[i].volumeMixer.gain(3, 0);
            
            //This removes dc offset (mostly from unison pulse waves) before the ensemble effect
            SharedAudio[i].dcOffsetFilter.octaveControl(1.0f);
            SharedAudio[i].dcOffsetFilter.frequency(12.0f);//Lower values will give clicks on note on/off
            SharedAudio[i].dcOffsetFilter.softClip(false); // Keep the main signal path linear
        }

        constant1Dc.amplitude(1.0);

        effectMixerLM.gain(0, 1.0f);
        effectMixerLM.gain(1, 1.0f);
        effectMixerLM.gain(2, 1.0f);
        effectMixerLM.gain(3, 1.0f);
        effectMixerRM.gain(0, 1.0f);
        effectMixerRM.gain(1, 1.0f);
        effectMixerRM.gain(2, 1.0f);
        effectMixerRM.gain(3, 1.0f);

        pink.amplitude(1.0);
        white.amplitude(1.0);
    }

    inline uint8_t maxVoices() { return MAX_NO_VOICE; }
    inline uint8_t maxTimbre() { return MAX_NO_TIMBER; }

    // Limited to 12 because we have 3 mixers funnelling into 1 mixer.
    inline uint8_t maxVoicesPerGroup() { return 12; }
    inline uint8_t maxTimbres() { return 12; }
};

#endif
