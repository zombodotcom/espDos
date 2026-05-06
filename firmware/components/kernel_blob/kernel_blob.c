#include "kernel_blob.h"

size_t kernel_blob_size(void) {
    return (size_t)(kernel_bin_end - kernel_bin_start);
}
