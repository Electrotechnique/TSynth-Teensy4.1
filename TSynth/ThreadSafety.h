#ifndef TSYNTH_THREAD_SAFETY_H
#define TSYNTH_THREAD_SAFETY_H

// The display runs in its own TeensyThreads thread, preemptively alongside the main loop.
//
// 1. Heap: newlib's malloc has no locking in the Teensy core, so the two threads
//    allocating at the same time (String is used heavily by both) can corrupt the heap.
//    These hooks stop thread switching for the duration of each malloc/free.
// 2. Display state: the Strings and patch list the display renders from are written by
//    the main loop. displayLock is held by the display thread while it renders, and by
//    the main loop while it changes that state.
//
// Included once, from TSynth.ino.

#include <TeensyThreads.h>

extern "C"
{
  static volatile uint32_t mallocLockDepth = 0;
  static int mallocThreadsState = 0;

  void __malloc_lock(struct _reent *)
  {
    int s = threads.stop();
    if (mallocLockDepth++ == 0)
      mallocThreadsState = s;
  }

  void __malloc_unlock(struct _reent *)
  {
    if (--mallocLockDepth == 0)
      threads.start(mallocThreadsState);
  }
}

// TeensyThreads' Mutex isn't recursive. Display functions call each other and are called
// from code that already holds the lock, so allow the owning thread to lock again.
//
// Waiting uses try_lock() and yield() only. Mutex::lock() suspends the waiting thread, but
// if the holder unlocks between the waiter's failed try and its suspend, nothing ever
// restarts it (lost wake-up): the display thread froze for good under heavy MIDI CC traffic.
class RecursiveMutex
{
public:
  void lock()
  {
    int id = threads.id();
    if (owner == id)
    {
      depth++;
      return;
    }
    while (!mutex.try_lock())
      threads.yield(); // let the holder run and release it
    owner = id;
    depth = 1;
  }
  void unlock()
  {
    if (--depth == 0)
    {
      owner = -1;
      mutex.unlock();
    }
  }

private:
  Threads::Mutex mutex;
  volatile int owner = -1;
  int depth = 0;
};

class LockGuard
{
public:
  explicit LockGuard(RecursiveMutex &m) : m(m) { m.lock(); }
  ~LockGuard() { m.unlock(); }

private:
  RecursiveMutex &m;
};

RecursiveMutex displayLock;

#endif
