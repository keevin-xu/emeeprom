#ifndef MTB_BLOCK_STORAGE_H
#define MTB_BLOCK_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "cy_result.h"

typedef struct mtb_block_storage mtb_block_storage_t;

struct mtb_block_storage
{
    void* context;
    cy_rslt_t (*read)(void* context, uintptr_t addr, uint32_t size, uint8_t* data);
    cy_rslt_t (*program)(void* context, uintptr_t addr, uint32_t size, const uint8_t* data);
    cy_rslt_t (*erase)(void* context, uintptr_t addr, uint32_t size);
    bool (*is_in_range)(void* context, uintptr_t addr, uint32_t size);
    bool (*is_erase_required)(void* context, uintptr_t addr, uint32_t size);
    uint32_t (*get_erase_size)(void* context, uintptr_t addr);
    uint32_t (*get_program_size)(void* context, uintptr_t addr);
};

cy_rslt_t mtb_block_storage_nvm_create(mtb_block_storage_t* block_device);
cy_rslt_t mtb_block_storage_cat2_create(mtb_block_storage_t* block_device);

#endif
