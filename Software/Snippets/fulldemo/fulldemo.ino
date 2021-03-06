/***************************************************
  This is our GFX example for the Adafruit ILI9341 Breakout and Shield
  ----> http://www.adafruit.com/products/1651

  Check out the links above for our tutorials and wiring diagrams
  These displays use SPI to communicate, 4 or 5 pins are required to
  interface (RST is optional)
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/


/*
I2C addresses:
Audio:  0x1A
IO_EXP: 0x20 => Spec sheet says 
"To enter the Read mode the master (microcontroller) addresses the slave
device and sets the last bit of the address byte to logic 1 (address byte read)"
The Wire lib automatically uses the right I/O port for read and write.
ADXL:   0x53
BME230: 0x77 (Temp/Humidity/Pressure)
*/

#define CONFIG_DISABLE_HAL_LOCKS 1

#include <Wire.h>
#include <SPI.h>

#include "Adafruit_GFX.h"
// Support for LCD screen
// The latest version of that library may not be up to date and miss a patch for ESP32
// which will cause a compilation error:
// Adafruit_ILI9341.cpp:113:3: error: 'mosiport' was not declared in this scope
// If so, get the latest version from github, or just patch this single line
// https://github.com/adafruit/Adafruit_ILI9341/blob/master/Adafruit_ILI9341.cpp#L98
#include "Adafruit_ILI9341.h"

// Support for APA106 RGB LEDs
// Current Adafruit code does not support writing to any LED strip on ESP32
#include "Adafruit_NeoPixel.h"

// Accelerometer
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

#include "defs.h"

// Touch screen
#include "XPT2046_Touchscreen.h"


// https://github.com/CCHS-Melbourne/iotuz-esp32-hardware/wiki has hardware mapping details

// LCD brightness control and touchscreen CS are behind the port
// expander, as well as both push buttons
#define I2C_EXPANDER 0x20 //0100000 (7bit) address of the IO expander on i2c bus

/* Port expander PCF8574, access via I2C on */
#define I2CEXP_ACCEL_INT    0x01 // (In)
#define I2CEXP_A_BUT	    0x02 // (In)
#define I2CEXP_B_BUT	    0x04 // (In)
#define I2CEXP_ENC_BUT	    0x08 // (In)
#define I2CEXP_SD_CS	    0x10 // (Out)
#define I2CEXP_TOUCH_INT    0x20 // (In)
#define I2CEXP_TOUCH_CS	    0x40 // (Out)
#define I2CEXP_LCD_BL_CTR   0x80 // (Out)

// Dealing with the I/O expander is a bit counter intuitive. There is no difference between a
// write meant to toggle an output port, and a write designed to turn off pull down resistors and trigger
// a read request.
// The write just before the read should have bits high on the bits you'd like to read back, but you
// may get high bits back on other bits you didn't turn off the pull down resistor on. This is normal.
// Just filter out the bits you're trying to get from the value read back and keep in mind that when
// you write, you should still send the right bit values for the output pins.
// This is all stored in i2cexp which we initialize to the bits used as input:
#define I2CEXP_IMASK ( I2CEXP_ACCEL_INT + I2CEXP_A_BUT + I2CEXP_B_BUT + I2CEXP_ENC_BUT + I2CEXP_TOUCH_INT )
// Any write to I2CEXP should contain those mask bits so allow reads to work later
uint8_t i2cexp = I2CEXP_IMASK;
bool butA   = false;
bool butB   = false;
bool butEnc = false;
// Are we drawing on the screen with joystick, accelerator, or finger?
uint16_t joyValueX;
uint16_t joyValueY;
bool joyBtnValue;

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// TFT + Touch Screen Setup Start
// These are the minimal changes from v0.1 to get the LCD working
#define TFT_DC 4
#define TFT_CS 19
#define TFT_RST 32
// SPI Pins are shared by TFT, touch screen, SD Card
#define MISO 12
#define MOSI 13
#define SPI_CLK 14

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Buffer to store strings going to be printed on tft
char tft_str[41];
// TFT Setup End

// Touch screen select is on port expander line 6, not directly connected, so the library
// cannot toggle it directly. It however requires a CS pin, so I'm giving it 33, a spare IO
// pin so that it doesn't break anything else.
#define TS_CS_PIN  33
// similarly, the interrupt pin is connected to the port expander, so the library can't use it
// I'm told it is possible to pass an interrupt through the IO expander, but I'm not doing this yet.
//#define TS_IRQ_PIN  0

