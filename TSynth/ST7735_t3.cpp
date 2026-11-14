/*************************************************** 
  This is a library for the Adafruit 1.8" SPI display.
  This library works with the Adafruit 1.8" TFT Breakout w/SD card
  ----> http://www.adafruit.com/products/358
  as well as Adafruit raw 1.8" TFT display
  ----> http://www.adafruit.com/products/618
 
  Check out the links above for our tutorials and wiring diagrams
  These displays use SPI to communicate, 4 or 5 pins are required to
  interface (RST is optional)
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/

#include "ST7735_t3.h"
#include <limits.h>
#include "pins_arduino.h"
#include "wiring_private.h"
#include <SPI.h>

#ifdef ENABLE_ST77XX_FRAMEBUFFER
//#define DEBUG_ASYNC_UPDATE
//#define DEBUG_ASYNC_LEDS
#ifdef DEBUG_ASYNC_LEDS
  #define DEBUG_PIN_1 0
  #define DEBUG_PIN_2 1
  #define DEBUG_PIN_3 2
#endif

volatile short _dma_dummy_rx;

ST7735_t3 *ST7735_t3::_dmaActiveDisplay[3] = {0, 0, 0};

#if defined(__IMXRT1062__)  // Teensy 4.x
// On T4 Setup the buffers to be used one per SPI buss... 
// This way we make sure it is hopefully in uncached memory
ST7735DMA_Data ST7735_t3::_dma_data[3];   // one structure for each SPI buss... 
#endif

#endif

// Constructor when using software SPI.  All output pins are configurable.
ST7735_t3::ST7735_t3(uint8_t cs, uint8_t rs, uint8_t sid, uint8_t sclk, uint8_t rst) :
   Adafruit_GFX(ST7735_TFTWIDTH, ST7735_TFTHEIGHT_160), hwSPI(false),
  _cs(cs), _rs(rs),  _rst(rst), _sid(sid), _sclk(sclk)
{
  #ifdef ENABLE_ST77XX_FRAMEBUFFER
    _pfbtft = NULL; 
    _use_fbtft = 0;           // Are we in frame buffer mode?
  _we_allocated_buffer = NULL;
  _dma_state = 0;
    #endif
  _screenHeight = ST7735_TFTHEIGHT_160;
  _screenWidth = ST7735_TFTWIDTH; 
}

/***************************************************************/
/*     Teensy 4.                                               */
/***************************************************************/
inline void ST7735_t3::spiwrite(uint8_t c)
{
//Serial.println(c, HEX);
  if (_pspi) {
    _pspi->transfer(c);
  } else {
    // Fast SPI bitbang swiped from LPD8806 library
    for(uint8_t bit = 0x80; bit; bit >>= 1) {
      if(c & bit) DIRECT_WRITE_HIGH(_mosiport, _mosipinmask);
      else        DIRECT_WRITE_LOW(_mosiport, _mosipinmask);
      DIRECT_WRITE_HIGH(_sckport, _sckpinmask);
      asm("nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop;");
      DIRECT_WRITE_LOW(_sckport, _sckpinmask);
    }
  }
}

void ST7735_t3::writecommand(uint8_t c)
{
  if (hwSPI) {
    maybeUpdateTCR(LPSPI_TCR_PCS(0) | LPSPI_TCR_FRAMESZ(7) /*| LPSPI_TCR_CONT*/);
    _pimxrt_spi->TDR = c;
    _pending_rx_count++;  //
    waitFifoNotFull();
  } else {
    DIRECT_WRITE_LOW(_dcport, _dcpinmask);
    spiwrite(c);
  }
}

void ST7735_t3::writecommand_last(uint8_t c)
{
  if (hwSPI) {
    maybeUpdateTCR(LPSPI_TCR_PCS(0) | LPSPI_TCR_FRAMESZ(7));
    _pimxrt_spi->TDR = c;
    _pending_rx_count++;  //
    waitTransmitComplete();
  } else {
    DIRECT_WRITE_LOW(_dcport, _dcpinmask);
    spiwrite(c);
  }

}

void ST7735_t3::writedata(uint8_t c)
{
  if (hwSPI) {
    maybeUpdateTCR(LPSPI_TCR_PCS(1) | LPSPI_TCR_FRAMESZ(7));
    _pimxrt_spi->TDR = c;
    _pending_rx_count++;  //
    waitTransmitComplete();
  } else {
    DIRECT_WRITE_HIGH(_dcport, _dcpinmask);
    spiwrite(c);
  }
} 

void ST7735_t3::writedata_last(uint8_t c)
{
  if (hwSPI) {
    maybeUpdateTCR(LPSPI_TCR_PCS(1) | LPSPI_TCR_FRAMESZ(7));
    _pimxrt_spi->TDR = c;
    _pending_rx_count++;  //
    waitTransmitComplete();
  } else {
    DIRECT_WRITE_HIGH(_dcport, _dcpinmask);
    spiwrite(c);
  }
} 


void ST7735_t3::writedata16(uint16_t d)
{
  if (hwSPI) {
    maybeUpdateTCR(LPSPI_TCR_PCS(1) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_CONT);
    _pimxrt_spi->TDR = d;
    _pending_rx_count++;  //
    waitFifoNotFull();
  } else {
    DIRECT_WRITE_HIGH(_dcport, _dcpinmask);
    spiwrite(d >> 8);
    spiwrite(d);
  }
} 

void ST7735_t3::writedata16_last(uint16_t d)
{
  if (hwSPI) {
    maybeUpdateTCR(LPSPI_TCR_PCS(1) | LPSPI_TCR_FRAMESZ(15));
    _pimxrt_spi->TDR = d;
//    _pimxrt_spi->SR = LPSPI_SR_WCF | LPSPI_SR_FCF | LPSPI_SR_TCF;
    _pending_rx_count++;  //
    waitTransmitComplete();
  } else {
    DIRECT_WRITE_HIGH(_dcport, _dcpinmask);
    spiwrite(d >> 8);
    spiwrite(d);
  }
} 

void ST7735_t3::setBitrate(uint32_t n)
{
  if (n >= 8000000) {
    SPI.setClockDivider(SPI_CLOCK_DIV2);
  } else if (n >= 4000000) {
    SPI.setClockDivider(SPI_CLOCK_DIV4);
  } else if (n >= 2000000) {
    SPI.setClockDivider(SPI_CLOCK_DIV8);
  } else {
    SPI.setClockDivider(SPI_CLOCK_DIV16);
  }
}


