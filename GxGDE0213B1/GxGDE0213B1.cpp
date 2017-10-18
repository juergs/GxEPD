/************************************************************************************
   class GxGDE0213B1 : Display class example for GDE0213B1 e-Paper from Dalian Good Display Co., Ltd.: www.good-display.com

   based on Demo Example from Good Display, available here: http://www.good-display.com/download_detail/downloadsId=515.html

   Author : J-M Zingg

   Version : 2.2

   Support: limited, provided as example, no claim to be fit for serious use

   Controller: IL3895 : http://www.good-display.com/download_detail/downloadsId=538.html

   connection to the e-Paper display is through DESTM32-S2 connection board, available from Good Display

   DESTM32-S2 pinout (top, component side view):
         |-------------------------------------------------
         |  VCC  |o o| VCC 5V  not needed
         |  GND  |o o| GND
         |  3.3  |o o| 3.3     3.3V
         |  nc   |o o| nc
         |  nc   |o o| nc
         |  nc   |o o| nc
   MOSI  |  DIN  |o o| CLK     SCK
   SS    |  CS   |o o| DC      e.g. D3
   D4    |  RST  |o o| BUSY    e.g. D2
         |  nc   |o o| BS      GND
         |-------------------------------------------------
*/

#include "GxGDE0213B1.h"

#if defined(ESP8266) || defined(ESP32)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif

// Partial Update Delay, may have an influence on degradation
#define PU_DELAY 300

#define xPixelsPar (GxGDE0213B1_X_PIXELS-1)
#define yPixelsPar (GxGDE0213B1_Y_PIXELS-1)

const uint8_t GxGDE0213B1::LUTDefault_full[] =
{
  0x32,  // command
  0x22, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x01, 0x00, 0x00, 0x00, 0x00
};

