#ifndef Oscilloscope_h_
#define Oscilloscope_h_
#include "AudioStream.h"
#include "ST7735_t3.h"

// Oscilloscope trace on the display.
//
// The audio interrupt only copies each block into a ring buffer. Triggering, scaling and
// drawing are done by the display thread (Display()), so the scope uses no audio CPU time.
//  - Rising zero-crossing (Schmitt) trigger, searched over the last two periods, so the
//    waveform stands still
//  - Timebase locked to the lowest held note: shows two cycles (setPeriod())
//  - Auto-scaled to the window height, fast attack and slow release
//  - Each pixel column draws the min/max of its samples, so peaks and edges aren't missed

#define SCOPE_RING_SIZE 8192 // samples (16KB), power of two and a multiple of AUDIO_BLOCK_SAMPLES
#define SCOPE_RING_MASK (SCOPE_RING_SIZE - 1)
#define SCOPE_X 15
#define SCOPE_WIDTH 128
#define SCOPE_Y_TOP 30
#define SCOPE_Y_BOTTOM 100
#define SCOPE_COLOUR 0x07B0
#define SCOPE_CYCLES 2           // cycles of the lowest note to show
#define SCOPE_MIN_SPAN 128       // samples; high notes show more cycles so the trace isn't stretched
#define SCOPE_DEFAULT_SPAN 512   // samples, used before any note is known
#define SCOPE_MAX_READ 7168      // span + trigger search, samples (leaves 8 blocks of the ring spare)
#define SCOPE_MAX_SEARCH 3072    // samples searched for the trigger point (two periods)
#define SCOPE_MIN_SCALE 1024.0f  // smallest full-height level, so noise isn't magnified
#define SCOPE_SCALE_RELEASE 0.05f // auto-scale release per frame

class Oscilloscope : public AudioStream
{
public:
  Oscilloscope(void) : AudioStream(1, inputQueueArray) {}
  virtual void update(void);
  void ScreenSetup(ST7735_t3 *);
  // Period of the lowest held note in samples, or 0 if unknown (keeps the last timebase)
  void setPeriod(float samples)
  {
    if (samples > 0.0f)
      periodSamples = samples;
  }
  void Display(void);

private:
  audio_block_t *inputQueueArray[1];
  ST7735_t3 *display = NULL;
  int16_t ring[SCOPE_RING_SIZE] = {};
  volatile uint32_t writeCount = 0; // total samples written, the ring index is writeCount & mask
  float periodSamples = 0.0f;       // display thread only
  float scale = SCOPE_MIN_SCALE;    // display thread only

  inline int16_t sampleAt(uint32_t n) const { return ring[n & SCOPE_RING_MASK]; }
  inline int16_t toY(int32_t v, float k) const
  {
    int32_t y = ((SCOPE_Y_TOP + SCOPE_Y_BOTTOM) / 2) - (int32_t)(v * k);
    if (y < SCOPE_Y_TOP)
      y = SCOPE_Y_TOP;
    if (y > SCOPE_Y_BOTTOM)
      y = SCOPE_Y_BOTTOM;
    return y;
  }
};
#endif

FLASHMEM void Oscilloscope::ScreenSetup(ST7735_t3 *screen)
{
  display = screen;
}

void Oscilloscope::update(void)
{
  audio_block_t *block = receiveReadOnly(0);
  if (!display)
  {
    if (block)
      release(block);
    return;
  }
  // Blocks never wrap in the ring because its size is a multiple of the block size
  int16_t *dst = &ring[writeCount & SCOPE_RING_MASK];
  if (block)
  {
    memcpy(dst, block->data, sizeof(block->data));
    release(block);
  }
  else
  {
    memset(dst, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t)); // silence
  }
  writeCount += AUDIO_BLOCK_SAMPLES;
}