// Rather than a bazillion writecommand() and writedata() calls, screen
// initialization commands and arguments are organized in these tables
// stored in PROGMEM.  The table may look bulky, but that's mostly the
// formatting -- storage-wise this is hundreds of bytes more compact
// than the equivalent code.  Companion function follows.
#define DELAY 0x80
static const uint8_t PROGMEM
  Bcmd[] = {                  // Initialization commands for 7735B screens
    18,                       // 18 commands in list:
    ST7735_SWRESET,   DELAY,  //  1: Software reset, no args, w/delay
      50,                     //     50 ms delay
    ST7735_SLPOUT ,   DELAY,  //  2: Out of sleep mode, no args, w/delay
      255,                    //     255 = 500 ms delay
    ST7735_COLMOD , 1+DELAY,  //  3: Set color mode, 1 arg + delay:
      0x05,                   //     16-bit color
      10,                     //     10 ms delay
    ST7735_FRMCTR1, 3+DELAY,  //  4: Frame rate control, 3 args + delay:
      0x00,                   //     fastest refresh
      0x06,                   //     6 lines front porch
      0x03,                   //     3 lines back porch
      10,                     //     10 ms delay
    ST7735_MADCTL , 1      ,  //  5: Memory access ctrl (directions), 1 arg:
      0x08,                   //     Row addr/col addr, bottom to top refresh
    ST7735_DISSET5, 2      ,  //  6: Display settings #5, 2 args, no delay:
      0x15,                   //     1 clk cycle nonoverlap, 2 cycle gate
                              //     rise, 3 cycle osc equalize
      0x02,                   //     Fix on VTL
    ST7735_INVCTR , 1      ,  //  7: Display inversion control, 1 arg:
      0x0,                    //     Line inversion
    ST7735_PWCTR1 , 2+DELAY,  //  8: Power control, 2 args + delay:
      0x02,                   //     GVDD = 4.7V
      0x70,                   //     1.0uA
      10,                     //     10 ms delay
    ST7735_PWCTR2 , 1      ,  //  9: Power control, 1 arg, no delay:
      0x05,                   //     VGH = 14.7V, VGL = -7.35V
    ST7735_PWCTR3 , 2      ,  // 10: Power control, 2 args, no delay:
      0x01,                   //     Opamp current small
      0x02,                   //     Boost frequency
    ST7735_VMCTR1 , 2+DELAY,  // 11: Power control, 2 args + delay:
      0x3C,                   //     VCOMH = 4V
      0x38,                   //     VCOML = -1.1V
      10,                     //     10 ms delay
    ST7735_PWCTR6 , 2      ,  // 12: Power control, 2 args, no delay:
      0x11, 0x15,
    ST7735_GMCTRP1,16      ,  // 13: Magical unicorn dust, 16 args, no delay:
      0x09, 0x16, 0x09, 0x20, //     (seriously though, not sure what
      0x21, 0x1B, 0x13, 0x19, //      these config values represent)
      0x17, 0x15, 0x1E, 0x2B,
      0x04, 0x05, 0x02, 0x0E,
    ST7735_GMCTRN1,16+DELAY,  // 14: Sparkles and rainbows, 16 args + delay:
      0x0B, 0x14, 0x08, 0x1E, //     (ditto)
      0x22, 0x1D, 0x18, 0x1E,
      0x1B, 0x1A, 0x24, 0x2B,
      0x06, 0x06, 0x02, 0x0F,
      10,                     //     10 ms delay
    ST7735_CASET  , 4      ,  // 15: Column addr set, 4 args, no delay:
      0x00, 0x02,             //     XSTART = 2
      0x00, 0x81,             //     XEND = 129
    ST7735_RASET  , 4      ,  // 16: Row addr set, 4 args, no delay:
      0x00, 0x02,             //     XSTART = 1
      0x00, 0x81,             //     XEND = 160
    ST7735_NORON  ,   DELAY,  // 17: Normal display on, no args, w/delay
      10,                     //     10 ms delay
    ST7735_DISPON ,   DELAY,  // 18: Main screen turn on, no args, w/delay
      255 },                  //     255 = 500 ms delay

  Rcmd1[] = {                 // Init for 7735R, part 1 (red or green tab)
    15,                       // 15 commands in list:
    ST7735_SWRESET,   DELAY,  //  1: Software reset, 0 args, w/delay
      150,                    //     150 ms delay
    ST7735_SLPOUT ,   DELAY,  //  2: Out of sleep mode, 0 args, w/delay
      255,                    //     500 ms delay
    ST7735_FRMCTR1, 3      ,  //  3: Frame rate ctrl - normal mode, 3 args:
      0x01, 0x2C, 0x2D,       //     Rate = fosc/(1x2+40) * (LINE+2C+2D)
    ST7735_FRMCTR2, 3      ,  //  4: Frame rate control - idle mode, 3 args:
      0x01, 0x2C, 0x2D,       //     Rate = fosc/(1x2+40) * (LINE+2C+2D)
    ST7735_FRMCTR3, 6      ,  //  5: Frame rate ctrl - partial mode, 6 args:
      0x01, 0x2C, 0x2D,       //     Dot inversion mode
      0x01, 0x2C, 0x2D,       //     Line inversion mode
    ST7735_INVCTR , 1      ,  //  6: Display inversion ctrl, 1 arg, no delay:
      0x07,                   //     No inversion
    ST7735_PWCTR1 , 3      ,  //  7: Power control, 3 args, no delay:
      0xA2,
      0x02,                   //     -4.6V
      0x84,                   //     AUTO mode
    ST7735_PWCTR2 , 1      ,  //  8: Power control, 1 arg, no delay:
      0xC5,                   //     VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD
    ST7735_PWCTR3 , 2      ,  //  9: Power control, 2 args, no delay:
      0x0A,                   //     Opamp current small
      0x00,                   //     Boost frequency
    ST7735_PWCTR4 , 2      ,  // 10: Power control, 2 args, no delay:
      0x8A,                   //     BCLK/2, Opamp current small & Medium low
      0x2A,  
    ST7735_PWCTR5 , 2      ,  // 11: Power control, 2 args, no delay:
      0x8A, 0xEE,
    ST7735_VMCTR1 , 1      ,  // 12: Power control, 1 arg, no delay:
      0x0E,
    ST7735_INVOFF , 0      ,  // 13: Don't invert display, no args, no delay
    ST7735_MADCTL , 1      ,  // 14: Memory access control (directions), 1 arg:
      0xC8,                   //     row addr/col addr, bottom to top refresh
    ST7735_COLMOD , 1      ,  // 15: set color mode, 1 arg, no delay:
      0x05 },                 //     16-bit color

  Rcmd2green[] = {            // Init for 7735R, part 2 (green tab only)
    2,                        //  2 commands in list:
    ST7735_CASET  , 4      ,  //  1: Column addr set, 4 args, no delay:
      0x00, 0x02,             //     XSTART = 0
      0x00, 0x7F+0x02,        //     XEND = 127
    ST7735_RASET  , 4      ,  //  2: Row addr set, 4 args, no delay:
      0x00, 0x01,             //     XSTART = 0
      0x00, 0x9F+0x01 },      //     XEND = 159
  Rcmd2red[] = {              // Init for 7735R, part 2 (red tab only)
    2,                        //  2 commands in list:
    ST7735_CASET  , 4      ,  //  1: Column addr set, 4 args, no delay:
      0x00, 0x00,             //     XSTART = 0
      0x00, 0x7F,             //     XEND = 127
    ST7735_RASET  , 4      ,  //  2: Row addr set, 4 args, no delay:
      0x00, 0x00,             //     XSTART = 0
      0x00, 0x9F },           //     XEND = 159

  Rcmd2green144[] = {         // Init for 7735R, part 2 (green 1.44 tab)
    2,                        //  2 commands in list:
    ST7735_CASET  , 4      ,  //  1: Column addr set, 4 args, no delay:
      0x00, 0x00,             //     XSTART = 0
      0x00, 0x7F,             //     XEND = 127
    ST7735_RASET  , 4      ,  //  2: Row addr set, 4 args, no delay:
      0x00, 0x00,             //     XSTART = 0
      0x00, 0x7F },           //     XEND = 127

  Rcmd2green160x80[] = {            // 7735R init, part 2 (mini 160x80)
    2,                              //  2 commands in list:
    ST7735_CASET,   4,              //  1: Column addr set, 4 args, no delay:
      0x00, 0x00,                   //     XSTART = 0
      0x00, 0x4F,                   //     XEND = 79
    ST7735_RASET,   4,              //  2: Row addr set, 4 args, no delay:
      0x00, 0x00,                   //     XSTART = 0
      0x00, 0x9F },                 //     XEND = 159

  Rcmd3[] = {                 // Init for 7735R, part 3 (red or green tab)
    4,                        //  4 commands in list:
    ST7735_GMCTRP1, 16      , //  1: Magical unicorn dust, 16 args, no delay:
      0x02, 0x1c, 0x07, 0x12,
      0x37, 0x32, 0x29, 0x2d,
      0x29, 0x25, 0x2B, 0x39,
      0x00, 0x01, 0x03, 0x10,
    ST7735_GMCTRN1, 16      , //  2: Sparkles and rainbows, 16 args, no delay:
      0x03, 0x1d, 0x07, 0x06,
      0x2E, 0x2C, 0x29, 0x2D,
      0x2E, 0x2E, 0x37, 0x3F,
      0x00, 0x00, 0x02, 0x10,
    ST7735_NORON  ,    DELAY, //  3: Normal display on, no args, w/delay
      10,                     //     10 ms delay
    ST7735_DISPON ,    DELAY, //  4: Main screen turn on, no args w/delay
      100 };                  //     100 ms delay