const uint8_t GxGDE0213B1::LUTDefault_part[] =
{
  0x32,  // command
  0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x0F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t GxGDE0213B1::GDOControl[] = {0x01, yPixelsPar % 256, yPixelsPar / 256, 0x00}; //for 2.13inch
const uint8_t GxGDE0213B1::softstart[] = {0x0c, 0xd7, 0xd6, 0x9d};
const uint8_t GxGDE0213B1::VCOMVol[] = {0x2c, 0xa8};  // VCOM 7c
const uint8_t GxGDE0213B1::DummyLine[] = {0x3a, 0x1a}; // 4 dummy line per gate
const uint8_t GxGDE0213B1::Gatetime[] = {0x3b, 0x08};  // 2us per line

GxGDE0213B1::GxGDE0213B1(GxIO& io, uint8_t rst, uint8_t busy) :
  GxEPD(GxGDE0213B1_VISIBLE_WIDTH, GxGDE0213B1_HEIGHT),
  IO(io), _rst(rst), _busy(busy),
  _current_page(-1), _using_partial_mode(false)
{
}

template <typename T> static inline void
swap(T& a, T& b)
{
  T t = a;
  a = b;
  b = t;
}

void GxGDE0213B1::drawPixel(int16_t x, int16_t y, uint16_t color)
{
  if ((x < 0) || (x >= width()) || (y < 0) || (y >= height())) return;

  // check rotation, move pixel around if necessary
  switch (getRotation())
  {
    case 1:
      swap(x, y);
      x = GxGDE0213B1_VISIBLE_WIDTH - x - 1;
      break;
    case 2:
      x = GxGDE0213B1_VISIBLE_WIDTH - x - 1;
      y = GxGDE0213B1_HEIGHT - y - 1;
      break;
    case 3:
      swap(x, y);
      y = GxGDE0213B1_HEIGHT - y - 1;
      break;
  }
  // flip y for y-decrement mode
  y = GxGDE0213B1_HEIGHT - y - 1;
  uint16_t i = x / 8 + y * GxGDE0213B1_WIDTH / 8;
  if (_current_page < 1)
  {
    if (i >= sizeof(_buffer)) return;
  }
  else
  {
    y -= _current_page * GxGDE0213B1_PAGE_HEIGHT;
    if ((y < 0) || (y >= GxGDE0213B1_PAGE_HEIGHT)) return;
    i = x / 8 + y * GxGDE0213B1_WIDTH / 8;
  }

  if (!color)
    _buffer[i] = (_buffer[i] | (1 << (7 - x % 8)));
  else
    _buffer[i] = (_buffer[i] & (0xFF ^ (1 << (7 - x % 8))));
}

void GxGDE0213B1::init(void)
{
  IO.init();
  IO.setFrequency(4000000); // 4MHz : 250ns > 150ns min RD cycle
  digitalWrite(_rst, HIGH);
  pinMode(_rst, OUTPUT);
  pinMode(_busy, INPUT);
  fillScreen(GxEPD_WHITE);
  _current_page = -1;
  _using_partial_mode = false;
}

void GxGDE0213B1::fillScreen(uint16_t color)
{
  uint8_t data = (color == GxEPD_BLACK) ? 0xFF : 0x00;
  for (uint16_t x = 0; x < sizeof(_buffer); x++)
  {
    _buffer[x] = data;
  }
}

void GxGDE0213B1::update(void)
{
  if (_current_page != -1) return;
  _using_partial_mode = false;
  _Init_Full(0x01);
  _writeCommand(0x24);
  for (uint16_t y = 0; y < GxGDE0213B1_HEIGHT; y++)
  {
    for (uint16_t x = 0; x < GxGDE0213B1_WIDTH / 8; x++)
    {
      uint16_t idx = y * (GxGDE0213B1_WIDTH / 8) + x;
      uint8_t data = (idx < sizeof(_buffer)) ? _buffer[idx] : 0x00;
      _writeData(~data);
    }
  }
  _Update_Full();
  _PowerOff();
}

void  GxGDE0213B1::drawBitmap(const uint8_t *bitmap, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, int16_t mode)
{
  if (mode & bm_default) mode |= bm_flip_x | bm_invert;
  drawBitmapBM(bitmap, x, y, w, h, color, mode);
}

void GxGDE0213B1::drawBitmap(const uint8_t *bitmap, uint32_t size, int16_t mode)
{
  if (_current_page != -1) return;
  // example bitmaps are made for y-decrement, x-increment, for origin on opposite corner
  // bm_flip_x for normal display (bm_flip_y would be rotated)
  // y-increment is not available with this controller
  if (mode & bm_default) mode |= bm_flip_x;
  uint8_t ram_entry_mode = 0x01; // (always) y-decrement, x-increment for normal mode
  if ((mode & bm_flip_y) && (mode & bm_flip_x)) ram_entry_mode = 0x00; // y-decrement, x-decrement
  else if (mode & bm_flip_y) ram_entry_mode = 0x00; // (always) y-decrement, x-decrement
  //else if (mode & bm_flip_x) ram_entry_mode = 0x02; // (always) y-decrement
  if (mode & bm_partial_update)
  {
    _using_partial_mode = true; // remember
    _Init_Part(ram_entry_mode);
    _writeCommand(0x24);
    for (uint32_t i = 0; i < GxGDE0213B1_BUFFER_SIZE; i++)
    {
      uint8_t data = 0xFF; // white is 0xFF on device
      if (i < size)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[i]);
#else
        data = bitmap[i];
#endif
        if (mode & bm_invert) data = ~data;
      }
      _writeData(data);
    }
    _Update_Part();
    delay(PU_DELAY);
    _writeCommand(0x24);
    for (uint32_t i = 0; i < GxGDE0213B1_BUFFER_SIZE; i++)
    {
      uint8_t data = 0xFF; // white is 0xFF on device
      if (i < size)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[i]);
#else
        data = bitmap[i];
#endif
        if (mode & bm_invert) data = ~data;
      }
      _writeData(data);
    }
    delay(PU_DELAY);
    _PowerOff();
  }
  else
  {
    _using_partial_mode = false; // remember
    _Init_Full(ram_entry_mode);
    _writeCommand(0x24);
    for (uint32_t i = 0; i < GxGDE0213B1_BUFFER_SIZE; i++)
    {
      uint8_t data = 0xFF; // white is 0xFF on device
      if (i < size)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[i]);
#else
        data = bitmap[i];
#endif
        if (mode & bm_invert) data = ~data;
      }
      _writeData(data);
    }
    _Update_Full();
    _PowerOff();
  }
}

