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

#ifndef __ST7735_t3_H_
#define __ST7735_t3_H_

#include "Arduino.h"
#include <SPI.h>
#include <Adafruit_GFX.h>

#ifndef DISABLE_ST77XX_FRAMEBUFFER
#if defined(__MK64FX512__) || defined(__MK66FX1M0__)
#define ENABLE_ST77XX_FRAMEBUFFER
#elif defined(__IMXRT1062__)
#define ENABLE_ST77XX_FRAMEBUFFER
#endif
// Lets allow the user to define if they want T3.2 to enable frame buffer.
// it will only work on subset of displays due to memory
#define ENABLE_ST77XX_FRAMEBUFFER_T32
#if defined(__MK20DX256__) && defined(ENABLE_ST77XX_FRAMEBUFFER_T32)
#define ENABLE_ST77XX_FRAMEBUFFER
#endif
#endif

//A compromise of speed vs noise introduced into analogue audio
//#define ST7735_SPICLOCK 48000000
#define ST7735_SPICLOCK 24000000

// some flags for initR() :(
#define INITR_GREENTAB 0x0
#define INITR_REDTAB   0x1
#define INITR_BLACKTAB 0x2

#define INITR_18GREENTAB    INITR_GREENTAB
#define INITR_18REDTAB      INITR_REDTAB
#define INITR_18BLACKTAB    INITR_BLACKTAB
#define INITR_144GREENTAB   0x1
#define INITR_144GREENTAB_OFFSET   0x4
#define INITR_MINI160x80  0x05

#define INIT_ST7789_TABCOLOR 42  // Not used except as a indicator to the code... 

#define ST7735_TFTWIDTH  128
#define ST7735_TFTWIDTH_80     80 // for mini
// for 1.44" display
#define ST7735_TFTHEIGHT_144 128
// for 1.8" display and mini
#define ST7735_TFTHEIGHT_160  160 // for 1.8" and mini display

#define ST7735_NOP     0x00
#define ST7735_SWRESET 0x01
#define ST7735_RDDID   0x04
#define ST7735_RDDST   0x09

#define ST7735_SLPIN   0x10
#define ST7735_SLPOUT  0x11
#define ST7735_PTLON   0x12
#define ST7735_NORON   0x13

#define ST7735_INVOFF  0x20
#define ST7735_INVON   0x21
#define ST7735_DISPOFF 0x28
#define ST7735_DISPON  0x29
#define ST7735_CASET   0x2A
#define ST7735_RASET   0x2B
#define ST7735_RAMWR   0x2C
#define ST7735_RAMRD   0x2E

#define ST7735_PTLAR   0x30
#define ST7735_COLMOD  0x3A
#define ST7735_MADCTL  0x36

#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR  0xB4
#define ST7735_DISSET5 0xB6

#define ST7735_PWCTR1  0xC0
#define ST7735_PWCTR2  0xC1
#define ST7735_PWCTR3  0xC2
#define ST7735_PWCTR4  0xC3
#define ST7735_PWCTR5  0xC4
#define ST7735_VMCTR1  0xC5

#define ST7735_RDID1   0xDA
#define ST7735_RDID2   0xDB
#define ST7735_RDID3   0xDC
#define ST7735_RDID4   0xDD

#define ST7735_PWCTR6  0xFC

#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1

// Color definitions
#define ST7735_BLACK   0x0000
#define ST7735_BLUE    0x001F
#define ST7735_RED     0xF800
#define ST7735_GREEN   0x07E0
#define ST7735_CYAN    0x07FF
#define ST7735_MAGENTA 0xF81F
#define ST7735_YELLOW  0xFFE0
#define ST7735_WHITE   0xFFFF

// Also define them in a non specific ST77XX specific name
#define ST77XX_BLACK      0x0000
#define ST77XX_WHITE      0xFFFF
#define ST77XX_RED        0xF800
#define ST77XX_GREEN      0x07E0
#define ST77XX_BLUE       0x001F
#define ST77XX_CYAN       0x07FF
#define ST77XX_MAGENTA    0xF81F
#define ST77XX_YELLOW     0xFFE0
#define ST77XX_ORANGE     0xFA00
#define ST77XX_DARKRED    0xA000
#define ST77XX_DARKGREY   0x2222


