/*
*
* DEEPCRAFT Ready Model
* Copyright © 2024- Imagimob AB, an Infineon Technology Company, All Rights Reserved.
*
*
*/

#ifndef FALL_LIB_H_
#define FALL_LIB_H_


#ifdef _MSC_VER
#pragma once
#endif

#include <stdint.h>


// All symbols in order
#define IMAI_SYMBOL_MAP {"unlabeled", "fall"}

// Model GUID (20 bytes)
#define IMAI_MODEL_ID {0x49, 0x4d, 0x52, 0x4d, 0x2d, 0x46, 0x61, 0x6c, 0x6c, 0x2d, 0x31, 0x2e, 0x39, 0x2d, 0x32, 0x30, 0x31, 0x38, 0x30, 0x39}

// First nibble is bit encoding, second nibble is number of bytes
#define IMAGINET_TYPES_FLOAT32    (0x14)

// data_in [1] (4 bytes)
#define IMAI_DATA_IN_COUNT (3)
#define IMAI_DATA_IN_TYPE float
#define IMAI_DATA_IN_TYPE_ID IMAGINET_TYPES_FLOAT32
#define IMAI_DATA_IN_SCALE (1)
#define IMAI_DATA_IN_OFFSET (0)
#define IMAI_DATA_IN_IS_QUANTIZED (0)

// data_out [2] (8 bytes)
#define IMAI_DATA_OUT_COUNT (2)
#define IMAI_DATA_OUT_TYPE int
#define IMAI_DATA_OUT_TYPE_ID IMAGINET_TYPES_FLOAT32
#define IMAI_DATA_OUT_SCALE (1)
#define IMAI_DATA_OUT_OFFSET (0)
#define IMAI_DATA_OUT_IS_QUANTIZED (0)

#define IMAI_KEY_MAX (49)



// Return codes
#define IMAI_RET_SUCCESS 0
#define IMAI_RET_NODATA -1
#define IMAI_RET_NOMEM -2
#define IMAI_RET_TIMEDOUT -3
#define IMAI_RET_OUTOFBOUNDS -4


/**
 * Function to initialise the library, this should be called whenever the program boots up or wakes up
 * to clear the buffers from old irrelevant data
 */
void IMAI_FED_init(void);

/**
 * This function is to pass data to the model/library and you should pass the data as it comes
 * PARAM data_in     this is an array with the number of features from the new sample and the expected size is
 *                     determined in IMAI_DATA_IN_COUNT
 * RET                 this is an int with a message whether this was performed successfully
 */
int IMAI_FED_enqueue(const float *restrict data_in);

/**
 * This function is to extract output from the model/library and it should be checked for new predictions everytime a new data point is passed
 * PARAM data_out     this is an array with the number of classes in the system and the expected size is determined in IMAI_DATA_OUT_COUNT.
 *                     this will return an array of flags, 1 for trigger and 0 for not trigger
 * RET                 this is an int with a message whether this was performed successfully. If it's not 0 (successful) then data_out is not updated. If -3 (timed out) is returned
 */
int IMAI_FED_dequeue(int *restrict data_out);


#endif /* FALL_LIB_H_ */