void GxGDE0213B1::eraseDisplay(bool using_partial_update)
{
  if (_current_page != -1) return;
  if (using_partial_update)
  {
    _using_partial_mode = true; // remember
    _Init_Part(0x01);
    _writeCommand(0x24);
    for (uint32_t i = 0; i < GxGDE0213B1_BUFFER_SIZE; i++)
    {
      _writeData(0xFF);
    }
    _Update_Part();
    delay(PU_DELAY);
    // update erase buffer
    _writeCommand(0x24);
    for (uint32_t i = 0; i < GxGDE0213B1_BUFFER_SIZE; i++)
    {
      _writeData(0xFF);
    }
    delay(PU_DELAY);
    _PowerOff();
  }
  else
  {
    _using_partial_mode = false; // remember
    _Init_Full(0x01);
    _writeCommand(0x24);
    for (uint32_t i = 0; i < GxGDE0213B1_BUFFER_SIZE; i++)
    {
      _writeData(0xFF);
    }
    _Update_Full();
    _PowerOff();
  }
}

void GxGDE0213B1::updateWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool using_rotation)
{
  if (_current_page != -1) return;
  if (using_rotation) _rotate(x, y, w, h);
  if (x >= GxGDE0213B1_WIDTH) return;
  if (y >= GxGDE0213B1_HEIGHT) return;
  uint16_t xe = min(GxGDE0213B1_WIDTH, x + w) - 1;
  uint16_t ye = min(GxGDE0213B1_HEIGHT, y + h) - 1;
  uint16_t xs_d8 = x / 8;
  uint16_t xe_d8 = xe / 8;
  uint16_t ys_bx = GxGDE0213B1_HEIGHT - ye - 1;
  uint16_t ye_bx = GxGDE0213B1_HEIGHT - y - 1;
  _Init_Part(0x01);
  _SetRamArea(xs_d8, xe_d8, ye % 256, ye / 256, y % 256, y / 256); // X-source area,Y-gate area
  _SetRamPointer(xs_d8, ye % 256, ye / 256); // set ram
  _waitWhileBusy();
  _writeCommand(0x24);
  for (int16_t y1 = ys_bx; y1 <= ye_bx; y1++)
  {
    for (int16_t x1 = xs_d8; x1 <= xe_d8; x1++)
    {
      uint16_t idx = y1 * (GxGDE0213B1_WIDTH / 8) + x1;
      uint8_t data = (idx < sizeof(_buffer)) ? _buffer[idx] : 0x00;
      _writeData(~data);
    }
  }
  _Update_Part();
  delay(PU_DELAY);
  // update erase buffer
  _SetRamArea(xs_d8, xe_d8, ye % 256, ye / 256, y % 256, y / 256); // X-source area,Y-gate area
  _SetRamPointer(xs_d8, ye % 256, ye / 256); // set ram
  _waitWhileBusy();
  _writeCommand(0x24);
  for (int16_t y1 = ys_bx; y1 <= ye_bx; y1++)
  {
    for (int16_t x1 = xs_d8; x1 <= xe_d8; x1++)
    {
      uint16_t idx = y1 * (GxGDE0213B1_WIDTH / 8) + x1;
      uint8_t data = (idx < sizeof(_buffer)) ? _buffer[idx] : 0x00;
      _writeData(~data);
    }
  }
  delay(PU_DELAY);
}

