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

#include "ConvolutionBuffer.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ConvolutionBuffer::ConvolutionBuffer(uInt32 size)
  : myFirstIndex(0),
    mySize(size)
{
  myData = make_unique<float[]>(mySize);
  memset(myData.get(), 0, mySize * sizeof(float));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ConvolutionBuffer::shift(float nextValue)
{
  myData[myFirstIndex] = nextValue;
  myFirstIndex = (myFirstIndex + 1) % mySize;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float ConvolutionBuffer::convoluteWith(float* kernel) const
{
  float result = 0.;

  for (uInt32 i = 0; i < mySize; ++i) {
    result += kernel[i] * myData[(myFirstIndex + i) % mySize];
  }

  return result;
}
