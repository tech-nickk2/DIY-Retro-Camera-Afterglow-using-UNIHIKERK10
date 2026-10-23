# UNIHIKER K10 (DFR0992) hardware reference for from-scratch PlatformIO firmware

Researched 2026-09-24. This covers the parts we need for a camera-first firmware that does not use DFRobot's closed `unihiker_k10` library.

## Sources and how they were used

| Tag | Source | Notes |
|---|---|---|
| **[SCH]** | Official schematic `UnihikerK10Schematic.pdf`, rev V1.0, 2025-02-13, 11 pages. https://www.unihiker.com/wiki/K10/HardwareReference/img/hardwarereference_onboard/UnihikerK10Schematic.pdf (listed on https://www.unihiker.com/wiki/K10/HardwareReference/hardwarereference_stepschematic/) | I extracted the text with `pdftotext`. Net labels come out in order but a little displaced from their pins, so I mapped them by sequence and anchored the mapping on pins every source agrees on (LCD 12/13/14/21, I²C 47/48, BOOT = IO0). |
| **[PKG]** | DFRobot Arduino board package 0.0.5: `https://downloadcd.dfrobot.com.cn/UNIHIKER/unihiker_k10-0.0.5.zip`, from the index `https://downloadcd.dfrobot.com.cn/UNIHIKER/package_unihiker_index.json` | I downloaded and read the package. Files used: `variants/unihiker_k10/pins_arduino.h`, `boards.txt`, `libraries/unihiker_k10/src/unihiker_k10.{h,cpp}`, `DFRobot_GT30L24A3W.cpp`, `GT30L24A3W.h`, `libraries/SD/src/SD.h`, `libraries/SPI/src/SPI.cpp`, `libraries/TFT_eSPI/User_Setup.h`, `tools/sdk/esp32s3/include/modules/{board/initBoard.h, camera/who_camera.h, lcd/who_lcd.h}`, and the symbol strings in `libmodules.a`. This is the vendor's own shipping code. |
| **[PIO]** | DFRobot PlatformIO platform: https://github.com/DFRobot/platform-unihiker (`boards/unihiker_k10.json`) | |
| **[XZ]** | xiaozhi-esp32 board `main/boards/dfrobot/df-k10` (`config.h`, `config.json`, `df_k10_board.cc`, `k10_audio_codec.cc`): https://github.com/78/xiaozhi-esp32/tree/main/main/boards/dfrobot/df-k10 | Widely used ESP-IDF 5.x port that runs on real boards. |
| **[MPOS]** | MicroPythonOS `internal_filesystem/lib/mpos/board/unihiker_k10.py`: https://github.com/MicroPythonOS/MicroPythonOS | Its display, camera, expander and sensor settings are consistent with the other sources. Its SD and I²S pins disagree with every other source (see §7 and §8). |
| **[RK]** | ricklon/unihiker-k10-demo `docs/HARDWARE.md`, `skills/.../field-notes.md`, `components/k10_bsp/k10_board.c`: https://github.com/ricklon/unihiker-k10-demo | Native ESP-IDF bring-up notes, some tested on a real unit. Its right-hand GPIO table has errors (see §8). |
| **[NL]** | johnnyclem/neon-link `docs/UNIHIKER_K10.md` §8: https://github.com/johnnyclem/neon-link (PR #64) | A pin table assembled from [PKG] and [SCH]. It flags its own uncertain rows. |
| **[WIKI]** | https://www.unihiker.com/wiki/K10/ and the DFRobot product page https://www.dfrobot.com/product-2904.html | Specs only. |

Confidence levels used below:
- **HIGH**: the vendor code and the schematic agree, and usually a third project agrees as well.
- **MED**: one strong source, or small conflicts that have been resolved.
- **LOW / UNVERIFIED**: needs checking on hardware.

---

## 0. Quick pin map (ESP32-S3 GPIO)

