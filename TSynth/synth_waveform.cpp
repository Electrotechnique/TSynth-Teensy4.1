/* Audio Library for Teensy 3.X
 * Copyright (c) 2018, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
   ElectroTechnique 2020
 Added WAVEFORM_SILENT, syncFlag                
 */

#include <Arduino.h>
#include "synth_waveform.h"
#include "arm_math.h"
#include "utility/dspinst.h"


// uncomment for more accurate but more computationally expensive frequency modulation
//#define IMPROVE_EXPONENTIAL_ACCURACY
#define BASE_AMPLITUDE 0x6000  // 0x7fff won't work due to Gibb's phenomenon, so use 3/4 of full range.



void AudioSynthWaveformTS::update(void)
{
  audio_block_t *block;
  int16_t *bp, *end;
  int32_t val1, val2;
  int16_t magnitude15;
  uint32_t i, ph, index, index2, scale;
  const uint32_t inc = phase_increment;

  if(syncFlag==1){
    phase_accumulator = 0;
    syncFlag = 0;
  }                      
  ph = phase_accumulator + phase_offset;
  if (magnitude == 0) {
    phase_accumulator += inc * AUDIO_BLOCK_SAMPLES;
    return;
  }
  block = allocate();
  if (!block) {
    phase_accumulator += inc * AUDIO_BLOCK_SAMPLES;
    return;
  }
  bp = block->data;

  switch(tone_type) {
  case WAVEFORM_SINE:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      index = ph >> 24;
      val1 = AudioWaveformSine[index];
      val2 = AudioWaveformSine[index+1];
      scale = (ph >> 8) & 0xFFFF;
      val2 *= scale;
      val1 *= 0x10000 - scale;
      *bp++ = multiply_32x32_rshift32(val1 + val2, magnitude);
      ph += inc;
    }
    break;

  case WAVEFORM_ARBITRARY:
    if (!arbdata) {
      release(block);
      phase_accumulator += inc * AUDIO_BLOCK_SAMPLES;
      return;
    }
    // len = 256
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      index = ph >> 24;
      index2 = index + 1;
      if (index2 >= 256) index2 = 0;
      val1 = *(arbdata + index);
      val2 = *(arbdata + index2);
      scale = (ph >> 8) & 0xFFFF;
      val2 *= scale;
      val1 *= 0x10000 - scale;
      *bp++ = multiply_32x32_rshift32(val1 + val2, magnitude);
      ph += inc;
    }
    break;

  case WAVEFORM_SQUARE:
    magnitude15 = signed_saturate_rshift(magnitude, 16, 1);
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      if (ph & 0x80000000) {
        *bp++ = -magnitude15;
      } else {
        *bp++ = magnitude15;
      }
      ph += inc;
    }
    break;

  case WAVEFORM_BANDLIMIT_SQUARE:
    for (int i = 0 ; i < AUDIO_BLOCK_SAMPLES ; i++)
    {
      uint32_t new_ph = ph + inc ;
      int16_t val = band_limit_waveform.generate_square (new_ph, i) ;
      *bp++ = (val * magnitude) >> 16 ;
      ph = new_ph ;
    }
    break;

  case WAVEFORM_SAWTOOTH:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      *bp++ = signed_multiply_32x16t(magnitude, ph);
      ph += inc;
    }
    break;

  case WAVEFORM_SAWTOOTH_REVERSE:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      *bp++ = signed_multiply_32x16t(0xFFFFFFFFu - magnitude, ph);
      ph += inc;
    }
    break;

  case WAVEFORM_BANDLIMIT_SAWTOOTH:
  case WAVEFORM_BANDLIMIT_SAWTOOTH_REVERSE:
    for (i = 0 ; i < AUDIO_BLOCK_SAMPLES; i++)
    {
      uint32_t new_ph = ph + inc ;
      int16_t val = band_limit_waveform.generate_sawtooth (new_ph, i) ;
      if (tone_type == WAVEFORM_BANDLIMIT_SAWTOOTH_REVERSE)
        *bp++ = (val * -magnitude) >> 16 ;
      else
        *bp++ = (val * magnitude) >> 16 ;
      ph = new_ph ;
    }
    break;

  case WAVEFORM_TRIANGLE:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      uint32_t phtop = ph >> 30;
      if (phtop == 1 || phtop == 2) {
        *bp++ = ((0xFFFF - (ph >> 15)) * magnitude) >> 16;
      } else {
        *bp++ = (((int32_t)ph >> 15) * magnitude) >> 16;
      }
      ph += inc;
    }
    break;

  case WAVEFORM_TRIANGLE_VARIABLE:
    do {
    uint32_t rise = 0xFFFFFFFF / (pulse_width >> 16);
    uint32_t fall = 0xFFFFFFFF / (0xFFFF - (pulse_width >> 16));
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      if (ph < pulse_width/2) {
        uint32_t n = (ph >> 16) * rise;
        *bp++ = ((n >> 16) * magnitude) >> 16;
      } else if (ph < 0xFFFFFFFF - pulse_width/2) {
        uint32_t n = 0x7FFFFFFF - (((ph - pulse_width/2) >> 16) * fall);
        *bp++ = (((int32_t)n >> 16) * magnitude) >> 16;
      } else {
        uint32_t n = ((ph + pulse_width/2) >> 16) * rise + 0x80000000;
        *bp++ = (((int32_t)n >> 16) * magnitude) >> 16;
      }
      ph += inc;
    }
    } while (0);
    break;

  case WAVEFORM_PULSE:
    magnitude15 = signed_saturate_rshift(magnitude, 16, 1);
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      if (ph < pulse_width) {
        *bp++ = magnitude15;
      } else {
        *bp++ = -magnitude15;
      }
      ph += inc;
    }
    break;

  case WAVEFORM_BANDLIMIT_PULSE:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++)
    {
      int32_t new_ph = ph + inc ;
      int32_t val = band_limit_waveform.generate_pulse (new_ph, pulse_width, i) ;
      *bp++ = (int16_t) ((val * magnitude) >> 16) ;
      ph = new_ph ;
    }
    break;

  case WAVEFORM_SAMPLE_HOLD:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      *bp++ = sample;
      uint32_t newph = ph + inc;
      if (newph < ph) {
        sample = random(magnitude) - (magnitude >> 1);
      }
      ph = newph;
    }
    break;
  }
  phase_accumulator = ph - phase_offset;

  if (tone_offset) {
    bp = block->data;
    end = bp + AUDIO_BLOCK_SAMPLES;
    do {
      val1 = *bp;
      *bp++ = signed_saturate_rshift(val1 + tone_offset, 16, 0);
    } while (bp < end);
  }
  transmit(block, 0);
  release(block);
}