XPT2046_Touchscreen ts(TS_CS_PIN);  // Param 2 - NULL - No interrupts
//XPT2046_Touchscreen ts(TS_CS_PIN, TS_IRQ_PIN);  // Param 2 - Touch IRQ Pin - interrupt enabled polling



// APA106 is somewhat compatible with WS2811 or WS2812 (but not quite, timing is different?)
// https://learn.adafruit.com/adafruit-neopixel-uberguide/
// FIXME: This should work, but currently does not due to lack of support for ESP32 in the adafruit lib
#define RGB_LED_PIN 23
#define NUMPIXELS 2
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// Joystick Setup
#define JOYSTICK_X_PIN 39
#define JOYSTICK_Y_PIN 34
#define JOYSTICK_BUT_PIN 0

// How many options to display in the rectangle screen
#define NHORIZ 5
#define NVERT 4
// Option names to display on screen
// 40 chars wide, 5 boxes, 8 char per box
// 30 chars high, 4 boxes, 7 lines per box
char* opt_name[NVERT][NHORIZ][3] = {
    { { "", "Finger", "Paint"}, { "Joystick", "Absolute", "Paint"}, { "Joystick", "Relative", "Paint"}, { "", "Accel", "Paint"}, { "", "", ""}, },
    { { "", "", ""}, { "", "", ""}, { "", "", ""}, { "", "Round", "Rects"}, { "Round", "Fill", "Rects"}, },
    { { "", "Text", ""}, { "", "Fill", ""}, { "", "Diagonal", "Lines"}, { "Horizon", "Vert", "Lines"}, { "", "Rectangle", ""}, },
    { { "", "Fill", "Rectangle"}, { "", "Circles", ""}, { "", "Fill", "Circles"}, { "", "Triangles", ""}, { "", "Fill", "Triangles"}, },
};
// tft_width, tft_height, calculated in setup after tft init
uint16_t tftw, tfth;
// number of pixels of each selection box (height and width)
uint16_t boxw, boxh;

unsigned long testFillScreen() {
  unsigned long start = micros();
  tft.fillScreen(ILI9341_RED);
  yield();
  tft.fillScreen(ILI9341_GREEN);
  yield();
  tft.fillScreen(ILI9341_BLUE);
  yield();
  tft.fillScreen(ILI9341_BLACK);
  yield();
  return micros() - start;
}

unsigned long testText() {
  unsigned long start = micros();
  tft.setCursor(0, 0);
  tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(1);
  tft.println("Hello World!");
  tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(2);
  tft.println(1234.56);
  tft.setTextColor(ILI9341_RED);    tft.setTextSize(3);
  tft.println(0xDEADBEEF, HEX);
  tft.println();
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(5);
  tft.println("Groop");
  tft.setTextSize(2);
  tft.println("I implore thee,");
  tft.setTextSize(1);
  tft.println("my foonting turlingdromes.");
  tft.println("And hooptiously drangle me");
  tft.println("with crinkly bindlewurdles,");
  tft.println("Or I will rend thee");
  tft.println("in the gobberwarts");
  tft.println("with my blurglecruncheon,");
  tft.println("see if I don't!");
  return micros() - start;
}

unsigned long testLines(uint16_t color) {
  unsigned long start, t;
  int           x1, y1, x2, y2,
                w = tft.width(),
                h = tft.height();

  x1 = y1 = 0;
  y2    = h - 1;
  start = micros();
  for(x2=0; x2<w; x2+=6) tft.drawLine(x1, y1, x2, y2, color);
  x2    = w - 1;
  for(y2=0; y2<h; y2+=6) tft.drawLine(x1, y1, x2, y2, color);
  t     = micros() - start; // fillScreen doesn't count against timing

  yield();
  tft.fillScreen(ILI9341_BLACK);
  yield();

  x1    = w - 1;
  y1    = 0;
  y2    = h - 1;
  start = micros();
  for(x2=0; x2<w; x2+=6) tft.drawLine(x1, y1, x2, y2, color);
  x2    = 0;
  for(y2=0; y2<h; y2+=6) tft.drawLine(x1, y1, x2, y2, color);
  t    += micros() - start;

  yield();
  tft.fillScreen(ILI9341_BLACK);
  yield();

  x1    = 0;
  y1    = h - 1;
  y2    = 0;
  start = micros();
  for(x2=0; x2<w; x2+=6) tft.drawLine(x1, y1, x2, y2, color);
  x2    = w - 1;
  for(y2=0; y2<h; y2+=6) tft.drawLine(x1, y1, x2, y2, color);
  t    += micros() - start;

  yield();
  tft.fillScreen(ILI9341_BLACK);
  yield();

  x1    = w - 1;
  y1    = h - 1;
  y2    = 0;
  start = micros();
  for(x2=0; x2<w; x2+=6) tft.drawLine(x1, y1, x2, y2, color);
  x2    = 0;
  for(y2=0; y2<h; y2+=6) tft.drawLine(x1, y1, x2, y2, color);

  yield();
  return micros() - start;
}

