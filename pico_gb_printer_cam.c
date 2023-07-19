#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include "globals.h"
#include "linkcable.h"
#include "gb_printer.h"

// Length of the pixel array and number of DMA transfers
#define RXCOUNT     (FRAME_WIDTH * FRAME_HEIGHT)

// image array and dump routine
uint16_t image[RXCOUNT];

// storage implementation
volatile uint32_t receive_data_pointer = 0;
uint8_t receive_data[BUFFER_SIZE_KB * 1024];
bool receiving_frame = false;
volatile bool image_received = false;

void receive_data_reset(void) {
    receive_data_pointer = 0;
}

void receive_data_command(uint8_t b) {
    if (receiving_frame = (b == CAM_COMMAND_TRANSFER)) LED_ON;
    receive_data_pointer = 0;
}

void receive_data_write(uint8_t b) {
    if (!receiving_frame) return;
    if (receive_data_pointer < sizeof(receive_data))
         receive_data[receive_data_pointer++] = b;
}

void receive_data_commit(void) {
    if (receiving_frame) image_received = true;
    receiving_frame = false;
    LED_OFF;
}

// image converter
inline void convert_tile_row(uint32_t x, uint32_t y, uint8_t a, uint8_t b) {
    static const uint16_t colors[2][2] = {{0x80FF, 0x8055}, {0x80AA, 0x8000}};
    uint16_t * dest = image + ((y * FRAME_WIDTH) + x);
    *dest++ = colors[(a >> 7) & 0x01][(b >> 7) & 0x01];
    *dest++ = colors[(a >> 6) & 0x01][(b >> 6) & 0x01];
    *dest++ = colors[(a >> 5) & 0x01][(b >> 5) & 0x01];
    *dest++ = colors[(a >> 4) & 0x01][(b >> 4) & 0x01];
    *dest++ = colors[(a >> 3) & 0x01][(b >> 3) & 0x01];
    *dest++ = colors[(a >> 2) & 0x01][(b >> 2) & 0x01];
    *dest++ = colors[(a >> 1) & 0x01][(b >> 1) & 0x01];
    *dest   = colors[(a >> 0) & 0x01][(b >> 0) & 0x01];
}

void convert_image(void) {
    uint8_t * src = receive_data + 3;

    for (uint32_t y = 0; y != 14; y++) {
        for (uint32_t x = 0; x != 16; x++) {
            for (uint32_t t = 0; t != 8; t++) {
                uint8_t  a = *src++, b = *src++;
                convert_tile_row(x << 3, (y << 3) + t, a, b);
            }
        }
    }
}

// link cable
bool link_cable_data_received = false;
void link_cable_ISR(void) {
    linkcable_send(protocol_data_process(linkcable_receive()));
    link_cable_data_received = true;
}

int64_t link_cable_watchdog(alarm_id_t id, void *user_data) {
    if (!link_cable_data_received) {
        linkcable_reset();
        protocol_reset();
    } else link_cable_data_received = false;
    return MS(300);
}

// usb camera
volatile bool in_transfer = false;
void tud_video_frame_xfer_complete_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx) {
    (void)ctl_idx; (void)stm_idx;

    in_transfer = false;
}

int tud_video_commit_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx, video_probe_and_commit_control_t const *parameters) {
    (void)ctl_idx; (void)stm_idx;

//    interval_ms = parameters->dwFrameInterval / 10000;
    return VIDEO_ERROR_NONE;
}

volatile bool camera_send_text_frame = false;

int64_t frame_rate_callback(alarm_id_t id, void *user_data) {
    camera_send_text_frame  = true;
    return MS(1000 / FRAME_RATE);
}

int main(void) {
    // led
#ifdef LED_PIN
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
#endif

    // initialize linc cable
    linkcable_init(link_cable_ISR);
    add_alarm_in_us(MS(300), link_cable_watchdog, NULL, true);
    add_alarm_in_us(MS(1000 / FRAME_RATE), frame_rate_callback, NULL, true);

    // initialize TinyUSB
    tud_init(BOARD_TUD_RHPORT);

    while (true) {
        // tinyusb device task
        tud_task();
        // send frame if ready and connected
        if (image_received) {
            // convert image
            convert_image();
            image_received = false;
            camera_send_text_frame = true;
        }
        // send prepared image or duplicate previous to match the frame rate
        if (camera_send_text_frame) {
            // check streaming is active
            if (tud_video_n_streaming(0, 0)) {
                // check that the last frame is not still in transfer, if not - transfer the current frame
                if (!in_transfer) tud_video_n_frame_xfer(0, 0, (void*)image, sizeof(image)), in_transfer = true;
            } else in_transfer = false;
            camera_send_text_frame = false;
        }
    }
}