#if defined(__IMXRT1062__)  // Teensy 4.x
// Also define these in lower memory so as to make sure they are not cached...
// try work around DMA memory cached.  So have a couple of buffers we copy frame buffer into
// as to move it out of the memory that is cached...
#define ST77XX_DMA_BUFFER_SIZE 512
typedef struct {
  DMASetting      _dmasettings[2];
  DMAChannel      _dmatx;
  uint16_t        _dma_buffer1[ST77XX_DMA_BUFFER_SIZE] __attribute__ ((aligned(4)));
  uint16_t        _dma_buffer2[ST77XX_DMA_BUFFER_SIZE] __attribute__ ((aligned(4)));  
} ST7735DMA_Data;
#endif


class ST7735_t3 : public Adafruit_GFX {

 public:

  ST7735_t3(uint8_t CS, uint8_t RS, uint8_t SID, uint8_t SCLK, uint8_t RST = -1);
  ST7735_t3(uint8_t CS, uint8_t RS, uint8_t RST = -1);

  void     initB(void),                             // for ST7735B displays
           initR(uint8_t options = INITR_GREENTAB), // for ST7735R
           setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1),
           pushColor(uint16_t color, boolean last_pixel=false),
           fillScreen(uint16_t color),
           drawPixel(int16_t x, int16_t y, uint16_t color),
           drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color),
           drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color),
           fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  virtual void setRotation(uint8_t r);
  void     invertDisplay(boolean i);
  void     setRowColStart(uint16_t x, uint16_t y);
  uint16_t  rowStart() {return _rowstart;}
  uint16_t  colStart() {return _colstart;}

  void setAddr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
    __attribute__((always_inline)) {
        writecommand(ST7735_CASET); // Column addr set
        writedata16(x0+_xstart);   // XSTART 
        writedata16(x1+_xstart);   // XEND
        writecommand(ST7735_RASET); // Row addr set
        writedata16(y0+_ystart);   // YSTART
        writedata16(y1+_ystart);   // YEND
  }

  void sendCommand(uint8_t commandByte, const uint8_t *dataBytes, uint8_t numDataBytes);



  // Pass 8-bit (each) R,G,B, get back 16-bit packed color
  inline uint16_t Color565(uint8_t r, uint8_t g, uint8_t b) {
           return ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3);
  }
  void setBitrate(uint32_t n);

  /* These are not for current use, 8-bit protocol only!
  uint8_t  readdata(void),
           readcommand8(uint8_t);
  uint16_t readcommand16(uint8_t);
  uint32_t readcommand32(uint8_t);
  void     dummyclock(void);
  */
  // Useful methods added from ili9341_t3 
  void writeRect(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pcolors);

// Frame buffer support
#ifdef ENABLE_ST77XX_FRAMEBUFFER
  enum {ST77XX_DMA_INIT=0x01, ST77XX_DMA_CONT=0x02, ST77XX_DMA_FINISH=0x04,ST77XX_DMA_ACTIVE=0x80};

  // added support to use optional Frame buffer
  void  setFrameBuffer(uint16_t *frame_buffer);
  uint8_t useFrameBuffer(boolean b);    // use the frame buffer?  First call will allocate
  void  freeFrameBuffer(void);      // explicit call to release the buffer
  void  updateScreen(void);       // call to say update the screen now. 
  bool  updateScreenAsync(bool update_cont = false);  // call to say update the screen optinoally turn into continuous mode. 
  void  waitUpdateAsyncComplete(void);
  void  endUpdateAsync();      // Turn of the continueous mode fla
  void  dumpDMASettings();
  uint16_t *getFrameBuffer() {return _pfbtft;}
  uint32_t frameCount() {return _dma_frame_count; }
  boolean asyncUpdateActive(void)  {return (_dma_state & ST77XX_DMA_ACTIVE);}
  void  initDMASettings(void);
  #if defined(__IMXRT1062__)
  // TSynth: whole frame in one DMA transfer straight from the frame buffer, completion
  // polled rather than counted in interrupts (see ST7735_t3.cpp). The caller must not
  // draw into the frame buffer until oneShotUpdateActive() returns false.
  bool  oneShotUpdate(void);         // start; false if it couldn't be started
  bool  oneShotUpdateActive(void);   // true while sending; finishes the transfer when done
  void  oneShotUpdateAbort(void);    // stop a transfer that never finished
  #endif
  #else
  // added support to use optional Frame buffer
  void  setFrameBuffer(uint16_t *frame_buffer) {return;}
  uint8_t useFrameBuffer(boolean b) {return 0;};    // use the frame buffer?  First call will allocate
  void  freeFrameBuffer(void) {return;}      // explicit call to release the buffer
  void  updateScreen(void) {return;}       // call to say update the screen now. 
  bool  updateScreenAsync(bool update_cont = false) {return false;}  // call to say update the screen optinoally turn into continuous mode. 
  void  waitUpdateAsyncComplete(void) {return;}
  void  endUpdateAsync() {return;}      // Turn of the continueous mode fla
  void  dumpDMASettings() {return;}

  uint32_t frameCount() {return 0; }
  uint16_t *getFrameBuffer() {return NULL;}
  boolean asyncUpdateActive(void)  {return false;}
  #endif


 protected:
  uint8_t  tabcolor;

  void     spiwrite(uint8_t),
           spiwrite16(uint16_t d),
           writecommand(uint8_t c),
           writecommand_last(uint8_t c),
           writedata(uint8_t d),
           writedata_last(uint8_t d),
           writedata16(uint16_t d),
           writedata16_last(uint16_t d),
           commandList(const uint8_t *addr),
           commonInit(const uint8_t *cmdList, uint8_t mode=SPI_MODE0);