unsigned long testFastLines(uint16_t color1, uint16_t color2) {
  unsigned long start;
  int           x, y, w = tft.width(), h = tft.height();

  start = micros();
  for(y=0; y<h; y+=5) tft.drawFastHLine(0, y, w, color1);
  for(x=0; x<w; x+=5) tft.drawFastVLine(x, 0, h, color2);

  return micros() - start;
}

unsigned long testRects(uint16_t color) {
  unsigned long start;
  int           n, i, i2,
                cx = tft.width()  / 2,
                cy = tft.height() / 2;

  n     = min(tft.width(), tft.height());
  start = micros();
  for(i=2; i<n; i+=6) {
    i2 = i / 2;
    tft.drawRect(cx-i2, cy-i2, i, i, color);
  }

  return micros() - start;
}

unsigned long testFilledRects(uint16_t color1, uint16_t color2) {
  unsigned long start, t = 0;
  int           n, i, i2,
                cx = tft.width()  / 2 - 1,
                cy = tft.height() / 2 - 1;

  n = min(tft.width(), tft.height());
  for(i=n; i>0; i-=6) {
    i2    = i / 2;
    start = micros();
    tft.fillRect(cx-i2, cy-i2, i, i, color1);
    t    += micros() - start;
    // Outlines are not included in timing results
    tft.drawRect(cx-i2, cy-i2, i, i, color2);
    yield();
  }

  return t;
}

unsigned long testFilledCircles(uint8_t radius, uint16_t color) {
  unsigned long start;
  int x, y, w = tft.width(), h = tft.height(), r2 = radius * 2;

  start = micros();
  for(x=radius; x<w; x+=r2) {
    for(y=radius; y<h; y+=r2) {
      tft.fillCircle(x, y, radius, color);
    }
  }

  return micros() - start;
}

unsigned long testCircles(uint8_t radius, uint16_t color) {
  unsigned long start;
  int           x, y, r2 = radius * 2,
                w = tft.width()  + radius,
                h = tft.height() + radius;

  // Screen is not cleared for this one -- this is
  // intentional and does not affect the reported time.
  start = micros();
  for(x=0; x<w; x+=r2) {
    for(y=0; y<h; y+=r2) {
      tft.drawCircle(x, y, radius, color);
    }
  }

  return micros() - start;
}

unsigned long testTriangles() {
  unsigned long start;
  int           n, i, cx = tft.width()  / 2 - 1,
                      cy = tft.height() / 2 - 1;

  n     = min(cx, cy);
  start = micros();
  for(i=0; i<n; i+=5) {
    tft.drawTriangle(
      cx    , cy - i, // peak
      cx - i, cy + i, // bottom left
      cx + i, cy + i, // bottom right
      tft.color565(i, i, i));
  }

  return micros() - start;
}

unsigned long testFilledTriangles() {
  unsigned long start, t = 0;
  int           i, cx = tft.width()  / 2 - 1,
                   cy = tft.height() / 2 - 1;

  start = micros();
  for(i=min(cx,cy); i>10; i-=5) {
    start = micros();
    tft.fillTriangle(cx, cy - i, cx - i, cy + i, cx + i, cy + i,
      tft.color565(0, i*10, i*10));
    t += micros() - start;
    tft.drawTriangle(cx, cy - i, cx - i, cy + i, cx + i, cy + i,
      tft.color565(i*10, i*10, 0));
    yield();
  }

  return t;
}