// Companion code to the above tables.  Reads and issues
// a series of LCD commands stored in PROGMEM byte array.
void ST7735_t3::commandList(const uint8_t *addr)
{
  uint8_t  numCommands, numArgs;
  uint16_t ms;

  beginSPITransaction();
  numCommands = pgm_read_byte(addr++);    // Number of commands to follow
  //Serial.printf("CommandList: numCmds:%d\n", numCommands); Serial.flush();
  while(numCommands--) {        // For each command...
    writecommand_last(pgm_read_byte(addr++)); //   Read, issue command
    numArgs  = pgm_read_byte(addr++); //   Number of args to follow
    ms       = numArgs & DELAY;   //   If hibit set, delay follows args
    numArgs &= ~DELAY;      //   Mask out delay bit
    while(numArgs > 1) {      //   For each argument...
      writedata(pgm_read_byte(addr++)); //   Read, issue argument
      numArgs--;
    }

    if (numArgs) writedata_last(pgm_read_byte(addr++)); //   Read, issue argument - wait until this one completes
    if(ms) {
      ms = pgm_read_byte(addr++); // Read post-command delay time (ms)
      if(ms == 255) ms = 500;   // If 255, delay for 500 ms
      //Serial.printf("delay %d\n", ms); Serial.flush();
      endSPITransaction();
      delay(ms);
      beginSPITransaction();
    }
  }
  endSPITransaction();
}

// Initialization code common to both 'B' and 'R' type displays
void ST7735_t3::commonInit(const uint8_t *cmdList, uint8_t mode)
{
  _colstart  = _rowstart = 0; // May be overridden in init func
    _ystart = _xstart = 0;

  if (_sid == (uint8_t)-1) _sid = 11;
  if (_sclk == (uint8_t)-1) _sclk = 13;
  if (SPI.pinIsMOSI(_sid) && SPI.pinIsSCK(_sclk)) {
    _pspi = &SPI;
    _spi_num = 0;          // Which buss is this spi on? 
    _pimxrt_spi = &IMXRT_LPSPI4_S;  // Could hack our way to grab this from SPI object, but...

  } else if (SPI1.pinIsMOSI(_sid) && SPI1.pinIsSCK(_sclk)) {
    _pspi = &SPI1;
    _spi_num = 1;          // Which buss is this spi on? 
    _pimxrt_spi = &IMXRT_LPSPI3_S;
  } else if (SPI2.pinIsMOSI(_sid) && SPI2.pinIsSCK(_sclk)) {
    _pspi = &SPI2;
    _spi_num = 2;          // Which buss is this spi on? 
    _pimxrt_spi = &IMXRT_LPSPI1_S;
  } else _pspi = nullptr;

  if (_pspi) {
    hwSPI = true;
    _pspi->begin();
    _pending_rx_count = 0;
    _spiSettings = SPISettings(ST7735_SPICLOCK, MSBFIRST, mode);
    _pspi->beginTransaction(_spiSettings); // Should have our settings. 
    _pspi->transfer(0); // hack to see if it will actually change then...
    _pspi->endTransaction();
    _spi_tcr_current = _pimxrt_spi->TCR; // get the current TCR value 
//    uint32_t *phack = (uint32_t* )&_spiSettings;
//    Serial.printf("SPI Settings: TCR: %x %x (%x %x)\n", _spi_tcr_current, _pimxrt_spi->TCR, phack[0], phack[1]);
    // Hack to get hold of the SPI Hardware information... 
    uint32_t *pa = (uint32_t*)((void*)_pspi);
    _spi_hardware = (SPIClass::SPI_Hardware_t*)(void*)pa[1];
  
  } else {
    hwSPI = false;
    _sckport = portOutputRegister(_sclk);
    _sckpinmask = digitalPinToBitMask(_sclk);
    pinMode(_sclk, OUTPUT); 
    DIRECT_WRITE_LOW(_sckport, _sckpinmask);

    _mosiport = portOutputRegister(_sid);
    _mosipinmask = digitalPinToBitMask(_sid);
    pinMode(_sid, OUTPUT);  
    DIRECT_WRITE_LOW(_mosiport, _mosipinmask);

  }
  if (_cs != 0xff) {
    _csport = portOutputRegister(_cs);
    _cspinmask = digitalPinToBitMask(_cs);
    pinMode(_cs, OUTPUT); 
    DIRECT_WRITE_HIGH(_csport, _cspinmask);   
  } else _csport = 0;

  if (_pspi && _pspi->pinIsChipSelect(_rs)) {
    _pspi->setCS(_rs);
    _dcport = 0;
    _dcpinmask = 0;
  } else {
    //Serial.println("ST7735_t3: Error not DC is not valid hardware CS pin");
    _dcport = portOutputRegister(_rs);
    _dcpinmask = digitalPinToBitMask(_rs);
    pinMode(_rs, OUTPUT); 
    DIRECT_WRITE_HIGH(_dcport, _dcpinmask);
  }
  maybeUpdateTCR(LPSPI_TCR_PCS(1) | LPSPI_TCR_FRAMESZ(7));

  // BUGBUG
//  digitalWrite(_cs, LOW);
  if (_rst != 0xff) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, HIGH);
    delay(100);
    digitalWrite(_rst, LOW);
    delay(100);
    digitalWrite(_rst, HIGH);
    delay(200);
  }

  if(cmdList) commandList(cmdList);
}


