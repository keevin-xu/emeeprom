/***************************************************************************//**
* \file cy_em_eeprom.c
* \version 2.30
*
* \brief
*  This file provides source code of the API for the Emulated EEPROM library.
*  The Emulated EEPROM API allows creating an emulated EEPROM in nvm that
*  has the ability to do wear leveling and restore corrupted data from a
*  redundant copy.
*
********************************************************************************
* \copyright
* (c) (2017-2021), Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation. All rights reserved.
* You may use this file only in accordance with the license, terms, conditions,
* disclaimers, and limitations in the end user license agreement accompanying
* the software package with which this file was provided.
*******************************************************************************/

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "cy_em_eeprom.h"
#include "cy_pdl.h"

/*******************************************************************************
* Global variables
*******************************************************************************/
static mtb_block_storage_t _mtb_emeeprom_bsd;
//Global RAM buffer to avoid stack corruption */
static uint32_t writeRamBuffer[CY_EM_EEPROM_MAXIMUM_ROW_SIZE / 4];

/*******************************************************************************
* Private Function Prototypes
*******************************************************************************/
static cy_en_em_eeprom_status_t ReadSimpleMode(uint32_t addr, void* eepromData, uint32_t size,
                                               const cy_stc_eeprom_context_t* context);
static cy_en_em_eeprom_status_t ReadExtendedMode(uint32_t addr, void* eepromData, uint32_t size,
                                                 cy_stc_eeprom_context_t* context);
static cy_en_em_eeprom_status_t WriteSimpleMode(uint32_t addr, const void* eepromData,
                                                uint32_t size,
                                                cy_stc_eeprom_context_t* context);
static cy_en_em_eeprom_status_t WriteExtendedMode(uint32_t addr, const void* eepromData,
                                                  uint32_t size,
                                                  cy_stc_eeprom_context_t* context);
static uint8_t CalcChecksum(const uint8_t rowData[], uint32_t len);
static cy_en_em_eeprom_status_t CheckRanges(const cy_stc_eeprom_config2_t* config,
                                            const cy_stc_eeprom_context_t* context);
static cy_en_em_eeprom_status_t WriteRow(uint32_t* const rowAddr, const uint32_t* rowData,
                                         const cy_stc_eeprom_context_t* context);
static cy_en_em_eeprom_status_t EraseRow(const uint32_t* rowAddr, const uint32_t* ramBuffAddr,
                                         const cy_stc_eeprom_context_t* context);
static uint32_t CalculateRowChecksum(const uint32_t* ptrRow, uint32_t rowSize);
static uint32_t GetStoredRowChecksum(const uint32_t* ptrRow);
static cy_en_em_eeprom_status_t CheckRowChecksum(const uint32_t* ptrRow, uint32_t rowSize);
static uint32_t GetStoredSeqNum(const uint32_t* ptrRow);
static cy_en_em_eeprom_status_t DefineLastWrittenRow(cy_stc_eeprom_context_t* context);
static cy_en_em_eeprom_status_t CheckLastWrittenRowIntegrity(uint32_t* ptrSeqNum,
                                                             cy_stc_eeprom_context_t* context);
static uint32_t* GetNextRowPointer(uint32_t* ptrRow, const cy_stc_eeprom_context_t* context);
static uint32_t* GetReadRowPointer(uint32_t* ptrRow, const cy_stc_eeprom_context_t* context);
static cy_en_em_eeprom_status_t CopyHistoricData(uint32_t* ptrRowWrite, uint32_t* ptrRow,
                                                 const cy_stc_eeprom_context_t* context);
static cy_en_em_eeprom_status_t CopyHeadersData(uint32_t* ptrRowWrite, uint32_t* ptrRow,
                                                const cy_stc_eeprom_context_t* context);
static uint32_t GetPhysicalSize(const cy_stc_eeprom_context_t* context,
                                const cy_stc_eeprom_config2_t* config);
static void ComputeEEPROMProgramSize(cy_stc_eeprom_context_t* context);

#if (CPUSS_FLASHC_ECT == 1)
static bool WorkFlashIsErased(
    const uint32_t* addr,
    uint32_t size);