| GPIO | Function | Confidence |
|---:|---|---|
| 0 | I²S BCLK **and** the BOOT button (strapping pin, pulled up) | HIGH |
| 1 | Edge P0 (native, ADC1_CH0); 3-pin Gravity "P0" | HIGH |
| 2 | Edge P1 (native, ADC1_CH1); 3-pin Gravity "P1" | HIGH |
| 3 | I²S MCLK to the ES7243E (strapping pin: JTAG source) | HIGH |
| 4 | Camera VSYNC | HIGH |
| 5 | Camera HREF | HIGH |
| 6 | Camera Y9 (esp32-camera `pin_d7`) | HIGH |
| 7 | Camera XCLK | HIGH |
| 8 | Camera Y2 (`pin_d0`) | HIGH |
| 9 | Camera Y5 (`pin_d3`) | HIGH |
| 10 | Camera Y3 (`pin_d1`) | HIGH |
| 11 | Camera Y4 (`pin_d2`) | HIGH |
| 12 | LCD SCLK | HIGH |
| 13 | LCD D/C | HIGH |
| 14 | LCD CS | HIGH |
| 15 | Camera Y8 (`pin_d6`) | HIGH |
| 16 | Camera Y7 (`pin_d5`) | HIGH |
| 17 | Camera PCLK | HIGH |
| 18 | Camera Y6 (`pin_d4`) | HIGH |
| 19 / 20 | USB D− / D+ (native USB Serial/JTAG) | HIGH |
| 21 | LCD MOSI | HIGH |
| 35 / 36 / 37 | Used by the module's octal PSRAM. **Do not use.** | HIGH |
| 38 | I²S LRCK/WS, shared by the mic ADC and the amp | HIGH |
| 39 | I²S DIN (ES7243E SDOUT into the ESP) | HIGH |
| 40 | SD card CS. The same line, inverted through an NPN transistor, is the font-ROM CS. | MED-HIGH |
| 41 | SD / font-ROM MISO | MED-HIGH |
| 42 | SD / font-ROM MOSI | MED-HIGH |
| 43 | XL9535 `INT` (BUS_INT). This is also U0TXD, where the ROM boot log appears. | MED |
| 44 | SD / font-ROM SCLK. This is also U0RXD, so UART0 is not available. | MED-HIGH |
| 45 | I²S DOUT to the NS4168 SDATA (strapping pin: VDD_SPI, must be low at boot; pulled down) | HIGH |
| 46 | WS2812 data, 3 LEDs (strapping pin, pulled down) | HIGH |
| 47 | I²C SDA: all on-board devices, camera SCCB, Gravity I²C, edge P20 | HIGH |
| 48 | I²C SCL (same bus) | HIGH |

Unused or free GPIOs: none of any use. Apart from P0 (GPIO1) and P1 (GPIO2), every usable pin is already taken.

---

## 1. MCU module

- **ESP32-S3-WROOM-1-N16R8**: 16 MB quad flash (QIO) and **8 MB octal (OPI) PSRAM**, dual-core LX7 at 240 MHz, native USB.
  - Sources: [RK], [NL], and [SCH] p1 (module symbol "ESP32-S3-WROOM-1"). [PKG] `boards.txt` has `flash_size=16MB` and offers only one PSRAM menu option, `PSRAM.opi` (`-DBOARD_HAS_PSRAM`, `psram_type=opi`). [XZ] `config.json` sets `CONFIG_SPIRAM_MODE_OCT=y`, and the [XZ] README says to pick "Octal Mode PSRAM".
  - Note: in `boards.txt` the default `psram_type` is `qspi`, but the only selectable option is OPI. **Use `qio_opi`.**
  - **Confidence: HIGH.**
- Octal PSRAM uses GPIO35, 36 and 37 (and the module keeps them internal). This is why no board net is attached to those pins in [SCH].

---

## 2. Display

- **Controller:** ILI9341, 2.8", **240×320** (portrait native), 16-bit RGB565 over 4-wire SPI.
  - Sources: [SCH] p5 (connector labelled ILI9341), [PKG] TFT_eSPI `User_Setup.h` (`ILI9341_DRIVER`), [XZ] (`esp_lcd_new_panel_ili9341`), [MPOS]. **HIGH.**

| Signal | Pin | Source / confidence |
|---|---|---|
| SCLK | GPIO12 | [PKG] `who_lcd.h`, `User_Setup.h`, `pins_arduino.h` SCK; [SCH]; [XZ]; [MPOS]. HIGH |
| MOSI | GPIO21 | same sources. HIGH |
| MISO | **none** (−1). The FPC has an SDO pin, but it is not routed to the ESP. | [PKG] `BOARD_LCD_MISO -1` / `TFT_MISO -1`; [XZ]; [SCH]. HIGH |
| DC | GPIO13 | HIGH |
| CS | GPIO14 | HIGH |
| RST | **none**. `LCD_RST` has an RC reset (10 kΩ pull-up to 3V3, 100 nF to GND). | [SCH] p5; [PKG] `BOARD_LCD_RST -1`. HIGH. **Use the software reset command (0x01).** |
| Backlight | **XL9535 P00** (`LCD_BLK`), which drives an MMBT3904 NPN switch. **High = on.** | [SCH] p3/p5; [PKG] `digital_write(eLCD_BLK,1)` after LVGL init; [MPOS]; [XZ]. HIGH. On/off only; the expander cannot do PWM dimming. |

