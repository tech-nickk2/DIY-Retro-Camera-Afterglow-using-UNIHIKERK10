// Hardware bring-up screen: checks every subsystem, then shows the live
// camera with sensor readouts and lets each button exercise one part.
//
//   A        take a full-resolution photo to /sd/DCIM (camera, JPEG, SD)
//   B        next mic gain split, analog vs digital (saved)
//   hold A   record 3 s from the mic and play it back (mic, speaker, SD)
//   hold B   rotate the screen 180 degrees (saved)
//   A+B      next viewfinder size, to compare sharpness and FPS (saved)
#pragma once

namespace bringup {
void setup();
void loop();
}
