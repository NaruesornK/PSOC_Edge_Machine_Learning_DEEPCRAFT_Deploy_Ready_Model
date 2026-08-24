/*
* ImagiNet Compiler 5.14.5788.0+51541183f4b9ca433a12c0d5c3809d9a54d31f3c
* Copyright © 2023- Imagimob AB, All Rights Reserved.
* 
* Generated at 08/22/2026 04:56:05 UTC. Any changes will be lost.
* 
* Model ID  64023c59-a015-409e-94b8-a3671c44ee7a
* 
* Memory    Size                      Efficiency
* Buffers   351300 bytes (RAM)        100 %
* State     891920 bytes (RAM)        100 %
* Readonly  1682000 bytes (Flash)     100 %
* 
* Exported functions:
* 
*  @param datain Input features. Input uint8_t[320,320,3].
*  @param dataout Output features. Output float[6,5].
*  @return IPWIN_RET_SUCCESS (0) or IPWIN_RET_NODATA (-1), IPWIN_RET_ERROR (-2), IPWIN_RET_STREAMEND (-3)
*  int YOLO_IMAI_compute(const uint8_t *datain, float *dataout);
* 
*  @description: Closes and flushes streams, free any heap allocated memory.
*  void YOLO_IMAI_finalize(void);
* 
*  @description: Initializes buffers to initial state.
*  @return IPWIN_RET_SUCCESS (0) or IPWIN_RET_NODATA (-1), IPWIN_RET_ERROR (-2), IPWIN_RET_STREAMEND (-3)
*  int YOLO_IMAI_init(void);
* 
* 
* Disclaimer:
*   The generated code relies on the optimizations done by the C compiler.
*   For example many for-loops of length 1 must be removed by the optimizer.
*   This can only be done if the functions are inlined and simplified.
*   Check disassembly if unsure.
*   tl;dr Compile using gcc with -O3 or -Ofast
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mtb_ml_model.h"
#define YOLO_IMAI_API_FUNCTION

typedef int8_t q7_t;         // 8-bit fractional data type in Q1.7 format.
typedef int16_t q15_t;       // 16-bit fractional data type in Q1.15 format.
typedef int32_t q31_t;       // 32-bit fractional data type in Q1.31 format.
typedef int64_t q63_t;       // 64-bit fractional data type in Q1.63 format.
typedef float timestamp_t;

// Model GUID (16 bytes)
#define YOLO_IMAI_MODEL_ID {0x59, 0x3c, 0x02, 0x64, 0x15, 0xa0, 0x9e, 0x40, 0x94, 0xb8, 0xa3, 0x67, 0x1c, 0x44, 0xee, 0x7a}


// First nibble is bit encoding, second nibble is number of bytes
#define IMAGINET_TYPES_NONE	(0x0)
#define IMAGINET_TYPES_FLOAT32	(0x14)
#define IMAGINET_TYPES_FLOAT64	(0x18)
#define IMAGINET_TYPES_INT8	(0x21)
#define IMAGINET_TYPES_INT16	(0x22)
#define IMAGINET_TYPES_INT32	(0x24)
#define IMAGINET_TYPES_INT64	(0x28)
#define IMAGINET_TYPES_Q7	(0x31)
#define IMAGINET_TYPES_Q15	(0x32)
#define IMAGINET_TYPES_Q31	(0x34)
#define IMAGINET_TYPES_BOOL	(0x41)
#define IMAGINET_TYPES_STRING	(0x54)
#define IMAGINET_TYPES_D8	(0x61)
#define IMAGINET_TYPES_D16	(0x62)
#define IMAGINET_TYPES_D32	(0x64)
#define IMAGINET_TYPES_UINT8	(0x71)
#define IMAGINET_TYPES_UINT16	(0x72)
#define IMAGINET_TYPES_UINT32	(0x74)
#define IMAGINET_TYPES_UINT64	(0x78)


#define YOLO_IMAI_COMPUTE_INPUTS (1)
#define YOLO_IMAI_COMPUTE_OUTPUTS (1)
#define YOLO_IMAI_COMPUTE_IN_TYPE uint8_t
#define YOLO_IMAI_COMPUTE_IN_TYPE_ID IMAGINET_TYPES_UINT8
#define YOLO_IMAI_COMPUTE_OUT_TYPE float
#define YOLO_IMAI_COMPUTE_OUT_TYPE_ID IMAGINET_TYPES_FLOAT32
#define YOLO_IMAI_COMPUTE_OUT_NO_COPY false

// datain [320,320,3] (307200 bytes)
#define YOLO_IMAI_DATAIN_RANK (3)
#define YOLO_IMAI_DATAIN_SHAPE ((int[]){3, 320, 320})
#define YOLO_IMAI_DATAIN_COUNT (307200)
#define YOLO_IMAI_DATAIN_BYTES (307200)
#define YOLO_IMAI_DATAIN_TYPE uint8_t
#define YOLO_IMAI_DATAIN_TYPE_ID IMAGINET_TYPES_UINT8
#define YOLO_IMAI_DATAIN_SHIFT 0
#define YOLO_IMAI_DATAIN_OFFSET 0
#define YOLO_IMAI_DATAIN_SCALE 1
#define YOLO_IMAI_DATAIN_SYMBOLS { }
// Temporary backwards compatibility defines, use the variants below
#define YOLO_IMAI_DATA_IN_RANK YOLO_IMAI_DATAIN_RANK
#define YOLO_IMAI_DATA_IN_SHAPE YOLO_IMAI_DATAIN_SHAPE
#define YOLO_IMAI_DATA_IN_COUNT YOLO_IMAI_DATAIN_COUNT
#define YOLO_IMAI_DATA_IN_BYTES YOLO_IMAI_DATAIN_BYTES
#define YOLO_IMAI_DATA_IN_TYPE YOLO_IMAI_DATAIN_TYPE
#define YOLO_IMAI_DATA_IN_TYPE_ID YOLO_IMAI_DATAIN_TYPE_ID
#define YOLO_IMAI_DATA_IN_SHIFT YOLO_IMAI_DATAIN_SHIFT
#define YOLO_IMAI_DATA_IN_OFFSET YOLO_IMAI_DATAIN_OFFSET
#define YOLO_IMAI_DATA_IN_SCALE YOLO_IMAI_DATAIN_SCALE
#define YOLO_IMAI_DATA_IN_SYMBOLS YOLO_IMAI_DATAIN_SYMBOLS

// dataout [6,5] (120 bytes)
#define YOLO_IMAI_DATAOUT_RANK (2)
#define YOLO_IMAI_DATAOUT_SHAPE ((int[]){5, 6})
#define YOLO_IMAI_DATAOUT_COUNT (30)
#define YOLO_IMAI_DATAOUT_BYTES (120)
#define YOLO_IMAI_DATAOUT_TYPE float
#define YOLO_IMAI_DATAOUT_TYPE_ID IMAGINET_TYPES_FLOAT32
#define YOLO_IMAI_DATAOUT_SHIFT 0
#define YOLO_IMAI_DATAOUT_OFFSET 0
#define YOLO_IMAI_DATAOUT_SCALE 1
#define YOLO_IMAI_DATAOUT_SYMBOLS { }
// Temporary backwards compatibility defines, use the variants below
#define YOLO_IMAI_DATA_OUT_RANK YOLO_IMAI_DATAOUT_RANK
#define YOLO_IMAI_DATA_OUT_SHAPE YOLO_IMAI_DATAOUT_SHAPE
#define YOLO_IMAI_DATA_OUT_COUNT YOLO_IMAI_DATAOUT_COUNT
#define YOLO_IMAI_DATA_OUT_BYTES YOLO_IMAI_DATAOUT_BYTES
#define YOLO_IMAI_DATA_OUT_TYPE YOLO_IMAI_DATAOUT_TYPE
#define YOLO_IMAI_DATA_OUT_TYPE_ID YOLO_IMAI_DATAOUT_TYPE_ID
#define YOLO_IMAI_DATA_OUT_SHIFT YOLO_IMAI_DATAOUT_SHIFT
#define YOLO_IMAI_DATA_OUT_OFFSET YOLO_IMAI_DATAOUT_OFFSET
#define YOLO_IMAI_DATA_OUT_SCALE YOLO_IMAI_DATAOUT_SCALE
#define YOLO_IMAI_DATA_OUT_SYMBOLS YOLO_IMAI_DATAOUT_SYMBOLS

#define YOLO_IMAI_KEY_MAX (9)

// Return codes
#define YOLO_IMAI_RET_SUCCESS 0
#define YOLO_IMAI_RET_NODATA -1
#define YOLO_IMAI_RET_ERROR -2
#define YOLO_IMAI_RET_STREAMEND -3

#define IPWIN_RET_SUCCESS 0
#define IPWIN_RET_NODATA -1
#define IPWIN_RET_ERROR -2
#define IPWIN_RET_STREAMEND -3

// Exported methods
int YOLO_IMAI_compute(const uint8_t *restrict datain, float *restrict dataout);
void YOLO_IMAI_finalize(void);
int YOLO_IMAI_init(void);

/// @brief This method will print neural network inference profiling results
void YOLO_IMAI_mtb_models_profile_log();
/// @brief This method will print neural network information
void YOLO_IMAI_mtb_models_print_info();
extern uint8_t YOLO_IMAI_mtb_models_count;
extern mtb_ml_model_t* YOLO_IMAI_mtb_models[];

// Profiling regions
#define YOLO_IMAI_REGIONS_COUNT 0
#define YOLO_IMAI_REGIONS_NAMES {}
#define YOLO_IMAI_REGIONS_NOTES {}

// Call macros — invoke any exported function via a void* array of arguments
#define YOLO_IMAI_COMPUTE_PTR(a) YOLO_IMAI_compute((const uint8_t *)(a)[0], (float *)(a)[1])
#define YOLO_IMAI_FINALIZE_PTR(a) YOLO_IMAI_finalize()
#define YOLO_IMAI_INIT_PTR(a) YOLO_IMAI_init()

typedef enum {
    YOLO_IMAI_PARAM_UNDEFINED = 0,
    YOLO_IMAI_PARAM_INPUT = 1,
    YOLO_IMAI_PARAM_OUTPUT = 2,
    YOLO_IMAI_PARAM_REFERENCE = 3,
    YOLO_IMAI_PARAM_HANDLE = 7,
    YOLO_IMAI_PARAM_CALLBACK = 8,
    YOLO_IMAI_PARAM_OUTPUT_REF = 18,
} YOLO_IMAI_param_attrib;

typedef char *label_text_t;

typedef struct {
    char* name;
    int size;
    label_text_t *labels;
} YOLO_IMAI_shape_dim;

typedef struct {
    char* name;
    YOLO_IMAI_param_attrib attrib;
    int32_t rank;
    YOLO_IMAI_shape_dim *shape;
    int32_t count;
    int32_t bytes;
    int32_t type_id;
    float frequency;
    int shift;
    float scale;
    long offset;
} YOLO_IMAI_param_def;

typedef enum {
    YOLO_IMAI_FUNC_ATTRIB_NONE = 0,
    YOLO_IMAI_FUNC_ATTRIB_CAN_FAIL = 1,
    YOLO_IMAI_FUNC_ATTRIB_PUBLIC = 2,
    YOLO_IMAI_FUNC_ATTRIB_INIT = 4,
    YOLO_IMAI_FUNC_ATTRIB_DESTRUCTOR = 8,
} YOLO_IMAI_func_attrib;

typedef struct {
    char* name;
    char* description;
    void* fn_ptr;
    YOLO_IMAI_func_attrib attrib;
    int32_t param_count;
    YOLO_IMAI_param_def *param_list;
} YOLO_IMAI_func_def;

typedef struct {
    uint32_t size;
    uint32_t peak_usage;
} YOLO_IMAI_mem_usage;

typedef enum {
    YOLO_IMAI_API_TYPE_UNDEFINED = 0,
    YOLO_IMAI_API_TYPE_FUNCTION = 1,
    YOLO_IMAI_API_TYPE_QUEUE = 2,
    YOLO_IMAI_API_TYPE_QUEUE_TIME = 3,
    YOLO_IMAI_API_TYPE_CALLBACK = 4,
    YOLO_IMAI_API_TYPE_CALLBACK_TIME = 5,
} YOLO_IMAI_api_type;

typedef struct {
    uint32_t api_ver;
    uint8_t id[16];
    YOLO_IMAI_api_type api_type;
    char* prefix;
    YOLO_IMAI_mem_usage buffer_mem;
    YOLO_IMAI_mem_usage static_mem;
    YOLO_IMAI_mem_usage readonly_mem;
    int32_t func_count;
    YOLO_IMAI_func_def *func_list;
} YOLO_IMAI_api_def;

YOLO_IMAI_api_def *YOLO_IMAI_api(void);

#define YOLO_IMAI_INPUT_META_COUNT 1
#define YOLO_IMAI_OUTPUT_META_COUNT 1
#define YOLO_IMAI_INPUT_META(i) ((YOLO_IMAI_param_def*)(&YOLO_IMAI_api()->func_list[0].param_list[(i)]))
#define YOLO_IMAI_OUTPUT_META(i) ((YOLO_IMAI_param_def*)(&YOLO_IMAI_api()->func_list[0].param_list[(i) + 1]))