//--------------------------------------------------------------------------------

void AudioSynthWaveformModulatedTS::update(void)
{
  audio_block_t *block, *moddata, *shapedata;
  int16_t *bp, *end;
  int32_t val1, val2;
  int16_t magnitude15;
  uint32_t i, ph, index, index2, scale, priorphase;
  const uint32_t inc = phase_increment;

  moddata = receiveReadOnly(0);
  shapedata = receiveReadOnly(1);

  if (suspended) {
    // Voice is silent: keep the phase moving, but produce nothing (so the filter and
    // mixers after it have nothing to process either)
    if (moddata) release(moddata);
    if (shapedata) release(shapedata);
    phase_accumulator += inc * AUDIO_BLOCK_SAMPLES;
    return;
  }

  if(syncFlag==1){
    phase_accumulator = 0;
    syncFlag = 0;
  }        
  
  // Pre-compute the phase angle for every output sample of this update
  ph = phase_accumulator;
  priorphase = phasedata[AUDIO_BLOCK_SAMPLES-1];
  if (moddata && modulation_type == 0) {
    // Frequency Modulation
    bp = moddata->data;
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      int32_t n = (*bp++) * modulation_factor; // n is # of octaves to mod
      int32_t ipart = n >> 27; // 4 integer bits
      n &= 0x7FFFFFF;          // 27 fractional bits
      #ifdef IMPROVE_EXPONENTIAL_ACCURACY
      // exp2 polynomial suggested by Stefan Stenzel on "music-dsp"
      // mail list, Wed, 3 Sep 2014 10:08:55 +0200
      int32_t x = n << 3;
      n = multiply_accumulate_32x32_rshift32_rounded(536870912, x, 1494202713);
      int32_t sq = multiply_32x32_rshift32_rounded(x, x);
      n = multiply_accumulate_32x32_rshift32_rounded(n, sq, 1934101615);
      n = n + (multiply_32x32_rshift32_rounded(sq,
        multiply_32x32_rshift32_rounded(x, 1358044250)) << 1);
      n = n << 1;
      #else
      // exp2 algorithm by Laurent de Soras
      // https://www.musicdsp.org/en/latest/Other/106-fast-exp2-approximation.html
      n = (n + 134217728) << 3;

      n = multiply_32x32_rshift32_rounded(n, n);
      n = multiply_32x32_rshift32_rounded(n, 715827883) << 3;
      n = n + 715827882;
      #endif
      uint32_t scale = n >> (14 - ipart);
      uint64_t phstep = (uint64_t)inc * scale;
      uint32_t phstep_msw = phstep >> 32;
      if (phstep_msw < 0x7FFE) {
        ph += phstep >> 16;
      } else {
        ph += 0x7FFE0000;
      }
      phasedata[i] = ph;
    }
    release(moddata);
  } else if (moddata) {
    // Phase Modulation
    bp = moddata->data;
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      // more than +/- 180 deg shift by 32 bit overflow of "n"
      uint32_t n = (uint16_t)(*bp++) * modulation_factor;
      phasedata[i] = ph + n;
      ph += inc;
    }
    release(moddata);
  } else {
    // No Modulation Input
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      phasedata[i] = ph;
      ph += inc;
    }
  }
  phase_accumulator = ph;

  //Amplitude is always 1 on TSynth when oscillator is sounding
  //magnitude must be set to zero, otherwise digital noise comes through
  if(tone_type == WAVEFORM_SILENT){
    magnitude  = 0;
  }else{
    magnitude = 65536.0;
    }                               
  // If the amplitude is zero, no output, but phase still increments properly
  if (magnitude == 0) {
    if (shapedata) release(shapedata);
    return;
  }
  block = allocate();
  if (!block) {
    if (shapedata) release(shapedata);
    return;
  }
  bp = block->data;

  // Now generate the output samples using the pre-computed phase angles
  switch(tone_type) {
  case WAVEFORM_SINE:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      ph = phasedata[i];
      index = ph >> 24;
      val1 = AudioWaveformSine[index];
      val2 = AudioWaveformSine[index+1];
      scale = (ph >> 8) & 0xFFFF;
      val2 *= scale;
      val1 *= 0x10000 - scale;
      *bp++ = multiply_32x32_rshift32(val1 + val2, magnitude);
    }
    break;

  case WAVEFORM_ARBITRARY:
    if (!arbdata) {
      release(block);
      if (shapedata) release(shapedata);
      return;
    }
    // len = 256
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      ph = phasedata[i];
      index = ph >> 24;
      index2 = index + 1;
      if (index2 >= 256) index2 = 0;
      val1 = *(arbdata + index);
      val2 = *(arbdata + index2);
      scale = (ph >> 8) & 0xFFFF;
      val2 *= scale;
      val1 *= 0x10000 - scale;
      *bp++ = multiply_32x32_rshift32(val1 + val2, magnitude);
    }
    break;

  case WAVEFORM_PULSE:
    if (shapedata) {
      magnitude15 = signed_saturate_rshift(magnitude, 16, 1);
      for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
        uint32_t width = ((shapedata->data[i] + 0x8000) & 0xFFFF) << 16;
        if (phasedata[i] < width) {
          *bp++ = magnitude15;
        } else {
          *bp++ = -magnitude15;
        }
      }
      break;
    } // else fall through to orginary square without shape modulation

  case WAVEFORM_SQUARE:
    magnitude15 = signed_saturate_rshift(magnitude, 16, 1);
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      if (phasedata[i] & 0x80000000) {
        *bp++ = -magnitude15;
      } else {
        *bp++ = magnitude15;
      }
    }
    break;

  case WAVEFORM_BANDLIMIT_PULSE:
    if (shapedata)
    {
      band_limit_waveform.generate_pulse_block (phasedata, shapedata->data, bp, magnitude) ;
      break;
    } // else fall through to orginary square without shape modulation

  case WAVEFORM_BANDLIMIT_SQUARE:
    band_limit_waveform.generate_square_block (phasedata, bp, magnitude) ;
    break;

  case WAVEFORM_SAWTOOTH:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      *bp++ = signed_multiply_32x16t(magnitude, phasedata[i]);
    }
    break;

  case WAVEFORM_SAWTOOTH_REVERSE:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      *bp++ = signed_multiply_32x16t(0xFFFFFFFFu - magnitude, phasedata[i]);
    }
    break;

  case WAVEFORM_BANDLIMIT_SAWTOOTH:
  case WAVEFORM_BANDLIMIT_SAWTOOTH_REVERSE:
    band_limit_waveform.generate_sawtooth_block (phasedata, bp, magnitude, tone_type == WAVEFORM_BANDLIMIT_SAWTOOTH_REVERSE) ;
    break;

  case WAVEFORM_TRIANGLE_VARIABLE:
    if (shapedata) {
      // Band-limited (the plain version aliased badly at high pitches and extreme widths)
      band_limit_waveform.generate_triangle_variable_block (phasedata, shapedata->data, bp, magnitude) ;
      break;
    } // else fall through to orginary triangle without shape modulation

  case WAVEFORM_TRIANGLE:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      ph = phasedata[i];
      uint32_t phtop = ph >> 30;
      if (phtop == 1 || phtop == 2) {
        *bp++ = ((0xFFFF - (ph >> 15)) * magnitude) >> 16;
      } else {
        *bp++ = (((int32_t)ph >> 15) * magnitude) >> 16;
      }
    }
    break;
  case WAVEFORM_SAMPLE_HOLD:
    for (i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      ph = phasedata[i];
      if (ph < priorphase) { // does not work for phase modulation
        sample = random(magnitude) - (magnitude >> 1);
      }
      priorphase = ph;
      *bp++ = sample;
    }
    break;
  }

  if (tone_offset) {
    bp = block->data;
    end = bp + AUDIO_BLOCK_SAMPLES;
    do {
      val1 = *bp;
      *bp++ = signed_saturate_rshift(val1 + tone_offset, 16, 0);
    } while (bp < end);
  }
  if (shapedata) release(shapedata);
  transmit(block, 0);
  release(block);
}


