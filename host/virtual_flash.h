#ifndef VIRTUAL_FLASH_H
#define VIRTUAL_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mtb_block_storage.h"

typedef struct
{
    uint8_t* mem;
    uintptr_t base_addr;
    uint32_t mapped_size;
    uint32_t flash_size;
    uint32_t program_size;
    uint32_t erase_size;
    uint8_t erased_value;
    unsigned read_count;
    unsigned program_count;
    unsigned erase_count;
    bool fail_next_program;
    bool fail_next_erase;
} virtual_flash_t;

cy_rslt_t virtual_flash_init(
    virtual_flash_t* flash,
    uint32_t flash_size,
    uint32_t program_size,
    uint32_t erase_size);
void virtual_flash_deinit(virtual_flash_t* flash);
void virtual_flash_reset(virtual_flash_t* flash);
void virtual_flash_make_block_device(virtual_flash_t* flash, mtb_block_storage_t* block_device);

#endif