// Initialization for ST7735B screens
void ST7735_t3::initB(void)
{
  commonInit(Bcmd);
}


// Initialization for ST7735R screens (green or red tabs)
void ST7735_t3::initR(uint8_t options)
{
  commonInit(Rcmd1);
  if (options == INITR_GREENTAB) {
    commandList(Rcmd2green);
    _colstart = 2;
    _rowstart = 1;
  } else if(options == INITR_144GREENTAB) {
    _screenHeight = ST7735_TFTHEIGHT_144;
    commandList(Rcmd2green144);
    _colstart = 2;
    _rowstart = 3;
  } else if(options == INITR_144GREENTAB_OFFSET) {
    _screenHeight = ST7735_TFTHEIGHT_144;
    commandList(Rcmd2green144);
    _colstart = 0;
    _rowstart = 32;
    } else if(options == INITR_MINI160x80) {
      _screenHeight   = ST7735_TFTHEIGHT_160;
      _screenWidth    = ST7735_TFTWIDTH_80;
      commandList(Rcmd2green160x80);
      _colstart = 24;
      _rowstart = 0;
  } else {
    // _colstart, _rowstart left at default '0' values
    commandList(Rcmd2red);
  }
  commandList(Rcmd3);

  // if black or mini, change MADCTL color filter
  if ((options == INITR_BLACKTAB)  || (options == INITR_MINI160x80)){
    writecommand(ST7735_MADCTL);
    writedata_last(0xC0);
  }

  tabcolor = options;
  setRotation(0);
}


void ST7735_t3::setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  beginSPITransaction();
  setAddr(x0, y0, x1, y1);
  writecommand(ST7735_RAMWR); // write to RAM
  // The setAddrWindow/pushColor will only work if SPI is kept active during this loop...
  endSPITransaction();
}


void ST7735_t3::pushColor(uint16_t color, boolean last_pixel)
{
  //beginSPITransaction();
  if (last_pixel) {
    writedata16_last(color);
    endSPITransaction();
  } else {
    writedata16(color);
  }
}

void ST7735_t3::drawPixel(int16_t x, int16_t y, uint16_t color)
{
  if ((x < 0) ||(x >= _width) || (y < 0) || (y >= _height)) return;
  #ifdef ENABLE_ST77XX_FRAMEBUFFER
  if (_use_fbtft) {
    _pfbtft[y*_width + x] = color;

  } else 
  #endif
  {
    beginSPITransaction();
    setAddr(x,y,x+1,y+1);
    writecommand(ST7735_RAMWR);
    writedata16_last(color);
    endSPITransaction();
  }
}


void ST7735_t3::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
  // Rudimentary clipping
  if ((x >= _width) || (y >= _height)) return;
  if ((y+h-1) >= _height) h = _height-y;
  #ifdef ENABLE_ST77XX_FRAMEBUFFER
  if (_use_fbtft) {
    uint16_t * pfbPixel = &_pfbtft[ y*_width + x];
    while (h--) {
      *pfbPixel = color;
      pfbPixel += _width;
    }
  } else 
  #endif
  {
    beginSPITransaction();
    setAddr(x, y, x, y+h-1);
    writecommand(ST7735_RAMWR);
    while (h-- > 1) {
      writedata16(color);
    }
    writedata16_last(color);
    endSPITransaction();
  }
}


void ST7735_t3::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
  // Rudimentary clipping
  if ((x >= _width) || (y >= _height)) return;
  if ((x+w-1) >= _width)  w = _width-x;

  #ifdef ENABLE_ST77XX_FRAMEBUFFER
  if (_use_fbtft) {
    if ((x&1) || (w&1)) {
      uint16_t * pfbPixel = &_pfbtft[ y*_width + x];
      while (w--) {
        *pfbPixel++ = color;
      }
    } else {
      // X is even and so is w, try 32 bit writes..
      uint32_t color32 = (color << 16) | color;
      uint32_t * pfbPixel = (uint32_t*)((uint16_t*)&_pfbtft[ y*_width + x]);
      while (w) {
        *pfbPixel++ = color32;
        w -= 2;
      }
    }
  } else 
  #endif
  {
    beginSPITransaction();
    setAddr(x, y, x+w-1, y);
    writecommand(ST7735_RAMWR);
    while (w-- > 1) {
      writedata16(color);
    }
    writedata16_last(color);
    endSPITransaction();
  }
}



void ST7735_t3::fillScreen(uint16_t color)
{
  fillRect(0, 0,  _width, _height, color);
}



