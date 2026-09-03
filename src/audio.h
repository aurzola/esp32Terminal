#ifndef TERM_AUDIO_H
#define TERM_AUDIO_H

// Key-click audio (same scheme as esp32LanderS3/src/audio.cpp): LEDC PWM on
// TERM_CLICK_GPIO driven by a 16 kHz hardware-timer ISR that plays a short
// synthesized click (12 ms of shaped noise) on every key press. Hardware-bound
// (timer + LEDC registers): not unit-tested on the host.
namespace Audio {

void begin();
void click();

}

#endif