#endif
/*******************************************************************************
*       Functions
*******************************************************************************/
//--------------------------------------------------------------------------------------------------
// Cy_Em_EEPROM_Init
//--------------------------------------------------------------------------------------------------
cy_en_em_eeprom_status_t Cy_Em_EEPROM_Init(
    const cy_stc_eeprom_config_t* config,
    cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_SUCCESS;
    cy_rslt_t bd_init_result = CY_RSLT_SUCCESS;
    #if (defined(COMPONENT_CAT2))
    bd_init_result = mtb_block_storage_cat2_create(&_mtb_emeeprom_bsd);
    #else
    bd_init_result = mtb_block_storage_nvm_create(&_mtb_emeeprom_bsd);
    #endif

    if (bd_init_result == CY_RSLT_SUCCESS)
    {
        cy_stc_eeprom_config2_t config_new;
        config_new.eepromSize = config->eepromSize;
        config_new.simpleMode = config->simpleMode;
        config_new.wearLevelingFactor = config->wearLevelingFactor;
        config_new.redundantCopy = config->redundantCopy;
        config_new.blockingWrite = config->blockingWrite;
        config_new.userNvmStartAddr = config->userFlashStartAddr;

        result = Cy_Em_EEPROM_Init_BD(&config_new, context, &_mtb_emeeprom_bsd);
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// Cy_Em_EEPROM_Init_BD
//--------------------------------------------------------------------------------------------------
cy_en_em_eeprom_status_t Cy_Em_EEPROM_Init_BD(
    const cy_stc_eeprom_config2_t* config,
    cy_stc_eeprom_context_t* context,
    mtb_block_storage_t* block_device)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_BAD_PARAM;

    if ((NULL != context) && (NULL != config) && (NULL != block_device))
    {
        /* Intialize the context to 0's */
        memset(context, 0, sizeof(cy_stc_eeprom_context_t));


        context->bd = block_device;
        context->userNvmStartAddr = config->userNvmStartAddr;
        context->simpleMode = config->simpleMode;

        /* Stores frequently used data for internal use */
        ComputeEEPROMProgramSize(context);

        /* Check if simpleMode is enabled */
        if (context->simpleMode == 1)
        {
            /* If yes, then the whole row will be dedicated to data*/
            context->byteInRow = context->rowSize;
        }
        else
        {
            /* If not, half the row will be dedicated to data and
                the other half to headers.*/
            context->byteInRow = context->rowSize / 2;
        }

        /* Defines the length of data that can be stored in the Em_EEPROM header. */
        context->headerDataLength = (context->byteInRow - CY_EM_EEPROM_HEADER_DATA_OFFSET);

        context->eepromSize = config->eepromSize;
        context->numberOfRows = ((((context->eepromSize) - 1uL) / (context->byteInRow)) + 1uL);

        result = CheckRanges(config, context);

        #if !defined(MTB_BLOCK_STORAGE_NON_BLOCKING_SUPPORTED)
        if (CY_RSLT_SUCCESS == result)
        {
            if (config->blockingWrite == 0u)
            {
                result = CY_EM_EEPROM_BAD_PARAM;
            }
        }
        #endif /* !defined(MTB_BLOCK_STORAGE_NON_BLOCKING_SUPPORTED) */

        if (CY_RSLT_SUCCESS == result)
        {
            /* Copies the user's config structure fields into the context */
            if (0u != context->simpleMode)
            {
                context->wearLevelingFactor = 1u;
                context->redundantCopy = 0u;
            }
            else
            {
                context->wearLevelingFactor = config->wearLevelingFactor;
                context->redundantCopy = config->redundantCopy;
            }
            context->blockingWrite = config->blockingWrite;

            /* Initialize the Last written row */
            (void)DefineLastWrittenRow(context);
        }
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// Cy_Em_EEPROM_Read
//--------------------------------------------------------------------------------------------------
cy_en_em_eeprom_status_t Cy_Em_EEPROM_Read(
    uint32_t addr,
    void* eepromData,
    uint32_t size,
    cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_BAD_PARAM;

    /* Validates the input parameters */
    if ((0u != size) && ((addr + size) <= (context->eepromSize)) && (NULL != eepromData))
    {
        if (0u != context->simpleMode)
        {
            result =
                ReadSimpleMode(addr, eepromData, size, (const cy_stc_eeprom_context_t*)context);
        }
        else
        {
            result = ReadExtendedMode(addr, eepromData, size, context);
        }
    }

    return result;
}


/*******************************************************************************
* Function Name: ReadSimpleMode
****************************************************************************//**
*
* Reads data from a specified location in Simple mode only.
*
* \param addr
* The logical start address in the Em_EEPROM storage to start reading data from.
*
* \param eepromData
* The pointer to a user array to write data to.
*
* \param size
* The amount of data to read in bytes.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* This function returns cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t ReadSimpleMode(
    uint32_t addr,
    void* eepromData,
    uint32_t size,
    const cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_SUCCESS;
    #if (CPUSS_FLASHC_ECT == 1)
    if (WorkFlashIsErased((uint32_t*)(context->userNvmStartAddr + addr), size))
    {
        /* Fills the RAM buffer with flash data for the case when not a whole row is requested to be
           overwritten */
        (void)memset((void*)eepromData, 0, size);
        result = CY_EM_EEPROM_SUCCESS;
    }
    else
    #endif /* (CPUSS_FLASHC_ECT == 1) */
    {
        cy_rslt_t readResult = context->bd->read(context->bd->context,
                                                 (context->userNvmStartAddr + addr), size,
                                                 (uint8_t*)eepromData);
        result = (readResult == CY_RSLT_SUCCESS) ? CY_EM_EEPROM_SUCCESS : CY_EM_EEPROM_BAD_DATA;
    }
    return result;
}


/*******************************************************************************
* Function Name: ReadExtendedMode
****************************************************************************//**
*
* Reads data from a specified location when Simple Mode is disabled.
*
* \param addr
* The logical start address in the Em_EEPROM storage to start reading data from.
*
* \param eepromData
* The pointer to a user array to write data to.
*
* \param size
* The amount of data to read in bytes.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* This function returns cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t ReadExtendedMode(
    uint32_t addr,
    void* eepromData,
    uint32_t size,
    cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_SUCCESS;
    cy_en_em_eeprom_status_t retHistoricCrc;
    uint32_t i;
    uint32_t k;
    uint32_t numRowReads;
    uint32_t seqNum;
    uint32_t sizeToCopy;
    uint32_t sizeRemaining;
    uint32_t userBufferAddr;
    uint8_t* userBufferAddr_p;
    uint32_t* ptrRow;
    uint32_t* ptrRowWork;
    uint32_t curRowOffset;
    uint32_t strHistAddr;
    uint32_t endHistAddr;
    uint32_t currentAddr;
    uint32_t strHeadAddr;
    uint32_t endHeadAddr;
    uint32_t dstOffset;
    uint32_t srcOffset;
    cy_en_em_eeprom_status_t crcStatus;
    uint32_t numReads = context->numberOfRows;

    /* 1. Clears the user buffer */
    (void)memset(eepromData, 0, size);

    /* 2. Ensures the last written row is correct */
    (void)CheckLastWrittenRowIntegrity(&seqNum, context);

    /* 3. Reads relevant historic data into user's buffer */
    currentAddr = addr;
    sizeRemaining = size;
    userBufferAddr_p = eepromData;
    userBufferAddr = (uint32_t)userBufferAddr_p;
    numRowReads = ((((addr + size) - 1u) / context->byteInRow) - (addr / context->byteInRow)) + 1u;

    ptrRow =
        (uint32_t*)(context->userNvmStartAddr +
                    ((context->rowSize) * (addr / context->byteInRow)));

    for (i = 0u; i < numRowReads; i++)
    {
        if (1u < context->wearLevelingFactor)
        {
            /* Jumps to the active sector if wear leveling is enabled */
            ptrRow = GetReadRowPointer(context->ptrLastWrittenRow, context);
            /* Finds a row in the active sector with a relevant historic data address */
            for (k = 0u; k < context->numberOfRows; k++)
            {
                ptrRow = GetNextRowPointer(ptrRow, context);
                strHistAddr = ((((uint32_t)ptrRow - context->userNvmStartAddr) /
                                (context->rowSize)) % context->numberOfRows) *
                              ((context->rowSize) / 2u);
                endHistAddr = strHistAddr + ((context->rowSize) / 2u);
                if ((currentAddr >= strHistAddr) && (currentAddr < endHistAddr))
                {
                    /* The row with needed address is found */
                    break;
                }
            }
        }

        curRowOffset = context->byteInRow + (currentAddr % context->byteInRow);
        sizeToCopy = context->byteInRow - (currentAddr % context->byteInRow);
        if (i >= (numRowReads - 1u))
        {
            sizeToCopy = sizeRemaining;
        }

        retHistoricCrc = CY_EM_EEPROM_SUCCESS;
        if (CY_EM_EEPROM_SUCCESS != CheckRowChecksum(ptrRow, context->rowSize))
        {
            /* CRC is bad. Checks if the redundant copy if enabled */
            retHistoricCrc = CY_EM_EEPROM_BAD_CHECKSUM;
            if (0u != context->redundantCopy)
            {
                ptrRow += ((context->numberOfRows * context->wearLevelingFactor) *
                           (context->rowSize /4));
                if (CY_EM_EEPROM_SUCCESS == CheckRowChecksum(ptrRow, (context->rowSize)))
                {
                    retHistoricCrc = CY_EM_EEPROM_REDUNDANT_COPY_USED;
                }
            }
        }
        /* If the correct CRC is found, then copies the data to the user's buffer */
        if (CY_EM_EEPROM_BAD_CHECKSUM != retHistoricCrc)
        {
            context->bd->read(context->bd->context, ((uint32_t)ptrRow + curRowOffset), sizeToCopy,
                              (uint8_t*)userBufferAddr);
        }
        else
        {
            (void)memset((uint8_t*)userBufferAddr, 0, sizeToCopy);
            if ((0u == GetStoredSeqNum(ptrRow)) && (0u == GetStoredRowChecksum(ptrRow)))
            {
                /*
                 * Considers a row with a bad checksum as the row never that has never been
                 * written before if the sequence number and CRC values are zeros.
                 * In this case, reports zeros and does not
                 * report the failed status of the write operation.
                 */
                retHistoricCrc = CY_EM_EEPROM_SUCCESS;
            }
        }


        if (1u >= context->wearLevelingFactor)
        {
            ptrRow = GetNextRowPointer(ptrRow, context);
        }

        sizeRemaining -= sizeToCopy;
        currentAddr += sizeToCopy;
        userBufferAddr += sizeToCopy;

        /* Reports the status of the CRC verification in the following order:
         * The highest priority: CY_EM_EEPROM_BAD_CHECKSUM
         *                       CY_EM_EEPROM_REDUNDANT_COPY_USED
         * The lowest priority:  CY_RSLT_SUCCESS
         */
        if (CY_EM_EEPROM_BAD_CHECKSUM == retHistoricCrc)
        {
            result = CY_EM_EEPROM_BAD_CHECKSUM;
        }
        if (CY_EM_EEPROM_SUCCESS == result)
        {
            result = retHistoricCrc;
        }
    }

    /* 4. Reads data from all active headers */
    ptrRow = GetReadRowPointer(context->ptrLastWrittenRow, context);
    for (i = 0u; i < numReads; i++)
    {
        ptrRow = GetNextRowPointer(ptrRow, context);
        ptrRowWork = ptrRow;
        /* Checks CRC of the row to be read except the last row of a recently created header */
        crcStatus = CheckRowChecksum(ptrRowWork, context->rowSize);
        if ((CY_EM_EEPROM_SUCCESS != crcStatus) && (0u != context->redundantCopy))
        {
            /* Calculates the redundant copy pointer */
            ptrRowWork += ((context->numberOfRows * context->wearLevelingFactor) *
                           context->rowSize / 4);
            crcStatus = CheckRowChecksum(ptrRowWork, context->rowSize);
        }

        /* Skips the row if CRC is bad */
        if (CY_EM_EEPROM_SUCCESS == crcStatus)
        {
            /* The address of header data */
            strHeadAddr = ptrRowWork[CY_EM_EEPROM_HEADER_ADDR_OFFSET_U32];
            endHeadAddr = strHeadAddr + ptrRowWork[CY_EM_EEPROM_HEADER_LEN_OFFSET_U32];

            /* Skips the row if the header data address is out of the user's requested address range
             */
            if ((strHeadAddr < (addr + size)) && (endHeadAddr > addr))
            {
                dstOffset = (strHeadAddr > addr) ? (strHeadAddr - addr) : (0u);
                srcOffset = (strHeadAddr > addr) ? (0u) : (addr - strHeadAddr);
                /* Calculates the number of bytes to be read from the current row's EEPROM header */
                sizeToCopy = (strHeadAddr > addr) ? (strHeadAddr) : (addr);
                sizeToCopy =
                    ((endHeadAddr < (addr + size)) ? endHeadAddr : (addr + size)) - sizeToCopy;

                userBufferAddr_p = eepromData;
                /* Reads from the memory and writes to the buffer */
                context->bd->read(context->bd->context,
                                  ((uint32_t)ptrRowWork + srcOffset +
                                   CY_EM_EEPROM_HEADER_DATA_OFFSET),
                                  sizeToCopy,
                                  (uint8_t*)((uint32_t)userBufferAddr_p + dstOffset));
            }
        }
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// Cy_Em_EEPROM_Write
//--------------------------------------------------------------------------------------------------
cy_en_em_eeprom_status_t Cy_Em_EEPROM_Write(
    uint32_t addr,
    const void* eepromData,
    uint32_t size,
    cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_BAD_PARAM;

    /* Checks if the Em_EEPROM data does not exceed the Em_EEPROM capacity */
    if ((0u != size) && ((addr + size) <= (context->eepromSize)) && (NULL != eepromData))
    {
        if (0u != context->simpleMode)
        {
            result = WriteSimpleMode(addr, eepromData, size, context);
        }
        else
        {
            result = WriteExtendedMode(addr, eepromData, size, context);
        }
    }
    return result;
}


/*******************************************************************************
* Function Name: WriteSimpleMode
****************************************************************************//**
*
* Writes data to a specified location in Simple Mode only.
*
* \param addr
* The logical start address in the Em_EEPROM storage to start writing data to.
*
* \param eepromData
* Data to write to Em_EEPROM.
*
* \param size
* The amount of data to write to Em_EEPROM in bytes.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* This function returns cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t WriteSimpleMode(
    uint32_t addr,
    const void* eepromData,
    uint32_t size,
    cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_SUCCESS;
    uint32_t wrCnt = 0u;
    uint32_t numBytes = 0;
    uint32_t startAddr = addr % (context->rowSize);
    uint32_t numWrites = (((size + startAddr) - 1u) / (context->rowSize)) + 1u;
    uint32_t* ptrRow = (uint32_t*)(context->userNvmStartAddr + (addr - startAddr));
    const uint8_t* ptrUserData_p = eepromData;
    uint32_t ptrUserData = (uint32_t)ptrUserData_p;

    uint32_t lc_size = size;

    while (wrCnt < numWrites)
    {
        #if (CPUSS_FLASHC_ECT == 1)
        /* Fills the RAM buffer with all 0s if the row has never been written before */
        if (WorkFlashIsErased(ptrRow, (context->rowSize)))
        {
            (void)memset((uint8_t*)&writeRamBuffer[0u], 0, (context->rowSize));
        }
        else
        #endif /* (CPUSS_FLASHC_ECT == 1) */
        {
            /* Fills the RAM buffer with nvm data for the case when not a whole row is requested to
               be
               overwritten */
            context->bd->read(context->bd->context,
                              (uint32_t)ptrRow,
                              (context->rowSize),
                              (uint8_t*)&writeRamBuffer[0u]);
        }

        /* Calculates the number of bytes to be written into the current row */
        numBytes = (context->rowSize) - startAddr;
        if (numBytes > lc_size)
        {
            numBytes = lc_size;
        }
        /* Overwrites the RAM buffer with new data */
        (void)memcpy((uint8_t*)((uint32_t)&writeRamBuffer[0u] + startAddr),
                     (const uint8_t*)ptrUserData, numBytes);

        /* Writes data to the specified nvm row */
        result = WriteRow(ptrRow, &writeRamBuffer[0u], context);

        if (CY_EM_EEPROM_SUCCESS == result)
        {
            context->ptrLastWrittenRow = ptrRow;
        }
        else
        {
            break;
        }
        /* Update pointers for the next row to be written if any */
        startAddr = 0u;
        lc_size -= numBytes;
        ptrUserData += numBytes;
        ptrRow += context->rowSize / 4;
        wrCnt++;
    }

    return result;
}


