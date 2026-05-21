#include "virtual_flash.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "cy_syslib.h"

#if !defined(_WIN32) && !defined(MAP_ANON)
#define MAP_ANON MAP_ANONYMOUS
#endif

#define VIRTUAL_FLASH_ERROR CY_RSLT_CREATE(CY_RSLT_TYPE_ERROR, 0x76u, 1u)

static uint32_t round_up(uint32_t value, uint32_t alignment)
{
    return ((value + alignment - 1u) / alignment) * alignment;
}

static bool vf_is_in_range(void* context, uintptr_t addr, uint32_t size)
{
    const virtual_flash_t* flash = (const virtual_flash_t*)context;

    if ((flash == NULL) || (size > flash->flash_size))
    {
        return false;
    }

    if (addr < flash->base_addr)
    {
        return false;
    }

    return ((addr - flash->base_addr) <= (flash->flash_size - size));
}

static cy_rslt_t vf_read(void* context, uintptr_t addr, uint32_t size, uint8_t* data)
{
    virtual_flash_t* flash = (virtual_flash_t*)context;

    if ((!vf_is_in_range(context, addr, size)) || (data == NULL))
    {
        return VIRTUAL_FLASH_ERROR;
    }

    memcpy(data, flash->mem + (addr - flash->base_addr), size);
    flash->read_count++;
    return CY_RSLT_SUCCESS;
}

static cy_rslt_t vf_program(void* context, uintptr_t addr, uint32_t size, const uint8_t* data)
{
    virtual_flash_t* flash = (virtual_flash_t*)context;

    if ((!vf_is_in_range(context, addr, size)) || (data == NULL))
    {
        return VIRTUAL_FLASH_ERROR;
    }

    if (flash->fail_next_program)
    {
        flash->fail_next_program = false;
        return VIRTUAL_FLASH_ERROR;
    }

    memcpy(flash->mem + (addr - flash->base_addr), data, size);
    flash->program_count++;
    return CY_RSLT_SUCCESS;
}

static cy_rslt_t vf_erase(void* context, uintptr_t addr, uint32_t size)
{
    virtual_flash_t* flash = (virtual_flash_t*)context;

    if ((!vf_is_in_range(context, addr, size)) || ((size % flash->erase_size) != 0u))
    {
        return VIRTUAL_FLASH_ERROR;
    }

    if (flash->fail_next_erase)
    {
        flash->fail_next_erase = false;
        return VIRTUAL_FLASH_ERROR;
    }

    memset(flash->mem + (addr - flash->base_addr), flash->erased_value, size);
    flash->erase_count++;
    return CY_RSLT_SUCCESS;
}

static bool vf_is_erase_required(void* context, uintptr_t addr, uint32_t size)
{
    const virtual_flash_t* flash = (const virtual_flash_t*)context;
    uint32_t offset;

    if (!vf_is_in_range(context, addr, size))
    {
        return true;
    }

    offset = addr - flash->base_addr;
    for (uint32_t i = 0; i < size; ++i)
    {
        if (flash->mem[offset + i] != flash->erased_value)
        {
            return true;
        }
    }

    return false;
}

static uint32_t vf_get_erase_size(void* context, uintptr_t addr)
{
    CY_UNUSED_PARAMETER(addr);
    return ((virtual_flash_t*)context)->erase_size;
}

static uint32_t vf_get_program_size(void* context, uintptr_t addr)
{
    CY_UNUSED_PARAMETER(addr);
    return ((virtual_flash_t*)context)->program_size;
}

cy_rslt_t virtual_flash_init(
    virtual_flash_t* flash,
    uint32_t flash_size,
    uint32_t program_size,
    uint32_t erase_size)
{
    uint32_t page_size;

    if ((flash == NULL) || (flash_size == 0u) || (program_size == 0u) || (erase_size == 0u))
    {
        return VIRTUAL_FLASH_ERROR;
    }

    if (((erase_size % program_size) != 0u) || ((flash_size % erase_size) != 0u))
    {
        return VIRTUAL_FLASH_ERROR;
    }

    memset(flash, 0, sizeof(*flash));
    flash->program_size = program_size;
    flash->erase_size = erase_size;
    flash->flash_size = flash_size;
    flash->erased_value = 0u;

#if defined(_WIN32)
    {
        SYSTEM_INFO system_info;
        GetSystemInfo(&system_info);
        page_size = (uint32_t)system_info.dwPageSize;
    }
#else
    {
        long host_page_size = sysconf(_SC_PAGESIZE);
        if (host_page_size <= 0)
        {
            return VIRTUAL_FLASH_ERROR;
        }
        page_size = (uint32_t)host_page_size;
    }
#endif

    if (page_size == 0u)
    {
        return VIRTUAL_FLASH_ERROR;
    }

    flash->mapped_size = round_up(flash_size, (uint32_t)page_size);

#if defined(_WIN32)
    void* mapped = VirtualAlloc(NULL, flash->mapped_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (mapped == NULL)
    {
        fprintf(stderr, "virtual_flash: VirtualAlloc failed: %lu\n", (unsigned long)GetLastError());
        return VIRTUAL_FLASH_ERROR;
    }
#else
    void* mapped = mmap(NULL,
                        flash->mapped_size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON,
                        -1,
                        0);
    if (mapped == MAP_FAILED)
    {
        fprintf(stderr, "virtual_flash: mmap failed: %s\n", strerror(errno));
        return VIRTUAL_FLASH_ERROR;
    }
#endif

    flash->base_addr = (uintptr_t)mapped;
    flash->mem = (uint8_t*)mapped;
    memset(flash->mem, flash->erased_value, flash->mapped_size);
    return CY_RSLT_SUCCESS;
}

void virtual_flash_deinit(virtual_flash_t* flash)
{
    if ((flash != NULL) && (flash->mem != NULL))
    {
#if defined(_WIN32)
        VirtualFree(flash->mem, 0, MEM_RELEASE);
#else
        munmap(flash->mem, flash->mapped_size);
#endif
        memset(flash, 0, sizeof(*flash));
    }
}

void virtual_flash_reset(virtual_flash_t* flash)
{
    if ((flash != NULL) && (flash->mem != NULL))
    {
        memset(flash->mem, flash->erased_value, flash->mapped_size);
        flash->read_count = 0u;
        flash->program_count = 0u;
        flash->erase_count = 0u;
        flash->fail_next_program = false;
        flash->fail_next_erase = false;
    }
}

void virtual_flash_make_block_device(virtual_flash_t* flash, mtb_block_storage_t* block_device)
{
    memset(block_device, 0, sizeof(*block_device));
    block_device->context = flash;
    block_device->read = vf_read;
    block_device->program = vf_program;
    block_device->erase = vf_erase;
    block_device->is_in_range = vf_is_in_range;
    block_device->is_erase_required = vf_is_erase_required;
    block_device->get_erase_size = vf_get_erase_size;
    block_device->get_program_size = vf_get_program_size;
}

cy_rslt_t mtb_block_storage_nvm_create(mtb_block_storage_t* block_device)
{
    CY_UNUSED_PARAMETER(block_device);
    return VIRTUAL_FLASH_ERROR;
}

cy_rslt_t mtb_block_storage_cat2_create(mtb_block_storage_t* block_device)
{
    CY_UNUSED_PARAMETER(block_device);
    return VIRTUAL_FLASH_ERROR;
}