// BandLimitedWaveformTS


#define SUPPORT_SHIFT 4
#define SUPPORT (1 << SUPPORT_SHIFT)
#define PTRMASK ((2 << SUPPORT_SHIFT) - 1)

#define SCALE 16
#define SCALE_MASK (SCALE-1)
#define N (SCALE * SUPPORT * 2)

#define GUARD_BITS 8
#define GUARD      (1 << GUARD_BITS)
#define HALF_GUARD (1 << (GUARD_BITS-1))


#define DEG180 0x80000000u

#define PHASE_SCALE (0x100000000L / (2 * BASE_AMPLITUDE))


extern "C"
{
  extern const int16_t step_table [258] ;
}

// The step table only stores half the step (odd symmetry). Unfolding it once into a full
// table, with BASE_AMPLITUDE included, removes the mirror branch and offset from every
// lookup. Every active step does a lookup on every sample, so at high pitches this is
// where the oscillator's time goes. Results are identical to the folded table.
static int32_t full_step [N + 1] ;
static int32_t full_delta [N] ;      // full_step [k+1] - full_step [k], for interpolation
static float full_step_norm [N + 1] ; // the same, scaled 0..1, for the variable triangle
static float full_delta_norm [N] ;
static bool full_step_ready = false ;

static void build_full_step (void)
{
  for (int k = 0 ; k <= N ; k++)
  {
    full_step [k] = BASE_AMPLITUDE + (k < N/2 ? step_table [k+1] : - step_table [N-k]) ;
    full_step_norm [k] = full_step [k] * (1.0f / (2 * BASE_AMPLITUDE)) ;
  }
  for (int k = 0 ; k < N ; k++)
  {
    full_delta [k] = full_step [k+1] - full_step [k] ;
    full_delta_norm [k] = full_step_norm [k+1] - full_step_norm [k] ;
  }
  full_step_ready = true ;
}

