#include "kernel_blob.h"

size_t kernel_blob_size(void) {
    return (size_t)(kernel_bin_end - kernel_bin_start);
}

size_t hello_blob_size(void) {
    return (size_t)(hello_bin_end - hello_bin_start);
}