/*******************************************************************************
* Function Name: WriteExtendedMode
****************************************************************************//**
*
* Writes data to a specified location when Simple Mode is disabled.
*
* \param addr
* The logical start address in the Em_EEPROM storage to start writing data to.
*
* \param eepromData
* Data to write to Em_EEPROM.
*
* \param size
* The amount of data to write to Em_EEPROM in bytes.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* This function returns cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t WriteExtendedMode(
    uint32_t addr,
    const void* eepromData,
    uint32_t size,
    cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_SUCCESS;
    cy_en_em_eeprom_status_t retWriteRow = CY_EM_EEPROM_SUCCESS;
    uint32_t wrCnt;
    uint32_t seqNum;
    uint32_t* ptrRow;
    uint32_t* ptrRowCopy;

    const uint8_t* userBufferAddr_p = eepromData;
    uint32_t ptrUserData = (uint32_t)userBufferAddr_p;
    uint32_t numWrites = ((size - 1u) / context->headerDataLength) + 1u;
    uint32_t lc_addr = addr;
    uint32_t lc_size = size;

    /* Checks CRC of the last written row and find the last written row if the CRC is broken */
    (void)CheckLastWrittenRowIntegrity(&seqNum, context);
    ptrRow = context->ptrLastWrittenRow;

    for (wrCnt = 0u; wrCnt < numWrites; wrCnt++)
    {
        ptrRow = GetNextRowPointer(ptrRow, context);
        seqNum++;

        /* 1. Clears the RAM buffer */
        (void)memset(&writeRamBuffer[0u], 0, (context->rowSize));

        /* 2. Fills the EM_EEPROM service header info */
        writeRamBuffer[CY_EM_EEPROM_HEADER_SEQ_NUM_OFFSET_U32] = seqNum;
        writeRamBuffer[CY_EM_EEPROM_HEADER_ADDR_OFFSET_U32] = lc_addr;
        writeRamBuffer[CY_EM_EEPROM_HEADER_LEN_OFFSET_U32] = context->headerDataLength;
        if (wrCnt == (numWrites - 1u))
        {
            /* Fills in the remaining size if this is the last row to write */
            writeRamBuffer[CY_EM_EEPROM_HEADER_LEN_OFFSET_U32] = lc_size;
        }

        /* 3. Writes the user's data to the buffer */
        (void)memcpy((uint8_t*)&writeRamBuffer[CY_EM_EEPROM_HEADER_DATA_OFFSET_U32],
                     (const uint8_t*)ptrUserData,
                     writeRamBuffer[CY_EM_EEPROM_HEADER_LEN_OFFSET_U32]);

        /* 4. Writes the historic data to the buffer */
        result = CopyHistoricData(&writeRamBuffer[0u], ptrRow, context);

        /* 5. Writes the data from other headers */
        result = CopyHeadersData(&writeRamBuffer[0u], ptrRow, context);

        /* 6. Calculates a checksum */
        writeRamBuffer[CY_EM_EEPROM_HEADER_CHECKSUM_OFFSET_U32] = CalculateRowChecksum(
            &writeRamBuffer[0u], (context->rowSize));

        /* 7. Writes data to the specified nvm row */
        retWriteRow = WriteRow(ptrRow, &writeRamBuffer[0u], context);
        if ((CY_EM_EEPROM_SUCCESS == retWriteRow) && (0u != context->redundantCopy))
        {
            /* Writes data to the specified nvm row in the redundant copy area */
            ptrRowCopy = ptrRow +
                         ((context->numberOfRows * context->wearLevelingFactor) *
                          (context->rowSize / 4));
            retWriteRow = WriteRow(ptrRowCopy, &writeRamBuffer[0u], context);
        }

        if (CY_EM_EEPROM_SUCCESS == retWriteRow)
        {
            context->ptrLastWrittenRow = ptrRow;
        }
        else
        {
            break;
        }

        /* Switches to the next row */
        lc_size -= context->headerDataLength;
        lc_addr += context->headerDataLength;
        ptrUserData += context->headerDataLength;
    }

    if (CY_EM_EEPROM_SUCCESS != retWriteRow)
    {
        result = retWriteRow;
    }

    return result;
}