- **SPI clock:** 40 MHz for writes. [PKG] `BOARD_LCD_PIXEL_CLOCK_HZ 40MHz` and TFT_eSPI `SPI_FREQUENCY 40000000` (read 20 MHz); [XZ] `pclk_hz = 40 MHz`; [MPOS] 40 MHz. **HIGH.** Pushing to 80 MHz on the IO_MUX pins is untested (**UNVERIFIED**).
- **SPI host:** any host works. [PKG] esp-who uses SPI2_HOST (FSPI, the TFT_eSPI default), [XZ] uses SPI3_HOST, [MPOS] uses SPI2. Pins 12 and 21 are not the IO_MUX pins for SPI2 on the S3 (those are 12 = FSPICLK and 11 = FSPID), so MOSI goes through the GPIO matrix, which is fine at 40 MHz. The SD card uses the *other* host (see §8), so give the LCD **SPI2 (FSPI)** and the SD card **SPI3 (HSPI)**, which is what the vendor does.
- **Colour and orientation:**
  - BGR element order: [XZ] `LCD_RGB_ELEMENT_ORDER_BGR`; [MPOS] `BYTE_ORDER_BGR`.
  - RGB565 bytes must be sent high byte first (swap on little-endian), as [MPOS] and [RK] do (`swap_bytes = true`).
  - No colour inversion ([XZ] passes `invert_color(false)`).
  - With USB-C at the top, the accepted transform is `mirror_x=false, mirror_y=true, swap_xy=false` ([XZ] and [RK], tested on a unit). [MPOS] instead uses a 180° rotation plus its own init, and [NL] uses `mirror(true,false)`. **MED.** Draw an "F" test pattern and check.
- No touch controller.

---

## 3. Camera

- **Sensor:** GalaxyCore **GC2145**, 2 MP (UXGA 1600×1200), 8-bit DVP. SCCB 7-bit address **0x3C**, on the shared I²C bus (GPIO47/48).
  - Sources: [SCH] p7, [MPOS], [XZ] (`CONFIG_CAMERA_GC2145`), and the `CAMERA_GC2145` strings in [PKG] `libmodules.a`. **HIGH.**
- **Power:** 2.8 V from a BL8555-28 LDO and 1.8 V from an AP7343Q-18 LDO ([SCH] p7). Both are always on.

**DVP pin map** (esp32-camera naming, where `pin_d0` = sensor Y2). The code block below is [PKG] `who_camera.h` (the non-"老板子"/old-board branch) exactly as written, and it is identical to [XZ], [MPOS], [RK] and [SCH] p1/p7. **HIGH.**

```c
#define CAM_PIN_PWDN   -1   // GC2145 PWDN is tied to a fixed level via 0R ([SCH] p7, [RK]); not controllable
#define CAM_PIN_RESET  -1   // RESET is on XL9535 P01 (active low) -> pulse it manually before esp_camera_init()
#define CAM_PIN_XCLK    7
#define CAM_PIN_SIOD   47   // shared I2C bus
#define CAM_PIN_SIOC   48
#define CAM_PIN_D7      6   // Y9
#define CAM_PIN_D6     15   // Y8
#define CAM_PIN_D5     16   // Y7
#define CAM_PIN_D4     18   // Y6
#define CAM_PIN_D3      9   // Y5
#define CAM_PIN_D2     11   // Y4
#define CAM_PIN_D1     10   // Y3
#define CAM_PIN_D0      8   // Y2
#define CAM_PIN_VSYNC   4
#define CAM_PIN_HREF    5
#define CAM_PIN_PCLK   17
```

- Ignore the SIOD 20 / SIOC 19 entries in [XZ]'s `config.h`. Those are the USB pins; [XZ] actually passes its existing I²C bus handle (`init_sccb=false, i2c_handle=i2c_bus_`).
- **XCLK frequency:**
  - [PKG] 16 MHz (a commented-out 32 MHz is also present)
  - [RK] 16 MHz (tested)
  - [MPOS] 20 MHz
  - [XZ] 6 MHz

  **Start at 16 MHz.** **MED-HIGH.**