//uint8_t  spiread(void);

  boolean  hwSPI=true;


  uint16_t _colstart, _rowstart, _xstart, _ystart, _rot, _screenHeight, _screenWidth;
  SPISettings _spiSettings;
#if defined(__MK66FX1M0__)
  uint8_t  _cs, _rs, _rst, _sid, _sclk;
  uint8_t pcs_data, pcs_command;
  uint32_t ctar;
  volatile uint8_t *datapin, *clkpin, *cspin, *rspin;

  SPIClass *_pspi = nullptr;
  uint8_t   _spi_num = 0;          // Which buss is this spi on? 
  KINETISK_SPI_t *_pkinetisk_spi = nullptr;
  SPIClass::SPI_Hardware_t *_spi_hardware = nullptr;
  void waitTransmitComplete(void);
  void waitTransmitComplete(uint32_t mcr);
  uint32_t _fifo_full_test = 0;

  inline void beginSPITransaction() {
    if (_pspi) _pspi->beginTransaction(_spiSettings);
    if (cspin) *cspin = 0;
  }

  inline void endSPITransaction()
  {
    if (cspin) *cspin = 1;
    if (_pspi) _pspi->endTransaction();  
  }


#endif
#if defined(__IMXRT1062__)  // Teensy 4.x
  SPIClass *_pspi = nullptr;
  uint8_t   _spi_num = 0;          // Which buss is this spi on? 
  IMXRT_LPSPI_t *_pimxrt_spi = nullptr;
  SPIClass::SPI_Hardware_t *_spi_hardware;
  uint8_t _pending_rx_count = 0;
  uint32_t _spi_tcr_current = 0; 


  void DIRECT_WRITE_LOW(volatile uint32_t * base, uint32_t mask)  __attribute__((always_inline)) {
    *(base+34) = mask;
  }
  void DIRECT_WRITE_HIGH(volatile uint32_t * base, uint32_t mask)  __attribute__((always_inline)) {
    *(base+33) = mask;
  }
  
  #define TCR_MASK  (LPSPI_TCR_PCS(3) | LPSPI_TCR_FRAMESZ(31) | LPSPI_TCR_CONT | LPSPI_TCR_RXMSK )

  void maybeUpdateTCR(uint32_t requested_tcr_state) {
  if ((_spi_tcr_current & TCR_MASK) != requested_tcr_state) {
      bool dc_state_change = (_spi_tcr_current & LPSPI_TCR_PCS(3)) != (requested_tcr_state & LPSPI_TCR_PCS(3));
      _spi_tcr_current = (_spi_tcr_current & ~TCR_MASK) | requested_tcr_state ;
      // only output when Transfer queue is empty.
      if (!dc_state_change || !_dcpinmask) {
        while ((_pimxrt_spi->FSR & 0x1f) )  ;
        _pimxrt_spi->TCR = _spi_tcr_current;  // update the TCR

      } else {
        waitTransmitComplete();
        if (requested_tcr_state & LPSPI_TCR_PCS(3)) DIRECT_WRITE_HIGH(_dcport, _dcpinmask);
        else DIRECT_WRITE_LOW(_dcport, _dcpinmask);
        _pimxrt_spi->TCR = _spi_tcr_current & ~(LPSPI_TCR_PCS(3) | LPSPI_TCR_CONT); // go ahead and update TCR anyway?  

      }
    }
  }

  inline void beginSPITransaction() {
    if (hwSPI) _pspi->beginTransaction(_spiSettings);
    if (_csport)DIRECT_WRITE_LOW(_csport, _cspinmask);
  }

  inline void endSPITransaction() {
    if (_csport)DIRECT_WRITE_HIGH(_csport, _cspinmask);
    if (hwSPI) _pspi->endTransaction();  
  }

 
  void waitFifoNotFull(void) {
    uint32_t tmp __attribute__((unused));
    do {
        if ((_pimxrt_spi->RSR & LPSPI_RSR_RXEMPTY) == 0)  {
            tmp = _pimxrt_spi->RDR;  // Read any pending RX bytes in
            if (_pending_rx_count) _pending_rx_count--; //decrement count of bytes still levt
        }
    } while ((_pimxrt_spi->SR & LPSPI_SR_TDF) == 0) ;
 }
 void waitTransmitComplete(void)  {
    uint32_t tmp __attribute__((unused));
//    digitalWriteFast(2, HIGH);

    while (_pending_rx_count) {
        if ((_pimxrt_spi->RSR & LPSPI_RSR_RXEMPTY) == 0)  {
            tmp = _pimxrt_spi->RDR;  // Read any pending RX bytes in
            _pending_rx_count--; //decrement count of bytes still levt
        }
    }
    _pimxrt_spi->CR = LPSPI_CR_MEN | LPSPI_CR_RRF;       // Clear RX FIFO
//    digitalWriteFast(2, LOW);
}


  uint8_t  _cs, _rs, _rst, _sid, _sclk;

  uint32_t _cspinmask;
  volatile uint32_t *_csport;
  uint32_t _dcpinmask;
  volatile uint32_t *_dcport;
  uint32_t _mosipinmask;
  volatile uint32_t *_mosiport;
  uint32_t _sckpinmask;
  volatile uint32_t *_sckport;
  

  uint32_t ctar;