//--------------------------------------------------------------------------------------------------
// Cy_Em_EEPROM_Erase
//--------------------------------------------------------------------------------------------------
cy_en_em_eeprom_status_t Cy_Em_EEPROM_Erase(cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_SUCCESS;
    cy_en_em_eeprom_status_t retStatus;
    uint32_t i;
    uint32_t* ptrRow;
    uint32_t* ptrRowCopy;
    uint32_t seqNum;
    uint32_t numRows = context->numberOfRows * context->wearLevelingFactor;

    //Clear buffer
    (void)memset(&writeRamBuffer[0u], 0x0, (context->rowSize));

    if (0u != context->simpleMode)
    {
        ptrRow = (uint32_t*)context->userNvmStartAddr;
        for (i = 0u; i < numRows; i++)
        {
            retStatus = EraseRow(ptrRow, &writeRamBuffer[0u], context);
            if (CY_EM_EEPROM_SUCCESS == result)
            {
                result = retStatus;
            }
            ptrRow += context->rowSize / 4;
        }
    }
    else
    {
        /* Checks CRC if it is really the last row and the max sequence number */
        (void)CheckLastWrittenRowIntegrity(&seqNum, context);

        /* Gets the last written row pointer */
        ptrRow = context->ptrLastWrittenRow;
        ptrRow = GetNextRowPointer(ptrRow, context);

        /* Prepares a zero buffer with a sequence number and checksum */
        writeRamBuffer[CY_EM_EEPROM_HEADER_SEQ_NUM_OFFSET_U32] = seqNum + 1u;
        writeRamBuffer[CY_EM_EEPROM_HEADER_CHECKSUM_OFFSET_U32] = CalculateRowChecksum(
            &writeRamBuffer[0u], (context->rowSize));

        /* Performs writing */
        result = WriteRow(ptrRow, &writeRamBuffer[0u], context);
        /* Duplicates writing into a redundant copy if enabled */
        if (0u != context->redundantCopy)
        {
            ptrRowCopy = ptrRow + (numRows * (context->rowSize / 4));
            retStatus = WriteRow(ptrRowCopy, &writeRamBuffer[0u], context);
            if (CY_EM_EEPROM_SUCCESS == result)
            {
                result = retStatus;
            }
        }

        /* If the write operation is unsuccessful, skip erasing Em_EEPROM */
        if (CY_EM_EEPROM_SUCCESS == result)
        {
            context->ptrLastWrittenRow = ptrRow;
            /* One row is already overwritten, so reduces the number of rows to be erased by one */
            for (i = 0u; i < (numRows - 1u); i++)
            {
                ptrRow = GetNextRowPointer(ptrRow, context);
                retStatus = EraseRow(ptrRow, &writeRamBuffer[0u], context);
                if (CY_EM_EEPROM_SUCCESS == result)
                {
                    result = retStatus;
                }
                /* Erases the redundant copy if enabled */
                if (0u != context->redundantCopy)
                {
                    ptrRowCopy = ptrRow + (numRows * (context->rowSize / 4));
                    retStatus = EraseRow(ptrRowCopy, &writeRamBuffer[0u], context);
                    if (CY_EM_EEPROM_SUCCESS == result)
                    {
                        result = retStatus;
                    }
                }
            }
        }
    }

    return (result);
}


//--------------------------------------------------------------------------------------------------
// Cy_Em_EEPROM_NumWrites
//--------------------------------------------------------------------------------------------------
uint32_t Cy_Em_EEPROM_NumWrites(cy_stc_eeprom_context_t* context)
{
    uint32_t seqNum;

    (void)CheckLastWrittenRowIntegrity(&seqNum, context);

    return (seqNum);
}


