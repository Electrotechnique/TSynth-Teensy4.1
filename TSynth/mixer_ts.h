#ifndef TSYNTH_MIXER_TS_H
#define TSYNTH_MIXER_TS_H

// 4 channel mixer, same results as the Audio library's AudioMixer4 but cheaper:
// - channels with zero gain are skipped (AudioMixer4 multiplies them by zero). Many of
//   TSynth's per-voice mixer channels are normally at zero, e.g. the filter type mixer
//   only uses one of LP/BP/HP.
// - only the first channel that is used is fetched writable, as AudioMixer4 does for
//   channel 0 (that copies the block when it is shared, e.g. an LFO feeding every voice).
// The saturating arithmetic is AudioMixer4's, so the output is bit-identical.

#include <Arduino.h>
#include "AudioStream.h"
#include "utility/dspinst.h"

class AudioMixer4TS : public AudioStream
{
public:
  AudioMixer4TS(void) : AudioStream(4, inputQueueArray)
  {
    for (int i = 0; i < 4; i++)
      multiplier[i] = UNITY;
  }
  void gain(unsigned int channel, float gain)
  {
    if (channel >= 4)
      return;
    if (gain > 32767.0f)
      gain = 32767.0f;
    else if (gain < -32767.0f)
      gain = -32767.0f;
    multiplier[channel] = gain * 65536.0f;
  }
  virtual void update(void)
  {
    audio_block_t *out = NULL;
    bool anyInput = false;
    for (unsigned int ch = 0; ch < 4; ch++)
    {
      const int32_t mult = multiplier[ch];
      if (mult == 0)
      {
        audio_block_t *in = receiveReadOnly(ch);
        if (in)
        {
          anyInput = true;
          release(in);
        }
        continue;
      }
      if (!out)
      {
        out = receiveWritable(ch);
        if (out)
        {
          anyInput = true;
          if (mult != UNITY)
            applyGain(out->data, mult);
        }
      }
      else
      {
        audio_block_t *in = receiveReadOnly(ch);
        if (in)
        {
          applyGainThenAdd(out->data, in->data, mult);
          release(in);
        }
      }
    }
    if (!out && anyInput)
    {
      // Every input present was at zero gain: AudioMixer4 sends silence in that case
      out = allocate();
      if (out)
        memset(out->data, 0, sizeof(out->data));
    }
    if (out)
    {
      transmit(out);
      release(out);
    }
  }

private:
  static const int32_t UNITY = 65536;
  int32_t multiplier[4];
  audio_block_t *inputQueueArray[4];

  // As AudioMixer4 (Teensy 4)
  static void applyGain(int16_t *data, int32_t mult)
  {
    uint32_t *p = (uint32_t *)data;
    const uint32_t *end = (uint32_t *)(data + AUDIO_BLOCK_SAMPLES);
    do
    {
      uint32_t tmp32 = *p;
      int32_t val1 = signed_multiply_32x16b(mult, tmp32);
      int32_t val2 = signed_multiply_32x16t(mult, tmp32);
      val1 = signed_saturate_rshift(val1, 16, 0);
      val2 = signed_saturate_rshift(val2, 16, 0);
      *p++ = pack_16b_16b(val2, val1);
    } while (p < end);
  }
  static void applyGainThenAdd(int16_t *data, const int16_t *in, int32_t mult)
  {
    uint32_t *dst = (uint32_t *)data;
    const uint32_t *src = (uint32_t *)in;
    const uint32_t *end = (uint32_t *)(data + AUDIO_BLOCK_SAMPLES);
    if (mult == UNITY)
    {
      do
      {
        uint32_t tmp32 = *dst;
        *dst++ = signed_add_16_and_16(tmp32, *src++);
        tmp32 = *dst;
        *dst++ = signed_add_16_and_16(tmp32, *src++);
      } while (dst < end);
    }
    else
    {
      do
      {
        uint32_t tmp32 = *src++;
        int32_t val1 = signed_multiply_32x16b(mult, tmp32);
        int32_t val2 = signed_multiply_32x16t(mult, tmp32);
        val1 = signed_saturate_rshift(val1, 16, 0);
        val2 = signed_saturate_rshift(val2, 16, 0);
        tmp32 = pack_16b_16b(val2, val1);
        uint32_t tmp32b = *dst;
        *dst++ = signed_add_16_and_16(tmp32, tmp32b);
      } while (dst < end);
    }
  }
};

#endif
