#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cy_em_eeprom.h"
#include "virtual_flash.h"

#define CLI_LINE_SIZE 1024
#define DEFAULT_FLASH_SIZE 4096u
#define DEFAULT_PROGRAM_SIZE 128u
#define DEFAULT_ERASE_SIZE 256u
#define DEFAULT_EEPROM_SIZE 128u

static char* next_token(char** cursor)
{
    char* start;
    char* current;

    if ((cursor == NULL) || (*cursor == NULL))
    {
        return NULL;
    }

    current = *cursor;
    while ((*current != '\0') && isspace((unsigned char)*current))
    {
        current++;
    }

    if (*current == '\0')
    {
        *cursor = NULL;
        return NULL;
    }

    start = current;
    while ((*current != '\0') && !isspace((unsigned char)*current))
    {
        current++;
    }

    if (*current == '\0')
    {
        *cursor = NULL;
    }
    else
    {
        *current = '\0';
        *cursor = current + 1;
    }

    return start;
}

typedef struct
{
    uint32_t flash_size;
    uint32_t program_size;
    uint32_t erase_size;
    uint32_t eeprom_size;
    uint8_t simple_mode;
    uint8_t wear_leveling;
    uint8_t redundant_copy;
    uint8_t blocking_write;
} cli_options_t;

static const char* status_name(cy_en_em_eeprom_status_t status)
{
    switch (status)
    {
        case CY_EM_EEPROM_SUCCESS:
            return "CY_EM_EEPROM_SUCCESS";
        case CY_EM_EEPROM_BAD_PARAM:
            return "CY_EM_EEPROM_BAD_PARAM";
        case CY_EM_EEPROM_BAD_CHECKSUM:
            return "CY_EM_EEPROM_BAD_CHECKSUM";
        case CY_EM_EEPROM_BAD_DATA:
            return "CY_EM_EEPROM_BAD_DATA";
        case CY_EM_EEPROM_WRITE_FAIL:
            return "CY_EM_EEPROM_WRITE_FAIL";
        case CY_EM_EEPROM_REDUNDANT_COPY_USED:
            return "CY_EM_EEPROM_REDUNDANT_COPY_USED";
        default:
            return "UNKNOWN_STATUS";
    }
}

static void print_hex_ascii(const uint8_t* data, uint32_t size, uint32_t base_addr)
{
    for (uint32_t offset = 0; offset < size; offset += 16u)
    {
        uint32_t chunk = (size - offset > 16u) ? 16u : (size - offset);
        printf("%08x  ", base_addr + offset);
        for (uint32_t i = 0; i < 16u; ++i)
        {
            if (i < chunk)
            {
                printf("%02x ", data[offset + i]);
            }
            else
            {
                printf("   ");
            }
        }
        printf(" |");
        for (uint32_t i = 0; i < chunk; ++i)
        {
            unsigned char ch = data[offset + i];
            printf("%c", isprint(ch) ? ch : '.');
        }
        printf("|\n");
    }
}

static uint32_t parse_u32(const char* text, int* ok)
{
    char* end = NULL;
    unsigned long value = strtoul(text, &end, 0);

    *ok = ((text != NULL) && (*text != '\0') && (end != NULL) && (*end == '\0') && (value <= 0xfffffffful));
    return *ok ? (uint32_t)value : 0u;
}

static int parse_hex_byte(const char* text, uint8_t* out)
{
    char* end = NULL;
    unsigned long value = strtoul(text, &end, 16);

    if ((text == NULL) || (*text == '\0') || (end == NULL) || (*end != '\0') || (value > 0xffu))
    {
        return 0;
    }

    *out = (uint8_t)value;
    return 1;
}

static void print_help(void)
{
    puts("Commands:");
    puts("  help");
    puts("  info");
    puts("  stats");
    puts("  numwrites");
    puts("  read <addr> <len>");
    puts("  write <addr> <hex-byte> [hex-byte ...]");
    puts("  write-str <addr> <text>");
    puts("  erase");
    puts("  dump-flash <offset> <len>");
    puts("  corrupt <offset> <hex-byte>");
    puts("  fail-program");
    puts("  fail-erase");
    puts("  reset-flash");
    puts("  quit");
}