/*******************************************************************************
* Function Name: CalcChecksum
****************************************************************************//**
*
* Implements CRC-8 used in the checksum calculation for the redundant copy
* algorithm.
*
* \param rowData
* The row data to be used to calculate the checksum.
*
* \param len
* The length of rowData.
*
* \return
* The calculated value of CRC-8.
*
*******************************************************************************/
static uint8_t CalcChecksum(const uint8_t rowData[], uint32_t len)
{
    uint8_t crc = CY_EM_EEPROM_CRC8_SEED;
    uint8_t i;
    uint16_t cnt = 0u;

    while (cnt != len)
    {
        crc ^= rowData[cnt];
        for (i = 0u; i < CY_EM_EEPROM_CRC8_POLYNOM_LEN; i++)
        {
            crc = CY_EM_EEPROM_CALCULATE_CRC8(crc);
        }
        cnt++;
    }

    return (crc);
}


/*******************************************************************************
* Function Name: CheckRanges
****************************************************************************//**
*
* Checks the provided configuration for parameter validness and size of
* the requested nvm.
*
* \param config
* The pointer to a configuration structure. See \ref cy_stc_eeprom_config2_t.

* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* Returns the status of operation. See cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t CheckRanges(
    const cy_stc_eeprom_config2_t* config,
    const cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_BAD_DATA;
    const cy_stc_eeprom_config2_t* cfg = config;
    bool isInRange = false;

    /* Checks the parameter validness */
    if ((0u != cfg->userNvmStartAddr) &&
        (0u != cfg->eepromSize) &&
        (1u >= cfg->simpleMode) &&
        (1u >= cfg->blockingWrite) &&
        (1u >= cfg->redundantCopy) &&
        (0u < cfg->wearLevelingFactor) &&
        (CY_EM_EEPROM_MAX_WEAR_LEVELING_FACTOR >= cfg->wearLevelingFactor))
    {
        /* Checks the nvm size and location */
        uint32_t startAddr = cfg->userNvmStartAddr;
        uint32_t size = GetPhysicalSize(context, config);

        isInRange = context->bd->is_in_range(context->bd->context, startAddr, size);
    }
    result = isInRange ? CY_EM_EEPROM_SUCCESS : CY_EM_EEPROM_BAD_DATA;

    return result;
}


/*******************************************************************************
* Function Name: WriteRow
****************************************************************************//**
*
* Writes one nvm row starting from the specified row address.
*
* \param rowAddr
* The pointer to the nvm row.
*
* \param rowData
* The pointer to the data to be written to the row.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* Returns the status of operation. See cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t WriteRow(
    uint32_t* const rowAddr,
    const uint32_t* rowData,
    const cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_SUCCESS;
    cy_rslt_t writeResult = CY_RSLT_SUCCESS;
    bool isEraseRequired = context->bd->is_erase_required(context->bd->context,
                                                          ((uint32_t)rowAddr),
                                                          context->rowSize);

    if (0u != context->blockingWrite)
    {
        if (isEraseRequired)
        {
            writeResult = context->bd->erase(context->bd->context,
                                             ((uint32_t)rowAddr),
                                             context->rowSize);
        }
        if (result == CY_RSLT_SUCCESS)
        {
            writeResult = context->bd->program(context->bd->context,
                                               ((uint32_t)rowAddr),
                                               context->rowSize,
                                               (uint8_t*)rowData);
        }
    }
    #if defined(MTB_BLOCK_STORAGE_NON_BLOCKING_SUPPORTED)
    else
    {
        if (isEraseRequired)
        {
            writeResult = context->bd->erase_nb(context->bd->context,
                                                ((uint32_t)rowAddr),
                                                context->rowSize);
        }
        if (result == CY_RSLT_SUCCESS)
        {
            writeResult = context->bd->program_nb(context->bd->context,
                                                  ((uint32_t)rowAddr),
                                                  context->rowSize,
                                                  (uint8_t*)rowData);
        }
    }
    #endif /* defined(MTB_BLOCK_STORAGE_NON_BLOCKING_SUPPORTED) */

    result  = ((writeResult == CY_RSLT_SUCCESS) ? CY_EM_EEPROM_SUCCESS : CY_EM_EEPROM_WRITE_FAIL);
    return result;
}


/*******************************************************************************
* Function Name: EraseRow
****************************************************************************//**
*
* Erases one nvm row starting from the specified row address. If the redundant
* copy option is enabled, the corresponding row in the redundant copy will also
* be erased.
*
* \param rowAddr
* The pointer of the nvm row.
*
*
* \param ramBuffAddr
* The address of the RAM buffer that contains zeroed data.
* Reserved for compatibility.
*
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* Returns the status of operation. See cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t EraseRow(
    const uint32_t* rowAddr,
    const uint32_t* ramBuffAddr,
    const cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_WRITE_FAIL;
    cy_rslt_t eraseResult = CY_RSLT_SUCCESS;
    bool isEraseRequired = context->bd->is_erase_required(context->bd->context,
                                                          ((uint32_t)rowAddr),
                                                          context->rowSize);

    if (0u != context->blockingWrite)
    {
        if (isEraseRequired)
        {
            eraseResult = context->bd->erase(context->bd->context,
                                             ((uint32_t)rowAddr),
                                             context->rowSize);
        }
        if (eraseResult == CY_RSLT_SUCCESS)
        {
            eraseResult = context->bd->program(context->bd->context,
                                               ((uint32_t)rowAddr),
                                               context->rowSize,
                                               (uint8_t*)ramBuffAddr);
        }
    }
    #if defined(MTB_BLOCK_STORAGE_NON_BLOCKING_SUPPORTED)
    else
    {
        if (isEraseRequired)
        {
            eraseResult = context->bd->erase_nb(context->bd->context,
                                                ((uint32_t)rowAddr),
                                                context->rowSize);
        }
        if (eraseResult == CY_RSLT_SUCCESS)
        {
            eraseResult = context->bd->program_nb(context->bd->context,
                                                  ((uint32_t)rowAddr),
                                                  context->rowSize,
                                                  (uint8_t*)ramBuffAddr);
        }
    }
    #endif /* defined(MTB_BLOCK_STORAGE_NON_BLOCKING_SUPPORTED) */

    result  = ((eraseResult == CY_RSLT_SUCCESS) ? CY_EM_EEPROM_SUCCESS : CY_EM_EEPROM_WRITE_FAIL);
    return result;
}


/*******************************************************************************
* Function Name: CalculateRowChecksum
****************************************************************************//**
*
* Calculates a checksum of the row specified by the ptrRow parameter.
* The first four bytes of the row are overwritten with zeros for calculation
* since it is the checksum location.
*
* \param ptrRow
* The pointer to a row.
*
* \param rowSize
* The size of the row passed in.
*
* \return
* The calculated value of CRC-8.
*
*******************************************************************************/
static uint32_t CalculateRowChecksum(const uint32_t* ptrRow, uint32_t rowSize)
{
    return ((uint32_t)CalcChecksum((const uint8_t*)((uint32_t)ptrRow + 1u),
                                   (rowSize) - CY_EM_EEPROM_U32));
}