// Interpolated table value. Same result as (frac*b + (GUARD-frac)*a + HALF_GUARD) >> GUARD_BITS.
// A step's fraction (low bits of its offset) never changes, as it moves on by whole entries.
static inline int32_t step_at (int idx, int frac)
{
  return full_step [idx] + ((frac * full_delta [idx] + HALF_GUARD) >> GUARD_BITS) ;
}

inline int32_t BandLimitedWaveformTS::lookup (int offset)
{
  int off = offset >> GUARD_BITS ;
  int frac = offset & (GUARD-1) ;
  int32_t a = full_step [off] ;
  int32_t b = full_step [off+1] ;
  return (frac * b + (GUARD - frac) * a + HALF_GUARD) >> GUARD_BITS ; // interpolated
}

// create a new step, apply its past waveform into the cyclic sample buffer
// and add a step_state object into active list so it can be added for the future samples
void BandLimitedWaveformTS::insert_step (int offset, bool rising, int i)
{
  const int frac = offset & (GUARD-1) ;
  const int32_t sign = rising ? 1 : -1 ;
  while (offset < 0) // not reached yet
  {
    offset += SCALE<<GUARD_BITS ;
    i ++ ;
  }
  // Before the step: into the delayed samples still to be output
  while (offset <= (N/2-SCALE)<<GUARD_BITS)
  {
    cyclic [i & 15] += sign * step_at (offset >> GUARD_BITS, frac) ;
    offset += SCALE<<GUARD_BITS ;
    i ++ ;
  }

  // From this sample on: the whole step as a level change, plus the residual of its shape
  // (table value minus full step) for the samples it still has to play out.
  dc_offset += sign * 2*BASE_AMPLITUDE ;
  int k = fut_pos ;
  while (offset < N<<GUARD_BITS)
  {
    future [k & 31] += sign * (step_at (offset >> GUARD_BITS, frac) - 2*BASE_AMPLITUDE) ;
    offset += SCALE<<GUARD_BITS ;
    k ++ ;
  }
}

void BandLimitedWaveformTS::reset_steps (void)
{
  for (int k = 0 ; k < 32 ; k++)
    future [k] = 0 ;
  fut_pos = 0 ;
}

// Waveform value for the current sample from the steps: level plus residuals
inline int32_t BandLimitedWaveformTS::process_active_steps (uint32_t new_phase)
{
  const int k = fut_pos & 31 ;
  int32_t sample = dc_offset + future [k] ;
  future [k] = 0 ;
  fut_pos ++ ;
  return sample ;
}

// for sawtooth need to add in the slope and compensate for all the steps being one way
inline int32_t BandLimitedWaveformTS::process_active_steps_saw (uint32_t new_phase)
{
  int32_t sample = process_active_steps (new_phase) ;

  sample += (int16_t) ((((uint64_t)phase_word * (2*BASE_AMPLITUDE)) >> 32) - BASE_AMPLITUDE) ;  // generate the sloped part of the wave

  if (new_phase < DEG180 && phase_word >= DEG180) // detect wrap around, correct dc offset
    dc_offset += 2*BASE_AMPLITUDE ;

  return sample ;
}

// for pulse need to adjust the baseline according to the pulse width to cancel the DC component.
inline int32_t BandLimitedWaveformTS::process_active_steps_pulse (uint32_t new_phase, uint32_t pulse_width)
{
  int32_t sample = process_active_steps (new_phase) ;

  // Correct the DC offset for the duty cycle: the raw pulse is +/-BASE_AMPLITUDE, so its mean is
  // BASE_AMPLITUDE * (2W - 1). This used to add BASE_AMPLITUDE/2, leaving a constant DC offset of
  // -BASE_AMPLITUDE/2. Uses the width the edges were made with (sampled once per cycle).
  (void) pulse_width ;
  return sample + BASE_AMPLITUDE - sampled_width / (0x80000000u / BASE_AMPLITUDE) ;
}