- **Reset sequence:** set XL9535 P01 as an output, drive it low for about 10 ms, then high, wait about 10 ms, then call `esp_camera_init()` ([MPOS], [RK], [XZ]). **HIGH.**
- **Sharing SCCB with I²C:** use esp32-camera with `pin_sccb_sda = -1`, `pin_sccb_scl = -1`, `sccb_i2c_port = <the port Wire/I2C already owns>` so the camera does not reinstall the bus. [MPOS] reports that letting the camera driver create its own SCCB bus on 47/48 breaks access to the expander until the bus is re-initialised. **HIGH** (this is a known pitfall).
- **Formats:** esp32-camera `sensors/gc2145.c` supports **RGB565 and YUV422 only (no hardware JPEG)**, up to UXGA. The ESP32-S3 has no JPEG encoder either, so JPEG must be done in software:
  - `frame2jpg()` in esp32-camera's `img_converters`, or
  - `esp_new_jpeg` / JPEGENC.

  **HIGH.** Practical notes:
  - Frame buffers must go in PSRAM (`CAMERA_FB_IN_PSRAM`). UXGA RGB565 is 3.84 MB per buffer; 800×600 RGB565 is about 0.96 MB.
  - RGB565 output is byte-swapped relative to what the LCD path expects ([XZ] `CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP`; [MPOS] `rgb565_byte_swap=True`; [PKG] swaps `pixel>>8|pixel<<8` when writing BMP).
  - [MPOS] sets `default_vflip=True` for the physical orientation. **MED.**
- The camera faces the rear ([RK]).

---

## 4. I/O expander: XL9535QF24

- **Part:** XL9535QF24, a 16-bit I²C GPIO expander that is register-compatible with the TCA9555/PCA9555. A2, A1 and A0 are grounded, so the **7-bit address is 0x20**. Its `INT` output goes to **GPIO43** (BUS_INT, 10 kΩ pull-up).
  - Sources: [SCH] p3 ("Addr = 0x20"), [PKG], [XZ] (uses the `esp_io_expander_tca95xx_16bit` driver at address 000), [MPOS]. **HIGH.**
- **Registers:** 0x00/0x01 input, 0x02/0x03 output, 0x04/0x05 polarity, 0x06/0x07 configuration (1 = input). This is the standard TCA9555 map.

Full pin mapping. The order matches both [SCH] p3 and the [PKG] `ePin_t` enum exactly:

| Expander bit | ePin_t index | Net | Use | Confidence |
|---|---:|---|---|---|
| P00 | 0 | LCD_BLK | LCD backlight, output, high = on | HIGH |
| P01 | 1 | Camera_RST | GC2145 reset, output, active low (keep high to run) | HIGH |
| P02 | 2 | P11 / KeyB | **Button B**, input, active low (external 10 kΩ pull-up) | HIGH |
| P03 | 3 | P12 | edge connector | HIGH |
| P04 | 4 | P13 | edge connector | HIGH |
| P05 | 5 | P14 | edge connector | HIGH |
| P06 | 6 | P15 | edge connector | HIGH |
| P07 | 7 | P2 | edge connector | HIGH |
| P10 | 8 | P8 | edge connector | HIGH |
| P11 | 9 | P9 | edge connector | HIGH |
| P12 | 10 | P10 | edge connector | HIGH |
| P13 | 11 | P6 | edge connector | HIGH |
| P14 | 12 | P5 / KeyA | **Button A**, input, active low (external 10 kΩ pull-up) | HIGH |
| P15 | 13 | P4 | edge connector | HIGH |
| P16 | 14 | P3 | edge connector | HIGH |
| P17 | 15 | UserLed | Small user LED (LED2, through 2 kΩ). The vendor library calls it `eAmp_Gain` and lights it while saving a photo or recording audio. It is **not** an amp control. | MED-HIGH ([SCH] p3/p4 net name, [RK]). Its polarity (whether high lights it) is **UNVERIFIED**. |

- The `pins_arduino.h` values for P2–P16 in [PKG] are expander **bit indices**, not GPIOs. Its `P2 = 3` entry under 原生IO ("native IO") is wrong: GPIO3 is MCLK and P2 is on expander P07.
- **Init order** ([PKG], [RK]):
  1. Configure P00 and P01 as outputs.
  2. Hold the backlight off (P00 = 0) and the camera out of reset (P01 = 1).
  3. Configure P02 and P14 as inputs.
  4. Turn the backlight on only after the LCD init and the first frame, which avoids showing stale GRAM at power-up.
  5. Leave the unused edge bits as inputs.

---

## 5. Buttons A and B