/*******************************************************************************
* Function Name: GetStoredRowChecksum
****************************************************************************//**
*
* Returns the stored in the row checksum. The row specified by the ptrRow parameter.
* The first four bytes of the row is the checksum.
*
* \param ptrRow
* The pointer to a row.
*
* \return
* The stored row checksum.
*
*******************************************************************************/
static uint32_t GetStoredRowChecksum(const uint32_t* ptrRow)
{
    #if (CPUSS_FLASHC_ECT == 1)
    uint32_t ret = 0U;
    if (!WorkFlashIsErased((uint32_t*)&ptrRow[CY_EM_EEPROM_HEADER_CHECKSUM_OFFSET_U32], 4))
    {
        ret = ptrRow[CY_EM_EEPROM_HEADER_CHECKSUM_OFFSET_U32];
    }
    return (ret);
    #else /* (CPUSS_FLASHC_ECT == 1) */
    return (ptrRow[CY_EM_EEPROM_HEADER_CHECKSUM_OFFSET_U32]);
    #endif /* (CPUSS_FLASHC_ECT == 1) */
}


/*******************************************************************************
* Function Name: CheckRowChecksum
****************************************************************************//**
*
* Checks if the specified row has a valid stored CRC.
*
* \param ptrRow
* The pointer to a row.
*
* \param rowSize
* The size of the row passed in.
*
* \return
* Returns the operation status. See cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t CheckRowChecksum(const uint32_t* ptrRow, uint32_t rowSize)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_BAD_CHECKSUM;
    #if (CPUSS_FLASHC_ECT == 1)
    uint32_t lc_buf[CY_EM_EEPROM_MAXIMUM_ROW_SIZE / 4U];

    if (WorkFlashIsErased((uint32_t*)ptrRow, rowSize))
    {
        /* Fills the RAM buffer with flash data for the case when not a whole row is requested to be
           overwritten */
        (void)memset((void*)lc_buf, 0, rowSize);
    }
    else
    {
        (void)memcpy((void*)lc_buf, (const void*)ptrRow, rowSize);
    }
    if (GetStoredRowChecksum(ptrRow) == CalculateRowChecksum(lc_buf, rowSize))
    {
        result = CY_EM_EEPROM_SUCCESS;
    }

    #else /* (CPUSS_FLASHC_ECT == 1) */
    if (GetStoredRowChecksum(ptrRow) == CalculateRowChecksum(ptrRow, rowSize))
    {
        result = CY_EM_EEPROM_SUCCESS;
    }
    #endif /* (CPUSS_FLASHC_ECT == 1) */
    return (result);
}


/*******************************************************************************
* Function Name: GetStoredSeqNum
****************************************************************************//**
*
* Returns the stored in the row seqNum (Sequence Number).
* The row specified by ptrRow parameter.
* The second four bytes of the row is the seqNum.
*
* \param ptrRow
* The pointer to a row.
*
* \return
* The stored sequence number.
*
*******************************************************************************/
static uint32_t GetStoredSeqNum(const uint32_t* ptrRow)
{
    #if (CPUSS_FLASHC_ECT == 1)
    uint32_t ret = 0U;
    if (!WorkFlashIsErased((uint32_t*)&ptrRow[CY_EM_EEPROM_HEADER_SEQ_NUM_OFFSET_U32], 4))
    {
        ret = ptrRow[CY_EM_EEPROM_HEADER_SEQ_NUM_OFFSET_U32];
    }
    return (ret);
    #else /* (CPUSS_FLASHC_ECT == 1) */
    return (ptrRow[CY_EM_EEPROM_HEADER_SEQ_NUM_OFFSET_U32]);
    #endif /* (CPUSS_FLASHC_ECT == 1) */
}


/*******************************************************************************
* Function Name: DefineLastWrittenRow
****************************************************************************//**
*
* Performs a search of the last written row address of the Em_EEPROM associated
* with the context structure. If there were no writes to the Em_EEPROM, the
* the Em_EEPROM start address is used.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* Returns the operation status. See cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t DefineLastWrittenRow(cy_stc_eeprom_context_t* context)
{
    uint32_t numRows;
    uint32_t rowIndex;
    uint32_t seqNum;
    uint32_t seqNumMax;
    uint32_t* ptrRow;
    uint32_t* ptrRowMax;
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_SUCCESS;

    context->ptrLastWrittenRow = (uint32_t*)context->userNvmStartAddr;

    if (0u == context->simpleMode)
    {
        seqNumMax = 0u;
        numRows = context->numberOfRows * context->wearLevelingFactor;
        ptrRow = (uint32_t*)context->userNvmStartAddr;
        ptrRowMax = ptrRow;

        for (rowIndex = 0u; rowIndex < numRows; rowIndex++)
        {
            seqNum = GetStoredSeqNum(ptrRow);
            /* Is it a bigger number? */
            if (seqNum > seqNumMax)
            {
                if (CY_EM_EEPROM_SUCCESS == CheckRowChecksum(ptrRow, context->rowSize))
                {
                    seqNumMax = seqNum;
                    ptrRowMax = ptrRow;
                }
            }
            /* Switches to the next row */
            ptrRow += (context->rowSize/4);
        }

        /* Does the same search algorithm through the redundant copy if enabled */
        if (0u != context->redundantCopy)
        {
            for (rowIndex = 0u; rowIndex < numRows; rowIndex++)
            {
                seqNum = GetStoredSeqNum(ptrRow);
                /* Is it a bigger number? */
                if (seqNum > seqNumMax)
                {
                    if (CY_EM_EEPROM_SUCCESS == CheckRowChecksum(ptrRow, context->rowSize))
                    {
                        seqNumMax = seqNum;
                        ptrRowMax = ptrRow;
                        result = CY_EM_EEPROM_REDUNDANT_COPY_USED;
                    }
                }
                /* Switches to the next row */
                ptrRow += (context->rowSize /4);
            }
        }
        context->ptrLastWrittenRow = ptrRowMax;
    }

    return result;
}


/*******************************************************************************
* Function Name: CheckLastWrittenRowIntegrity
****************************************************************************//**
*
* Checks the CRC of the last written row. If CRC is valid then the function
* stores sequence number into the provided ptrSeqNum pointer. If CRC of the
* last written row is invalid, then the function searches for the row with
* the highest sequence number and valid CRC. In this case, the pointer to the
* last written row is redefined inside context structure and the sequence
* number of this row is returned.
*
* If the redundant copy is enabled, then the copy also considered at
* the correct CRC verification and sequence number searching.
*
* If Simple Mode is enabled, the sequence number is not available and
* this function returns 0.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \param ptrSeqNum
* The pointer to the sequence number.
*
* \return
* Returns the operation status. See cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t CheckLastWrittenRowIntegrity(
    uint32_t* ptrSeqNum,
    cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_SUCCESS;

    const uint32_t* ptrRowCopy;
    uint32_t seqNum = 0u;

    if (0u == context->simpleMode)
    {
        /* Checks the row CRC */
        if (CY_EM_EEPROM_SUCCESS == CheckRowChecksum(context->ptrLastWrittenRow, context->rowSize))
        {
            seqNum = GetStoredSeqNum(context->ptrLastWrittenRow);
        }
        else
        {
            /* CRC is bad. Checks if the redundant copy if enabled */
            if (0u != context->redundantCopy)
            {
                /* Calculates the redundant copy pointer */
                ptrRowCopy = ((context->numberOfRows * context->wearLevelingFactor) *
                              (context->rowSize/4)) + context->ptrLastWrittenRow;

                /* Checks CRC of the redundant copy */
                if (CY_EM_EEPROM_SUCCESS == CheckRowChecksum(ptrRowCopy, context->rowSize))
                {
                    seqNum = GetStoredSeqNum(ptrRowCopy);
                    result = CY_EM_EEPROM_REDUNDANT_COPY_USED;
                }
                else
                {
                    (void)DefineLastWrittenRow(context);
                    if (CY_EM_EEPROM_SUCCESS ==
                        CheckRowChecksum(context->ptrLastWrittenRow, context->rowSize))
                    {
                        seqNum = GetStoredSeqNum(context->ptrLastWrittenRow);
                    }
                    result = CY_EM_EEPROM_BAD_CHECKSUM;
                }
            }
            else
            {
                (void)DefineLastWrittenRow(context);
                if (CY_EM_EEPROM_SUCCESS == CheckRowChecksum(context->ptrLastWrittenRow,
                                                             context->rowSize))
                {
                    seqNum = GetStoredSeqNum(context->ptrLastWrittenRow);
                }
                result = CY_EM_EEPROM_BAD_CHECKSUM;
            }
        }
    }
    *ptrSeqNum = seqNum;

    return result;
}


