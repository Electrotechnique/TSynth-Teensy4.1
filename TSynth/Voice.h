#ifndef TSYNTH_VOICE_H
#define TSYNTH_VOICE_H

#include <stdint.h>
#include <stddef.h>
#include "AudioPatching.h"
#include "Constants.h"
#include "Detune.h"
#include "Velocity.h"

// TODO: Store / update these in the VoiceGroup instead of re-initializing for each noteOn.
// Key tracking pivots around this note: higher notes open the filter, lower notes close it
#define KEYTRACK_REF_NOTE 60

struct VoiceParams {
    float keytrackingAmount; // 1.0 = cutoff moves one octave per octave played
    float filterOctave;      // Octave range of the filter control input, set with the cutoff
    float mixerLevel;
    int prevNote;
    float glideSpeed;
    int unisonMode;
    uint32_t chordDetune;
    float detune;
    int oscPitchA;
    int oscPitchB;
};

class Voice {
    private:
        Patch &_oscillator;
        long _timeOn;
        uint8_t _note;
        float _velocity;
        bool _voiceOn;
        uint8_t _idx;
        uint8_t _noteId;
        Mixer* mixer = nullptr;

        // Note-on held back while a stolen voice fades out (see noteOn / service)
        bool _pending = false;
        float _pendingGain = 0;
        bool _pendingGlide = false;
        float _pendingGlideFrom = 0;
        float _pendingGlideTime = 0;
        uint8_t _pendingNotesOn = 0;
        VoiceParams *_pendingParams = nullptr;

        // Set the new note's level, key tracking, glide and pitch
        void applyNoteOn() {
            Patch& osc = this->patch();
            this->updateKeytracking(*_pendingParams);
            mixer->gain(_pendingGain);
            if (_pendingGlide) {
                osc.glide_.amplitude(_pendingGlideFrom);    //Set glide to previous note frequency (limited to 1 octave max)
                osc.glide_.amplitude(0, _pendingGlideTime); //Glide to current note
            }
            this->updateVoice(*_pendingParams, _pendingNotesOn);
        }

    public:
        Voice(Patch& p, uint8_t i): _oscillator(p), _timeOn(-1), _note(0), _velocity(0), _voiceOn(false), _idx(i), _noteId(0) {
            p.waveformMod_a.frequencyModulation(PITCHLFOOCTAVERANGE);
            p.waveformMod_a.begin(WAVEFORMLEVEL, 440.0f, WAVEFORM_SQUARE);
            p.waveformMod_b.frequencyModulation(PITCHLFOOCTAVERANGE);
            p.waveformMod_b.begin(WAVEFORMLEVEL, 440.0f, WAVEFORM_SQUARE);

            //Arbitary waveform needs initializing to something
            p.waveformMod_a.arbitraryWaveform(PARABOLIC_WAVE, AWFREQ);
            p.waveformMod_b.arbitraryWaveform(PARABOLIC_WAVE, AWFREQ);
        }

        inline void setMixer(Mixer* mixer_) {
            delete mixer;
            mixer = mixer_;
        }

        inline uint8_t index() {
            return this->_idx;
        }

        inline bool on() { 
            return this->_voiceOn;
        }

        inline uint8_t note() {
            return this->_note;
        }

        inline uint8_t noteId() {
            return this->_noteId;
        }

        inline uint8_t velocity() {
            return this->_velocity;
        }

        inline long timeOn() {
            return this->_timeOn;
        }

        inline Patch& patch() {
            return this->_oscillator;
        }

        // The filter cutoff is fc * 2^(control * filterOctave), so a control of
        // semitones / (12 * filterOctave) moves it by exactly that many semitones.
        void updateKeytracking(VoiceParams &params) {
            if (_pending)
                return; // Old note still fading out, applied when the new note starts
            float control = 0.0f;
            if (params.filterOctave > 0.0f)
                control = ((int)this->_note - KEYTRACK_REF_NOTE) * params.keytrackingAmount / (12.0f * params.filterOctave);
            if (control > 1.0f) control = 1.0f;
            else if (control < -1.0f) control = -1.0f;
            this->patch().keytracking_.amplitude(control);
        }

        // True while the voice is audible (including its release tail)
        inline bool sounding() {
            return this->_oscillator.ampEnvelope_.isActive();
        }

