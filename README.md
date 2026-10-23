# Afterglow

Retro camera firmware for the DFRobot UNIHIKER K10: film rolls you wind by hand, a darkroom that develops each shot, instant prints, and Super 8 / camcorder video.

▶️ **[Click here to watch the DIY Retro Camera video on YouTube](https://youtu.be/rEtMDax1rDs)** 

[![DIY Retro Camera (Afterglow)](https://img.youtube.com/vi/rEtMDax1rDs/maxresdefault.jpg)](https://youtu.be/rEtMDax1rDs)

<img width="4000" height="3000" alt="20261022_170730" src="https://github.com/user-attachments/assets/136f364f-b48a-43f6-8a94-a314696d1c3e" />

https://youtu.be/rEtMDax1rDs

- `mockup/afterglow.html` is the clickable UI prototype that the firmware follows.
- `docs/k10_hardware.md` is the board's pin map, with sources and confidence levels.

## Status: stage 2, step 4 — instant packs, cine mode, delete

The camera boots into a live viewfinder with the loaded film's look and the mockup's overlays. Rolls, packs, settings, photos and clips all survive power-off.

**Photos:** **A** opens the shutter (black screen, first curtain click; the second click comes when the frame is captured, about 1.5 s later). A roll film print develops in the darkroom tray; an instant print ejects and develops out of a blue-grey haze. Photos are saved to `/sd/DCIM/AFT_NNNN.JPG` (1200×900, instant 900×900). **B** winds a roll on; instant packs need no winding and count down 10 shots.

**Cine:** press-and-turn the knob (or **A+B**) to switch between photo and cine. Press to record, turn to change the look. Clips are MJPEG `.AVI` files (240×180) in `/sd/DCIM`, and play on a computer or in the album.

| Look | What it does |
|---|---|
| SUPER 8 | Gate weave, flicker, dust, rounded gate; counts feet of a 50 ft cartridge (3 min 20 s) |
| CAMCORDER | Scanlines, colour bleed, tracking glitches, date burned in |
| SILENT FILM | Sepia, heavy flicker and scratches, no sound, plays back 1.4× fast |
| CCTV | Grey-green, records every other frame, CAM 01 and a running timestamp burned in |
| POCKET CAM | Four greens with ordered dither, like an early handheld console camera |
| WEBCAM | Oversaturated, chunky pixels, heavy JPEG compression |

**VIEWFINDER** in settings: WIDE (default) shows exactly the field of view the photo captures, from a custom 1024×1200 sensor window shrunk 4×; SHARP is the older 640×480 view, crisper but only 80% of the photo's view. In cine mode it offers WIDE (the photo's full view, about 6 fps) or SMOOTH (zoomed in to about 40% of the view, up to about 17 fps); both record 240×180.

**EXPOSURE** in settings shifts the auto exposure by half stops (−1.0 to +1.0). Photos start from the exposure the viewfinder has already settled on, so they come out right outdoors too.

### Wi-Fi transfer (and setting the clock)

**Settings → WI-FI TRANSFER** turns the camera into its own Wi-Fi network (no router or internet needed). The camera pauses while it is on.

1. Scan the first QR code with your phone's camera to join **AFTERGLOW-XXXX** (the password is on screen; it stays the same for this camera).
2. Press the knob for the second QR code, or open **http://192.168.4.1** in the phone's browser.
3. The gallery page shows every photo and clip with **View** and **Save** buttons. Opening it also sends the phone's time and time zone to the camera, which sets the clock ("CLOCK SET" on screen).
4. Hold the knob to switch Wi-Fi off and go back to shooting.

Phones may warn that the network has no internet; choose to stay connected. Clips are MJPEG AVI: they play in VLC and on most computers, but some phones only save them.

### The knob (KY-040 rotary encoder)

| KY-040 pin | K10 pin |
|---|---|
| CLK | **P0** (GPIO1) |
| DT | **P1** (GPIO2) |
| SW | **P2** (edge connector) plus a **10 kΩ resistor from P2 to 3V3** |
| + | **3V3** (never 5 V) |
| GND | **GND** |

P0 and P1 are on the small 3-pin Gravity sockets. P2 is only on the edge connector, so you need a breakout. Without the 10 kΩ pull-up, the knob's switch pin floats and the camera sees phantom presses.

| | Turn | Press | Hold | Press + turn |
|---|---|---|---|---|
| **Photo** | Right: wind on (5 clicks a frame). Left at the end of a roll: crank the rewind | Shutter; load film when empty; new pack when an instant pack is empty | Settings | Cine mode |
| **Cine** | Right: camcorder, left: Super 8 | Record / stop | Settings | Photo mode |
| **Menu** | Scroll | Change | Close | |
| **Film shelf** | Browse | Load | Cancel | |
| **Album** | Scroll | Open / play | Back | Delete |
| **Full photo** | Previous / next | Delete | Back to the strip | |
| **Delete?** | KEEP ↔ DELETE | OK | Keep | |
| **Set date** | Change the value | Next field | Save | |

If the knob turns the wrong way, set **KNOB** to REVERSED in settings. If it needs two clicks per step, set **KNOB SPEED** to 2X.

### The flash (white LED on edge P8)

Edge **P8** is an expander pin (P10). It can switch a transistor but can't power a bright LED itself, so:

```
P8 ──1kΩ──┬── base    NPN (2N2222, BC337, S8050)
          10kΩ        emitter ── GND
          │           collector ── LED cathode (−)
         GND          LED anode (+) ── resistor ── supply
```

- **Supply 5 V** (if your breakout brings out 5 V or VIN): a 5 mm white LED with **100 Ω** gives about 18 mA.
- **Supply 3V3 only:** a white LED needs about 3.0–3.2 V, which leaves almost nothing across the resistor. Use a **10 Ω** resistor and expect it to be dim, or use a ready-made LED module that runs on 3.3 V.
- The 10 kΩ from base to GND keeps the LED off while the camera boots.

**FLASH** in settings turns it on for photos: the LED stays lit through the whole exposure (about 1.5 s) so the auto exposure sees the light, then the flash "charges" (you hear the whine and the ⚡ in the viewfinder blinks) and the shutter waits about 2 s before the next flash shot. The K10's RGB LEDs also light white during a flash shot. In cine mode, **VIDEO LIGHT** keeps the LED on while filming.

### Backup buttons (A and B)

The K10's own buttons still work as before:

| Viewfinder | Photo mode | Cine mode |
|---|---|---|
| **A** | Shutter (or self-timer) | Record / stop |
| **B** | Wind on; rewind at the end of the roll; new pack when empty | Super 8 ↔ camcorder |
| **Hold B** | Settings | Settings |
| **Hold A** | Album | Album |
| **Hold A+B** | Cine mode | Photo mode |

**Settings** (A next, B change, hold B close, tilt to scroll): film (opens the shelf), date stamp, self-timer, LED flash, winding, roll length, volume, set date, info overlay, album. In cine mode: look, date stamp, volume, set date, album.

**Album:** photos and clips as a contact strip, newest first. A next, hold A open or play, B back, **hold A+B delete** (asks first; A deletes, B keeps).

Hold **A** while powering on to get the hardware bring-up screen, which shows the knob's switch (K) and its raw count (about 4 per click).

### Bring-up screen

The firmware checks every part of the board, then shows the live camera with sensor readouts. Each button tests one part.

| Button | Tests |
|---|---|
| **A** | Takes a photo to `/sd/DCIM/IMG_NNNN.JPG` and shows how long each step took (camera, rotation, JPEG encode, SD card). The sensor is mounted a quarter turn from the landscape grip, so photos are rotated and cropped to 1200×900. |
| **B** | Cycles the mic gain split (analog / digital dB): +6/+32, +18/+18, +24/+12, +30/+6 (saved). Picture orientation is saved from earlier testing. |
| **Hold A** | Records 3 s from both mics, shows each mic's peak and noise floor, plays the mix back and saves `MIC_NNNN.WAV` |
| **Hold B** | Rotates the screen 180° for USB-C on the left or the right (saved) |
| **Hold A+B** | Restarts the camera with the next viewfinder size (320×240, 400×296, 480×320, 640×480) to compare sharpness against FPS (saved) |

## Build and flash

You need PlatformIO (the VS Code extension or `pio` on the command line). The first build downloads the ESP32 toolchain.

```
pio run -t upload
pio device monitor
```

If the upload doesn't start, hold **BOOT**, tap **RST**, then release **BOOT**.

## Bring-up checklist

1. **Boot screen:** every line should say OK. A WARN on SD CARD just means no card is inserted.
2. **Orientation:** hold the K10 the way you want to hold the camera.
   - The amber arrow should sit in the top-left corner and the text should read normally.
   - If it's upside down, hold **B**.
3. **Camera picture:** if it's noisy or scratchy, hold **A+B** to step to the next camera setting (shown top right as `CLK`) until the picture is clean and the colours are right. Then press **B** until the picture isn't mirrored or upside down.
4. **Photo:** insert a FAT32 card and press **A**. The pop-up shows the resolution, the file size and the time for each step.
5. **Mic:** hold **A**, talk for 3 seconds, and listen to the playback. The pop-up shows the peak and RMS level.

Send back the serial log (`pio device monitor`), a photo of the screen, and the timings. Those numbers decide the capture resolution and the preview frame rate for the real camera app.

## Layout

```
src/board/   pin map
src/hal/     drivers: i2c_bus, xl9535, buttons, display, camera, sdcard, audio, leds, sensors
src/gfx/     canvas (320x240 frame in PSRAM) and the mockup's 5x7 pixel font
src/app/     bring-up app (the camera app replaces this in stage 2)
```

Two design rules:
- All I²C goes through `i2cbus` on the ESP-IDF 5 driver, and the camera attaches to the same bus by port number. Don't call `Wire.begin()` on GPIO47/48: two I²C drivers on one bus crash the chip at boot.
- Canvas pixels are stored in panel byte order. That's also the camera's order, so a frame goes from camera to film look to screen with no conversion. Build colours with `gfx::rgb()`.