/*******************************************************************************
* Function Name: GetNextRowPointer
****************************************************************************//**
*
* Increments the row pointer and performs out of the Em_EEPROM range verification.
* The memory range is defined as the number of rows multiplied by the wear leveling
* factor. It does not include the redundant copy area.
*
* \param ptrRow
* The pointer to the nvm row.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* Returns the pointer to the next row.
*
*******************************************************************************/
static uint32_t* GetNextRowPointer(
    uint32_t* ptrRow,
    const cy_stc_eeprom_context_t* context)
{
    uint32_t wlEndAddr = ((context->rowSize * context->numberOfRows) *
                          context->wearLevelingFactor) + context->userNvmStartAddr;
    uint32_t* lc_ptrRow = ptrRow;

    /* Gets the pointer to the next row to be processed without the range verification */
    lc_ptrRow += (context->rowSize /sizeof(uint32_t));

    if ((uint32_t)lc_ptrRow >= wlEndAddr)
    {
        lc_ptrRow = (uint32_t*)context->userNvmStartAddr;
    }
    return (lc_ptrRow);
}


/*******************************************************************************
* Function Name: GetReadRowPointer
****************************************************************************//**
*
* Calculates the row pointer to be used to read historic and headers data.
*
* \param ptrRow
* The pointer to the nvm row.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* Returns the pointer to the row where data is read from.
*
*******************************************************************************/
static uint32_t* GetReadRowPointer(
    uint32_t* ptrRow,
    const cy_stc_eeprom_context_t* context)
{
    uint32_t wlBlockSize;
    uint32_t* lc_ptrRow = ptrRow;

    /* Adjusts the row read pointer if wear leveling is enabled */
    if (context->wearLevelingFactor > 1u)
    {
        /* The size of one wear-leveling block */
        wlBlockSize = context->numberOfRows * (context->rowSize);
        /* Checks if it is the first block of the wear leveling */
        if (((uint32_t)lc_ptrRow) < (context->userNvmStartAddr + wlBlockSize))
        {
            /* Jumps to the last wear-leveling block */
            lc_ptrRow =
                (uint32_t*)((uint32_t)lc_ptrRow +
                            (wlBlockSize * (context->wearLevelingFactor - 1u)));
        }
        else
        {
            /* Decreases the read pointer for one wear-leveling block */
            lc_ptrRow = (uint32_t*)((uint32_t)lc_ptrRow - wlBlockSize);
        }
    }
    return (lc_ptrRow);
}


/*******************************************************************************
* Function Name: CopyHistoricData
****************************************************************************//**
*
* Copies relevant historic data into the specified buffer.
*
* The function includes the proper handling of the redundant copy and wear leveling
* if enabled.
*
* \param ptrRowWrite
* The pointer to the buffer to store historic data.
*
* \param ptrRow
* The pointer to the current active nvm row.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \return
* Returns the operation status. See cy_en_em_eeprom_status_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t CopyHistoricData(
    uint32_t* ptrRowWrite,
    uint32_t* ptrRow,
    const cy_stc_eeprom_context_t* context)
{
    cy_en_em_eeprom_status_t result = CY_EM_EEPROM_BAD_CHECKSUM;
    cy_rslt_t readResult = CY_RSLT_SUCCESS;
    uint32_t historicDataOffsetU32 = ((context->rowSize /4) /2);
    const uint32_t* ptrRowRead = GetReadRowPointer(ptrRow, context);

    if (CY_EM_EEPROM_SUCCESS == CheckRowChecksum(ptrRowRead, context->rowSize))
    {
        readResult = context->bd->read(context->bd->context,
                                       (uint32_t)&ptrRowRead[historicDataOffsetU32],
                                       context->byteInRow,
                                       (uint8_t*)&ptrRowWrite[historicDataOffsetU32]);
        result = (readResult == CY_RSLT_SUCCESS) ? CY_EM_EEPROM_SUCCESS : CY_EM_EEPROM_BAD_DATA;
    }
    else
    {
        /* CRC is bad. Checks if the redundant copy if enabled */
        if (0u != context->redundantCopy)
        {
            ptrRowRead += ((context->numberOfRows * context->wearLevelingFactor) *
                           (context->rowSize/4));
            if (CY_EM_EEPROM_SUCCESS == CheckRowChecksum(ptrRowRead, context->rowSize))
            {
                /* Copies the Em_EEPROM historic data from the redundant copy */
                readResult = context->bd->read(context->bd->context,
                                               (uint32_t)&ptrRowRead[historicDataOffsetU32],
                                               context->byteInRow,
                                               (uint8_t*)&ptrRowWrite[historicDataOffsetU32]);
                /* Reports that the redundant copy was used */
                result =
                    (readResult ==
                     CY_RSLT_SUCCESS) ? CY_EM_EEPROM_REDUNDANT_COPY_USED : CY_EM_EEPROM_BAD_DATA;
            }
        }
        if ((0 == GetStoredSeqNum(ptrRowRead)) &&
            (0 == GetStoredRowChecksum(ptrRowRead)))
        {
            /*
             * Considers a row with a bad checksum as the row that never has never been
             * written before if the sequence number and CRC values are the erased value.
             * In this case, reports zeros and does not
             * report the failed status of the write operation.
             */
            result = CY_EM_EEPROM_SUCCESS;
        }
    }
    return result;
}