- **A** is on XL9535 **P14** (bit 4 of input register 0x01). **B** is on XL9535 **P02** (bit 2 of input register 0x00). Both are **active low**, with a 10 kΩ pull-up to 3V3 and the switch to GND ([SCH] p4 R8 and R9).
  - Sources: [SCH], [PKG] (`Button(eP5_KeyA)`, `Button(eP11_KeyB)`), [MPOS] (`BTNA_BIT` on P1.4, `BTNB_BIT` on P0.2, "active-low"), [XZ] (`DRV_IO_EXP_INPUT_MASK = PIN_2 | PIN_12`), [RK]. **HIGH.**
- **Reading them:** poll the expander over I²C (a 2-byte read of 0x00/0x01), or use the **GPIO43 falling-edge interrupt** (INT asserts on any input change and clears when the input port is read) and then read. Using INT is **MED**, since no source uses it; they all poll.
- The buttons also appear on edge pins P5 and P11.
- **BOOT (GPIO0)** is a separate button, and it doubles as I²S BCLK. Do not use it as a UI button while audio is running. **RST** is connected to EN.

---

## 6. I²C bus and devices

- **Bus:** SDA **GPIO47**, SCL **GPIO48**, with external 10 kΩ pull-ups ([SCH] p1). The same bus goes to the Gravity 4-pin I²C connector and to edge P19 (SCL) / P20 (SDA). **HIGH.** Use 400 kHz ([MPOS]) or 100 kHz while the camera SCCB is active ([XZ] SCCB runs at 100 kHz).

| 7-bit addr | Device | Notes | Confidence |
|---|---|---|---|
| 0x19 | SC7A20H 3-axis accelerometer (LIS3DH-like; WHO_AM_I register 0x0F = 0x11) | SDO pulled high. [SCH] p10; [PKG] `_sc7a20hAddr = 0x19`; [MPOS]. [PKG] also defines `MSA311_ADDR 0x62`, which looks like an older board revision, so probe both. | HIGH (0x19) |
| 0x20 | XL9535 I/O expander | §4 | HIGH |
| 0x29 | LTR-303ALS-01 ambient light sensor (PART_ID register 0x86 = 0xA0, MANUF_ID register 0x87 = 0x05) | [SCH], [PKG], [MPOS] | HIGH |
| 0x38 | AHT20 temperature and humidity | [SCH], [MPOS] | HIGH |
| **0x11 or 0x15** | ES7243E mic ADC. The address pattern is `0010 AD2 AD1 AD0`. | The schematic itself prints "0x15 ?". [RK] measured **0x11** on a real unit and says the DFRobot runtime supports both 0x11 and 0x15. [XZ] passes the 8-bit address 0x23, which is 0x11 in 7-bit form. **Probe 0x10–0x17 at boot.** | MED |
| 0x3C | GC2145 SCCB | [MPOS] | HIGH |

---

## 7. Audio

- **Microphones:** two MSM381ACT001 analog MEMS mics feeding an **ES7243E** stereo ADC (I²C controlled, I²S/TDM output). **Speaker:** an **NS4168** mono class-D amplifier (I²S input, no I²C) driving a speaker rated at about 2 W. [SCH] p6. **HIGH.**
- **I²S pins** (legacy driver in [PKG] `unihiker_k10.h` and `initI2S()`; [XZ] `config.h`; [SCH] p1/p6; [NL]; [RK]). **HIGH:**

| Signal | GPIO |
|---|---|
| MCLK | 3 (goes to the ES7243E only) |
| BCLK | 0 (shared by ES7243E SCLK and NS4168 BCLK) |
| WS / LRCK | 38 (shared) |
| DOUT (ESP to NS4168 SDATA) | 45 |
| DIN (ES7243E SDOUT to ESP) | 39 |

  - [MPOS] lists BCLK 40 and LRCK 41, which contradicts the vendor code and the schematic. **Treat those as wrong.**
- **Amp enable:** there is **no enable GPIO**. The NS4168 CTRL pin is strapped on the board. On this chip, a CTRL voltage between 1.5 V and VDD selects the right channel, 0.9–1.15 V selects left, and below 0.4 V shuts it down ([SCH] p6; [NL]; [RK]; [XZ] `AUDIO_CODEC_PA_PIN = NC`). **HIGH.**
  - Which channel it is strapped to is **UNVERIFIED**. Send the same sample on both slots (mono duplicated, as [XZ] does with `I2S_SLOT_MODE_MONO` + `SLOT_BOTH`), or write I²S zeros to silence the amp.
  - Expect some idle hiss.
