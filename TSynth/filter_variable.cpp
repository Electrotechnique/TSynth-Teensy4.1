/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
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
 */

#include <Arduino.h>
#include "filter_variable.h"
#include "utility/dspinst.h"

// State Variable Filter (Chamberlin) with 2X oversampling
// http://www.musicdsp.org/showArchiveComment.php?ArchiveID=92

// The fast 32x32 with rshift32 discards 2 bits, which probably
// never matter.
//#define MULT(a, b) (int32_t)(((int64_t)(a) * (b)) >> 30)
#define MULT(a, b) (multiply_32x32_rshift32_rounded(a, b) << 2)

// It's very unlikely anyone could hear any difference, but if you
// care deeply about numerical precision in seldom-used cases,
// uncomment this to improve the control signal accuracy
//#define IMPROVE_HIGH_FREQUENCY_ACCURACY

// This increases the exponential approximation accuracy from
// about 0.341% error to only 0.012% error, which probably makes
// no audible difference.
//#define IMPROVE_EXPONENTIAL_ACCURACY

// Internal headroom: input is scaled by 2^INPUT_SHIFT. Was 12, which let the
// states and output sums overflow (wrap around) at high resonance with loud input.
// 10 gives 4x headroom, enough for Q = 15 with a full scale input.
#define INPUT_SHIFT 10

#define SOFT_CLIP_KNEE 24576 // 0.75 of full scale

// Linear below the knee, then a smooth curve approaching full scale with
// matching slope at the knee, so loud resonant peaks round off instead of clipping.
static inline int16_t softLimit16(int32_t x)
{
	if (x <= SOFT_CLIP_KNEE && x >= -SOFT_CLIP_KNEE)
		return x;
	const float range = 32767.0f - SOFT_CLIP_KNEE;
	float u = ((x < 0 ? -x : x) - SOFT_CLIP_KNEE) / range;
	int32_t y = SOFT_CLIP_KNEE + (int32_t)(range * u / (1.0f + u));
	return x < 0 ? -y : y;
}

static inline int16_t hardLimit16(int32_t x)
{
	return x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
}

// Average of the two oversampled values, scaled back to 16 bit (halved first so the sum can't overflow)
#define FILTER_OUT(a, b) ((((a) >> 1) + ((b) >> 1)) >> INPUT_SHIFT)

#if defined(__ARM_ARCH_7EM__)

void AudioFilterStateVariableTS::update_fixed(const int16_t *in,
	int16_t *lp, int16_t *bp, int16_t *hp)
{
	const int16_t *end = in + AUDIO_BLOCK_SAMPLES;
	int32_t input, inputprev;
	int32_t lowpass, bandpass, highpass;
	int32_t lowpasstmp, bandpasstmp, highpasstmp;
	int32_t fmult, damp;

	fmult = setting_fmult;
	damp = setting_damp;
	const int32_t input_gain = setting_input_gain;
	inputprev = state_inputprev;
	lowpass = state_lowpass;
	bandpass = state_bandpass;
	do {
		input = ((*in++) * input_gain) >> (16 - INPUT_SHIFT);
		lowpass = lowpass + MULT(fmult, bandpass);
		highpass = ((input + inputprev)>>1) - lowpass - MULT(damp, bandpass);
		inputprev = input;
		bandpass = bandpass + MULT(fmult, highpass);
		lowpasstmp = lowpass;
		bandpasstmp = bandpass;
		highpasstmp = highpass;
		lowpass = lowpass + MULT(fmult, bandpass);
		highpass = input - lowpass - MULT(damp, bandpass);
		bandpass = bandpass + MULT(fmult, highpass);
		lowpasstmp = FILTER_OUT(lowpass, lowpasstmp);
		bandpasstmp = FILTER_OUT(bandpass, bandpasstmp);
		highpasstmp = FILTER_OUT(highpass, highpasstmp);
		if (soft_clip) {
			*lp++ = softLimit16(lowpasstmp);
			*bp++ = softLimit16(bandpasstmp);
			*hp++ = softLimit16(highpasstmp);
		} else {
			*lp++ = hardLimit16(lowpasstmp);
			*bp++ = hardLimit16(bandpasstmp);
			*hp++ = hardLimit16(highpasstmp);
		}
	} while (in < end);
	state_inputprev = inputprev;
	state_lowpass = lowpass;
	state_bandpass = bandpass;
}


