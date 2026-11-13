#ifndef TSYNTH_CPU_GUARD_H
#define TSYNTH_CPU_GUARD_H

#include <Arduino.h>
#include "AudioStream.h"

// Audio CPU overload guard.
//
// If the audio processing takes longer than real time, the audio interrupt runs back to
// back: the main loop and the display get no time at all, so note-offs are never read,
// the load never drops and the synth hangs (the audio hardware keeps replaying its last
// buffer). This object runs at the end of every audio update and looks at the previous
// update's processing time. If it is over GUARD_THRESHOLD of the time available for
// GUARD_BLOCKS blocks in a row, it calls onOverload (TSynth silences the oldest voice),
// then waits GUARD_COOLDOWN blocks for that to take effect.

#define GUARD_THRESHOLD 0.92f
#define GUARD_BLOCKS 2
#define GUARD_COOLDOWN 4

class AudioCpuGuard : public AudioStream
{
public:
  AudioCpuGuard() : AudioStream(1, inputQueueArray) {}
  void (*onOverload)() = nullptr;
  volatile uint32_t overloads = 0; // number of times it had to act

  virtual void update(void)
  {
    audio_block_t *block = receiveReadOnly(0);
    if (block)
      release(block);

    // cpu_cycles_total is in units of 64 CPU cycles (see AudioProcessorUsage)
    const float budget = (float)F_CPU_ACTUAL / (AUDIO_SAMPLE_RATE_EXACT / AUDIO_BLOCK_SAMPLES) / 64.0f;
    if (cooldown > 0)
    {
      cooldown--;
      return;
    }
    if (AudioStream::cpu_cycles_total > budget * GUARD_THRESHOLD)
    {
      if (++over >= GUARD_BLOCKS)
      {
        over = 0;
        cooldown = GUARD_COOLDOWN;
        overloads++;
        if (onOverload)
          onOverload();
      }
    }
    else
    {
      over = 0;
    }
  }

private:
  audio_block_t *inputQueueArray[1];
  uint8_t over = 0;
  uint8_t cooldown = 0;
};

#endif
