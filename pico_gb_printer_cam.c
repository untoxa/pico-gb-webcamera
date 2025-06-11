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
int32_t receive_counter;
bool receiving_frame = false;
volatile bool image_received = false;
uint16_t x, y, dy;

void __not_in_flash_func(receive_data_reset)(void) { }
void __not_in_flash_func(receive_data_init)(void) { }

void __not_in_flash_func(receive_data_command)(uint8_t b) {
    if (receiving_frame = (b == CAM_COMMAND_TRANSFER)) LED_ON;
    receive_counter = 0;
    x = y = dy = 0;
}

void __not_in_flash_func(receive_data_write)(uint8_t b) {
    static uint8_t l, h;
    // if not "transfer image" then exit
    if (!receiving_frame) return;
    // skip header bytes
    if (++receive_counter < 4) return;
    // collect bitplanes data
    if (receive_counter & 0x01) h = b; else l = b;
    // if receive_counter is odd then exit
    if ((receive_counter & 0x01) == 0) return;
    // calculate destination
    uint16_t * dest = image + ((y + dy) * FRAME_WIDTH) + (x * TILE_WIDTH);
    // decode bitplanes
    static uint16_t colors[4] = { 0x80FF, 0x8055, 0x80AA, 0x8000 };
    *dest++ = colors[((l >> 7) & 0x01) | ((h >> 6) & 0x02)];
    *dest++ = colors[((l >> 6) & 0x01) | ((h >> 5) & 0x02)];
    *dest++ = colors[((l >> 5) & 0x01) | ((h >> 4) & 0x02)];
    *dest++ = colors[((l >> 4) & 0x01) | ((h >> 3) & 0x02)];
    *dest++ = colors[((l >> 3) & 0x01) | ((h >> 2) & 0x02)];
    *dest++ = colors[((l >> 2) & 0x01) | ((h >> 1) & 0x02)];
    *dest++ = colors[((l >> 1) & 0x01) | ((h >> 0) & 0x02)];
    *dest   = colors[((l >> 0) & 0x01) | ((h << 1) & 0x02)];
    // fix coordinates                     
    if (++dy == TILE_HEIGHT) {
        dy = 0;
        if (++x == (FRAME_WIDTH / TILE_WIDTH)) {
            x = 0;
            if ((y += TILE_HEIGHT) >= FRAME_HEIGHT) y = 0;
        }
    }
}

void __not_in_flash_func(receive_data_commit)(uint8_t cmd) {
    if (receiving_frame) image_received = true;
    receiving_frame = false;
    LED_OFF;
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

int64_t frame_rate_callback(alarm_id_t id, void *user_data) {
    image_received  = true;
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

    // main loop
    while (true) {
        // tinyusb device task
        tud_task();
        // send prepared image or duplicate previous to match the frame rate
        if (image_received) {
            // check streaming is active
            if (tud_video_n_streaming(0, 0)) {
                // check that the last frame is not still in transfer, if not - transfer the current frame
                if (!in_transfer) tud_video_n_frame_xfer(0, 0, (void*)image, sizeof(image)), in_transfer = true;
            } else in_transfer = false;
            image_received = false;
        }
    }
}