void GxGDE0213B1::updateToWindow(uint16_t xs, uint16_t ys, uint16_t xd, uint16_t yd, uint16_t w, uint16_t h, bool using_rotation)
{
  if (using_rotation)
  {
    switch (getRotation())
    {
      case 1:
        swap(xs, ys);
        swap(xd, yd);
        swap(w, h);
        xs = GxGDE0213B1_WIDTH - xs - w - 1;
        xd = GxGDE0213B1_WIDTH - xd - w - 1;
        break;
      case 2:
        xs = GxGDE0213B1_WIDTH - xs - w - 1;
        ys = GxGDE0213B1_HEIGHT - ys - h - 1;
        xd = GxGDE0213B1_WIDTH - xd - w - 1;
        yd = GxGDE0213B1_HEIGHT - yd - h - 1;
        break;
      case 3:
        swap(xs, ys);
        swap(xd, yd);
        swap(w, h);
        ys = GxGDE0213B1_HEIGHT - ys - h - 1;
        yd = GxGDE0213B1_HEIGHT - yd - h - 1;
        break;
    }
  }
  if (xs >= GxGDE0213B1_WIDTH) return;
  if (ys >= GxGDE0213B1_HEIGHT) return;
  if (xd >= GxGDE0213B1_WIDTH) return;
  if (yd >= GxGDE0213B1_HEIGHT) return;
  // flip y for y-decrement mode
  ys = GxGDE0213B1_HEIGHT - ys - h;
  // the screen limits are the hard limits
  uint16_t xde = min(GxGDE0213B1_WIDTH, xd + w) - 1;
  uint16_t yde = min(GxGDE0213B1_HEIGHT, yd + h) - 1;
  uint16_t xds_d8 = xd / 8;
  uint16_t xde_d8 = xde / 8;
  // soft limits, must send as many bytes as set by _SetRamArea
  uint16_t xse_d8 = xs / 8 + xde_d8 - xds_d8;
  uint16_t yse = ys + yde - yd;
  _Init_Part(0x01);
  _SetRamArea(xds_d8, xde_d8, yde % 256, yde / 256, yd % 256, yd / 256); // X-source area,Y-gate area
  _SetRamPointer(xds_d8, yde % 256, yde / 256); // set ram
  _waitWhileBusy();
  _writeCommand(0x24);
  for (int16_t y1 = ys; y1 <= yse; y1++)
  {
    for (int16_t x1 = xs / 8; x1 <= xse_d8; x1++)
    {
      uint16_t idx = y1 * (GxGDE0213B1_WIDTH / 8) + x1;
      uint8_t data = (idx < sizeof(_buffer)) ? _buffer[idx] : 0x00;
      _writeData(~data);
    }
  }
  _Update_Part();
  delay(PU_DELAY);
  // update erase buffer
  _SetRamArea(xds_d8, xde_d8, yde % 256, yde / 256, yd % 256, yd / 256); // X-source area,Y-gate area
  _SetRamPointer(xds_d8, yde % 256, yde / 256); // set ram
  _waitWhileBusy();
  _writeCommand(0x24);
  for (int16_t y1 = ys; y1 <= yse; y1++)
  {
    for (int16_t x1 = xs / 8; x1 <= xse_d8; x1++)
    {
      uint16_t idx = y1 * (GxGDE0213B1_WIDTH / 8) + x1;
      uint8_t data = (idx < sizeof(_buffer)) ? _buffer[idx] : 0x00;
      _writeData(~data);
    }
  }
  delay(PU_DELAY);
}

void GxGDE0213B1::powerDown()
{
  _using_partial_mode = false;
  _PowerOff();
}

void GxGDE0213B1::_writeCommand(uint8_t command)
{
  if (digitalRead(_busy))
  {
    String str = String("command 0x") + String(command, HEX);
    _waitWhileBusy(str.c_str());
  }
  IO.writeCommandTransaction(command);
}