#endif

#ifdef ENABLE_ST77XX_FRAMEBUFFER
    // Add support for optional frame buffer
  uint16_t  *_pfbtft;           // Optional Frame buffer 
  uint8_t   _use_fbtft;         // Are we in frame buffer mode?
  uint16_t  *_we_allocated_buffer;      // We allocated the buffer; 
  uint32_t  _count_pixels;       // How big is the display in total pixels...

  // Add DMA support. 
  // Note: We have enough memory to have more than one, so could have multiple active devices (one per SPI BUS)
  //     All three devices have 3 SPI buss so hard coded
  static  ST7735_t3     *_dmaActiveDisplay[3];  // Use pointer to this as a way to get back to object...
  volatile uint8_t      _dma_state;         // DMA status
  volatile uint32_t     _dma_frame_count;   // Can return a frame count...

  #if defined(__MK66FX1M0__) 
  // T3.6 use Scatter/gather with chain to do transfer
  static DMASetting   _dmasettings[3][4];
  DMAChannel   _dmatx;
  uint8_t      _cnt_dma_settings;   // how many do we need for this display?
  #elif defined(__IMXRT1062__)  // Teensy 4.x
  static ST7735DMA_Data _dma_data[3];   // one structure for each SPI buss... 
  // try work around DMA memory cached.  So have a couple of buffers we copy frame buffer into
  // as to move it out of the memory that is cached...
  volatile    uint32_t _dma_pixel_index = 0;
  volatile uint16_t _dma_sub_frame_count = 0; // Can return a frame count...
  uint16_t          _dma_buffer_size;   // the actual size we are using <= DMA_BUFFER_SIZE;
  uint16_t          _dma_cnt_sub_frames_per_frame;  
  uint32_t      _spi_fcr_save;    // save away previous FCR register value
  DMAChannel    _oneshot_dma;     // TSynth: single transfer frame update
  volatile bool _oneshot_active = false;
  void          oneShotFinish(void);

  #elif defined(__MK64FX512__)
  // T3.5 - had issues scatter/gather so do just use channels/interrupts
  // and update and continue
  uint8_t _cspinmask;
  volatile uint8_t *_csport = nullptr;
  DMAChannel   _dmatx;
  DMAChannel   _dmarx;
  uint32_t   _dma_count_remaining;
  uint16_t   _dma_write_size_words;
  #elif defined(__MK20DX256__)
  // For first pass maybe emulate T3.5 on SPI...
  uint8_t _cspinmask;
  volatile uint8_t *_csport = nullptr;
  DMAChannel   _dmatx;
  DMAChannel   _dmarx;
  uint16_t   _dma_count_remaining;
  uint16_t   _dma_write_size_words;

  #endif  
  static void dmaInterrupt(void);
  static void dmaInterrupt1(void);
  static void dmaInterrupt2(void);
  void process_dma_interrupt(void);
#endif

};

#endif