- **Vendor configuration** ([PKG]):
  - Uses I2S_NUM_0 as master in TX+RX mode, 16 kHz, 16-bit, `I2S_CHANNEL_FMT_RIGHT_LEFT`, Philips standard format, MCLK = 256×fs.
  - The ES7243E is initialised inside the closed `init_board()` (`libmodules.a` contains `es7243e_adc_init`, `es7243e_write_reg`, and the esp-adf log strings).
- **ES7243E init registers.** The esp-adf `components/audio_hal/driver/es7243e/es7243e.c` sequence below matches the vendor's error strings. Write it through I²C in slave mode, with MCLK already running:

```
01=3A 00=80 F9=00 04=02 04=01 F9=01 00=1E 01=00
02=00 03=20 04=01 0D=00 05=00 06=03 07=00 08=FF
09=CA 0A=85 0B=00 0E=BF 0F=80 14=0C 15=0C 17=02
18=26 19=77 1A=F4 1B=66 1C=44 1E=00 1F=0C
20=1A 21=1A   (PGA +30 dB; see note)
00=80 01=3A 16=3F 16=00
```

  - [RK] found that on the K10, +30 dB PGA **clips almost continuously**. It set the gain to 0 dB (registers 0x20/0x21 lowered), and also applied the "DFRobot K10 profile" overrides **0B=2C, 0E=FF**. These are physically tested on one unit. **MED.**
  - The alternative is to use `esp_codec_dev` (`es7243e_codec_new`), which is what [XZ] and [RK] do (ESP-IDF 5.x only).
- **Full duplex on IDF 5 / Arduino core 3.x:**
  - [RK] uses I2S0 as RX slave and I2S1 as TX master, sharing BCLK and WS.
  - [XZ] uses one port with `i2s_new_channel(&tx,&rx)`, standard mode for TX and TDM for RX.

  For a camera firmware (a shutter sound and maybe a voice memo), a simple TX-only std channel on GPIO 0/38/45 is enough. MCLK is only needed if the mic is used.

---

## 8. microSD

- **Interface:** **SPI mode, on its own bus** (not SDMMC, and **not shared with the LCD**). The bus is shared with the GT30L24A3W font ROM. [SCH] p9 shows the TF socket plus the font chip on nets `SCLK3`, `MOSI3`, `MISO3` and `CS3`.
- **Pins.** These come from the vendor code: `GT_Font_Init()` calls `SPI1.begin(44, 41, 42, -1)`, which is SCK = 44, MISO = 41, MOSI = 42. Also `FONTCS = 40`, `SD.begin()` defaults to `ssPin = SS = 40` on `SPI1`, and `SPI1` is `SPIClass(HSPI)`. My sequence-mapping of [SCH] p1 gives the same result (IO44 = SCLK3, IO42 = MOSI3, IO41 = MISO3, IO40 = CS3, IO43 = BUS_INT). **MED-HIGH:**

| Signal | GPIO |
|---|---|
| SCLK | 44 |
| MOSI | 42 |
| MISO | 41 (10 kΩ pull-up) |
| CS | 40 |

