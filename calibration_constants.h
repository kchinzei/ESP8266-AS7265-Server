/*
  The MIT License (MIT)
  Copyright (c) Kiyo Chinzei (kchinzei@gmail.com)
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/
/*
  Calibration constants for AS7265x / AS7341

  SparkFun AS7265x constants:
  Determined by measurements of the white LED on SparkFun AS7265x
  (LUXEON 3014/5700k) and reading of the spectrum curve on the spec sheet.

  Make Asayake to Wake Project.
  Kiyo Chinzei
  https://github.com/kchinzei/ESP8266-AS7265-Server
*/

#ifndef _calibration_constants_h_
#define _calibration_constants_h_

static float CalParams7265x[] = {
  // Order of A-W channels.
  0.723852705,
  0.723852705,
  0.723852705,
  0.723852705,
  0.723852705,
  0.723852705,
  1.0,
  1.0,
  1.0,
  1.0,
  1.0,
  1.0,
  0.477765969,
  0.477765969,
  0.477765969,
  0.477765969,
  0.477765969,
  0.477765969
};

static float CalParams7341[] = {
  // Order of wavelength, 415 to 910 nm
  6.054618363,
  3.299449928,
  2.648300472,
  2.060079981,
  1.709161169,
  1.532353456,
  1.318766377,
  1.0,
  2.494843599
};

#endif
