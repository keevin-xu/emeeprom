#include "virtual_flash.h"
#include <stdlib.h>

int main(void) {
    

}

void initialize(virtual_flash_t* flashMem, size_t pageSize, size_t numPages){
    flashMem->mem = malloc(numPages*pageSize*8);
    flashMem->activeSector = 0;
    flashMem->nextRow = 0;
}

void write(virtual_flash_t* flashMem, uint32_t addr, uint8_t val){
    flashMem->mem[addr] = val;
    return;
} 