void AudioFilterStateVariableTS::update_variable(const int16_t *in,
	const int16_t *ctl, int16_t *lp, int16_t *bp, int16_t *hp)
{
	const int16_t *end = in + AUDIO_BLOCK_SAMPLES;
	int32_t input, inputprev, control;
	int32_t lowpass, bandpass, highpass;
	int32_t lowpasstmp, bandpasstmp, highpasstmp;
	int32_t fcenter, fmult, damp, octavemult;
	int32_t n;

	fcenter = setting_fcenter;
	octavemult = setting_octavemult;
	damp = setting_damp;
	const int32_t input_gain = setting_input_gain;
	inputprev = state_inputprev;
	lowpass = state_lowpass;
	bandpass = state_bandpass;
	do {
		// compute fmult using control input, fcenter and octavemult
		control = *ctl++;          // signal is always 15 fractional bits
		control *= octavemult;     // octavemult range: 0 to 28671 (12 frac bits)
		n = control & 0x7FFFFFF;   // 27 fractional control bits
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
		n = n >> (6 - (control >> 27)); // 4 integer control bits
		fmult = multiply_32x32_rshift32_rounded(fcenter, n);
		//if (fmult > 5378279) fmult = 5378279;
		if (fmult > 4205728) fmult = 4205728;
		fmult = fmult << 8;
		// fmult is within 0.4% accuracy for all but the top 2 octaves
		// of the audio band.  This math improves accuracy above 5 kHz.
		// Without this, the filter still works fine for processing
		// high frequencies, but the filter's corner frequency response
		// can end up about 6% higher than requested.
		#ifdef IMPROVE_HIGH_FREQUENCY_ACCURACY
		// From "Fast Polynomial Approximations to Sine and Cosine"
		// Charles K Garrett, http://krisgarrett.net/
		fmult = (multiply_32x32_rshift32_rounded(fmult, 2145892402) +
			multiply_32x32_rshift32_rounded(
			multiply_32x32_rshift32_rounded(fmult, fmult),
			multiply_32x32_rshift32_rounded(fmult, -1383276101))) << 1;
		#endif
		// now do the state variable filter as normal, using fmult
		input = ((*in++) * input_gain) >> (16 - INPUT_SHIFT);
		lowpass = lowpass + MULT(fmult, bandpass);
		highpass = ((input + inputprev)>>1) - lowpass - MULT(damp, bandpass);
		inputprev = input;
		bandpass = bandpass + MULT(fmult, highpass);
		lowpasstmp = lowpass;
		bandpasstmp = bandpass;
		highpasstmp = highpass;
		lowpass = lowpass + MULT(fmult, bandpass);
		highpass = input - lowpass - MULT(damp, bandpass);
		bandpass = bandpass + MULT(fmult, highpass);
		lowpasstmp = FILTER_OUT(lowpass, lowpasstmp);
		bandpasstmp = FILTER_OUT(bandpass, bandpasstmp);
		highpasstmp = FILTER_OUT(highpass, highpasstmp);
		if (soft_clip) {
			*lp++ = softLimit16(lowpasstmp);
			*bp++ = softLimit16(bandpasstmp);
			*hp++ = softLimit16(highpasstmp);
		} else {
			*lp++ = hardLimit16(lowpasstmp);
			*bp++ = hardLimit16(bandpasstmp);
			*hp++ = hardLimit16(highpasstmp);
		}
	} while (in < end);
	state_inputprev = inputprev;
	state_lowpass = lowpass;
	state_bandpass = bandpass;
}


// ---------------------------------------------------------------------------------
// TPT state variable filter (Zavalishin, "The Art of VA Filter Design").
//   g = tan(pi fc / fs), k = 1/Q
//   hp = (x - (k + g) s1 - s2) / (1 + g (g + k))
//   bp = g hp + s1,  s1 = bp + g hp
//   lp = g bp + s2,  s2 = lp + g bp
// Bandpass and lowpass gain at the cutoff are Q, as with the Chamberlin filter, so levels
// and the resonance input compensation carry over. The cutoff is fc * 2^(control * N),
// worked out per sample from the control input like the Chamberlin version.

#define TPT_MAX_FC (0.45f * AUDIO_SAMPLE_RATE_EXACT) // ~19.9kHz
#define TPT_MIN_FC 1.0f

// 2^x, about 0.25 cent accurate, for |x| < 30
static inline float tpt_exp2(float x)
{
	int i = (int)x;
	if (x < i) i--;
	const float f = x - i;
	float p = 1.0f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
	union { float f; int32_t i; } u;
	u.f = p;
	u.i += i << 23;
	return u.f;
}

// tan(x) = n/d for 0 <= x <= ~1.42 (cutoff up to 0.45 fs), rational approximation.
// Returned as numerator and denominator so the filter needs only one division per sample.
static inline void tpt_tan(float x, float &n, float &d)
{
	const float x2 = x * x;
	n = x * (135135.0f + x2 * (-17325.0f + x2 * (378.0f - x2)));
	d = 135135.0f + x2 * (-62370.0f + x2 * (3150.0f - 28.0f * x2));
}