// Check for new steps using the phase update for the current sample for a square wave
inline void BandLimitedWaveformTS::new_step_check_square (uint32_t new_phase, int i)
{
  if (new_phase >= DEG180 && phase_word < DEG180) // detect falling step
  {
    int32_t offset = (int32_t) ((uint64_t) (SCALE<<GUARD_BITS) * (sampled_width - phase_word) / (new_phase - phase_word)) ;
    if (offset == SCALE<<GUARD_BITS)
      offset -- ;
    if (pulse_state) // guard against two falling steps in a row (if pulse width changing for instance)
    {
      insert_step (- offset, false, i) ;
      pulse_state = false ;
    }
  }
  else if (new_phase < DEG180 && phase_word >= DEG180) // detect wrap around, rising step
  {
    int32_t offset = (int32_t) ((uint64_t) (SCALE<<GUARD_BITS) * (- phase_word) / (new_phase - phase_word)) ;
    if (offset == SCALE<<GUARD_BITS)
      offset -- ;
    if (!pulse_state) // guard against two rising steps in a row (if pulse width changing for instance)
    {
      insert_step (- offset, true, i) ;
      pulse_state = true ;
    }
  }
}

// Checking for new steps for pulse waveform has to deal with changing frequency and pulse width and
// not letting a pulse glitch out of existence as these change across a single period of the waveform
// now we detect the rising edge just like for a square wave and use that to sample the pulse width
// parameter, which then has to be checked against the instantaneous frequency every sample.
inline void BandLimitedWaveformTS::new_step_check_pulse (uint32_t new_phase, uint32_t pulse_width, int i)
{
  if (pulse_state && phase_word < sampled_width && (new_phase >= sampled_width || new_phase < phase_word))  // falling edge
  {
    int32_t offset = (int32_t) ((uint64_t) (SCALE<<GUARD_BITS) * (sampled_width - phase_word) / (new_phase - phase_word)) ;
    if (offset == SCALE<<GUARD_BITS)
      offset -- ;
    insert_step (- offset, false, i) ;
    pulse_state = false ;
  }
  if ((!pulse_state) && phase_word >= DEG180 && new_phase < DEG180) // detect wrap around, rising step
  {
    // sample the pulse width value so its not changing under our feet later in cycle due to modulation
    sampled_width = pulse_width ;

    int32_t offset = (int32_t) ((uint64_t) (SCALE<<GUARD_BITS) * (- phase_word) / (new_phase - phase_word)) ;
    if (offset == SCALE<<GUARD_BITS)
      offset -- ;
    insert_step (- offset, true, i) ;
    pulse_state = true ;
    
    if (pulse_state && new_phase >= sampled_width) // detect falling step directly after a rising edge
    //if (new_phase - sampled_width < DEG180) // detect falling step directly after a rising edge
    {
      int32_t offset = (int32_t) ((uint64_t) (SCALE<<GUARD_BITS) * (sampled_width - phase_word) / (new_phase - phase_word)) ;
      if (offset == SCALE<<GUARD_BITS)
  offset -- ;
      insert_step (- offset, false, i) ;
      pulse_state = false ;
    }
  }
}

// new steps for sawtooth are at 180 degree point, always falling.
inline void BandLimitedWaveformTS::new_step_check_saw (uint32_t new_phase, int i)
{
  if (new_phase >= DEG180 && phase_word < DEG180) // detect falling step
  {
    int32_t offset = (int32_t) ((uint64_t) (SCALE<<GUARD_BITS) * (DEG180 - phase_word) / (new_phase - phase_word)) ;
    if (offset == SCALE<<GUARD_BITS)
      offset -- ;
    insert_step (- offset, false, i) ;
  }
}
  
// the generation function pushd new sample into cyclic buffer, having taken out the oldest entry
// to return.  The output is thus 16 samples behind, which allows the non-casual step function to
// work in real time.
inline int16_t BandLimitedWaveformTS::generate_sawtooth (uint32_t new_phase, int i)
{
  new_step_check_saw (new_phase, i) ;
  int32_t val = process_active_steps_saw (new_phase) ;
  int16_t sample = (int16_t) cyclic [i&15] ;
  cyclic [i&15] = val ;
  phase_word = new_phase ;
  return sample ;
}

inline int16_t BandLimitedWaveformTS::generate_square (uint32_t new_phase, int i)
{
  new_step_check_square (new_phase, i) ;
  int32_t val = process_active_steps (new_phase) ;
  int16_t sample = (int16_t) cyclic [i&15] ;
  cyclic [i&15] = val ;
  phase_word = new_phase ;
  return sample ;
}

inline int16_t BandLimitedWaveformTS::generate_pulse (uint32_t new_phase, uint32_t pulse_width, int i)
{
  new_step_check_pulse (new_phase, pulse_width, i) ;
  int32_t val = process_active_steps_pulse (new_phase, pulse_width) ;
  int32_t sample = cyclic [i&15] ;
  cyclic [i&15] = val ;
  phase_word = new_phase ;
  return (int16_t) ((sample >> 1) - (sample >> 5)) ; // scale down to avoid overflow on narrow pulses, where the DC shift is big
}

