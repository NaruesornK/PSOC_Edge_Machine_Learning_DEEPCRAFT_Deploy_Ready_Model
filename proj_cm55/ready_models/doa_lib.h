/*
*
* Imagimob Studio 4.4.845.65534+build20231124083044
* Copyright © 2023- Imagimob AB, All Rights Reserved.
*
*
*/

#ifndef LIBRARY_H_
#define LIBRARY_H_


#ifdef _MSC_VER
#pragma once
#endif

#include <stdint.h>

typedef struct {
    char *name;
    double TP; // True Positive or Correct Positive Prediction
    double FN; // False Negative or Incorrect Negative Prediction
    double FP; // False Positive or Incorrect Positive Prediction
    double TN; // True Negative or Correct Negative Prediction
    double TPR; // True Positive Rate or Sensitivity, Recall
    double TNR; // True Negative Rate or Specificity, Selectivity
    double PPV; // Positive Predictive Value or Precision
    double NPV; // Negative Predictive Value
    double FNR; // False Negative Rate or Miss Rate
    double FPR; // False Positive Rate or Fall-Out
    double FDR; // False Discovery Rate
    double FOR; // False Omission Rate
    double F1S; // F1 Score
} IMAI_stats;


#define IMAI_API_QUEUE

// All symbols in order
#define IMAI_SYMBOL_MAP {"unlabelled", "N", "S", "W", "E", "NE", "SW", "NW", "SE"}

// Model GUID (20 bytes)
#define IMAI_MODEL_ID {0x49, 0x4d, 0x52, 0x4d,0x71, 0x36, 0xc1, 0x56, 0xae, 0x4f, 0x3e, 0x47, 0xad, 0x94, 0xfb, 0x7e, 0x05, 0x0e, 0x7c, 0x4e}

// First nibble is bit encoding, second nibble is number of bytes
#define IMAGINET_TYPES_NONE    (0x0)
#define IMAGINET_TYPES_FLOAT32    (0x14)
#define IMAGINET_TYPES_FLOAT64    (0x18)
#define IMAGINET_TYPES_INT8    (0x21)
#define IMAGINET_TYPES_INT16    (0x22)
#define IMAGINET_TYPES_INT32    (0x24)
#define IMAGINET_TYPES_INT64    (0x28)
#define IMAGINET_TYPES_Q7    (0x31)
#define IMAGINET_TYPES_Q15    (0x32)
#define IMAGINET_TYPES_Q31    (0x34)
#define IMAGINET_TYPES_BOOL    (0x41)
#define IMAGINET_TYPES_STRING    (0x54)
#define IMAGINET_TYPES_D8    (0x61)
#define IMAGINET_TYPES_D16    (0x62)
#define IMAGINET_TYPES_D32    (0x64)
#define IMAGINET_TYPES_UINT8    (0x71)
#define IMAGINET_TYPES_UINT16    (0x72)
#define IMAGINET_TYPES_UINT32    (0x74)
#define IMAGINET_TYPES_UINT64    (0x78)

// dataout [9] (36 bytes)
#define IMAI_DATAOUT_RANK (1)
#define IMAI_DATAOUT_SHAPE (((int[]){9})
#define IMAI_DATAOUT_COUNT (9)
#define IMAI_DATAOUT_TYPE float
#define IMAI_DATAOUT_TYPE_ID IMAGINET_TYPES_FLOAT32
#define IMAI_DATAOUT_SHIFT 9
#define IMAI_DATAOUT_OFFSET 0
#define IMAI_DATAOUT_SCALE 1
#define IMAI_DATAOUT_SYMBOLS {"unlabelled", "N", "S", "W", "E", "NE", "SW", "NW", "SE"}

// datain [4] (16 bytes)
#define IMAI_DATAIN_RANK (1)
#define IMAI_DATAIN_SHAPE (((int[]){4})
#define IMAI_DATAIN_COUNT (4)
#define IMAI_DATAIN_TYPE float
#define IMAI_DATAIN_TYPE_ID IMAGINET_TYPES_FLOAT32
#define IMAI_DATAIN_SHIFT 0
#define IMAI_DATAIN_OFFSET 0
#define IMAI_DATAIN_SCALE 1
#define IMAI_DATAIN_SYMBOLS { }

#define IMAI_KEY_MAX (20)

// Return codes
#define IMAI_RET_SUCCESS 0
#define IMAI_RET_NODATA -1
#define IMAI_RET_NOMEM -2
#define IMAI_RET_TIMEDOUT -3
#define IMAI_RET_OUTOFBOUNDS -4


typedef struct PP_config {
    float confidence;     // confidence threshold to be compared against
    uint8_t average;          // averages every X values and checks against the confidence threshold before a prediction is made
    uint8_t subsequent;        // checks that X values are above the confidence threshold before a prediction is made
    uint8_t pool;            // creates a pool of values for checking pool_selection
    uint8_t pool_selection; // checks the pool so that at least X are above threshold before a prediction is made
} PP_config_t;


/**
 * Function to initialise the library, this should be called whenever the program boots up or wakes up
 * to clear the buffers from old irrelevant data
 */
void IMAI_DOA_init(void);

/**
 * This function is to pass data to the model/library and you should pass the data as it comes
 * PARAM data_in     this is an array with the number of features from the new sample and the expected size is
 *                     determined in IMAI_DATA_IN_COUNT
 * RET                 this is an int with a message whether this was performed successfully
 */
int IMAI_DOA_enqueue(const float *restrict data_in);

/**
 * This function is to extract output from the model/library and it should be checked for new predictions everytime a new data point is passed
 * PARAM data_out     this is an array with the number of classes in the system and the expected size is determined in IMAI_DATA_OUT_COUNT.
 *                     this will return an array of flags, 1 for trigger and 0 for not trigger
 * RET                 this is an int with a message whether this was performed successfully. If it's not 0 (successful) then data_out is not updated. If -3 (timed out) is returned
 */
int IMAI_DOA_dequeue(int *restrict data_out);

/**
 * This function is to adjust the sensitivity of the model/library, which is directly tied to the post processing internal to the library
 * PARAM postprocessing         this is a value for setting the postprocessing for model to activate a trigger. Depending on the model some of these maybe configurable
 * RET                 this is an int with a message whether this was performed successfullyv
 */
 
 /* Note: for better performance we recommend using a postprocessing sliding window of 3 predictions. Then check that the prediction is the same, take the average of the confidence and 
    apply a threshold.*/
int IMAI_DOA_sensitivity(PP_config_t postprocessing);

/**
 * This function resets the postprocessing to its original settings
 */
void IMAI_DOA_sensitivity_reset(void);


#endif /* LIBRARY_H_ */