void GxGDE0213B1::_writeData(uint8_t data)
{
  IO.writeDataTransaction(data);
}

void GxGDE0213B1::_writeCommandData(const uint8_t* pCommandData, uint8_t datalen)
{
  if (digitalRead(_busy))
  {
    String str = String("command 0x") + String(pCommandData[0], HEX);
    _waitWhileBusy(str.c_str());
  }
  IO.startTransaction();
  IO.writeCommand(*pCommandData++);
  for (uint8_t i = 0; i < datalen - 1; i++)	// sub the command
  {
    IO.writeData(*pCommandData++);
  }
  IO.endTransaction();

}

void GxGDE0213B1::_waitWhileBusy(const char* comment)
{
  unsigned long start = micros();
  while (1)
  {
    if (!digitalRead(_busy)) break;
    delay(1);
    if (micros() - start > 10000000)
    {
      Serial.println("Busy Timeout!");
      break;
    }
  }
  if (comment)
  {
    //unsigned long elapsed = micros() - start;
    //Serial.print(comment);
    //Serial.print(" : ");
    //Serial.println(elapsed);
  }
}

void GxGDE0213B1::_setRamDataEntryMode(uint8_t em)
{
  em = min(em, 0x03);
  _writeCommand(0x11);
  _writeData(em);
  switch (em)
  {
    case 0x00: // x decrease, y decrease
      _SetRamArea(xPixelsPar / 8, 0x00, yPixelsPar % 256, yPixelsPar / 256, 0x00, 0x00);  // X-source area,Y-gate area
      _SetRamPointer(xPixelsPar / 8, yPixelsPar % 256, yPixelsPar / 256); // set ram
      break;
    case 0x01: // x increase, y decrease : as in demo code
      _SetRamArea(0x00, xPixelsPar / 8, yPixelsPar % 256, yPixelsPar / 256, 0x00, 0x00);  // X-source area,Y-gate area
      _SetRamPointer(0x00, yPixelsPar % 256, yPixelsPar / 256); // set ram
      break;
    case 0x02: // x decrease, y increase
      _SetRamArea(xPixelsPar / 8, 0x00, 0x00, 0x00, yPixelsPar % 256, yPixelsPar / 256);  // X-source area,Y-gate area
      _SetRamPointer(xPixelsPar / 8, 0x00, 0x00); // set ram
      break;
    case 0x03: // x increase, y increase : normal mode
      _SetRamArea(0x00, xPixelsPar / 8, 0x00, 0x00, yPixelsPar % 256, yPixelsPar / 256);  // X-source area,Y-gate area
      _SetRamPointer(0x00, 0x00, 0x00); // set ram
      break;
  }
}

void GxGDE0213B1::_SetRamArea(uint8_t Xstart, uint8_t Xend, uint8_t Ystart, uint8_t Ystart1, uint8_t Yend, uint8_t Yend1)
{
  _writeCommand(0x44);
  _writeData(Xstart);
  _writeData(Xend);
  _writeCommand(0x45);
  _writeData(Ystart);
  _writeData(Ystart1);
  _writeData(Yend);
  _writeData(Yend1);
}

void GxGDE0213B1::_SetRamPointer(uint8_t addrX, uint8_t addrY, uint8_t addrY1)
{
  _writeCommand(0x4e);
  _writeData(addrX);
  _writeCommand(0x4f);
  _writeData(addrY);
  _writeData(addrY1);
}

void GxGDE0213B1::_PowerOn(void)
{
  _writeCommand(0x22);
  _writeData(0xc0);
  _writeCommand(0x20);
  _waitWhileBusy("_PowerOn");
}

void GxGDE0213B1::_PowerOff(void)
{
  _writeCommand(0x22);
  _writeData(0xc3);
  _writeCommand(0x20);
  _waitWhileBusy("_PowerOff");
}