/*******************************************************************************
* Function Name: CopyHeadersData
****************************************************************************//**
*
* Copies relevant data located in the headers into a row specified by the
* pointer ptrRow.
* The function includes the proper handling of a redundant copy and wear leveling
* if enabled.
*
* \param ptrRowWrite
* The pointer to the buffer where to store the headers data.
*
* \param ptrRow
* The pointer to the current active nvm row.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
*******************************************************************************/
static cy_en_em_eeprom_status_t CopyHeadersData(
    uint32_t* ptrRowWrite,
    uint32_t* ptrRow,
    const cy_stc_eeprom_context_t* context)
{
    uint32_t i;
    uint32_t strHistAddr;
    uint32_t endHistAddr;
    uint32_t strHeadAddr;
    uint32_t endHeadAddr;
    uint32_t dstOffset;
    uint32_t srcOffset;
    uint32_t sizeToCopy;
    cy_en_em_eeprom_status_t crcStatus = CY_EM_EEPROM_SUCCESS;
    uint32_t numReads = context->numberOfRows;
    uint32_t historicDataOffsetU32 = ((context->rowSize /2u) /4u);
    uint32_t* ptrRowWork;
    uint32_t* ptrRowRead = GetReadRowPointer(ptrRow, context);
    bool readingRam = false;

    /* Skips unwritten rows if any */
    if (numReads > GetStoredSeqNum(ptrRowWrite))
    {
        numReads = GetStoredSeqNum(ptrRowWrite);
        /* Only the first N rows have been written so far, only read up to the
            current row starting from the first row*/
        ptrRowRead = (uint32_t*)context->userNvmStartAddr;
    }
    else
    {
        /* No need to check the row about to write since it was checked at the last
            write, so start with the following row. */
        ptrRowRead = GetNextRowPointer(ptrRowRead, context);
    }

    if (context->numberOfRows <= 0U)
    {
        crcStatus = CY_EM_EEPROM_WRITE_FAIL;
    }

    if (crcStatus == CY_EM_EEPROM_SUCCESS)
    {
        /* The address within the Em_EEPROM storage of historic data of the specified by the ptrRow
           row */
        strHistAddr = ((((uint32_t)ptrRow - context->userNvmStartAddr) /
                        context->rowSize) % context->numberOfRows) *
                      (context->rowSize / 2u);
        endHistAddr = strHistAddr + (context->rowSize / 2u);

        for (i = 0u; i < numReads; i++)
        {
            ptrRowWork = ptrRowRead;

            /* For the last header-read operation, checks data in the recently created header */
            if (i >= (numReads - 1u))
            {
                ptrRowWork = ptrRowWrite;
                readingRam = true;
            }
            else
            {
                /* Checks CRC of the row to be read except the last row of a recently created header
                 */
                crcStatus = CheckRowChecksum(ptrRowWork, context->rowSize);
                if ((CY_EM_EEPROM_SUCCESS != crcStatus) && (0u != context->redundantCopy))
                {
                    /* Calculates the redundant copy pointer */
                    ptrRowWork += ((context->numberOfRows * context->wearLevelingFactor) *
                                   (context->rowSize/4));
                    crcStatus = CheckRowChecksum(ptrRowWork, context->rowSize);
                }
            }

            /* Skips the row if CRC is bad */
            if (CY_EM_EEPROM_SUCCESS == crcStatus)
            {
                /* The address of header data */
                strHeadAddr = ptrRowWork[CY_EM_EEPROM_HEADER_ADDR_OFFSET_U32];
                sizeToCopy = ptrRowWork[CY_EM_EEPROM_HEADER_LEN_OFFSET_U32];
                endHeadAddr = strHeadAddr + sizeToCopy;

                /* Skips the row if the header data address is out of the historic data address
                   range */
                if ((strHeadAddr < endHistAddr) && (endHeadAddr > strHistAddr))
                {
                    dstOffset = 0u;
                    srcOffset = 0u;

                    if (strHeadAddr >= strHistAddr)
                    {
                        dstOffset = strHeadAddr % context->byteInRow;
                        if (endHeadAddr > endHistAddr)
                        {
                            sizeToCopy = endHistAddr - strHeadAddr;
                        }
                    }
                    else
                    {
                        srcOffset = context->byteInRow - (strHeadAddr % context->byteInRow);
                        sizeToCopy = endHeadAddr - strHistAddr;
                    }

                    if (readingRam == true)
                    {
                        (void)memcpy((uint8_t*)((uint32_t)(&ptrRowWrite[historicDataOffsetU32]) +
                                                dstOffset),
                                     (const uint8_t*)((uint32_t)(&ptrRowWork[
                                                                     CY_EM_EEPROM_HEADER_DATA_OFFSET_U32
                                                                 ]) + srcOffset),
                                     sizeToCopy);
                    }
                    else
                    {
                        context->bd->read(context->bd->context,
                                          (uint32_t)(&ptrRowWork[CY_EM_EEPROM_HEADER_DATA_OFFSET_U32
                                                     ]) +
                                          srcOffset,
                                          sizeToCopy,
                                          (uint8_t*)((uint32_t)(&ptrRowWrite[historicDataOffsetU32])
                                                     +
                                                     dstOffset));
                    }
                }
            }
            ptrRowRead = GetNextRowPointer(ptrRowRead, context);
        }
    }
    return (crcStatus);
}


/*******************************************************************************
* Function Name: GetPhysicalSize
****************************************************************************//**
*
* Returns the size of nvm allocated for Em_EEPROM including wear leveling
* and a redundant copy overhead
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
* \param config
* The pointer to the configuration structure.
*
*******************************************************************************/
static uint32_t GetPhysicalSize(
    const cy_stc_eeprom_context_t* context,
    const cy_stc_eeprom_config2_t* config)
{
    /** Defines the size of nvm without wear leveling and redundant copy overhead */
    uint32_t num_data = context->numberOfRows * (context->rowSize);

    return (num_data * \
            ((((1uL - (config->simpleMode)) * (config->wearLevelingFactor)) *
              ((config->redundantCopy) + 1uL)) + (config->simpleMode)));
}


/*******************************************************************************
* Function Name: ComputeEEPROMProgramSize
****************************************************************************//**
*
* Makes sure that the program size in EEPROM context is an integer multiple of actual
* program size coming from the block storage API whenever the actual program size
* is smaller than the minimum EEPROM ROW SIZE in extended mode. In SimpleMode we have
* no need for a minimum EEPROM ROW SIZE so we can use the actualProgramSize returned
* by the block device.
*
* \param context
* The pointer to the Em_EEPROM context structure \ref cy_stc_eeprom_context_t.
*
*******************************************************************************/
static void ComputeEEPROMProgramSize(cy_stc_eeprom_context_t* context)
{
    uint32_t actualProgramSize =
        ((uint32_t)context->bd->get_program_size(context->bd->context, context->userNvmStartAddr));
    if ((actualProgramSize < CY_EM_EEPROM_MINIMUM_ROW_SIZE) && (context->simpleMode == 0))
    {
        context->rowSize = (((CY_EM_EEPROM_MINIMUM_ROW_SIZE - 1) / actualProgramSize) + 1) *
                           actualProgramSize;
    }
    else
    {
        context->rowSize = actualProgramSize;
    }
}


#if (CPUSS_FLASHC_ECT == 1)
/*******************************************************************************
* Function Name: WorkFlashIsErased
****************************************************************************//**
*
* Checks if XMC7xxx Work Flash is Blank/Erased state
*
* \param addr
* The pointer to the Work Flash starting address to check for blank
*
* \param size
* The size of the Work Flash to check from address passed
*
*
*******************************************************************************/
static bool WorkFlashIsErased(const uint32_t* addr, uint32_t size)
{
    cy_stc_flash_blankcheck_config_t config;
    cy_en_flashdrv_status_t status;

    config.addrToBeChecked = addr;
    config.numOfWordsToBeChecked = size / 4U;

    status = Cy_Flash_BlankCheck(&config, CY_FLASH_DRIVER_BLOCKING);

    return (status == CY_FLASH_DRV_SUCCESS);
}


#endif /* (CPUSS_FLASHC_ECT == 1) */

/* [] END OF FILE */
