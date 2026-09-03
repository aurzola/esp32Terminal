#ifndef VIDEO_S3_H
#define VIDEO_S3_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void video_graphics_s3(void);
uint8_t *video_get_frame_buffer_address(void);
int video_width(void);
int video_height(void);
void video_wait_frame(void);
uint32_t video_last_compose_us(void);
const uint8_t *video_field_row(int line);
void video_pause(void);
void video_resume(void);

#ifdef __cplusplus
}
#endif

#endif