void GxGDE0213B1::_InitDisplay(uint8_t em)
{
  _writeCommandData(GDOControl, sizeof(GDOControl));  // Pannel configuration, Gate selection
  _writeCommandData(softstart, sizeof(softstart));  // X decrease, Y decrease
  _writeCommandData(VCOMVol, sizeof(VCOMVol));    // VCOM setting
  _writeCommandData(DummyLine, sizeof(DummyLine));  // dummy line per gate
  _writeCommandData(Gatetime, sizeof(Gatetime));    // Gate time setting
  _setRamDataEntryMode(em);
}

void GxGDE0213B1::_Init_Full(uint8_t em)
{
  _InitDisplay(em);
  _writeCommandData(LUTDefault_full, sizeof(LUTDefault_full));
  _PowerOn();
}

void GxGDE0213B1::_Init_Part(uint8_t em)
{
  _InitDisplay(em);
  _writeCommandData(LUTDefault_part, sizeof(LUTDefault_part));
  _PowerOn();
}

void GxGDE0213B1::_Update_Full(void)
{
  _writeCommand(0x22);
  _writeData(0xc4);
  _writeCommand(0x20);
  _waitWhileBusy("_Update_Full");
  _writeCommand(0xff);
}

void GxGDE0213B1::_Update_Part(void)
{
  _writeCommand(0x22);
  _writeData(0x04);
  _writeCommand(0x20);
  _waitWhileBusy("_Update_Part");
  _writeCommand(0xff);
}

void GxGDE0213B1::drawPaged(void (*drawCallback)(void))
{
  if (_current_page != -1) return;
  _using_partial_mode = false;
  _Init_Full(0x01);
  _writeCommand(0x24);
  for (_current_page = 0; _current_page < GxGDE0213B1_PAGES; _current_page++)
  {
    fillScreen(GxEPD_WHITE);
    drawCallback();
    for (int16_t y1 = 0; y1 < GxGDE0213B1_PAGE_HEIGHT; y1++)
    {
      for (int16_t x1 = 0; x1 < GxGDE0213B1_WIDTH / 8; x1++)
      {
        uint16_t idx = y1 * (GxGDE0213B1_WIDTH / 8) + x1;
        uint8_t data = (idx < sizeof(_buffer)) ? _buffer[idx] : 0x00;
        _writeData(~data);
      }
    }
  }
  _current_page = -1;
  _Update_Full();
  _PowerOff();
}

void GxGDE0213B1::drawPaged(void (*drawCallback)(uint32_t), uint32_t p)
{
  if (_current_page != -1) return;
  _using_partial_mode = false;
  _Init_Full(0x01);
  _writeCommand(0x24);
  for (_current_page = 0; _current_page < GxGDE0213B1_PAGES; _current_page++)
  {
    fillScreen(GxEPD_WHITE);
    drawCallback(p);
    for (int16_t y1 = 0; y1 < GxGDE0213B1_PAGE_HEIGHT; y1++)
    {
      for (int16_t x1 = 0; x1 < GxGDE0213B1_WIDTH / 8; x1++)
      {
        uint16_t idx = y1 * (GxGDE0213B1_WIDTH / 8) + x1;
        uint8_t data = (idx < sizeof(_buffer)) ? _buffer[idx] : 0x00;
        _writeData(~data);
      }
    }
  }
  _current_page = -1;
  _Update_Full();
  _PowerOff();
}

void GxGDE0213B1::drawPaged(void (*drawCallback)(const void*), const void* p)
{
  if (_current_page != -1) return;
  _using_partial_mode = false;
  _Init_Full(0x01);
  _writeCommand(0x24);
  for (_current_page = 0; _current_page < GxGDE0213B1_PAGES; _current_page++)
  {
    fillScreen(GxEPD_WHITE);
    drawCallback(p);
    for (int16_t y1 = 0; y1 < GxGDE0213B1_PAGE_HEIGHT; y1++)
    {
      for (int16_t x1 = 0; x1 < GxGDE0213B1_WIDTH / 8; x1++)
      {
        uint16_t idx = y1 * (GxGDE0213B1_WIDTH / 8) + x1;
        uint8_t data = (idx < sizeof(_buffer)) ? _buffer[idx] : 0x00;
        _writeData(~data);
      }
    }
  }
  _current_page = -1;
  _Update_Full();
  _PowerOff();
}