unsigned long testRoundRects() {
  unsigned long start;
  int           w, i, i2,
                cx = tft.width()  / 2 - 1,
                cy = tft.height() / 2 - 1;

  w     = min(tft.width(), tft.height());
  start = micros();
  for(i=0; i<w; i+=6) {
    i2 = i / 2;
    tft.drawRoundRect(cx-i2, cy-i2, i, i, i/8, tft.color565(i, 0, 0));
  }

  return micros() - start;
}

unsigned long testFilledRoundRects() {
  unsigned long start;
  int           i, i2,
                cx = tft.width()  / 2 - 1,
                cy = tft.height() / 2 - 1;

  start = micros();
  for(i=min(tft.width(), tft.height()); i>20; i-=6) {
    i2 = i / 2;
    tft.fillRoundRect(cx-i2, cy-i2, i, i, i/8, tft.color565(0, i, 0));
    yield();
  }

  return micros() - start;
}

// To clear bit #7, send 128
void i2cexp_clear_bits(uint8_t bitfield) {
    // set bits to clear to 0, all other to 1, binary and to clear the bits
    i2cexp &= (~bitfield);
    pcf8574_write(i2cexp);
}

// To set bit #7, send 128
void i2cexp_set_bits(uint8_t bitfield) {
    i2cexp |= bitfield;
    pcf8574_write(i2cexp);
}

uint8_t i2cexp_read() {
    // For read to work, we must have sent 1 bits on the ports that get used as input
    // This is done by i2cexp_clear_bits called in setup.
    Wire.requestFrom(I2C_EXPANDER, 1); // FIXME: deal with returned error here?
    while (Wire.available() < 1) ;
    uint8_t read = ~Wire.read(); // Apparently one has to invert the bits read
    // When no buttons are pushed, this returns 0x91, which includes some ports
    // we use as output, so we do need to filter out the ports used as read.
    // Serial.println(read, HEX);
    return read;
}

// I2C/TWI success (transaction was successful).
#define ku8TWISuccess    0
// I2C/TWI device not present (address sent, NACK received).
#define ku8TWIDeviceNACK 2
// I2C/TWI data not received (data sent, NACK received).
#define ku8TWIDataNACK   3
// I2C/TWI other error.
#define ku8TWIError      4

void pcf8574_write(uint8_t dt){
    uint8_t error;

    Wire.beginTransmission(I2C_EXPANDER);
    // Serial.print("Writing to I2CEXP: ");
    // Serial.println(dt);
    Wire.write(dt);
    error = Wire.endTransmission();
    if (error != ku8TWISuccess) {
	// FIXME: do something here if you like
    }
}

void lcd_test(LCDtest choice) {
    switch (choice) {

    case TEXT:
	Serial.print(F("Text                     "));
	Serial.println(testText());
        break;
	 
    case FILL:
	Serial.print(F("Screen fill              "));
	Serial.println(testFillScreen());
        break;

    case LINES:
	Serial.print(F("Lines                    "));
	Serial.println(testLines(ILI9341_CYAN));
	break;

    case HORIZVERT:
	Serial.print(F("Horiz/Vert Lines         "));
	Serial.println(testFastLines(ILI9341_RED, ILI9341_BLUE));
        break;

    case RECT:
	Serial.print(F("Rectangles (outline)     "));
	Serial.println(testRects(ILI9341_GREEN));
	break;

    case RECTFILL:
	Serial.print(F("Rectangles (filled)      "));
	Serial.println(testFilledRects(ILI9341_YELLOW, ILI9341_MAGENTA));
	break;

    case CIRCLE:
	Serial.print(F("Circles (outline)        "));
	Serial.println(testCircles(10, ILI9341_WHITE));
	break;

    case CIRCFILL:
	Serial.print(F("Circles (filled)         "));
	Serial.println(testFilledCircles(10, ILI9341_MAGENTA));
	break;

    case TRIANGLE:
	Serial.print(F("Triangles (outline)      "));
	Serial.println(testTriangles());
	break;

    case TRIFILL:
	Serial.print(F("Triangles (filled)       "));
	Serial.println(testFilledTriangles());
	break;

    case ROUNDREC:
	Serial.print(F("Rounded rects (outline)  "));
	Serial.println(testRoundRects());
	break;

    case ROUNDRECFILL:
	Serial.print(F("Rounded rects (filled)   "));
	Serial.println(testFilledRoundRects());
	break;
    }
}