// fill a rectangle
void ST7735_t3::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  // rudimentary clipping (drawChar w/big text requires this)
  if ((x >= _width) || (y >= _height)) return;
  if ((x + w - 1) >= _width)  w = _width  - x;
  if ((y + h - 1) >= _height) h = _height - y;
  #ifdef ENABLE_ST77XX_FRAMEBUFFER
  if (_use_fbtft) {
    if ((x&1) || (w&1)) {
      uint16_t * pfbPixel_row = &_pfbtft[ y*_width + x];
      for (;h>0; h--) {
        uint16_t * pfbPixel = pfbPixel_row;
        for (int i = 0 ;i < w; i++) {
          *pfbPixel++ = color;
        }
        pfbPixel_row += _width;
      }
    } else {
      // Horizontal is even number so try 32 bit writes instead
      uint32_t color32 = (color << 16) | color;
      uint32_t * pfbPixel_row = (uint32_t *)((uint16_t*)&_pfbtft[ y*_width + x]);
      w = w/2;  // only iterate half the times
      for (;h>0; h--) {
        uint32_t * pfbPixel = pfbPixel_row;
        for (int i = 0 ;i < w; i++) {
          *pfbPixel++ = color32;
        }
        pfbPixel_row += (_width/2);
      }
    }
  } else 
  #endif
  {
    beginSPITransaction();
    setAddr(x, y, x+w-1, y+h-1);
    writecommand(ST7735_RAMWR);
    for (y=h; y>0; y--) {
      for(x=w; x>1; x--) {
        writedata16(color);
      }
      writedata16_last(color);
    }
    endSPITransaction();
  }
}


#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_ML  0x10
#define MADCTL_RGB 0x00
#define MADCTL_BGR 0x08
#define MADCTL_MH  0x04

void ST7735_t3::setRotation(uint8_t m)
{
  beginSPITransaction();
  writecommand(ST7735_MADCTL);
  rotation = m % 4; // can't be higher than 3
  switch (rotation) {
  case 0:
      if ((tabcolor == INITR_BLACKTAB) || (tabcolor == INITR_MINI160x80)) {
      writedata_last(MADCTL_MX | MADCTL_MY | MADCTL_RGB);
    } else {
      writedata_last(MADCTL_MX | MADCTL_MY | MADCTL_BGR);
    }
    _width  = _screenWidth;
    _height = _screenHeight;
      _xstart = _colstart;
      _ystart = _rowstart;
    break;
  case 1:
      if ((tabcolor == INITR_BLACKTAB) || (tabcolor == INITR_MINI160x80)) {
      writedata_last(MADCTL_MY | MADCTL_MV | MADCTL_RGB);
    } else {
      writedata_last(MADCTL_MY | MADCTL_MV | MADCTL_BGR);
    }
    _height = _screenWidth;
    _width = _screenHeight;
      _ystart = _colstart;
      _xstart = _rowstart;
    break;
  case 2:
      if ((tabcolor == INITR_BLACKTAB) || (tabcolor == INITR_MINI160x80)) {
      writedata_last(MADCTL_RGB);
    } else {
      writedata_last(MADCTL_BGR);
    }
    _width  = _screenWidth;
    _height = _screenHeight;
      _xstart = _colstart;
      // hack to make work on a couple different displays
      _ystart = (_rowstart==0 || _rowstart==32)? 0 : 1;//_rowstart;
    break;
  case 3:
      if ((tabcolor == INITR_BLACKTAB) || (tabcolor == INITR_MINI160x80)) {
      writedata_last(MADCTL_MX | MADCTL_MV | MADCTL_RGB);
    } else {
      writedata_last(MADCTL_MX | MADCTL_MV | MADCTL_BGR);
    }
    _width = _screenHeight;
    _height = _screenWidth;
      _ystart = _colstart;
      // hack to make work on a couple different displays
      _xstart = (_rowstart==0 || _rowstart==32)? 0 : 1;//_rowstart;
    break;
  }
  _rot = rotation;  // remember the rotation... 
  //Serial.printf("SetRotation(%d) _xstart=%d _ystart=%d _width=%d, _height=%d\n", _rot, _xstart, _ystart, _width, _height);
  endSPITransaction();
}

void ST7735_t3::setRowColStart(uint16_t x, uint16_t y) {
  _rowstart = x;
  _colstart = y;
  if (_rot != 0xff) setRotation(_rot);
}


void ST7735_t3::invertDisplay(boolean i)
{
  beginSPITransaction();
  writecommand_last(i ? ST7735_INVON : ST7735_INVOFF);
  endSPITransaction();

}

/*!
 @brief   Adafruit_SPITFT Send Command handles complete sending of commands and const data
 @param   commandByte       The Command Byte
 @param   dataBytes         A pointer to the Data bytes to send
 @param   numDataBytes      The number of bytes we should send
 */
void ST7735_t3::sendCommand(uint8_t commandByte, const uint8_t *dataBytes, uint8_t numDataBytes) {
    beginSPITransaction();

    writecommand_last(commandByte); // Send the command byte
  
    while (numDataBytes > 1) {
    writedata(*dataBytes++); // Send the data bytes
    numDataBytes--;
    }
    if (numDataBytes) writedata_last(*dataBytes);
  
    endSPITransaction();
}

void ST7735_t3::writeRect(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pcolors)
{
  beginSPITransaction();
  setAddr(x, y, x + w - 1, y + h - 1);
  writecommand(ST7735_RAMWR);
  for (y = h; y > 0; y--) {
    for (x = w; x > 1; x--) {
      writedata16(*pcolors++);
    }
    writedata16_last(*pcolors++);
  }
  endSPITransaction();
}
#ifdef ENABLE_ST77XX_FRAMEBUFFER
void ST7735_t3::dmaInterrupt(void) {
  if (_dmaActiveDisplay[0])  {
    _dmaActiveDisplay[0]->process_dma_interrupt();
  }
}
void ST7735_t3::dmaInterrupt1(void) {
  if (_dmaActiveDisplay[1])  {
    _dmaActiveDisplay[1]->process_dma_interrupt();
  }
}
void ST7735_t3::dmaInterrupt2(void) {
  if (_dmaActiveDisplay[2])  {
    _dmaActiveDisplay[2]->process_dma_interrupt();
  }
}

//=============================================================================
// Frame buffer support. 
//=============================================================================
#ifdef ENABLE_ST77XX_FRAMEBUFFER
#ifdef DEBUG_ASYNC_UPDATE
extern void dumpDMA_TCD(DMABaseClass *dmabc);
#endif