void GxGDE0213B1::drawPaged(void (*drawCallback)(const void*, const void*), const void* p1, const void* p2)
{
  if (_current_page != -1) return;
  _using_partial_mode = false;
  _Init_Full(0x01);
  _writeCommand(0x24);
  for (_current_page = 0; _current_page < GxGDE0213B1_PAGES; _current_page++)
  {
    fillScreen(GxEPD_WHITE);
    drawCallback(p1, p2);
    for (int16_t y1 = 0; y1 < GxGDE0213B1_PAGE_HEIGHT; y1++)
    {
      for (int16_t x1 = 0; x1 < GxGDE0213B1_WIDTH / 8; x1++)
      {
        uint16_t idx = y1 * (GxGDE0213B1_WIDTH / 8) + x1;
        uint8_t data = (idx < sizeof(_buffer)) ? _buffer[idx] : 0x00;
        _writeData(~data);
      }
    }
  }
  _current_page = -1;
  _Update_Full();
  _PowerOff();
}

void GxGDE0213B1::_rotate(uint16_t& x, uint16_t& y, uint16_t& w, uint16_t& h)
{
  switch (getRotation())
  {
    case 1:
      swap(x, y);
      swap(w, h);
      x = GxGDE0213B1_WIDTH - x - w - 1;
      break;
    case 2:
      x = GxGDE0213B1_WIDTH - x - w - 1;
      y = GxGDE0213B1_HEIGHT - y - h - 1;
      break;
    case 3:
      swap(x, y);
      swap(w, h);
      y = GxGDE0213B1_HEIGHT - y - h - 1;
      break;
  }
}

void GxGDE0213B1::drawPagedToWindow(void (*drawCallback)(void), uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  if (_current_page != -1) return;
  _rotate(x, y, w, h);
  if (!_using_partial_mode)
  {
    eraseDisplay(false);
    eraseDisplay(true);
  }
  _using_partial_mode = true;
  for (_current_page = GxGDE0213B1_PAGES - 1; _current_page >= 0; _current_page--)
  {
    // flip y for y-decrement mode
    uint16_t yds = (GxGDE0213B1_PAGES - _current_page - 1) * GxGDE0213B1_PAGE_HEIGHT;
    uint16_t yde = yds + GxGDE0213B1_PAGE_HEIGHT;
    yds = max(y, yds);
    yde = min(y + h, yde);
    if (yde > yds)
    {
      fillScreen(GxEPD_WHITE);
      drawCallback();
      uint16_t ys = (GxGDE0213B1_PAGES - 1) * GxGDE0213B1_PAGE_HEIGHT + (yds % GxGDE0213B1_PAGE_HEIGHT);
      updateToWindow(x, ys, x, yds, w, yde - yds + 1, false);
    }
  }
  _current_page = -1;
  _PowerOff();
}

void GxGDE0213B1::drawPagedToWindow(void (*drawCallback)(uint32_t), uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t p)
{
  if (_current_page != -1) return;
  _rotate(x, y, w, h);
  if (!_using_partial_mode)
  {
    eraseDisplay(false);
    eraseDisplay(true);
  }
  _using_partial_mode = true;
  for (_current_page = GxGDE0213B1_PAGES - 1; _current_page >= 0; _current_page--)
  {
    // flip y for y-decrement mode
    uint16_t yds = (GxGDE0213B1_PAGES - _current_page - 1) * GxGDE0213B1_PAGE_HEIGHT;
    uint16_t yde = yds + GxGDE0213B1_PAGE_HEIGHT;
    yds = max(y, yds);
    yde = min(y + h, yde);
    if (yde > yds)
    {
      fillScreen(GxEPD_WHITE);
      drawCallback(p);
      uint16_t ys = (GxGDE0213B1_PAGES - 1) * GxGDE0213B1_PAGE_HEIGHT + (yds % GxGDE0213B1_PAGE_HEIGHT);
      updateToWindow(x, ys, x, yds, w, yde - yds, false);
    }
  }
  _current_page = -1;
  _PowerOff();
}