void AudioFilterStateVariableTS::update_tpt(const int16_t *in, const int16_t *ctl,
	int16_t *lp, int16_t *bp, int16_t *hp)
{
	// Ramp parameter changes over the block instead of stepping (cutoff in octaves)
	const float log2fc_end = tpt_log2fc, k_end = tpt_k, gain_end = tpt_gain, oct_end = tpt_oct;
	if (!tpt_started) {
		tpt_log2fc_now = log2fc_end; tpt_k_now = k_end; tpt_gain_now = gain_end; tpt_oct_now = oct_end;
		tpt_started = true;
	}
	const float step = 1.0f / AUDIO_BLOCK_SAMPLES;
	const float d_log2fc = (log2fc_end - tpt_log2fc_now) * step;
	const float d_k = (k_end - tpt_k_now) * step;
	const float d_gain = (gain_end - tpt_gain_now) * step;
	const float d_oct = (oct_end - tpt_oct_now) * step;
	float log2fc = tpt_log2fc_now, k = tpt_k_now, gain = tpt_gain_now, oct = tpt_oct_now;
	float s1 = tpt_s1, s2 = tpt_s2;
	const float wscale = 3.14159265f / AUDIO_SAMPLE_RATE_EXACT;

	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		log2fc += d_log2fc; k += d_k; gain += d_gain; oct += d_oct;
		float lf = log2fc;
		if (ctl) lf += ctl[i] * (1.0f / 32768.0f) * oct;
		float fc = tpt_exp2(lf);
		if (fc > TPT_MAX_FC) fc = TPT_MAX_FC;
		else if (fc < TPT_MIN_FC) fc = TPT_MIN_FC;
		// g = n/d and 1/(1 + g(g + k)) = d^2/q with q = d^2 + n(n + k d): one division gives both
		float n, d;
		tpt_tan(fc * wscale, n, d);
		const float q = d * d + n * (n + k * d);
		const float inv = 1.0f / (d * q);
		const float g = n * q * inv;
		const float x = in[i] * gain;
		const float h = (x - (k + g) * s1 - s2) * (d * d * d * inv);
		const float v1 = g * h;
		const float b = v1 + s1;
		s1 = b + v1;
		const float v2 = g * b;
		const float l = v2 + s2;
		s2 = l + v2;
		if (soft_clip) {
			lp[i] = softLimit16((int32_t)l);
			bp[i] = softLimit16((int32_t)b);
			hp[i] = softLimit16((int32_t)h);
		} else {
			lp[i] = hardLimit16((int32_t)l);
			bp[i] = hardLimit16((int32_t)b);
			hp[i] = hardLimit16((int32_t)h);
		}
	}
	// Guard against the states running away (cannot happen with valid settings)
	if (!(s1 < 1e7f && s1 > -1e7f)) s1 = 0;
	if (!(s2 < 1e7f && s2 > -1e7f)) s2 = 0;
	tpt_s1 = s1; tpt_s2 = s2;
	tpt_log2fc_now = log2fc_end; tpt_k_now = k_end; tpt_gain_now = gain_end; tpt_oct_now = oct_end;
}

void AudioFilterStateVariableTS::update(void)
{
	audio_block_t *input_block=NULL, *control_block=NULL;
	audio_block_t *lowpass_block=NULL, *bandpass_block=NULL, *highpass_block=NULL;

	input_block = receiveReadOnly(0);
	control_block = receiveReadOnly(1);
	if (!input_block) {
		if (control_block) release(control_block);
		return;
	}
	lowpass_block = allocate();
	if (!lowpass_block) {
		release(input_block);
		if (control_block) release(control_block);
		return;
	}
	bandpass_block = allocate();
	if (!bandpass_block) {
		release(input_block);
		release(lowpass_block);
		if (control_block) release(control_block);
		return;
	}
	highpass_block = allocate();
	if (!highpass_block) {
		release(input_block);
		release(lowpass_block);
		release(bandpass_block);
		if (control_block) release(control_block);
		return;
	}

#if FILTER_TPT
	update_tpt(input_block->data, control_block ? control_block->data : NULL,
		 lowpass_block->data, bandpass_block->data, highpass_block->data);
	if (control_block) release(control_block);
#else
	if (control_block) {
		update_variable(input_block->data,
			 control_block->data,
			 lowpass_block->data,
			 bandpass_block->data,
			 highpass_block->data);
		release(control_block);
	} else {
		update_fixed(input_block->data,
			 lowpass_block->data,
			 bandpass_block->data,
			 highpass_block->data);
	}
#endif
	release(input_block);
	transmit(lowpass_block, 0);
	release(lowpass_block);
	transmit(bandpass_block, 1);
	release(bandpass_block);
	transmit(highpass_block, 2);
	release(highpass_block);
	return;
}

#elif defined(KINETISL)

void AudioFilterStateVariableTS::update(void)
{
	audio_block_t *block;

	block = receiveReadOnly(0);
	if (block) release(block);
	block = receiveReadOnly(1);
	if (block) release(block);
}

#endif