// maxlength is the maximum number of characters that need to be deleted before writing on top
void tftprint(uint16_t x, uint16_t y, uint8_t maxlength, char *text) {
    if (maxlength > 0) tft.fillRect(x*8, y*8, maxlength*8, 8, ILI9341_BLACK);
    tft.setCursor(x*8, y*8);
    tft.println(text);
}

TS_Point get_touch() {
    // Clear (i.e. set) CS for TS before talking to it
    i2cexp_clear_bits(I2CEXP_TOUCH_CS);
    // Calling getpoint calls SPI.beginTransaction with a speed of only 2MHz, so we need to
    // reset the speed to something faster before talking to the screen again.
    TS_Point p = ts.getPoint();
    // Then disable it again so that talking SPI to LCD doesn't reach TS
    i2cexp_set_bits(I2CEXP_TOUCH_CS);

    return p;
}

void touchcoord2pixelcoord(uint16_t *pixel_x, uint16_t *pixel_y) {
    // Pressure goes from 1000 to 2200 with a stylus but is unreliable,
    // 3000 if you mash a finger in, let's say 2048 range
    // Colors are 16 bits, so multiply pressure by 32 to get a color range from pressure
    // X goes from 320 to 3900 (let's say 3600), Y goes from 200 to 3800 (let's say 3600 too)
    // each X pixel is 11.25 dots of resolution on digitizer, and 15 dots for Y.
    Serial.print("Converted touch coordinates ");
    Serial.print(*pixel_x);
    Serial.print("x");
    Serial.print(*pixel_y);
    *pixel_x = constrain((*pixel_x-320)/11.25, 0, 319);
    *pixel_y = constrain((*pixel_y-200)/15, 0, 239);
    Serial.print(" to pixel coordinates ");
    Serial.print(*pixel_x);
    Serial.print("x");
    Serial.println(*pixel_y);
}

void finger_draw() {
    uint16_t color_pressure, color;
    static uint8_t update_coordinates = 0;
    TS_Point p = get_touch();

    if (p.z) {
	uint16_t pixel_x = p.x, pixel_y = p.y;
	touchcoord2pixelcoord(&pixel_x, &pixel_y);
	
	// Colors are 16 bits, 5 bit: red, 6 bits: green, 5 bits: blue
	// to map a pressure number to colors and avoid random black looking colors,
	// let's seed the color with 2 lowest bits per color: 0001100001100011
	// this gives us 10 bits we need to fill in for which color we'll use,
	color_pressure = p.z-1000;
	if (p.z < 1000) color_pressure = 0;
	color_pressure = constrain(color_pressure, 0, 2047)/2;
	color = tenbitstocolor(color_pressure);
	tft.fillCircle(pixel_x, pixel_y, 2, color);
	update_coordinates = 1;
    // Writing coordinates every time is too slow, write less often
    } else if (update_coordinates) {
	update_coordinates = 0;
	sprintf(tft_str, "%d", p.x);
	tftprint(2, 0, 4, tft_str);
	sprintf(tft_str, "%d", p.y);
	tftprint(2, 1, 4, tft_str);
    }
}

void read_joystick(bool showdebug=true) {
    // read the analog in value:
    joyValueX = 4096-analogRead(JOYSTICK_X_PIN);
    joyValueY = analogRead(JOYSTICK_Y_PIN);
    joyBtnValue = !digitalRead(JOYSTICK_BUT_PIN);

    if (showdebug) {
	// print the results to the serial monitor:
	Serial.print("X Axis = ");
	Serial.print(joyValueX);
	Serial.print("\t Y Axis = ");
	Serial.print(joyValueY);
	Serial.print("\t Joy Button = ");
	Serial.println(joyBtnValue);
    }
}

// Draw the dot directly to where the joystick is pointing.
void joystick_draw() {
    static int8_t update_cnt = 0;
    // 4096 -> 320 (divide by 12.8) and -> 240 (divide by 17)
    // Sadly on my board, the middle is 2300/1850 and not 2048/2048
    read_joystick();
    uint16_t pixel_x = joyValueX/12.8;
    uint16_t pixel_y = joyValueY/17;
    tft.fillCircle(pixel_x, pixel_y, 2, ILI9341_WHITE);

    // Do not write the cursor values too often, it's too slow
    if (!update_cnt++ % 16)
    {
	sprintf(tft_str, "%d > %d", joyValueX, pixel_x);
	tftprint(2, 0, 10, tft_str);
	sprintf(tft_str, "%d > %d", joyValueY, pixel_y);
	tftprint(2, 1, 10, tft_str);
    }
}

