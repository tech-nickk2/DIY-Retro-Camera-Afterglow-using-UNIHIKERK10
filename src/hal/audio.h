// I2S0 full duplex at 16 kHz / 16-bit: NS4168 speaker amp out, ES7243E mic
// ADC in. The amp has no enable pin and its channel strap is unknown, so
// every output sample goes to both slots.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace audio {

constexpr int RATE = 16000;

bool begin();
bool speakerOk();
bool micOk();
uint8_t micAddr();

// Master volume for everything played, 0 = silent, 1 = full scale.
void setVolume(float v);
float volume();

// Blocking. vol 0..1.
void tone(float hz, int ms, float vol = 0.25f, bool square = true);
void play(const int16_t* mono, size_t n);
// Blocking read of n samples per mic (the K10 has two); right may be null.
// Returns how many samples were read.
size_t record(int16_t* left, int16_t* right, size_t n);

// Splits the mic gain between the analog PGA (0..30 dB, 3 dB steps) and the
// ADC's digital volume (0..32 dB, 0.5 dB steps). Analog gain is quieter:
// digital gain also amplifies the ADC's own noise floor.
void setMicGain(int analogDb, int digitalDb);

}  // namespace audio