static int init_options(int argc, char** argv, cli_options_t* options)
{
    memset(options, 0, sizeof(*options));
    options->flash_size = DEFAULT_FLASH_SIZE;
    options->program_size = DEFAULT_PROGRAM_SIZE;
    options->erase_size = DEFAULT_ERASE_SIZE;
    options->eeprom_size = DEFAULT_EEPROM_SIZE;
    options->simple_mode = 0u;
    options->wear_leveling = 2u;
    options->redundant_copy = 0u;
    options->blocking_write = 1u;

    for (int i = 1; i < argc; i += 2)
    {
        int ok = 0;
        uint32_t value;

        if ((i + 1) >= argc)
        {
            fprintf(stderr, "Missing value for %s\n", argv[i]);
            return 0;
        }

        value = parse_u32(argv[i + 1], &ok);
        if (!ok)
        {
            fprintf(stderr, "Invalid numeric value for %s: %s\n", argv[i], argv[i + 1]);
            return 0;
        }

        if (strcmp(argv[i], "--flash-size") == 0)
        {
            options->flash_size = value;
        }
        else if (strcmp(argv[i], "--program-size") == 0)
        {
            options->program_size = value;
        }
        else if (strcmp(argv[i], "--erase-size") == 0)
        {
            options->erase_size = value;
        }
        else if (strcmp(argv[i], "--eeprom-size") == 0)
        {
            options->eeprom_size = value;
        }
        else if (strcmp(argv[i], "--simple") == 0)
        {
            options->simple_mode = (uint8_t)value;
        }
        else if (strcmp(argv[i], "--wear") == 0)
        {
            options->wear_leveling = (uint8_t)value;
        }
        else if (strcmp(argv[i], "--redundant") == 0)
        {
            options->redundant_copy = (uint8_t)value;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 0;
        }
    }

    return 1;
}