void BandLimitedWaveformTS::generate_sawtooth_block (const uint32_t *phases, int16_t *out, int32_t magnitude, bool reverse)
{
  for (int i = 0 ; i < AUDIO_BLOCK_SAMPLES ; i++)
  {
    int16_t val = generate_sawtooth (phases[i], i) ;
    val = (int16_t) ((val * magnitude) >> 16) ;
    out[i] = reverse ? (int16_t) -val : (int16_t) +val ;
  }
}

void BandLimitedWaveformTS::generate_square_block (const uint32_t *phases, int16_t *out, int32_t magnitude)
{
  for (int i = 0 ; i < AUDIO_BLOCK_SAMPLES ; i++)
  {
    int32_t val = generate_square (phases[i], i) ;
    out[i] = (int16_t) ((val * magnitude) >> 16) ;
  }
}

void BandLimitedWaveformTS::generate_pulse_block (const uint32_t *phases, const int16_t *shape, int16_t *out, int32_t magnitude)
{
  for (int i = 0 ; i < AUDIO_BLOCK_SAMPLES ; i++)
  {
    uint32_t width = ((shape[i] + 0x8000) & 0xFFFF) << 16 ;
    int32_t val = generate_pulse (phases[i], width, i) ;
    out[i] = (int16_t) ((val * magnitude) >> 16) ;
  }
}

void BandLimitedWaveformTS::init_sawtooth (uint32_t freq_word)
{
  phase_word = 0 ;
  reset_steps () ;
  for (int i = 0 ; i < 2*SUPPORT ; i++)
    phase_word -= freq_word ;
  dc_offset = phase_word < DEG180 ? BASE_AMPLITUDE : -BASE_AMPLITUDE ;
  for (int i = 0 ; i < 2*SUPPORT ; i++)
  {
    uint32_t new_phase = phase_word + freq_word ;
    new_step_check_saw (new_phase, i) ;
    cyclic [i & 15] = (int16_t) process_active_steps_saw (new_phase) ;
    phase_word = new_phase ;
  }
}


void BandLimitedWaveformTS::init_square (uint32_t freq_word)
{
  init_pulse (freq_word, DEG180) ;
}

void BandLimitedWaveformTS::init_pulse (uint32_t freq_word, uint32_t pulse_width)
{
  phase_word = 0 ;
  sampled_width = pulse_width ;
  reset_steps () ;
  for (int i = 0 ; i < 2*SUPPORT ; i++)
    phase_word -= freq_word ;

  if (phase_word < pulse_width)
  {
    dc_offset = BASE_AMPLITUDE ;
    pulse_state = true ;
  }
  else
  {
    dc_offset = -BASE_AMPLITUDE ;
    pulse_state = false ;
  }
  
  for (int i = 0 ; i < 2*SUPPORT ; i++)
  {
    uint32_t new_phase = phase_word + freq_word ;
    new_step_check_pulse (new_phase, pulse_width, i) ;
    cyclic [i & 15] = (int16_t) process_active_steps_pulse (new_phase, pulse_width) ;
    phase_word = new_phase ;
  }
}

// Band-limited variable triangle.
//
// The triangle rises for a fraction W of the cycle and falls for the rest, so its slope is
// +2/W then -2/(1-W) (per cycle). Each corner is a band-limited step in the slope, of
// whatever size the corner needs, made with the same step table as the other waveforms;
// the slope is then integrated. W is latched at the start of each cycle (like the pulse
// width), so a width change just sets the size of that cycle's corners and stays exact
// under fast modulation. A very slow pull towards the plain triangle (~3.5Hz) only has to
// mop up rounding.
//
// Phase 0..W rises from -1 to +1: the plain variable triangle shifted by W/2, which
// sounds the same.

#define TRI_MIN_WIDTH 0x00400000u        // 1/1024 of the cycle, avoids dividing by zero
#define TRI_DRIFT_K 0.0005f              // drift correction per sample
#define VT_DC_MAX_PERIOD 4096            // longest cycle measured for the start-up DC fix
#define VT_DC_RAMP 64                    // samples over which that fix is applied
#define TRI_OUT_SCALE (0.92f * 32767.0f) // a little headroom for overshoot at extreme widths

inline void BandLimitedWaveformTS::vt_set_width (uint32_t w32)
{
  vt_w32 = w32 ;
  vt_w = w32 * (1.0f / 4294967296.0f) ;
  vt_inv_w = 1.0f / vt_w ;
  vt_inv_1mw = 1.0f / (1.0f - vt_w) ;
}

// Start-up DC fix (see the sync code): measure, then remove gradually. Cheap when idle.
inline void BandLimitedWaveformTS::vt_dc_track (float &y)
{
  if (vt_dc_count > 0)
  {
    if (vt_dc_count <= vt_dc_period)
      vt_dc_sum += y ;
    if (--vt_dc_count == 0)
    {
      vt_dc_step = vt_dc_sum / (float) vt_dc_period / VT_DC_RAMP ;
      vt_dc_ramp = VT_DC_RAMP ;
    }
  }
  else if (vt_dc_ramp > 0)
  {
    y -= vt_dc_step ;
    vt_dc_ramp-- ;
  }
}

