CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -g -DCY_EM_EEPROM_HOST_TEST

HOST_INCLUDES = -Ihost/include -Iinclude
HOST_SOURCES = source/cy_em_eeprom.c host/virtual_flash.c host/emeeprom_cli.c

.PHONY: host-cli clean

host-cli: build/emeeprom_cli

build/emeeprom_cli: $(HOST_SOURCES)
	mkdir -p build
	$(CC) $(CFLAGS) $(HOST_INCLUDES) $(HOST_SOURCES) -o $@

clean:
	rm -rf build
