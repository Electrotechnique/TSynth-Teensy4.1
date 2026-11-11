/* Audio Library for Teensy 3.X
   Portions Copyright (c) 2019, Alexander Davis info@matrixwide.com
   Portions Copyright (c) 2021, Vince R. Pearson

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice, development funding notice, and this permission
   notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
*/

// String machine style ensemble chorus (after the Roland RS-202 / Lowrey Symphonic Strings).
// Six modulated delay taps, each driven by a slow + fast sine LFO pair, 60 degrees apart.
// The right channel uses the same taps with the LFO shifted by 90 degrees for stereo spread.
//
// The LFO and delay interpolation are computed per sample in fixed point, so the delay
// time glides smoothly instead of stepping (stepping caused audible zipper/grit noise).

#ifndef effect_ensemble_h_
#define effect_ensemble_h_

#include <Arduino.h>
#include "AudioStream.h"

#define ENSEMBLE_BUFFER_SIZE 1024 // must be a power of two
#define ENSEMBLE_BUFFER_MASK (ENSEMBLE_BUFFER_SIZE - 1)
#define ENSEMBLE_TAPS 6

class AudioEffectEnsemble : public AudioStream
{
public:
  AudioEffectEnsemble(void);
  virtual void update(void);
  // Rate is the effect amount (2.0 - 20.0); same mapping as the original table based version
  void lfoRate(float rate);

private:
  audio_block_t *inputQueueArray[1];
  int16_t delayBuffer[ENSEMBLE_BUFFER_SIZE];
  uint16_t writeIndex;
  uint32_t lfoPhaseSlow;
  uint32_t lfoPhaseFast;
  uint32_t lfoIncSlow;
  uint32_t lfoIncFast;

  inline int32_t readTap(int32_t delayQ16);
};

#endif