inline float BandLimitedWaveformTS::vt_ref_at (uint32_t phase)
{
  float ph = phase * (1.0f / 4294967296.0f) ;
  return ph < vt_w ? 2.0f * ph * vt_inv_w - 1.0f : 1.0f - 2.0f * (ph - vt_w) * vt_inv_1mw ;
}

// New corner, dist into this sample's phase step of dphase. As insert_step: the part before
// the corner goes into the delayed samples, the rest is applied as the step plays out.
void BandLimitedWaveformTS::vt_insert (uint32_t dist, uint32_t dphase, float amp, int i)
{
  int offset = (int) ((float) dist / (float) dphase * (SCALE<<GUARD_BITS)) ;
  if (offset >= SCALE<<GUARD_BITS)
    offset = (SCALE<<GUARD_BITS) - 1 ;
  offset = - offset ;
  const float frac = (offset & (GUARD-1)) * (1.0f / GUARD) ;
  while (offset < 0)
  {
    offset += SCALE<<GUARD_BITS ;
    i ++ ;
  }
  while (offset <= (N/2-SCALE)<<GUARD_BITS)
  {
    const int idx = offset >> GUARD_BITS ;
    vt_cyc [i & 15] += amp * (full_step_norm [idx] + frac * full_delta_norm [idx]) ;
    offset += SCALE<<GUARD_BITS ;
    i ++ ;
  }
  vt_level += amp ;
  int k = vt_fpos ;
  while (offset < N<<GUARD_BITS)
  {
    const int idx = offset >> GUARD_BITS ;
    vt_future [k & 31] += amp * (full_step_norm [idx] + frac * full_delta_norm [idx] - 1.0f) ;
    offset += SCALE<<GUARD_BITS ;
    k ++ ;
  }
}

// Corners in this sample, in time order
void BandLimitedWaveformTS::vt_corners (uint32_t new_phase, uint32_t pulse_width, uint32_t dphase, int i)
{
  if (vt_rising && phase_word < vt_w32 && (new_phase >= vt_w32 || new_phase < phase_word))
  {
    vt_insert (vt_w32 - phase_word, dphase, -2.0f * (vt_inv_1mw + vt_inv_w), i) ;
    vt_rising = false ;
  }
  if (!vt_rising && new_phase < phase_word) // start of cycle
  {
    const float fall = 2.0f * vt_inv_1mw ;
    vt_set_width (pulse_width) ;
    vt_insert (- phase_word, dphase, 2.0f * vt_inv_w + fall, i) ;
    vt_rising = true ;
    if (new_phase >= vt_w32) // narrow: also past the top corner
    {
      vt_insert (vt_w32 - phase_word, dphase, -2.0f * (vt_inv_1mw + vt_inv_w), i) ;
      vt_rising = false ;
    }
  }
}

int16_t BandLimitedWaveformTS::vt_step (uint32_t new_phase, uint32_t pulse_width, int i)
{
  const uint32_t dphase = new_phase - phase_word ;
  vt_corners (new_phase, pulse_width, dphase, i) ;

  // Slope for this sample
  const int f = vt_fpos & 31 ;
  const float slope = vt_level + vt_future [f] ;
  vt_future [f] = 0.0f ;
  vt_fpos ++ ;

  // Integrate the delayed slope
  const int idx = i & 15 ;
  const float d = vt_cyc [idx], dp = vt_dph [idx], ref = vt_ref [idx] ;
  vt_cyc [idx] = slope ;
  vt_dph [idx] = dphase * (1.0f / 4294967296.0f) ;
  vt_ref [idx] = vt_ref_at (new_phase) ;
  phase_word = new_phase ;

  vt_y += d * dp ;
  // Per sample: doing this every few samples instead adds spurious tones
  vt_y += TRI_DRIFT_K * (ref - vt_y) ;
  vt_dc_track (vt_y) ;
  if (vt_y > 1.5f) vt_y = 1.5f ;
  else if (vt_y < -1.5f) vt_y = -1.5f ;

  int32_t out = (int32_t) (vt_y * TRI_OUT_SCALE) ;
  if (out > 32767) out = 32767 ;
  else if (out < -32768) out = -32768 ;
  return (int16_t) out ;
}