// Move the dot relative to the joystick position (like driving a ball).
void joystick_draw_relative() {
    static uint16_t update_cnt = 0;
    static float pixel_x = 160;
    static float pixel_y = 120;
    // Sadly on my board, the middle is 2300/1850 and not 2048/2048
    read_joystick();
    float move_x = (joyValueX-2300.0)/2048;
    float move_y = (joyValueY-1850.0)/2048;

    tft.fillCircle(int(pixel_x), int(pixel_y), 2, tenbitstocolor(update_cnt % 1024));
    pixel_x = constrain(pixel_x + move_x, 0, 319);
    pixel_y = constrain(pixel_y + move_y, 0, 239);

    // Do not write the cursor values too often, it's too slow
    if (!(update_cnt++ % 32)) {
	sprintf(tft_str, "%.1f > %.1f", move_x, int(pixel_x));
	tftprint(2, 0, 10, tft_str);
	sprintf(tft_str, "%.1f > %.1f", move_y, int(pixel_y));
	tftprint(2, 1, 10, tft_str);
    }
}

void accel_draw() {
    static uint16_t update_cnt = 0;
    static float pixel_x = 160;
    static float pixel_y = 120;
    sensors_event_t event; 
    accel.getEvent(&event);
    float accel_x = -event.acceleration.x;
    // My accelerator isn't really level, it shows 2 when my board is flat
    float accel_y = event.acceleration.y - 2;

    tft.fillCircle(int(pixel_x), int(pixel_y), 2, tenbitstocolor(update_cnt % 1024));
    pixel_x = constrain(pixel_x + accel_x, 0, 319);
    pixel_y = constrain(pixel_y + accel_y, 0, 239);

    // Do not write the cursor values too often, it's too slow
    if (!(update_cnt++ % 32)) {
	sprintf(tft_str, "%.1f > %.1f", accel_x, int(pixel_x));
	tftprint(2, 0, 10, tft_str);
	sprintf(tft_str, "%.1f > %.1f", accel_y, int(pixel_y));
	tftprint(2, 1, 10, tft_str);
    }
}

uint16_t tenbitstocolor(uint16_t tenbits) {
    uint16_t color;

    // TFT colors are 5 bit Red, 6 bits Green, 5 bits Blue, we want to avoid
    // black looking colors, so we seed the last 2 bits of each color to 1
    // and then interleave 10 bits ot color spread across the remaining bits
    // that affect the end color more visibly.
    // 3 highest bits (9-7), get shifted 6 times to 15-13
    // 4 middle  bits (6-3), get shifted 4 times to 10-07
    // 3 lowest  bits (2-0), get shifted 2 times to 04-02
    // This means that if the 10 input bits are 0, the output color is
    // 00011000 01100011 = 0x1863
    color = B00011000*256 + B01100011 + ((tenbits & 0x380) << 6) +
					((tenbits & B01111000) << 4) +
					((tenbits & B00000111) << 2);

//    Serial.print("Color: ");
//    Serial.print(tenbits, HEX);
//    Serial.print(" -> ");
//    Serial.println(color, HEX);
    return color;
}


void scan_buttons(bool *need_select) {
    uint8_t butt_state = i2cexp_read();
    *need_select = false;

    if (butt_state & I2CEXP_A_BUT && !butA)
    {
	butA = true;
	reset_tft();
	reset_textcoord();
	tftprint(0, 2, 0, "But A");
    }
    if (!(butt_state & I2CEXP_A_BUT) && butA)
    {
	butA = false;
	tftprint(0, 2, 5, "");
    }
    if (butt_state & I2CEXP_B_BUT && !butB)
    {
	butB = true;
	tftprint(0, 3, 0, "But B");
	*need_select = true;
    }
    if (!(butt_state & I2CEXP_B_BUT) && butB)
    {
	butB = false;
	tftprint(0, 3, 5, "");
    }
    if (butt_state & I2CEXP_ENC_BUT && !butEnc)
    {
	butEnc = true;
	tftprint(0, 4, 0, "Enc But");
    }
    if (!(butt_state & I2CEXP_ENC_BUT) && butEnc)
    {
	butEnc = false;
	tftprint(0, 4, 7, "");
    }
}

void reset_tft() {
    tft.setRotation(3);
    tft.fillScreen(ILI9341_BLACK);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
}

