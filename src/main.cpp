// Afterglow — retro camera firmware for the DFRobot UNIHIKER K10.
// Hold A while powering on to get the hardware bring-up screen instead of
// the camera. Only A counts: it has a pull-up on the board, while the knob's
// switch reads as pressed if it is unwired or missing its pull-up.
#include <Arduino.h>
#include "app/bringup.h"
#include "app/camera_app.h"
#include "board/k10_pins.h"
#include "hal/i2c_bus.h"
#include "hal/xl9535.h"

static bool s_bringup = false;

void setup() {
  uint16_t in = 0xFFFF;
  if (i2cbus::begin() && xl9535::begin()) xl9535::readInputs(&in);
  s_bringup = !(in & (1u << K10_EXP_KEY_A));  // active low
  if (s_bringup) bringup::setup();
  else cameraApp::setup();
}

void loop() {
  if (s_bringup) bringup::loop();
  else cameraApp::loop();
}