void ST7735_t3::process_dma_interrupt(void) {
#ifdef DEBUG_ASYNC_LEDS
  digitalWriteFast(DEBUG_PIN_2, HIGH);
#endif
  // Serial.println(" ST7735_t3::process_dma_interrupt");

  // T4
  bool still_more_dma = true;
  _dma_sub_frame_count++;
  #if defined(DEBUG_ASYNC_UPDATE)
  Serial.print(".");
  #endif
  if (_dma_sub_frame_count == _dma_cnt_sub_frames_per_frame) {
  #ifdef DEBUG_ASYNC_LEDS
    digitalWriteFast(DEBUG_PIN_3, HIGH);
  #endif
    #if defined(DEBUG_ASYNC_UPDATE)
    Serial.println("*");
    #endif
    // We completed a frame. 
    _dma_frame_count++;
    // See if we are logically done
    if (_dma_state & ST77XX_DMA_FINISH) {
      //Serial.println("$");
      still_more_dma = false;

      // We are in single refresh mode or the user has called cancel so
      // Lets try to release the CS pin
      // Lets wait until FIFO is not empty
      //Serial.printf("Before FSR wait: %x %x\n", _pimxrt_spi->FSR, _pimxrt_spi->SR);
      while (_pimxrt_spi->FSR & 0x1f)  ;  // wait until this one is complete

      //Serial.printf("Before SR busy wait: %x\n", _pimxrt_spi->SR);
      while (_pimxrt_spi->SR & LPSPI_SR_MBF)  ; // wait until this one is complete

      _dma_data[_spi_num]._dmatx.clearComplete();
      //Serial.println("Restore FCR");
      _pimxrt_spi->FCR = LPSPI_FCR_TXWATER(15); // _spi_fcr_save; // restore the FSR status... 
      _pimxrt_spi->DER = 0;   // DMA no longer doing TX (or RX)

      _pimxrt_spi->CR = LPSPI_CR_MEN | LPSPI_CR_RRF | LPSPI_CR_RTF;   // actually clear both...
      _pimxrt_spi->SR = 0x3f00; // clear out all of the other status...


//      maybeUpdateTCR(LPSPI_TCR_PCS(0) | LPSPI_TCR_FRAMESZ(7));  // output Command with 8 bits
      // Serial.printf("Output NOP (SR %x CR %x FSR %x FCR %x %x TCR:%x)\n", _pimxrt_spi->SR, _pimxrt_spi->CR, _pimxrt_spi->FSR, 
      //  _pimxrt_spi->FCR, _spi_fcr_save, _pimxrt_spi->TCR);
      _pending_rx_count = 0;  // Make sure count is zero
//      writecommand_last(ST7735_NOP);

      // Serial.println("Do End transaction");
      endSPITransaction();
      _dma_state &= ~(ST77XX_DMA_ACTIVE | ST77XX_DMA_FINISH);
      _dmaActiveDisplay[_spi_num] = 0;  // We don't have a display active any more... 

      // Serial.println("After End transaction");
      #if defined(DEBUG_ASYNC_UPDATE)
      Serial.println("$");
      #endif
    }
    _dma_sub_frame_count = 0;
#ifdef DEBUG_ASYNC_LEDS
    digitalWriteFast(DEBUG_PIN_3, LOW);
#endif
  }

  if (still_more_dma) {
    // we are still in a sub-frame so we need to copy memory down...
    if (_dma_sub_frame_count == (_dma_cnt_sub_frames_per_frame-2)) {
      if ((_dma_state & ST77XX_DMA_CONT) == 0) {
        if (_dma_sub_frame_count & 1) _dma_data[_spi_num]._dmasettings[0].disableOnCompletion();
        else _dma_data[_spi_num]._dmasettings[1].disableOnCompletion();
        #if defined(DEBUG_ASYNC_UPDATE)
        Serial.println("!");
        #endif
        _dma_state |= ST77XX_DMA_FINISH;  // let system know we set the finished state

      }
    }
    if (_dma_sub_frame_count & 1) {
      memcpy(_dma_data[_spi_num]._dma_buffer1, &_pfbtft[_dma_pixel_index], _dma_buffer_size*2);
    } else {      
      memcpy(_dma_data[_spi_num]._dma_buffer2, &_pfbtft[_dma_pixel_index], _dma_buffer_size*2);
    }
    _dma_pixel_index += _dma_buffer_size;
    if (_dma_pixel_index >= (_count_pixels))
      _dma_pixel_index = 0;   // we will wrap around 
  }
  _dma_data[_spi_num]._dmatx.clearInterrupt();
  _dma_data[_spi_num]._dmatx.clearComplete();
  asm("dsb");

#ifdef DEBUG_ASYNC_LEDS
  digitalWriteFast(DEBUG_PIN_2, LOW);
#endif
}
//=======================================================================
// Add optinal support for using frame buffer to speed up complex outputs
//=======================================================================
void ST7735_t3::setFrameBuffer(uint16_t *frame_buffer) 
{
  _pfbtft = frame_buffer;
  // we may not know the size of it, if called before init.
/*  if (_pfbtft != NULL) {
    memset(_pfbtft, 0, _width*_height*2);
  } */
}

uint8_t ST7735_t3::useFrameBuffer(boolean b)    // use the frame buffer?  First call will allocate
{
  if (b) {
    // First see if we need to allocate buffer
    if (_pfbtft == NULL) {
      // Hack to start frame buffer on 32 byte boundary
      // Note: If called before init maybe larger than we need
      _count_pixels =  _width * _height;
      _we_allocated_buffer = (uint16_t *)malloc(_count_pixels*2+32);
      if (_we_allocated_buffer == NULL)
        return 0; // failed 
      _pfbtft = (uint16_t*) (((uintptr_t)_we_allocated_buffer + 32) & ~ ((uintptr_t) (31)));
      memset(_pfbtft, 0, _count_pixels*2);  
    }
    _use_fbtft = 1;
  } else 
    _use_fbtft = 0;

  return _use_fbtft;  
}

void ST7735_t3::freeFrameBuffer(void)           // explicit call to release the buffer
{
  if (_we_allocated_buffer) {
    free(_we_allocated_buffer);
    _pfbtft = NULL;
    _use_fbtft = 0; // make sure the use is turned off
    _we_allocated_buffer = NULL;
  }
}
void ST7735_t3::updateScreen(void)          // call to say update the screen now.
{
  // Not sure if better here to check flag or check existence of buffer.
  // Will go by buffer as maybe can do interesting things?
  if (_use_fbtft) {
    beginSPITransaction();
    // Doing full window. 
    setAddr(0, 0, _width-1, _height-1);
    writecommand(ST7735_RAMWR);

    // BUGBUG doing as one shot.  Not sure if should or not or do like
    // main code and break up into transactions...
    uint16_t *pfbtft_end = &_pfbtft[(_count_pixels)-1]; // setup 
    uint16_t *pftbft = _pfbtft;

    // Quick write out the data;
    while (pftbft < pfbtft_end) {
      writedata16(*pftbft++);
    }
    writedata16_last(*pftbft);

    endSPITransaction();
  }
}      