int main(int argc, char** argv)
{
    cli_options_t options;
    virtual_flash_t flash;
    mtb_block_storage_t block_device;
    cy_stc_eeprom_context_t context;
    cy_stc_eeprom_config2_t config;
    cy_en_em_eeprom_status_t status;
    char line[CLI_LINE_SIZE];

    if (!init_options(argc, argv, &options))
    {
        return 1;
    }

    if (virtual_flash_init(&flash, options.flash_size, options.program_size, options.erase_size) != CY_RSLT_SUCCESS)
    {
        return 1;
    }

    virtual_flash_make_block_device(&flash, &block_device);

    memset(&config, 0, sizeof(config));
    config.eepromSize = options.eeprom_size;
    config.simpleMode = options.simple_mode;
    config.wearLevelingFactor = options.wear_leveling;
    config.redundantCopy = options.redundant_copy;
    config.blockingWrite = options.blocking_write;
    config.userNvmStartAddr = flash.base_addr;

    status = Cy_Em_EEPROM_Init_BD(&config, &context, &block_device);
    if (status != CY_EM_EEPROM_SUCCESS)
    {
        fprintf(stderr, "Init failed: %s (0x%08x)\n", status_name(status), (unsigned)status);
        virtual_flash_deinit(&flash);
        return 1;
    }

    printf("Virtual flash ready.\n");
    printf("  flash_base   = 0x%zx\n", (size_t)flash.base_addr);
    printf("  flash_size   = %u\n", flash.flash_size);
    printf("  program_size = %u\n", flash.program_size);
    printf("  erase_size   = %u\n", flash.erase_size);
    printf("  eeprom_size  = %u\n", context.eepromSize);
    printf("  row_size     = %u\n", context.rowSize);
    printf("  byte_in_row  = %u\n", context.byteInRow);
    printf("Type 'help' for commands.\n");

    while (1)
    {
        char* tokens[128];
        size_t token_count = 0u;
        char* cursor;
        char* token;

        printf("emeeprom> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            break;
        }

        cursor = line;
        while ((token = next_token(&cursor)) != NULL)
        {
            tokens[token_count++] = token;
            if (token_count == (sizeof(tokens) / sizeof(tokens[0])))
            {
                break;
            }
        }

        if (token_count == 0u)
        {
            continue;
        }

        if ((strcmp(tokens[0], "quit") == 0) || (strcmp(tokens[0], "exit") == 0))
        {
            break;
        }
        else if (strcmp(tokens[0], "help") == 0)
        {
            print_help();
        }
        else if (strcmp(tokens[0], "info") == 0)
        {
            printf("row_size=%u byte_in_row=%u rows=%u wear=%u simple=%u redundant=%u last_row=0x%08zx\n",
                   context.rowSize, context.byteInRow, context.numberOfRows, context.wearLevelingFactor,
                   context.simpleMode, context.redundantCopy, (size_t)(uintptr_t)context.ptrLastWrittenRow);
        }
        else if (strcmp(tokens[0], "stats") == 0)
        {
            printf("reads=%u programs=%u erases=%u\n",
                   flash.read_count, flash.program_count, flash.erase_count);
        }
        else if (strcmp(tokens[0], "numwrites") == 0)
        {
            printf("%u\n", Cy_Em_EEPROM_NumWrites(&context));
        }
        else if (strcmp(tokens[0], "erase") == 0)
        {
            status = Cy_Em_EEPROM_Erase(&context);
            printf("%s (0x%08x)\n", status_name(status), (unsigned)status);
        }
        else if (strcmp(tokens[0], "fail-program") == 0)
        {
            flash.fail_next_program = true;
            puts("next program will fail");
        }
        else if (strcmp(tokens[0], "fail-erase") == 0)
        {
            flash.fail_next_erase = true;
            puts("next erase will fail");
        }
        else if (strcmp(tokens[0], "reset-flash") == 0)
        {
            virtual_flash_reset(&flash);
            status = Cy_Em_EEPROM_Init_BD(&config, &context, &block_device);
            printf("%s (0x%08x)\n", status_name(status), (unsigned)status);
        }
        else if (strcmp(tokens[0], "read") == 0)
        {
            int ok_addr;
            int ok_len;
            uint32_t addr;
            uint32_t len;
            uint8_t* buffer;

            if (token_count != 3u)
            {
                puts("usage: read <addr> <len>");
                continue;
            }

            addr = parse_u32(tokens[1], &ok_addr);
            len = parse_u32(tokens[2], &ok_len);
            if ((!ok_addr) || (!ok_len) || (len == 0u))
            {
                puts("invalid arguments");
                continue;
            }

            buffer = (uint8_t*)malloc(len);
            if (buffer == NULL)
            {
                puts("allocation failed");
                continue;
            }

            status = Cy_Em_EEPROM_Read(addr, buffer, len, &context);
            printf("%s (0x%08x)\n", status_name(status), (unsigned)status);
            if ((status == CY_EM_EEPROM_SUCCESS) || (status == CY_EM_EEPROM_REDUNDANT_COPY_USED))
            {
                print_hex_ascii(buffer, len, addr);
            }
            free(buffer);
        }
        else if (strcmp(tokens[0], "write") == 0)
        {
            int ok_addr;
            uint32_t addr;
            uint8_t buffer[256];

            if (token_count < 3u)
            {
                puts("usage: write <addr> <hex-byte> [hex-byte ...]");
                continue;
            }

            addr = parse_u32(tokens[1], &ok_addr);
            if (!ok_addr || ((token_count - 2u) > sizeof(buffer)))
            {
                puts("invalid arguments");
                continue;
            }

            for (size_t i = 2u; i < token_count; ++i)
            {
                if (!parse_hex_byte(tokens[i], &buffer[i - 2u]))
                {
                    puts("invalid hex byte");
                    ok_addr = 0;
                    break;
                }
            }

            if (!ok_addr)
            {
                continue;
            }

            status = Cy_Em_EEPROM_Write(addr, buffer, (uint32_t)(token_count - 2u), &context);
            printf("%s (0x%08x)\n", status_name(status), (unsigned)status);
        }
        else if (strcmp(tokens[0], "write-str") == 0)
        {
            int ok_addr;
            uint32_t addr;
            char text[512];
            size_t text_len = 0u;

            if (token_count < 3u)
            {
                puts("usage: write-str <addr> <text>");
                continue;
            }

            addr = parse_u32(tokens[1], &ok_addr);
            if (!ok_addr)
            {
                puts("invalid address");
                continue;
            }

            text[0] = '\0';
            for (size_t i = 2u; i < token_count; ++i)
            {
                size_t part_len = strlen(tokens[i]);
                if ((text_len + part_len + 1u) >= sizeof(text))
                {
                    puts("text too long");
                    text_len = 0u;
                    break;
                }
                if (i > 2u)
                {
                    text[text_len++] = ' ';
                }
                memcpy(text + text_len, tokens[i], part_len);
                text_len += part_len;
                text[text_len] = '\0';
            }

            if (text_len == 0u)
            {
                continue;
            }

            status = Cy_Em_EEPROM_Write(addr, text, (uint32_t)text_len, &context);
            printf("%s (0x%08x)\n", status_name(status), (unsigned)status);
        }
        else if (strcmp(tokens[0], "dump-flash") == 0)
        {
            int ok_offset;
            int ok_len;
            uint32_t offset;
            uint32_t len;

            if (token_count != 3u)
            {
                puts("usage: dump-flash <offset> <len>");
                continue;
            }

            offset = parse_u32(tokens[1], &ok_offset);
            len = parse_u32(tokens[2], &ok_len);
            if ((!ok_offset) || (!ok_len) || ((offset + len) > flash.flash_size))
            {
                puts("invalid range");
                continue;
            }

            print_hex_ascii(flash.mem + offset, len, flash.base_addr + offset);
        }
        else if (strcmp(tokens[0], "corrupt") == 0)
        {
            int ok_offset;
            uint32_t offset;
            uint8_t value;

            if (token_count != 3u)
            {
                puts("usage: corrupt <offset> <hex-byte>");
                continue;
            }

            offset = parse_u32(tokens[1], &ok_offset);
            if ((!ok_offset) || (offset >= flash.flash_size) || (!parse_hex_byte(tokens[2], &value)))
            {
                puts("invalid arguments");
                continue;
            }

            flash.mem[offset] = value;
            printf("flash[0x%zx] = 0x%02x\n", (size_t)(flash.base_addr + offset), value);
        }
        else
        {
            puts("unknown command");
        }
    }

    virtual_flash_deinit(&flash);
    return 0;
}
