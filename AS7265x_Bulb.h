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
  AS7265x_Bulb.h
  Make Asayake to Wake Project.
  Kiyo Chinzei
  https://github.com/kchinzei/ESP8266-AS7265x-Server
*/

#ifndef _as7265x_bulb_h_
#define _as7265x_hulb_h_

#include "SparkFun_AS7265X.h"

static uint8_t AS7265xBulb_Current[] = {0,
    AS7265X_LED_CURRENT_LIMIT_12_5MA,
    AS7265X_LED_CURRENT_LIMIT_25MA,
    AS7265X_LED_CURRENT_LIMIT_50MA,
    AS7265X_LED_CURRENT_LIMIT_100MA};

class AS7265xBulb
{
public:
    int getState() {
        return current_index != 0;
    }

    void setState(int onoff) {
        if (!sensor) return;
        if (onoff) {
            current_index = 1;
            sensor->setBulbCurrent(AS7265xBulb_Current[current_index], bulbtype);
            sensor->enableBulb(bulbtype);
        } else {
            sensor->disableBulb(bulbtype);
            current_index = 0;
        }
    }

    void toggle() {
        setState(!getState());
    }

    void toggleUp() {
        if (!sensor) return;
        if (current_index+1 >= current_limit_index) {
            sensor->disableBulb(bulbtype);
            current_index = 0;
        } else {
            current_index++;
            sensor->setBulbCurrent(AS7265xBulb_Current[current_index], bulbtype);
            if (current_index == 1)
                sensor->enableBulb(bulbtype);
        }
        // Serial.printf(" -- bulb %d state %d (%.1fmA) \n", bulbtype, getState(), getCurrent());
    }
    
    void init(AS7265X *sensor, uint8_t ledtype, uint8_t limit = 0b1111) {
        this->sensor = sensor;
        bulbtype = ledtype;
        
        // Set H/W limit
        switch (ledtype) {
        case AS7265x_LED_WHITE:
            current_limit_index = 5; // no limit
            break;
        case AS7265x_LED_IR:
            current_limit_index = 2; // 25 mA not allowed
            break;
        case AS7265x_LED_UV:
            current_limit_index = 4; // 100 mA not allowed
            break;
        default:
            // error. can typeguard by enum ledtype but not that serious.
            exit(1);
            break;
        }
        // Apply user-set limit if it's smaller than H/W limit
        int i;
        for (i=1; i<current_limit_index; i++) {
            if (AS7265xBulb_Current[i] == limit) {
                current_limit_index = i+1;
                break;
            }
        }
        current_index = 0;
        this->sensor->disableBulb(bulbtype);
    }

    AS7265xBulb() {
        this->sensor = nullptr;
        this->bulbtype = AS7265x_LED_WHITE;
        this->current_index = 0;
        this->current_limit_index = 0;
    }
    
    ~AS7265xBulb() {
        // You may want turn off it. But we don't know if sensor is still valid when this destructor called. Leave it as is.
    }
    
    float getCurrent() {
        float mA = 0;
        if (getState()) {
            switch (AS7265xBulb_Current[current_index]) {
            case AS7265X_LED_CURRENT_LIMIT_12_5MA:
                mA = 12.5; break;
            case AS7265X_LED_CURRENT_LIMIT_25MA:
                mA = 25; break;
            case AS7265X_LED_CURRENT_LIMIT_50MA:
                mA = 50; break;
            case AS7265X_LED_CURRENT_LIMIT_100MA:
                mA = 100; break;
            }
        }
        return mA;
    }

protected:
    AS7265X *sensor;
    uint8_t bulbtype;
    int current_index;
    int current_limit_index;
};

#endif