void GxGDE0213B1::drawPagedToWindow(void (*drawCallback)(const void*), uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void* p)
{
  if (_current_page != -1) return;
  _rotate(x, y, w, h);
  if (!_using_partial_mode)
  {
    eraseDisplay(false);
    eraseDisplay(true);
  }
  _using_partial_mode = true;
  for (_current_page = GxGDE0213B1_PAGES - 1; _current_page >= 0; _current_page--)
  {
    // flip y for y-decrement mode
    uint16_t yds = (GxGDE0213B1_PAGES - _current_page - 1) * GxGDE0213B1_PAGE_HEIGHT;
    uint16_t yde = yds + GxGDE0213B1_PAGE_HEIGHT;
    yds = max(y, yds);
    yde = min(y + h, yde);
    if (yde > yds)
    {
      fillScreen(GxEPD_WHITE);
      drawCallback(p);
      uint16_t ys = (GxGDE0213B1_PAGES - 1) * GxGDE0213B1_PAGE_HEIGHT + (yds % GxGDE0213B1_PAGE_HEIGHT);
      updateToWindow(x, ys, x, yds, w, yde - yds, false);
    }
  }
  _current_page = -1;
  _PowerOff();
}

void GxGDE0213B1::drawPagedToWindow(void (*drawCallback)(const void*, const void*), uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void* p1, const void* p2)
{
  if (_current_page != -1) return;
  _rotate(x, y, w, h);
  if (!_using_partial_mode)
  {
    eraseDisplay(false);
    eraseDisplay(true);
  }
  _using_partial_mode = true;
  for (_current_page = GxGDE0213B1_PAGES - 1; _current_page >= 0; _current_page--)
  {
    // flip y for y-decrement mode
    uint16_t yds = (GxGDE0213B1_PAGES - _current_page - 1) * GxGDE0213B1_PAGE_HEIGHT;
    uint16_t yde = yds + GxGDE0213B1_PAGE_HEIGHT;
    yds = max(y, yds);
    yde = min(y + h, yde);
    if (yde > yds)
    {
      fillScreen(GxEPD_WHITE);
      drawCallback(p1, p2);
      uint16_t ys = (GxGDE0213B1_PAGES - 1) * GxGDE0213B1_PAGE_HEIGHT + (yds % GxGDE0213B1_PAGE_HEIGHT);
      updateToWindow(x, ys, x, yds, w, yde - yds, false);
    }
  }
  _current_page = -1;
  _PowerOff();
}

void GxGDE0213B1::drawCornerTest(uint8_t em)
{
  if (_current_page != -1) return;
  _using_partial_mode = false;
  _Init_Full(em);
  _writeCommand(0x24);
  for (uint32_t y = 0; y < GxGDE0213B1_HEIGHT; y++)
  {
    for (uint32_t x = 0; x < GxGDE0213B1_WIDTH / 8; x++)
    {
      uint8_t data = 0xFF;
      if ((x < 1) && (y < 8)) data = 0x00;
      if ((x > GxGDE0213B1_WIDTH / 8 - 3) && (y < 16)) data = 0x00;
      if ((x > GxGDE0213B1_WIDTH / 8 - 4) && (y > GxGDE0213B1_HEIGHT - 25)) data = 0x00;
      if ((x < 4) && (y > GxGDE0213B1_HEIGHT - 33)) data = 0x00;
      _writeData(data);
    }
  }
  _Update_Full();
  _PowerOff();
}

