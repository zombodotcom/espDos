#include "esp_err.h"
#include "display.h"

/* Real init, timer, and dispatch land in Task 11. */

esp_err_t display_init(void) {
    return 0;
}

void display_putc(uint8_t ch) {
    (void)ch;
}

void display_set_program(const char *name) {
    (void)name;
}

void display_set_beat(uint32_t beat) {
    (void)beat;
}
