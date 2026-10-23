// UNIHIKER K10 pin map. Sources and confidence levels: docs/k10_hardware.md
#pragma once

// I2C bus shared by the expander, sensors, mic ADC and camera SCCB
#define K10_I2C_PORT      0
#define K10_I2C_SDA       47
#define K10_I2C_SCL       48

// I2C addresses (7-bit)
#define K10_ADDR_XL9535   0x20
#define K10_ADDR_SC7A20H  0x19
#define K10_ADDR_MSA311   0x62   // older boards, probed as a fallback
#define K10_ADDR_LTR303   0x29
#define K10_ADDR_AHT20    0x38
#define K10_ADDR_GC2145   0x3C
// ES7243E sits at 0x11 or 0x15 depending on the board; probed at boot

// XL9535 expander bits (bit n of the 16-bit port, P0x = 0..7, P1x = 8..15)
#define K10_EXP_LCD_BL    0      // P00, high = backlight on
#define K10_EXP_CAM_RST   1      // P01, low = camera in reset
#define K10_EXP_KEY_B     2      // P02, active low
#define K10_EXP_KEY_A     12     // P14, active low
#define K10_EXP_USER_LED  15     // P17

// ILI9341 LCD on SPI2
#define K10_LCD_SCLK      12
#define K10_LCD_MOSI      21
#define K10_LCD_DC        13
#define K10_LCD_CS        14
#define K10_LCD_HZ        40000000

// microSD on SPI3 (shares the bus with the font ROM, whose CS is GPIO40 inverted)
#define K10_SD_SCLK       44
#define K10_SD_MOSI       42
#define K10_SD_MISO       41
#define K10_SD_CS         40
#define K10_SD_KHZ        20000

// GC2145 camera (DVP). PWDN is hardwired; RESET is on the expander.
#define K10_CAM_XCLK      7
#define K10_CAM_PCLK      17
#define K10_CAM_VSYNC     4
#define K10_CAM_HREF      5
#define K10_CAM_D0        8
#define K10_CAM_D1        10
#define K10_CAM_D2        11
#define K10_CAM_D3        9
#define K10_CAM_D4        18
#define K10_CAM_D5        16
#define K10_CAM_D6        15
#define K10_CAM_D7        6
#define K10_CAM_XCLK_HZ   10000000  // clean on a real K10; 16 MHz gives a scratchy picture

// I2S: ES7243E mic ADC in, NS4168 amp out
#define K10_I2S_MCLK      3
#define K10_I2S_BCLK      0
#define K10_I2S_WS        38
#define K10_I2S_DOUT      45
#define K10_I2S_DIN       39

// KY-040 rotary encoder on the edge connector / Gravity sockets.
// CLK and DT need native GPIOs for the hardware pulse counter; P0 and P1 are
// the only free ones. The push switch goes through the expander (edge P2).
#define K10_KNOB_A        1      // P0: KY-040 CLK
#define K10_KNOB_B        2      // P1: KY-040 DT
#define K10_EXP_KNOB_SW   7      // expander P07 = edge P2: KY-040 SW, active low (10k pull-up to 3V3)

// External white flash LED, switched by a transistor from edge P8 (expander
// P10). Edge P5 and P11 are buttons A and B, and P2 is the knob switch.
#define K10_EXP_FLASH     8      // expander P10 = edge P8, high = flash on

// 3x WS2812, GRB order
#define K10_RGB_PIN       46
#define K10_RGB_COUNT     3