        void updateVoice(VoiceParams &params, uint8_t notesOn) {
            Patch& osc = this->patch();

            if (params.unisonMode == 1) {
                // Spread the unison oscillators evenly (in cents) either side of the played
                // pitch. DETUNE gives each oscillator's place in the spread, 0 to 1. The
                // spread is as wide as before (detune x pitch up to the pitch), but it used to
                // sit entirely below the pitch: the stack was flat by half the detune (about
                // 53 cents at full detune) and voice 1 always took the full amount (issue #125).
                int offset = 2 * this->index();
                const float width = -log2f(params.detune); // octaves
                osc.waveformMod_a.frequency(NOTEFREQS[this->_note + params.oscPitchA] * exp2f((DETUNE[notesOn - 1][offset] - 0.5f) * width));
                osc.waveformMod_b.frequency(NOTEFREQS[this->_note + params.oscPitchB] * exp2f((DETUNE[notesOn - 1][offset + 1] - 0.5f) * width));
            } else if (params.unisonMode == 2) {
                // TODO: This approach doesn't make sense with voices spread across multiple timbres.
                osc.waveformMod_a.frequency(NOTEFREQS[this->_note + params.oscPitchA + CHORD_DETUNE[this->index()][params.chordDetune]]) ;
                osc.waveformMod_b.frequency(NOTEFREQS[this->_note + params.oscPitchB + CHORD_DETUNE[this->index()][params.chordDetune]] * CDT_DETUNE);
            } else {
                osc.waveformMod_a.frequency(NOTEFREQS[this->_note + params.oscPitchA]);
                osc.waveformMod_b.frequency(NOTEFREQS[this->_note + params.oscPitchB] * params.detune);
            }
        }

        void noteOn(uint8_t note, int velocity, VoiceParams &params, uint8_t notesOn, uint8_t id) {
            Patch& osc = this->patch();
            bool stealing = osc.ampEnvelope_.isActive();
            // Oscillators of a silent voice are suspended to save CPU (see service())
            osc.waveformMod_a.resume();
            osc.waveformMod_b.resume();

            // Work out the new note's settings now, glide depends on the previous note
            _pendingGain = VELOCITY[velocitySens][velocity] * params.mixerLevel;
            _pendingGlide = params.glideSpeed > 0 && note != params.prevNote;
            _pendingGlideFrom = (params.prevNote - note) * DIV24;
            _pendingGlideTime = params.glideSpeed * GLIDEFACTOR;
            _pendingParams = &params;
            _pendingNotesOn = notesOn;

            this->_noteId = id;
            this->_voiceOn = true;
            this->_note = note;
            this->_velocity = velocity;
            this->_timeOn = millis();

            if (stealing) {
                // The envelopes fade the old note out first (5ms forced release). Changing
                // the level and pitch now would be heard on that fade as a click/blip, so
                // wait until it has finished: service() then applies them and starts the envelopes.
                _pending = true;
                osc.filterEnvelope_.noteOnDeferred();
                osc.ampEnvelope_.noteOnDeferred();
                return;
            }

            _pending = false;
            applyNoteOn();
            osc.filterEnvelope_.noteOn();
            osc.ampEnvelope_.noteOn();
        }

        // Call regularly from the main loop. Starts a deferred note-on once the old note has faded out.
        void service() {
            Patch& osc = this->patch();
            // Voice finished sounding: stop its oscillators (and so its filter) until the next note
            if (!_pending && !_voiceOn && !osc.waveformMod_a.isSuspended() && !osc.ampEnvelope_.isActive()) {
                AudioNoInterrupts();
                if (!_pending && !_voiceOn && !osc.ampEnvelope_.isActive()) {
                    osc.waveformMod_a.suspend();
                    osc.waveformMod_b.suspend();
                }
                AudioInterrupts();
                return;
            }
            if (!_pending || !osc.ampEnvelope_.isWaiting())
                return;
            AudioNoInterrupts();
            _pending = false;
            applyNoteOn();
            this->patch().filterEnvelope_.start();
            this->patch().ampEnvelope_.start();
            AudioInterrupts();
        }

        // Silence at once and stop the oscillators (CPU overload). Returns true if it was held.
        bool forceSilence() {
            Patch& osc = this->patch();
            bool wasOn = _voiceOn;
            _pending = false;
            _voiceOn = false;
            _noteId = 0;
            osc.filterEnvelope_.close();
            osc.ampEnvelope_.close();
            osc.waveformMod_a.suspend();
            osc.waveformMod_b.suspend();
            return wasOn;
        }

        void noteOff() {
            _pending = false;
            if (!this->_voiceOn) return;
            this->_voiceOn = false;
            this->_noteId = 0;
            this->patch().filterEnvelope_.noteOff();
            this->patch().ampEnvelope_.noteOff();
        }
};

#endif
