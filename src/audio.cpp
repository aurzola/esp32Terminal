#include <Arduino.h>

#include "audio.h"
#include "config.h"
#include "soc/ledc_struct.h"

#define AUDIO_PIN TERM_CLICK_GPIO
#define AUDIO_LEDC_CHANNEL 0
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_PWM_FREQ 312500
#define AUDIO_PWM_RESOLUTION 8

// Click length in samples (16 kHz): 12 ms.
#define CLICK_SAMPLES 192

// Idle marker: any position >= CLICK_SAMPLES means "no click playing".
#define CLICK_IDLE 0xFFFF

static hw_timer_t *audioTimer = NULL;
static uint8_t clickBuf[CLICK_SAMPLES];
static volatile uint16_t clickPos = CLICK_IDLE;

// 16 kHz ISR: play the click sample if active, else silence (mid-duty). Writes
// the LEDC duty register directly (IRAM-safe, no FPU), as in the Lander audio.
static void IRAM_ATTR audioIsr()
{
    int32_t v = 0;
    if (clickPos < CLICK_SAMPLES) {
        v = (int32_t)clickBuf[clickPos] - 128;
        clickPos++;
        if (clickPos >= CLICK_SAMPLES) {
            clickPos = CLICK_IDLE;
        }
    }
    if (v < -128) {
        v = -128;
    } else if (v > 127) {
        v = 127;
    }
    uint32_t duty = (uint32_t)((uint8_t)(v + 128)) << 4;
    ledc_dev_t *hw = &LEDC;
    hw->channel_group[LEDC_LOW_SPEED_MODE].channel[0].duty.duty = duty;
    hw->channel_group[LEDC_LOW_SPEED_MODE].channel[0].conf1.val =
        hw->channel_group[LEDC_LOW_SPEED_MODE].channel[0].conf1.val | (1U << 31);
    hw->channel_group[LEDC_LOW_SPEED_MODE].channel[0].conf0.low_speed_update = 1;
}

void Audio::begin()
{
    Serial.printf("[audio] click begin (pin %d)\n", AUDIO_PIN);
    ledcSetClockSource(LEDC_USE_APB_CLK);
    bool ok = ledcAttachChannel(AUDIO_PIN, AUDIO_PWM_FREQ, AUDIO_PWM_RESOLUTION,
                                AUDIO_LEDC_CHANNEL);
    ledcWriteChannel(AUDIO_LEDC_CHANNEL, 128); // mid-duty = silence
    Serial.printf("[audio] ledcAttachChannel: %d\n", (int)ok);

    // Synthesize the click: high-passed noise (sharp "tick" character) with a
    // linear decay over 12 ms, generated once into internal RAM.
    uint32_t rng = 0x9E3779B9u;
    int prev = 0;
    for (int i = 0; i < CLICK_SAMPLES; i++) {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        int n = (int)(rng & 0xFF) - 128;
        int hp = n - prev; // high-pass: crisper tick
        prev = n;
        int env = (CLICK_SAMPLES - i) << 3; // 8 at attack -> 0 at the tail
        int s = hp * env >> 8;
        if (s < -128) {
            s = -128;
        } else if (s > 127) {
            s = 127;
        }
        clickBuf[i] = (uint8_t)(s + 128);
    }

    audioTimer = timerBegin(AUDIO_SAMPLE_RATE);
    timerAttachInterrupt(audioTimer, audioIsr);
    timerAlarm(audioTimer, 1, true, 0);
    Serial.println("[audio] timer started");
}

void Audio::click()
{
    clickPos = 0;
}