#ifdef DEBUG_ASYNC_UPDATE
void dumpDMA_TCD(DMABaseClass *dmabc)
{
  Serial.printf("%x %x:", (uint32_t)dmabc, (uint32_t)dmabc->TCD);

  Serial.printf("SA:%x SO:%d AT:%x NB:%x SL:%d DA:%x DO: %d CI:%x DL:%x CS:%x BI:%x\n", (uint32_t)dmabc->TCD->SADDR,
    dmabc->TCD->SOFF, dmabc->TCD->ATTR, dmabc->TCD->NBYTES, dmabc->TCD->SLAST, (uint32_t)dmabc->TCD->DADDR, 
    dmabc->TCD->DOFF, dmabc->TCD->CITER, dmabc->TCD->DLASTSGA, dmabc->TCD->CSR, dmabc->TCD->BITER);
}
#endif

//==============================================
#ifdef ENABLE_ST77XX_FRAMEBUFFER
void  ST7735_t3::initDMASettings(void) 
{
  // Serial.printf("initDMASettings called %d\n", _dma_state);
  if (_dma_state) {  // should test for init, but...
    return; // we already init this. 
  }
#ifdef DEBUG_ASYNC_LEDS 
  pinMode(DEBUG_PIN_1, OUTPUT); digitalWrite(DEBUG_PIN_1, LOW);
  pinMode(DEBUG_PIN_2, OUTPUT); digitalWrite(DEBUG_PIN_2, LOW);
  pinMode(DEBUG_PIN_3, OUTPUT); digitalWrite(DEBUG_PIN_3, LOW);
#endif


  //Serial.println("InitDMASettings");
  uint8_t dmaTXevent = _spi_hardware->tx_dma_channel;
  _count_pixels = _width*_height; // cache away the size of the display. 

//  Serial.printf("cbDisplay: %u COUNT_WORDS_WRITE:%d(%x) spi_num:%d\n", _count_pixels, COUNT_WORDS_WRITE, COUNT_WORDS_WRITE, _spi_num);

  // See if moving the frame buffer to other memory that is not cached helps out
  // to remove tearing and the like...I know with 256 it will be either 256 or 248...
  _dma_buffer_size = ST77XX_DMA_BUFFER_SIZE;
  _dma_cnt_sub_frames_per_frame = (_count_pixels) / _dma_buffer_size;
  while ((_dma_cnt_sub_frames_per_frame * _dma_buffer_size) != (_count_pixels)) {
    _dma_buffer_size--;
    _dma_cnt_sub_frames_per_frame = (_count_pixels) / _dma_buffer_size;   
  }

#if defined(DEBUG_ASYNC_UPDATE)
  Serial.printf("DMA Init buf size: %d sub frames:%d spi num: %d\n", _dma_buffer_size, _dma_cnt_sub_frames_per_frame, _spi_num);
#endif

  _dma_data[_spi_num]._dmasettings[0].sourceBuffer(_dma_data[_spi_num]._dma_buffer1, _dma_buffer_size*2);
  _dma_data[_spi_num]._dmasettings[0].destination(_pimxrt_spi->TDR);
  _dma_data[_spi_num]._dmasettings[0].TCD->ATTR_DST = 1;
  _dma_data[_spi_num]._dmasettings[0].replaceSettingsOnCompletion(_dma_data[_spi_num]._dmasettings[1]);
  _dma_data[_spi_num]._dmasettings[0].interruptAtCompletion();

  _dma_data[_spi_num]._dmasettings[1].sourceBuffer(_dma_data[_spi_num]._dma_buffer2, _dma_buffer_size*2);
  _dma_data[_spi_num]._dmasettings[1].destination(_pimxrt_spi->TDR);
  _dma_data[_spi_num]._dmasettings[1].TCD->ATTR_DST = 1;
  _dma_data[_spi_num]._dmasettings[1].replaceSettingsOnCompletion(_dma_data[_spi_num]._dmasettings[0]);
  _dma_data[_spi_num]._dmasettings[1].interruptAtCompletion();

  // Setup DMA main object
  //Serial.println("Setup _dmatx");
  // Serial.println("DMA initDMASettings - before dmatx");
  _dma_data[_spi_num]._dmatx.begin(true);
  _dma_data[_spi_num]._dmatx.triggerAtHardwareEvent(dmaTXevent);
  _dma_data[_spi_num]._dmatx = _dma_data[_spi_num]._dmasettings[0];
  // probably could use const table of functions...
  if (_spi_num == 0) _dma_data[_spi_num]._dmatx.attachInterrupt(dmaInterrupt);
  else if (_spi_num == 1) _dma_data[_spi_num]._dmatx.attachInterrupt(dmaInterrupt1);
  else _dma_data[_spi_num]._dmatx.attachInterrupt(dmaInterrupt2);

  _dma_state = ST77XX_DMA_INIT;  // Should be first thing set!
  // Serial.println("DMA initDMASettings - end");

}
#endif

void ST7735_t3::dumpDMASettings() {
#ifdef DEBUG_ASYNC_UPDATE
  // Serial.printf("DMA dump TCDs %d\n", _dmatx.channel);
  dumpDMA_TCD(&_dma_data[_spi_num]._dmatx);
  dumpDMA_TCD(&_dma_data[_spi_num]._dmasettings[0]);
  dumpDMA_TCD(&_dma_data[_spi_num]._dmasettings[1]);
#endif

}