int16_t BandLimitedWaveformTS::generate_triangle_variable (uint32_t new_phase, uint32_t pulse_width, int i)
{
  if (pulse_width < TRI_MIN_WIDTH)
    pulse_width = TRI_MIN_WIDTH ;
  else if (pulse_width > 0xFFFFFFFFu - TRI_MIN_WIDTH)
    pulse_width = 0xFFFFFFFFu - TRI_MIN_WIDTH ;

  if (vt_sync)
  {
    // First sample after the waveform was selected (or a silent voice resumed): start from
    // the real phase by filling the 16 sample delay with the plain triangle's slope for
    // the samples before this one. Their corners aren't band-limited, but this is the very
    // start of a note. (A 32 sample pre-roll used to be run here instead; with many notes
    // starting together that was a noticeable CPU spike.)
    vt_sync = false ;
    const uint32_t inc = vt_inc ;
    vt_set_width (pulse_width) ;
    for (int k = 0 ; k < 32 ; k++)
      vt_future [k] = 0.0f ;
    vt_fpos = 0 ;
    uint32_t p = new_phase - 16 * inc ;
    vt_y = vt_ref_at (p - inc) ;
    for (int k = 0 ; k < 16 ; k++, p += inc)
    {
      const int idx = (i + k) & 15 ;
      vt_cyc [idx] = p < vt_w32 ? 2.0f * vt_inv_w : -2.0f * vt_inv_1mw ;
      vt_dph [idx] = inc * (1.0f / 4294967296.0f) ;
      vt_ref [idx] = vt_ref_at (p) ;
    }
    // Corners near the start aren't placed exactly, which leaves a small DC offset (up to a
    // few % of full scale at high pitches) that the drift correction would take ~100ms to
    // remove. A triangle averages zero over a cycle, so measure the mean over the first
    // cycle (after the 16 filled samples) and remove it, spread over VT_DC_RAMP samples.
    uint32_t period = inc > 0 ? (uint32_t) (4294967296.0f / inc) : 0 ;
    if (period > VT_DC_MAX_PERIOD) period = VT_DC_MAX_PERIOD ;
    vt_dc_period = period ;
    vt_dc_count = period > 0 ? 16 + period : 0 ;
    vt_dc_sum = 0.0f ;
    vt_dc_ramp = 0 ;
    phase_word = new_phase - inc ;
    vt_rising = phase_word < vt_w32 ;
    vt_level = vt_rising ? 2.0f * vt_inv_w : -2.0f * vt_inv_1mw ;
  }
  return vt_step (new_phase, pulse_width, i) ;
}

void BandLimitedWaveformTS::generate_triangle_variable_block (const uint32_t *phases, const int16_t *shape, int16_t *out, int32_t magnitude)
{
  int i = 0 ;
  if (vt_sync) // first sample after selecting the waveform: the per-sample path sets up
  {
    uint32_t width = ((shape[0] + 0x8000) & 0xFFFF) << 16 ;
    int32_t val = generate_triangle_variable (phases[0], width, 0) ;
    out[0] = (int16_t) ((val * magnitude) >> 16) ;
    i = 1 ;
  }
  // Hot state in locals; written back around the (rare) corners, which use the members
  uint32_t pw = phase_word ;
  float y = vt_y ;
  float level = vt_level ;
  int fpos = vt_fpos ;
  for ( ; i < AUDIO_BLOCK_SAMPLES ; i++)
  {
    uint32_t width = ((shape[i] + 0x8000) & 0xFFFF) << 16 ;
    if (width < TRI_MIN_WIDTH)
      width = TRI_MIN_WIDTH ;
    else if (width > 0xFFFFFFFFu - TRI_MIN_WIDTH)
      width = 0xFFFFFFFFu - TRI_MIN_WIDTH ;
    const uint32_t np = phases[i] ;
    const uint32_t dphase = np - pw ;
    if ((vt_rising && pw < vt_w32 && (np >= vt_w32 || np < pw)) || (!vt_rising && np < pw))
    {
      phase_word = pw ;
      vt_level = level ;
      vt_fpos = fpos ;
      vt_corners (np, width, dphase, i) ;
      level = vt_level ;
    }

    const int f = fpos & 31 ;
    const float slope = level + vt_future [f] ;
    vt_future [f] = 0.0f ;
    fpos ++ ;

    const int idx = i & 15 ;
    const float d = vt_cyc [idx], dp = vt_dph [idx], ref = vt_ref [idx] ;
    vt_cyc [idx] = slope ;
    vt_dph [idx] = dphase * (1.0f / 4294967296.0f) ;
    vt_ref [idx] = vt_ref_at (np) ;
    pw = np ;

    y += d * dp ;
    y += TRI_DRIFT_K * (ref - y) ;
    vt_dc_track (y) ;
    if (y > 1.5f) y = 1.5f ;
    else if (y < -1.5f) y = -1.5f ;

    int32_t o = (int32_t) (y * TRI_OUT_SCALE) ;
    if (o > 32767) o = 32767 ;
    else if (o < -32768) o = -32768 ;
    out[i] = (int16_t) ((o * magnitude) >> 16) ;
  }
  phase_word = pw ;
  vt_y = y ;
  vt_level = level ;
  vt_fpos = fpos ;
}

void BandLimitedWaveformTS::init_triangle_variable (uint32_t freq_word)
{
  vt_inc = freq_word ;
  vt_sync = true ; // set up on the first sample, when the real phase is known
}

BandLimitedWaveformTS::BandLimitedWaveformTS()
{
  if (!full_step_ready)
    build_full_step () ;
  vt_fpos = 0 ;
  for (int k = 0 ; k < 32 ; k++)
    vt_future [k] = 0.0f ;
  vt_sync = true ;
  vt_inc = 0 ;
  reset_steps () ;
  dc_offset = BASE_AMPLITUDE ;
  phase_word = 0 ;
}