- **The CS line is shared through an inverter** ([SCH] p9: CS3 drives an MMBT3904 that produces the font chip's CS#). The vendor raises GPIO40 **high** to select the font ROM and holds it **low** to select the SD card. The consequences:
  - GPIO40 low selects the SD card, and it also deselects the font chip, which is what we want.
  - GPIO40 high deselects the SD card, but it also selects the font chip, which then responds on MISO when clocked. This is harmless as long as nothing else is on the bus.
  - The standard Arduino `SD.begin(40, spi)` / `esp_vfs_fat_sdspi_mount` with CS = 40 works this way (the vendor does exactly this).
  - The ESP-IDF SDSPI driver drives CS high between transactions, which puts the font ROM on the bus in that gap. It is not clocked during that time, so this should be fine. **UNVERIFIED.**
- **Conflicts to be aware of:**
  - [NL] guessed that the SD card shared the LCD bus (12/21/41/40), based on the Arduino `SPI` defaults `SCK=12, MOSI=21, MISO=41, SS=40`. But the vendor's SD library is patched to use `SPI1`, so that guess is wrong.
  - [RK] uses CS = 39, MISO = 40, MOSI = 41, CLK = 42 and claims GPIO39 is shared between SD CS and mic DIN. That table is shifted by one net: it also lists GPIO35 as BOOT, which is impossible on an N16R8 module. Its SD result may have been "NO CARD".
  - [MPOS] lists 38/42/1/2, which clashes with P0/P1 and I²S WS.

  **Test with 44/42/41/40 first.**
- **Gotcha:** GPIO44 is U0RXD and GPIO43 is U0TXD (used for BUS_INT). Never start `Serial0` / UART0 on its default pins; use USB CDC for logging instead.
- Use about 20 MHz SPI ([PKG] default `frequency = 20000000`). The vendor's FAQ recommends good-quality 16 or 32 GB cards, FAT32.

---

## 9. RGB LEDs

- **3 WS2812-compatible LEDs** (daisy-chained DIN to DO) on **GPIO46**, GRB order at 800 kHz. [PKG] `PIXEL_PIN 46`, `PIXEL_COUNT 3`, `NEO_GRB + NEO_KHZ800`; [SCH] p4; [MPOS]; [XZ] `BUILTIN_LED_GPIO 46`. **HIGH.**
- The schematic also draws a fourth, unfitted "RGB?" footprint.
- GPIO46 is a strapping pin that is pulled down. Do not hold it high during reset.
- Use the RMT peripheral (Adafruit_NeoPixel, FastLED, or `led_strip`).
- The vendor default brightness is low (5/255 in `RGB(...)`).

---

## 10. Battery and power

- **Connector:** a 2-pin **PH2.0** battery input rated **3.0–6.0 V DC**. DFRobot recommends a 3.7 V single-cell LiPo or three AA/AAA cells ([WIKI]; [RK]). USB-C provides 5 V.
- **Charging:** there is **no charger IC in the schematic** ([SCH] p8 shows an SS34 Schottky from VUSB to VCC, a BCM857BS dual-PNP power-path switch, an inductor and a regulator, U13; [RK] names the main regulator "JW3651" and also found no charger). The fact that three alkaline cells are supported is consistent with there being no charger.
  - Some web summaries claim "the K10 charges via USB-C". I treat that as **UNVERIFIED / likely wrong**.
  - **Conclusion: MED-HIGH that there is no on-board LiPo charging.** Charge cells externally.
- **Battery voltage sense:** **none**. No VBAT or VCC divider goes to an ADC GPIO ([SCH]; [NL]; [RK]). **MED-HIGH.** The only way to measure it would be an external divider into P0 or P1 (GPIO1/2, ADC1).
- There is a power LED (LED1) on 3V3.

---

## 11. Gotchas, strapping and USB

1. **Strapping pins are all in use** ([SCH] p1 strapping table):
   - GPIO0 (pulled up; BOOT; also I²S BCLK).
   - GPIO3 (MCLK).
   - GPIO45 (pulled down; I²S DOUT). It must be low at reset so VDD_SPI stays at 3.3 V.
   - GPIO46 (pulled down; RGB).

   Do not add external loads that pull these the wrong way.
2. **Download mode:** hold **BOOT** and tap **RST**. The native USB Serial/JTAG (VID 0x303A, PID 0x1001) normally auto-resets for esptool, so no manual step is needed. The DFRobot board JSON also lists hwid 0x303A:0x83CF.
3. **USB mode:** the vendor default is `USBMode = hwcdc` (`ARDUINO_USB_MODE=1`), and the vendor PlatformIO example sets `ARDUINO_USB_CDC_ON_BOOT=1`. **Use those two flags** so `Serial` is the USB port. UART0 is unusable because its pins are taken (GPIO43 = expander INT, GPIO44 = SD SCLK).
4. **Everything is on one I²C bus** (expander, sensors, codec, camera SCCB, external Gravity/edge devices). Put a mutex around it. Button polling competes with camera register writes, and the camera must reuse the existing I²C driver (see §3).
5. **GPIO 35, 36 and 37 belong to the octal PSRAM.** No pins are spare for expansion except GPIO1 and GPIO2 (P0/P1).
6. **LCD and camera share nothing** (separate pins). The LCD SPI and the SD SPI are separate buses, so the camera preview to the LCD and the SD writes can run in parallel on different hosts.
7. The vendor's `pins_arduino.h` sets `LED_BUILTIN = SOC_GPIO_PIN_COUNT+48` (a bogus value inherited from the S3 devkit template), `TX` / `RX = -1`, `SDA = 47`, `SCL = 48`, and `SS / MOSI / MISO / SCK = 40 / 21 / 41 / 12`. The SPI defaults are mixed: SCK and MOSI are the LCD's, while SS and MISO are the SD card's. Do not rely on them.
8. A clean boot shows stale LCD GRAM if the backlight is switched on before the LCD init ([RK]).
9. Camera buffers and any full-screen framebuffer (240×320×2 = 150 KB) should go in PSRAM. DMA line buffers for the LCD need `MALLOC_CAP_DMA` internal RAM.

---

## 12. PlatformIO / platform recommendations

- **There is no K10 board in upstream espressif/arduino-esp32 variants, in pioarduino, or in platformio/platform-espressif32.** I checked the repository trees on 2026-09-24: the only DFRobot variants are Beetle, FireBeetle, Romeo and LoRaWAN.
- **DFRobot's own platform** (`https://github.com/DFRobot/platform-unihiker`, board `unihiker_k10`):
  - Based on the old Arduino-ESP32 2.0.x / IDF 4.4 (toolchain 8.4.0), with a precompiled closed SDK.
  - Board JSON: `flash_mode qio`, `f_flash 80 MHz`, `-DBOARD_HAS_PSRAM`, `partitions large_spiffs_16MB.csv`, `upload speed 460800`.
  - Fine for vendor-library projects, but not what we want.
- **Recommendation:** use **pioarduino** (Arduino core 3.x on IDF 5.x) with a custom board JSON based on `esp32-s3-devkitc-1` N16R8. Put it in `boards/unihiker_k10.json`:

```json
{
  "build": {
    "arduino": { "ldscript": "esp32s3_out.ld", "memory_type": "qio_opi", "partitions": "default_16MB.csv" },
    "core": "esp32",
    "extra_flags": ["-DARDUINO_UNIHIKER_K10", "-DBOARD_HAS_PSRAM", "-DARDUINO_USB_MODE=1", "-DARDUINO_USB_CDC_ON_BOOT=1"],
    "f_cpu": "240000000L",
    "f_flash": "80000000L",
    "flash_mode": "qio",
    "psram_type": "opi",
    "hwids": [["0x303A", "0x1001"]],
    "mcu": "esp32s3",
    "variant": "esp32s3"
  },
  "connectivity": ["wifi", "bluetooth"],
  "debug": { "openocd_target": "esp32s3.cfg" },
  "frameworks": ["arduino", "espidf"],
  "name": "DFRobot UNIHIKER K10 (ESP32-S3-WROOM-1-N16R8)",
  "upload": { "flash_size": "16MB", "maximum_ram_size": 327680, "maximum_size": 16777216, "require_upload_port": true, "speed": 921600 },
  "url": "https://www.dfrobot.com/product-2904.html",
  "vendor": "DFRobot"
}
```

  With this `platformio.ini`:

```ini
[env:k10]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = unihiker_k10
framework = arduino
board_build.partitions = default_16MB.csv   ; or a custom table with a big FAT/app
monitor_speed = 115200
lib_deps =
  espressif/esp32-camera          ; bundled in arduino-esp32 3.x as "esp32-camera" – check availability
  bodmer/TFT_eSPI or lovyan03/LovyanGFX  ; LovyanGFX recommended: runtime config, BGR, DMA
  adafruit/Adafruit NeoPixel
```

  - `esp32-camera` is included in the arduino-esp32 3.x build (`esp_camera.h`).
  - With LovyanGFX, configure `Panel_ILI9341`: `pin_cs=14, pin_rst=-1, pin_busy=-1`, `bus: spi_host=SPI2_HOST, pin_sclk=12, pin_mosi=21, pin_miso=-1, pin_dc=13, freq_write=40000000`, `rgb_order` / `invert` per §2. The backlight is handled by your own XL9535 code.
- **Fallback:** pure ESP-IDF 5.x via pioarduino `framework = espidf`, using `esp_lcd_ili9341`, `esp32-camera`, `esp_codec_dev` and `esp_io_expander_tca95xx_16bit`. [XZ] and [RK] show that exact stack working.

---

## 13. Open items to check on real hardware

1. SD pins 44/42/41/40, and whether the inverted font-ROM CS causes trouble with the IDF SDSPI driver. The sources conflict on this.
2. The ES7243E address (0x11 vs 0x15): scan 0x10–0x17.
3. Which I²S slot the NS4168 plays (L or R): send the same sample on both until this is known.
4. The polarity of the XL9535 P17 UserLed.
5. The LCD mirror/rotation flags, checked with a test pattern with USB-C at the top.
6. Camera vflip/hmirror defaults and the best stable XCLK (16 MHz vs 20 MHz) at each resolution.
7. Whether an accelerometer at 0x19 (SC7A20H) or 0x62 (MSA311) is fitted.
8. That there is really no charger on the battery path (inspect the U13 part marking).