bool ST7735_t3::updateScreenAsync(bool update_cont)         // call to say update the screen now.
{
  // Not sure if better here to check flag or check existence of buffer.
  // Will go by buffer as maybe can do interesting things?
  // BUGBUG:: only handles full screen so bail on the rest of it...
  // Also bail if we are working with a hardware SPI port. 
  #ifdef ENABLE_ST77XX_FRAMEBUFFER
  if (!_use_fbtft || !_pspi) return false;

#ifdef DEBUG_ASYNC_LEDS
  digitalWriteFast(DEBUG_PIN_1, HIGH);
#endif
  // Init DMA settings. 
  initDMASettings();

  // Don't start one if already active.
  if (_dma_state & ST77XX_DMA_ACTIVE) {
  #ifdef DEBUG_ASYNC_LEDS
    digitalWriteFast(DEBUG_PIN_1, LOW);
  #endif
    return false;
  }


    // Start off remove disable on completion from both...
  // it will be the ISR that disables it... 
  _dma_data[_spi_num]._dmasettings[0].TCD->CSR &= ~( DMA_TCD_CSR_DREQ);
  _dma_data[_spi_num]._dmasettings[1].TCD->CSR &= ~( DMA_TCD_CSR_DREQ);

#ifdef DEBUG_ASYNC_UPDATE
  dumpDMASettings();
#endif
  // Lets copy first parts of frame buffer into our two sub-frames
  memcpy(_dma_data[_spi_num]._dma_buffer1, _pfbtft, _dma_buffer_size*2);
  memcpy(_dma_data[_spi_num]._dma_buffer2, &_pfbtft[_dma_buffer_size], _dma_buffer_size*2);
  _dma_pixel_index = _dma_buffer_size*2;
  _dma_sub_frame_count = 0; // 

  beginSPITransaction();
  // Doing full window. 
  setAddr(0, 0, _width-1, _height-1);
  writecommand_last(ST7735_RAMWR);

  // Update TCR to 16 bit mode. and output the first entry.
  _spi_fcr_save = _pimxrt_spi->FCR; // remember the FCR
  _pimxrt_spi->FCR = 0; // clear water marks...   
  maybeUpdateTCR(LPSPI_TCR_PCS(1) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK /*| LPSPI_TCR_CONT*/);
//  _pimxrt_spi->CFGR1 |= LPSPI_CFGR1_NOSTALL;
//  maybeUpdateTCR(LPSPI_TCR_PCS(1) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_CONT);
  _pimxrt_spi->DER = LPSPI_DER_TDDE;
  _pimxrt_spi->SR = 0x3f00; // clear out all of the other status...

    _dma_data[_spi_num]._dmatx.triggerAtHardwareEvent( _spi_hardware->tx_dma_channel );


  _dma_data[_spi_num]._dmatx = _dma_data[_spi_num]._dmasettings[0];

    _dma_data[_spi_num]._dmatx.begin(false);
    _dma_data[_spi_num]._dmatx.enable();

  _dma_frame_count = 0;  // Set frame count back to zero. 
  _dmaActiveDisplay[_spi_num] = this;
  if (update_cont) {
    _dma_state |= ST77XX_DMA_CONT;
  } else {
    _dma_state &= ~ST77XX_DMA_CONT;

  }

  _dma_state |= ST77XX_DMA_ACTIVE;

#ifdef DEBUG_ASYNC_LEDS
  digitalWriteFast(DEBUG_PIN_1, LOW);
#endif
  return true;
    #else
    return false;     // no frame buffer so will never start... 
  #endif

}      

#if defined(__IMXRT1062__)
// TSynth: frame update as a single DMA transfer, polled for completion.
//
// updateScreenAsync() sends the frame as 40 sub-frames through two small ping-pong buffers
// and counts completion interrupts to know where it is. Each sub-frame takes ~340us; if
// the interrupt is held up longer than that, two completions arrive as one interrupt, the
// count slips, the channel is stopped on the wrong sub-frame and the final interrupt never
// comes: the update stays "active" for ever and the display freezes (seen under heavy MIDI
// load). Here the whole frame (20480 pixels, within the DMA's 32767 transfer limit) goes in
// one transfer from the frame buffer itself, flushed from the cache first, and the display
// thread polls the channel's done flag. No interrupts are involved.

void ST7735_t3::setPanelVariant(bool invert, uint8_t madctl, uint16_t xstart, uint16_t ystart)
{
  beginSPITransaction();
  writecommand(ST7735_MADCTL);
  writedata_last(madctl);
  endSPITransaction();
  invertDisplay(invert);
  _xstart = xstart;
  _ystart = ystart;
}

bool ST7735_t3::oneShotUpdate(void)
{
  if (!_use_fbtft || !_pspi || _oneshot_active || (_dma_state & ST77XX_DMA_ACTIVE))
    return false;
  if (_count_pixels > 32767)
    return false;

  // The frame buffer is in cached RAM: write it back so the DMA sees what was drawn
  arm_dcache_flush((void *)_pfbtft, _count_pixels * 2);

  _oneshot_dma.disable();
  _oneshot_dma.sourceBuffer(_pfbtft, _count_pixels * 2);
  _oneshot_dma.destination(_pimxrt_spi->TDR);
  _oneshot_dma.TCD->ATTR_DST = 1; // 16 bit writes to the SPI
  _oneshot_dma.disableOnCompletion();
  _oneshot_dma.triggerAtHardwareEvent(_spi_hardware->tx_dma_channel);
  _oneshot_dma.clearComplete();

  // Same SPI set up as updateScreenAsync()
  beginSPITransaction();
  setAddr(0, 0, _width-1, _height-1);
  writecommand_last(ST7735_RAMWR);
  _spi_fcr_save = _pimxrt_spi->FCR;
  _pimxrt_spi->FCR = 0;
  maybeUpdateTCR(LPSPI_TCR_PCS(1) | LPSPI_TCR_FRAMESZ(15) | LPSPI_TCR_RXMSK);
  _pimxrt_spi->DER = LPSPI_DER_TDDE;
  _pimxrt_spi->SR = 0x3f00;

  _oneshot_active = true;
  _oneshot_dma.enable();
  return true;
}

// Same clean up as the end of process_dma_interrupt(), with the waits bounded
void ST7735_t3::oneShotFinish(void)
{
  uint32_t n = 0;
  while ((_pimxrt_spi->FSR & 0x1f) && ++n < 1000000) ; // TX FIFO empty
  n = 0;
  while ((_pimxrt_spi->SR & LPSPI_SR_MBF) && ++n < 1000000) ; // last word sent
  _oneshot_dma.clearComplete();
  _pimxrt_spi->FCR = LPSPI_FCR_TXWATER(15);
  _pimxrt_spi->DER = 0;
  _pimxrt_spi->CR = LPSPI_CR_MEN | LPSPI_CR_RRF | LPSPI_CR_RTF;
  _pimxrt_spi->SR = 0x3f00;
  _pending_rx_count = 0;
  endSPITransaction();
  _oneshot_active = false;
}

bool ST7735_t3::oneShotUpdateActive(void)
{
  if (!_oneshot_active)
    return false;
  if (!_oneshot_dma.complete())
    return true;
  oneShotFinish();
  return false;
}

void ST7735_t3::oneShotUpdateAbort(void)
{
  if (!_oneshot_active)
    return;
  _oneshot_dma.disable();
  oneShotFinish();
}
#endif

void ST7735_t3::endUpdateAsync() {
  // make sure it is on
  #ifdef ENABLE_ST77XX_FRAMEBUFFER
  if (_dma_state & ST77XX_DMA_CONT) {
    _dma_state &= ~ST77XX_DMA_CONT; // Turn of the continueous mode
  }
  #endif
}
  
void ST7735_t3::waitUpdateAsyncComplete(void) 
{
  #ifdef ENABLE_ST77XX_FRAMEBUFFER
#ifdef DEBUG_ASYNC_LEDS
  digitalWriteFast(DEBUG_PIN_3, HIGH);
#endif

  while ((_dma_state & ST77XX_DMA_ACTIVE)) {
    // asm volatile("wfi");
  };
#ifdef DEBUG_ASYNC_LEDS
  digitalWriteFast(DEBUG_PIN_3, LOW);
#endif
  #endif  
}
#endif

#endif
