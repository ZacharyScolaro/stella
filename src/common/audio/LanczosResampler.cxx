//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2018 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include <cmath>
#ifndef M_PI
  #define M_PI 3.14159265358979323846f
#endif

#include "LanczosResampler.hxx"

namespace {

  constexpr float CLIPPING_FACTOR = 0.75;
  constexpr float HIGH_PASS_CUT_OFF = 10;

  uInt32 reducedDenominator(uInt32 n, uInt32 d)
  {
    for (uInt32 i = std::min(n ,d); i > 1; --i) {
      if ((n % i == 0) && (d % i == 0)) {
        n /= i;
        d /= i;
        i = std::min(n ,d);
      }
    }

    return d;
  }

  float sinc(float x)
  {
    // We calculate the sinc with double precision in order to compensate for precision loss
    // around zero
    return x == 0.f ? 1 : static_cast<float>(
        sin(M_PI * static_cast<double>(x)) / M_PI / static_cast<double>(x)
    );
  }

  float lanczosKernel(float x, uInt32 a) {
    return sinc(x) * sinc(x / static_cast<float>(a));
  }

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
LanczosResampler::LanczosResampler(
  Resampler::Format formatFrom,
  Resampler::Format formatTo,
  Resampler::NextFragmentCallback nextFragmentCallback,
  uInt32 kernelParameter)
:
  Resampler(formatFrom, formatTo, nextFragmentCallback),
  // In order to find the number of kernels we need to precompute, we need to find N minimal such that
  //
  // N / formatTo.sampleRate = M / formatFrom.sampleRate
  //
  // with integral N and M. Equivalently, we have
  //
  // formatFrom.sampleRate / formatTo.sampleRate = M / N
  //
  // -> we find N from fully reducing the fraction.
  myPrecomputedKernelCount(reducedDenominator(formatFrom.sampleRate, formatTo.sampleRate)),
  myKernelSize(2 * kernelParameter),
  myCurrentKernelIndex(0),
  myKernelParameter(kernelParameter),
  myCurrentFragment(nullptr),
  myFragmentIndex(0),
  myIsUnderrun(true),
  myHighPassL(HIGH_PASS_CUT_OFF, float(formatFrom.sampleRate)),
  myHighPassR(HIGH_PASS_CUT_OFF, float(formatFrom.sampleRate)),
  myHighPass(HIGH_PASS_CUT_OFF, float(formatFrom.sampleRate)),
  myTimeIndex(0)
{
  myPrecomputedKernels = make_unique<float[]>(myPrecomputedKernelCount * myKernelSize);

  if (myFormatFrom.stereo)
  {
    myBufferL = make_unique<ConvolutionBuffer>(myKernelSize);
    myBufferR = make_unique<ConvolutionBuffer>(myKernelSize);
  }
  else
    myBuffer = make_unique<ConvolutionBuffer>(myKernelSize);

  precomputeKernels();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LanczosResampler::precomputeKernels()
{
  // timeIndex = time * formatFrom.sampleRate * formatTo.sampleRAte
  uInt32 timeIndex = 0;

  for (uInt32 i = 0; i < myPrecomputedKernelCount; ++i) {
    float* kernel = myPrecomputedKernels.get() + myKernelSize * i;
    // The kernel is normalized such to be evaluate on time * formatFrom.sampleRate
    float center =
      static_cast<float>(timeIndex) / static_cast<float>(myFormatTo.sampleRate);

    for (uInt32 j = 0; j < 2 * myKernelParameter; ++j) {
      kernel[j] = lanczosKernel(
          center - static_cast<float>(j) + static_cast<float>(myKernelParameter) - 1.f, myKernelParameter
        ) * CLIPPING_FACTOR;
    }

    // Next step: time += 1 / formatTo.sampleRate
    //
    // By construction, we limit the argument during kernel evaluation to 0 .. 1, which
    // corresponds to 0 .. 1 / formatFrom.sampleRate for time. To implement this, we decompose
    // time as follows:
    //
    // time = N / formatFrom.sampleRate + delta
    // timeIndex = N * formatTo.sampleRate + delta * formatTo.sampleRate * formatFrom.sampleRate
    //
    // with N integral and delta < 0. From this, it follows that we replace
    // time with delta, i.e. take the modulus of timeIndex.
    timeIndex = (timeIndex + myFormatFrom.sampleRate) % myFormatTo.sampleRate;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LanczosResampler::fillFragment(float* fragment, uInt32 length)
{
  if (myIsUnderrun) {
    Int16* nextFragment = myNextFragmentCallback();

    if (nextFragment) {
      myCurrentFragment = nextFragment;
      myFragmentIndex = 0;
      myIsUnderrun = false;
    }
  }

  if (!myCurrentFragment) {
    memset(fragment, 0, sizeof(float) * length);
    return;
  }

  const uInt32 outputSamples = myFormatTo.stereo ? (length >> 1) : length;

  for (uInt32 i = 0; i < outputSamples; ++i) {
    float* kernel = myPrecomputedKernels.get() + (myCurrentKernelIndex * myKernelSize);
    myCurrentKernelIndex = (myCurrentKernelIndex + 1) % myPrecomputedKernelCount;

    if (myFormatFrom.stereo) {
      float sampleL = myBufferL->convoluteWith(kernel);
      float sampleR = myBufferR->convoluteWith(kernel);

      if (myFormatTo.stereo) {
        fragment[2*i] = sampleL;
        fragment[2*i + 1] = sampleR;
      }
      else
        fragment[i] = (sampleL + sampleR) / 2.f;
    } else {
      float sample = myBuffer->convoluteWith(kernel);

      if (myFormatTo.stereo)
        fragment[2*i] = fragment[2*i + 1] = sample;
      else
        fragment[i] = sample;
    }

    myTimeIndex += myFormatFrom.sampleRate;

    uInt32 samplesToShift = myTimeIndex / myFormatTo.sampleRate;
    if (samplesToShift == 0) continue;

    myTimeIndex %= myFormatTo.sampleRate;
    shiftSamples(samplesToShift);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void LanczosResampler::shiftSamples(uInt32 samplesToShift)
{
  while (samplesToShift-- > 0) {
    if (myFormatFrom.stereo) {
      myBufferL->shift(myHighPassL.apply(myCurrentFragment[2*myFragmentIndex] / static_cast<float>(0x7fff)));
      myBufferR->shift(myHighPassR.apply(myCurrentFragment[2*myFragmentIndex + 1] / static_cast<float>(0x7fff)));
    }
    else
      myBuffer->shift(myHighPass.apply(myCurrentFragment[myFragmentIndex] / static_cast<float>(0x7fff)));

    ++myFragmentIndex;

    if (myFragmentIndex >= myFormatFrom.fragmentSize) {
      myFragmentIndex %= myFormatFrom.fragmentSize;

      Int16* nextFragment = myNextFragmentCallback();
      if (nextFragment) {
        myCurrentFragment = nextFragment;
        myIsUnderrun = false;
      } else {
        (cerr << "audio buffer underrun\n").flush();
        myIsUnderrun = true;
      }
    }
  }
}