// Called from the display thread before each screen update.
// Reads the ring while the audio interrupt keeps writing: the oldest sample read is at most
// SCOPE_MAX_READ old, leaving 8 blocks (~23ms) before it is overwritten.
void Oscilloscope::Display()
{
  if (!display)
    return;
  const uint32_t end = writeCount;

  // Timebase: a whole number of cycles of the lowest note
  uint32_t span = SCOPE_DEFAULT_SPAN;
  uint32_t search = SCOPE_DEFAULT_SPAN;
  if (periodSamples > 0.0f)
  {
    // Search two periods so a trigger is always found, then fit the span in what's left
    search = (uint32_t)(2.0f * periodSamples + 4.0f);
    if (search > SCOPE_MAX_SEARCH)
      search = SCOPE_MAX_SEARCH;
    float s = periodSamples * SCOPE_CYCLES;
    if (s < SCOPE_MIN_SPAN)
      s = ceilf(SCOPE_MIN_SPAN / periodSamples) * periodSamples;
    if (s > SCOPE_MAX_READ - search)
      s = SCOPE_MAX_READ - search; // very low notes show fewer cycles
    span = (uint32_t)(s + 0.5f);
  }
  if (end < span + search)
    return; // not enough captured yet

  // Peak level, for the trigger hysteresis and auto-scale
  const uint32_t first = end - span - search;
  int32_t peak = 0;
  for (uint32_t n = first; n < end; n++)
  {
    int32_t v = abs(sampleAt(n));
    if (v > peak)
      peak = v;
  }

  // Latest rising zero crossing in the search window, found with a Schmitt trigger: arms
  // below -hysteresis and fires above +hysteresis, so noise can't cause false triggers.
  // The trace then starts at the zero crossing just before the firing point.
  // Free runs if nothing is found.
  const int32_t hysteresis = max(peak / 4, (int32_t)32);
  const uint32_t lastStart = end - span - 1; // -1: room for interpolation past the end
  uint32_t start = lastStart;
  float startFrac = 0.0f; // sub-sample trigger position, stops high notes jittering
  bool armed = false;
  for (uint32_t n = first + 1; n <= lastStart; n++)
  {
    int32_t v = sampleAt(n);
    if (v < -hysteresis)
      armed = true;
    else if (armed && v > hysteresis)
    {
      uint32_t m = n - 1;
      while (m > first && sampleAt(m) >= 0)
        m--;
      // crossing lies between m (negative) and m + 1
      int32_t p = sampleAt(m), q = sampleAt(m + 1);
      start = m;
      startFrac = (float)(-p) / (float)(q - p);
      armed = false;
    }
  }

  // Auto-scale: instant attack, slow release
  if (peak > scale)
    scale = peak;
  else
    scale += (peak - scale) * SCOPE_SCALE_RELEASE;
  if (scale < SCOPE_MIN_SCALE)
    scale = SCOPE_MIN_SCALE;
  const float k = ((SCOPE_Y_BOTTOM - SCOPE_Y_TOP) / 2 - 2) / scale; // 2px headroom

  if (span < 2 * SCOPE_WIDTH)
  {
    // Under 2 samples per column: draw the interpolated waveform, joining the columns
    const float step = (float)span / SCOPE_WIDTH;
    int16_t prevY = 0;
    for (uint32_t c = 0; c <= SCOPE_WIDTH; c++)
    {
      float pos = startFrac + c * step;
      uint32_t i = (uint32_t)pos;
      float fr = pos - i;
      float v = sampleAt(start + i) * (1.0f - fr) + sampleAt(start + i + 1) * fr;
      int16_t y = toY((int32_t)v, k);
      if (c > 0)
      {
        int16_t yTop = min(y, prevY), yBottom = max(y, prevY);
        display->drawFastVLine(SCOPE_X + c - 1, yTop, yBottom - yTop + 1, SCOPE_COLOUR);
      }
      prevY = y;
    }
    return;
  }

  // 2 or more samples per column: each column draws the range of its samples, joined to the previous one
  int16_t prevY = toY(sampleAt(start), k);
  for (uint32_t c = 0; c < SCOPE_WIDTH; c++)
  {
    uint32_t a = start + (c * span) / SCOPE_WIDTH;
    uint32_t b = start + ((c + 1) * span) / SCOPE_WIDTH;
    int16_t lo = sampleAt(a), hi = lo;
    for (uint32_t n = a + 1; n < b; n++)
    {
      int16_t v = sampleAt(n);
      if (v < lo)
        lo = v;
      if (v > hi)
        hi = v;
    }
    int16_t yTop = toY(hi, k);
    int16_t yBottom = toY(lo, k);
    if (prevY < yTop)
      yTop = prevY;
    if (prevY > yBottom)
      yBottom = prevY;
    display->drawFastVLine(SCOPE_X + c, yTop, yBottom - yTop + 1, SCOPE_COLOUR);
    prevY = toY(sampleAt(b - 1), k);
  }
}
