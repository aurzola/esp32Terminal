#include <Arduino.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "soc/lcd_cam_struct.h"
#include "soc/gpio_sig_map.h"
#include "esp_private/periph_ctrl.h"
#include "esp_private/gdma.h"
#include "hal/dma_types.h"
#include "esp_rom_gpio.h"
#include "esp_heap_caps.h"
#include "video_s3.h"

namespace {

constexpr int W = 320;
constexpr int H = 240;
constexpr int SAMPLES_PER_LINE = 390;
constexpr int LINES = 262;
constexpr int HALFLINE = SAMPLES_PER_LINE / 2;
constexpr int HSYNC_SAMPLES = 29;
constexpr int VSYNC_SHORT = 14;
constexpr int VSYNC_LONG = 166;
constexpr int OFFX = 59;
constexpr int OFFY = 11;
constexpr uint8_t CODE_SYNC = 0;
constexpr uint8_t CODE_BLACK = 19;
constexpr uint8_t CODE_WHITE = 63;
constexpr uint32_t PCLK_HZ = 6153846;

constexpr size_t FIELD_BYTES = (size_t)SAMPLES_PER_LINE * LINES;

const int DATA_PINS[8] = {4, 5, 6, 7, 15, 16, 40, 41};
constexpr int VIS_Y = OFFY - 1;

constexpr int DMA_CHUNKS = 26;
static_assert(FIELD_BYTES % DMA_CHUNKS == 0, "field must split evenly");
constexpr int DMA_CHUNK = FIELD_BYTES / DMA_CHUNKS;

uint8_t *fbShadow = nullptr;
// DMA field buffer. Allocated in PSRAM (the S3 GDMA can source from PSRAM):
// keeping this 100KB buffer out of BSS frees the internal DRAM the SSH task
// stack (ticket 05) needs. Diverges from the esp32LanderS3 verbatim copy.
uint8_t *fieldBuf = nullptr;
uint8_t lut[256];

dma_descriptor_t sDescs[DMA_CHUNKS];
gdma_channel_handle_t sDmaChan = nullptr;
TaskHandle_t sFrameTask = nullptr;
volatile uint32_t sLastComposeUs = 0;

void composeHalfPulse(uint8_t *row, int pulseWidth)
{
    memset(row, CODE_SYNC, pulseWidth);
    memset(row + pulseWidth, CODE_BLACK, HALFLINE - pulseWidth);
}

void composeVsyncLine(uint8_t *row, int firstWidth, int secondWidth)
{
    composeHalfPulse(row, firstWidth);
    composeHalfPulse(row + HALFLINE, secondWidth);
}

void composeStaticLines(void)
{
    for (int l = 0; l < LINES; l++) {
        if (l >= VIS_Y && l < VIS_Y + H) {
            continue;
        }
        uint8_t *row = fieldBuf + (size_t)l * SAMPLES_PER_LINE;
        if (l < 3 || (l >= 6 && l < 9)) {
            composeVsyncLine(row, VSYNC_SHORT, VSYNC_SHORT);
        } else if (l < 6) {
            composeVsyncLine(row, VSYNC_LONG, VSYNC_LONG);
        } else {
            memset(row, CODE_SYNC, HSYNC_SAMPLES);
            memset(row + HSYNC_SAMPLES, CODE_BLACK, SAMPLES_PER_LINE - HSYNC_SAMPLES);
        }
    }
}

void composeField(void)
{
    uint64_t t0 = esp_timer_get_time();
    for (int l = VIS_Y; l < VIS_Y + H; l++) {
        uint8_t *row = fieldBuf + (size_t)l * SAMPLES_PER_LINE;
        memset(row, CODE_SYNC, HSYNC_SAMPLES);
        memset(row + HSYNC_SAMPLES, CODE_BLACK, SAMPLES_PER_LINE - HSYNC_SAMPLES);
        const uint8_t *src = fbShadow + (size_t)(l - VIS_Y) * W;
        uint8_t *dst = row + OFFX;
        for (int x = 0; x < W; x++) {
            dst[x] = lut[src[x]];
        }
    }
    sLastComposeUs = (uint32_t)(esp_timer_get_time() - t0);
}

bool IRAM_ATTR dmaEofIsr(gdma_channel_handle_t, gdma_event_data_t *, void *)
{
    BaseType_t hpw = pdFALSE;
    if (sFrameTask) {
        vTaskNotifyGiveFromISR(sFrameTask, &hpw);
    }
    return hpw == pdTRUE;
}

void buildDescriptors(void)
{
    for (int i = 0; i < DMA_CHUNKS; i++) {
        sDescs[i].dw0.size = DMA_CHUNK;
        sDescs[i].dw0.length = DMA_CHUNK;
        sDescs[i].dw0.suc_eof = (i == DMA_CHUNKS - 1);
        sDescs[i].dw0.owner = 1;
        sDescs[i].buffer = fieldBuf + (size_t)i * DMA_CHUNK;
        sDescs[i].next = &sDescs[(i + 1) % DMA_CHUNKS];
    }
}

void lcdCamInit(void)
{
    periph_module_enable(PERIPH_LCD_CAM_MODULE);

    for (int i = 0; i < 8; i++) {
        int pin = DATA_PINS[i];
        if (pin < 0) {
            continue;
        }
        pinMode(pin, OUTPUT);
        esp_rom_gpio_connect_out_signal(pin, LCD_DATA_OUT0_IDX + i, false, false);
    }

    LCD_CAM.lcd_user.lcd_reset = 1;
    LCD_CAM.lcd_misc.lcd_afifo_reset = 1;

    LCD_CAM.lcd_clock.val = 0;
    LCD_CAM.lcd_clock.clk_en = 1;
    LCD_CAM.lcd_clock.lcd_clk_sel = 3;
    LCD_CAM.lcd_clock.lcd_clkm_div_num = 25;
    LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 1;

    LCD_CAM.lcd_ctrl.val = 0;
    LCD_CAM.lcd_ctrl.lcd_va_height = LINES;
    LCD_CAM.lcd_ctrl.lcd_vt_height = LINES;
    LCD_CAM.lcd_ctrl1.val = 0;
    LCD_CAM.lcd_ctrl1.lcd_ht_width = SAMPLES_PER_LINE;
    LCD_CAM.lcd_ctrl1.lcd_ha_width = SAMPLES_PER_LINE;

    LCD_CAM.lcd_misc.val = 0;
    LCD_CAM.lcd_misc.lcd_next_frame_en = 1;

    LCD_CAM.lcd_user.val = 0;
    LCD_CAM.lcd_user.lcd_dout = 1;
    LCD_CAM.lcd_user.lcd_always_out_en = 1;
    LCD_CAM.lcd_user.lcd_dout_cyclelen = 4095;
}

} // namespace