void reset_textcoord() {
    tft.setCursor(0, 0);
    tft.println("x=");
    tft.print("y=");
}

void displaySensorDetails(void)
{
    sensor_t sensor;
    accel.getSensor(&sensor);
    Serial.println("------------------------------------");
    Serial.print  ("Sensor:       "); Serial.println(sensor.name);
    Serial.print  ("Driver Ver:   "); Serial.println(sensor.version);
    Serial.print  ("Unique ID:    "); Serial.println(sensor.sensor_id);
    Serial.print  ("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" m/s^2");
    Serial.print  ("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" m/s^2");
    Serial.print  ("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" m/s^2");  
    Serial.println("------------------------------------");
    Serial.println("");
}




void draw_choices(void) {

    for(uint16_t x=tftw/NHORIZ; x<tftw; x+=boxw) tft.drawFastVLine(x, 0, tfth, ILI9341_LIGHTGREY);
    for(uint16_t y=tfth/NVERT;  y<tfth; y+=boxh) tft.drawFastHLine(0, y, tftw, ILI9341_LIGHTGREY);
    
    for(uint8_t y=0; y<NVERT; y++) { 
	for(uint8_t x=0; x<NHORIZ; x++) { 
	    for(uint8_t line=0; line<3; line++) { 
		tft.setCursor(x*boxw + 4, y*boxh + line*8 + 16);
		tft.println(opt_name[y][x][line]);
	    }
	}
    }

}

void show_selected_box(uint8_t x, uint8_t y) {
    tft.fillRect(x*boxw, y*boxh, boxw, boxh, ILI9341_LIGHTGREY);
}

uint8_t get_selection(void) {
    TS_Point p;
    uint8_t x, y, select;

    Serial.println("Waiting for finger touch to select option");
    do {
	p = get_touch();
    // at boot, I get a fake touch with pressure 1030
    } while ( p.z < 1060);

    uint16_t pixel_x = p.x, pixel_y = p.y;
    touchcoord2pixelcoord(&pixel_x, &pixel_y);

    x = pixel_x/(tftw/NHORIZ);
    y = pixel_y/(tfth/NVERT);
    Serial.print("Got touch in box coordinates ");
    Serial.print(x);
    Serial.print("x");
    Serial.print(y);
    Serial.print(" pressure: ");
    Serial.println(p.z);
    return (x + y*NHORIZ);
}

void loop(void) {
    static bool need_select = true;
    static uint8_t select;
    
    if (need_select) {
	reset_tft();
	draw_choices();
	select = get_selection();
	Serial.print("Got menu selection #");
	Serial.println(select);
	tft.fillScreen(ILI9341_BLACK);
    }

    switch (select) {
    case FINGERPAINT:
	if (need_select) reset_textcoord();
	finger_draw();
	break;
    case JOYABS:
	if (need_select) reset_textcoord();
	joystick_draw();
	break;
    case JOYREL:
	if (need_select) reset_textcoord();
	joystick_draw_relative();
	break;
    case ACCELPAINT:
	if (need_select) reset_textcoord();
	accel_draw();
	break;
    default:
	if (select >= 8) {
	    Serial.print("Running LCD Demo #");
	    Serial.println(select);
	    lcd_test((LCDtest) select);
	    delay(500);
	    need_select = true;
	    return;
	}
    }
    scan_buttons(&need_select);
    delay(1);
    return;
    

    if (joyBtnValue == true) select++;
    // tilting joystick back (not too far) while clicking goes back one demo
    if (joyValueX < 2000) select-=2;
    if (select == 12) select = 0;
    if (select == -1) select = 11;

    // Try to light up LEDs (not working yet)
    pixels.setPixelColor(0, select*20, 0, 0);
    pixels.setPixelColor(1, 0, select*20, 0);
    pixels.show();


    Serial.print("Running LCD Demo #");
    Serial.println(select);
    lcd_test((LCDtest) select);

    do {
	read_joystick(false);
	// wait 50 milliseconds before the next loop
	// for the analog-to-digital converter to settle
	// after the last reading:
	// this also calls yield() for us
	delay(50);

	// left
	if (joyValueX < 500)
	{
	    tft.setRotation(0);
	    lcd_test((LCDtest) select);
	}

	// right
	if (joyValueX > 3500)
	{
	    tft.setRotation(2);
	    lcd_test((LCDtest) select);
	}

	// up
	if (joyValueY < 500)
	{
	    tft.setRotation(1);
	    lcd_test((LCDtest) select);
	}

	// down
	if (joyValueY > 3500)
	{
	    tft.setRotation(3);
	    lcd_test((LCDtest) select);
	}
    } while (joyBtnValue  == false);

}


void setup() {
    Serial.begin(115200);
    Serial.println("Serial Begin"); 

    // Currently, we are using software SPI, but this init should also be done by the
    // adafruit library
    pinMode(MOSI, OUTPUT);
    pinMode(MISO, INPUT);
    pinMode(SPI_CLK, OUTPUT);

    // Joystick Setup
    pinMode(JOYSTICK_BUT_PIN, INPUT_PULLUP);

    // TFT Setup
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    // Hardware SPI on ESP32 is actually slower than software SPI. Giving 80Mhz
    // here does not make things any faster. There seems to be a fair amount of
    // overhead in the fancier hw SPI on ESP32 which is designed to send more than
    // one byte at the time, and only ends up sending one byte when called from an
    // arduino library.
    // Sadly, using software SPI in the adafruit library would prevent SPI from working
    // in the touch screen code which only supports hardware SPI
    // The TFT code runs at 24Mhz as per below, but testing shows that any speed over 2Mhz
    // seems ignored and taken down to 2Mhz
    //SPI.beginTransaction(SPISettings(24000000, MSBFIRST, SPI_MODE0));

    // Talking to the touch screen can only work at 2Mhz, and both drivers change the SPI
    // speed before sending data, so this works transparently.

    // ESP32 requires an extended begin with pin mappings (which is not supported by the
    // adafruit library), so we do an explicit begin here and then the other SPI libraries work
    // with hardware SPI as setup here (they will do a second begin without pin mappings and
    // that will be ignored).
    SPI.begin(SPI_CLK, MISO, MOSI);

    // Until further notice, there is a hack to get HW SPI be as fast as SW SPI:
    // in espressif/esp32/cores/esp32/esp32-hal.h after the first define, add
    // #define CONFIG_DISABLE_HAL_LOCKS 1
    // Use with caution, this may cause unknown issues

    Wire.begin();
    // LCD backlight is inverted logic,
    // This turns the backlight off
    // i2cexp_set_bits(I2CEXP_LCD_BL_CTR);
    // And this turns it on
    i2cexp_clear_bits(I2CEXP_LCD_BL_CTR);
    // Note this also initializes the read bits on PCF8574 by setting them to 1 as per I2CEXP_IMASK

    Serial.println("ILI9341 Test!"); 

    tft.begin();
    // read diagnostics (optional but can help debug problems)
    uint8_t x = tft.readcommand8(ILI9341_RDMODE);
    Serial.print("Display Power Mode: 0x"); Serial.println(x, HEX);
    x = tft.readcommand8(ILI9341_RDMADCTL);
    Serial.print("MADCTL Mode: 0x"); Serial.println(x, HEX);
    x = tft.readcommand8(ILI9341_RDPIXFMT);
    Serial.print("Pixel Format: 0x"); Serial.println(x, HEX);
    x = tft.readcommand8(ILI9341_RDIMGFMT);
    Serial.print("Image Format: 0x"); Serial.println(x, HEX);
    x = tft.readcommand8(ILI9341_RDSELFDIAG);
    Serial.print("Self Diagnostic: 0x"); Serial.println(x, HEX); 
    tft.setRotation(3);
    tftw = tft.width(), tfth = tft.height();
    boxw = tftw/NHORIZ, boxh = tfth/NVERT;
    Serial.print("Resolution: "); Serial.print(tftw); 
    Serial.print(" x "); Serial.println(tfth);
    Serial.print("Selection Box Size: "); Serial.print(boxw); 
    Serial.print(" x "); Serial.println(boxh);
    Serial.println(F("Done!"));

    // Tri-color APA106 LEDs Setup (not working right now)
    pixels.begin();
    pixels.setPixelColor(0, 255, 0, 0);
    pixels.setPixelColor(1, 0, 255, 0);
    pixels.show();

    // ADXL345
    if(!accel.begin()) {
	/* There was a problem detecting the ADXL345 ... check your connections */
	Serial.println("Ooops, no ADXL345 detected ... Check your wiring!");
	while(1);
    }
    accel.setRange(ADXL345_RANGE_16_G);
    displaySensorDetails();

}

// vim:sts=4:sw=4
