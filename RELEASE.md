# Emulated EEPROM Library Release Notes

### What's Included?
Please refer to the [README.md](./README.md) and the [API Reference Guide](https://infineon.github.io/emeeprom/html/index.html) for a complete description of the Emulated EEPROM Middleware.
The revision history of the Emulated EEPROM Middleware is also available on the [API Reference Guide Changelog](https://github.com/Infineon/emeeprom/blob/master/RELEASE.md).
### What Changed?
Release version v2.70.0:
* Documentation update

Release version v2.60.0:
* Updated implementation based on program size

Release version v2.50.0:
* Added support for MXS40v2 devices

Release version v2.40.0:
* Moved RAM buffer to file scope to avoid overflowing the stack

Release version v2.30.1:
* Bug fixes and BWC fixes
* Updated documentation
* Updated patch version

Release version v2.30:
* Introduced block storage implementation

Release version v2.20:
* Added support for XMC7xxx devices
* Added support for T2G-B-H devices
* Updated documentation
* Updated minor version

Release version v2.10:
* Added support for PSoC 4 devices
* Updated documentation
* Updated minor version

Release version v2.0:
* Updated major and minor version defines for consistency with other libraries
* Updated documentation for user experience improvement
* Added migration guide from PSoC Creator Em EEPROM component
* Added mechanism to restore corrupted redundant copy from the main data copy

### Defect Fixes
* Fixed MISRA Violation
* Fixed defect of the Cy_Em_EEPROM_Read() function when Emulated EEPROM data corruption in some cases caused infinite loop.
* Fixed defect of the Cy_Em_EEPROM_Read() function when the function returns incorrect data after restoring data from redundant copy.


### Supported Software and Tools
This version of the Emulated EEPROM Library was validated for compatibility with the following Software and Tools:

| Software and Tools                                      | Version |
| :---                                                    | :----:  |
| ModusToolbox Software Environment                       | 3.1     |
| GCC Compiler                                            | 11.3.1  |
| IAR Compiler                                            | 9.40.2  |
| ARM Compiler 6                                          | 6.16    |

### More information
For more information, refer to the following documents:
* [Emulated EEPROM Middleware README.md](./README.md)
* [Emulated EEPROM Middleware API Reference Guide](https://infineon.github.io/emeeprom/em_eeprom_api_reference_manual/html/index.html)
* [ModusToolbox Software Environment, Quick Start Guide, Documentation, and Videos](https://www.infineon.com/cms/en/design-support/tools/sdk/modustoolbox-software/)
* [CAT1 PDL API Reference](https://infineon.github.io/mtb-pdl-cat1/pdl_api_reference_manual/html/index.html)
* [CAT2 PDL API Reference](https://infineon.github.io/mtb-pdl-cat2/pdl_api_reference_manual/html/index.html)
* [AN219434 Importing PSoC Creator Code into an IDE for a PSoC 6 Project](https://www.cypress.com/an219434)
* [AN210781 Getting Started with PSoC 6 MCU with Bluetooth Low Energy (BLE) Connectivity](http://www.cypress.com/an210781)
* [PSoC 6 Technical Reference Manual](https://www.cypress.com/documentation/technical-reference-manuals/psoc-6-mcu-psoc-63-ble-architecture-technical-reference)
* [PSoC 63 with BLE Datasheet Programmable System-on-Chip datasheet](http://www.cypress.com/ds218787)

---
� Cypress Semiconductor Corporation (an Infineon company) or an affiliate of Cypress Semiconductor Corporation, 2021-2023.
