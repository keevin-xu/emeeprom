#ifndef CY_RESULT_H
#define CY_RESULT_H

#include <stdint.h>

typedef uint32_t cy_rslt_t;

#define CY_RSLT_SUCCESS (0u)
#define CY_RSLT_TYPE_ERROR (1u)
#define CY_RSLT_MODULE_MIDDLEWARE_EM_EEPROM (0x52u)
#define CY_RSLT_CREATE(type, module, code) \
    ((((uint32_t)(type)) << 24) | (((uint32_t)(module)) << 8) | ((uint32_t)(code)))

#endif