uint8_t *video_get_frame_buffer_address(void) { return fbShadow; }
int video_width(void) { return W; }
int video_height(void) { return H; }
uint32_t video_last_compose_us(void) { return sLastComposeUs; }

const uint8_t *video_field_row(int line)
{
    if (line < 0 || line >= LINES) {
        return nullptr;
    }
    return fieldBuf + (size_t)line * SAMPLES_PER_LINE;
}

void video_wait_frame(void)
{
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    composeField();
}

void video_pause(void)
{
    // Halt the NTSC field DMA and LCD_CAM refresh so the BLE controller is
    // not starved during latency-sensitive link work (connect + GATT
    // discovery). The signal simply freezes on the last field.
    if (sDmaChan == nullptr) {
        return;
    }
    LCD_CAM.lcd_user.lcd_start = 0;
    LCD_CAM.lcd_user.lcd_update = 0;
    gdma_stop(sDmaChan);
}

void video_resume(void)
{
    if (sDmaChan == nullptr) {
        return;
    }
    buildDescriptors();
    gdma_start(sDmaChan, (intptr_t)&sDescs[0]);
    delay(10);
    LCD_CAM.lcd_user.lcd_update = 1;
    LCD_CAM.lcd_user.lcd_start = 1;
}

void video_graphics_s3(void)
{
    fbShadow = (uint8_t *)heap_caps_malloc((size_t)W * H, MALLOC_CAP_SPIRAM);
    if (fbShadow == nullptr) {
        Serial.printf("[video_s3] ERROR: no PSRAM para framebuffer\n");
        return;
    }

    // DMA field buffer in PSRAM: the S3 GDMA sources from PSRAM directly.
    // Allocate 16-byte aligned for the DMA descriptors.
    uint8_t *raw = (uint8_t *)heap_caps_malloc(
        (size_t)FIELD_BYTES + 15, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (raw == nullptr) {
        Serial.printf("[video_s3] ERROR: no PSRAM para fieldBuf\n");
        return;
    }
    fieldBuf = (uint8_t *)(((uintptr_t)raw + 15) & ~(uintptr_t)15);

    for (int v = 0; v < 256; v++) {
        lut[v] = CODE_BLACK + (uint8_t)(((uint32_t)v * (CODE_WHITE - CODE_BLACK)) / 255);
    }

    memset(fieldBuf, CODE_BLACK, FIELD_BYTES);
    composeStaticLines();
    memset(fbShadow, 0, (size_t)W * H);

    lcdCamInit();

    gdma_channel_alloc_config_t acfg = {};
    acfg.direction = GDMA_CHANNEL_DIRECTION_TX;
    esp_err_t err = gdma_new_channel(&acfg, &sDmaChan);
    if (err != ESP_OK) {
        Serial.printf("[video_s3] gdma_new_channel err=%d\n", err);
        return;
    }
    err = gdma_connect(sDmaChan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));
    if (err != ESP_OK) {
        Serial.printf("[video_s3] gdma_connect err=%d\n", err);
        return;
    }
    gdma_tx_event_callbacks_t cbs = {};
    cbs.on_trans_eof = dmaEofIsr;
    err = gdma_register_tx_event_callbacks(sDmaChan, &cbs, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[video_s3] gdma_register_cbs err=%d\n", err);
        return;
    }

    sFrameTask = xTaskGetCurrentTaskHandle();
    buildDescriptors();
    err = gdma_start(sDmaChan, (intptr_t)&sDescs[0]);
    if (err != ESP_OK) {
        Serial.printf("[video_s3] gdma_start err=%d\n", err);
        return;
    }
    delay(10);
    LCD_CAM.lcd_user.lcd_update = 1;
    LCD_CAM.lcd_user.lcd_start = 1;

    Serial.printf("[video_s3] NTSC ring %dx%d pclk=%u field=%uB chunks=%dx%d\n",
                  W, H, (unsigned)PCLK_HZ, (unsigned)FIELD_BYTES,
                  DMA_CHUNKS, DMA_CHUNK);
}
