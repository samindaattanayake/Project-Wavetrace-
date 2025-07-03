#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H

#include <SPI.h>
#include <XPT2046_Touchscreen.h>
// In Touchscreen.h

#define XPT2046_CS 33      // Must match TOUCH_CS
#define XPT2046_IRQ -1     // Must match TOUCH_IRQ
#define XPT2046_CLK 25     // Must match TOUCH_CLK
#define XPT2046_MISO 34    // Must match TOUCH_DOUT
#define XPT2046_MOSI 32    // Must match TOUCH_DIN

extern SPIClass touchscreenSPI;
extern XPT2046_Touchscreen ts;

#define TS_MINX 300
#define TS_MAXX 3800
#define TS_MINY 300
#define TS_MAXY 3800
#define DISPLAY_WIDTH 240  
#define DISPLAY_HEIGHT 320 

extern bool feature_active;

void setupTouchscreen(); 

#endif
