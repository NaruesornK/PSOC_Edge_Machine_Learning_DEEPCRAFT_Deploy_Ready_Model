################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Makefile for configuring the Cortex-M55 core project
#
################################################################################
# (c) 2025-2026, Infineon Technologies AG, or an affiliate of Infineon
# Technologies AG. All rights reserved.
# This software, associated documentation and materials ("Software") is
# owned by Infineon Technologies AG or one of its affiliates ("Infineon")
# and is protected by and subject to worldwide patent protection, worldwide
# copyright laws, and international treaty provisions. Therefore, you may use
# this Software only as provided in the license agreement accompanying the
# software package from which you obtained this Software. If no license
# agreement applies, then any use, reproduction, modification, translation, or
# compilation of this Software is prohibited without the express written
# permission of Infineon.
# 
# Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
# IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
# INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
# THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
# SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
# Infineon reserves the right to make changes to the Software without notice.
# You are responsible for properly designing, programming, and testing the
# functionality and safety of your intended application of the Software, as
# well as complying with any legal requirements related to its use. Infineon
# does not guarantee that the Software will be free from intrusion, data theft
# or loss, or other breaches ("Security Breaches"), and Infineon shall have
# no liability arising out of any Security Breaches. Unless otherwise
# explicitly approved by Infineon, the Software may not be used in any
# application where a failure of the Product or any consequences of the use
# thereof can reasonably be expected to result in personal injury.
################################################################################

include ../common.mk

################################################################################
# Basic Configuration
################################################################################

# model selection

# For KIT_PSE84_EVAL_EPC2, KIT_PSE84_EVAL_EPC4, KIT_PSE84_AI and KIT_PSE84_HMI below models are supported.
# COUGH_MODEL
# ALARM_MODEL
# BABYCRY_MODEL
# DIRECTIONOFARRIVAL_MODEL
# FALLDETECTION_MODEL 
# GESTURE_MODEL (For KIT_PSE84_AI and KIT_PSE84_HMI only)
# SIREN_MODEL

MODEL_SELECTION = BABYCRY_MODEL

ifeq (,$(filter $(TARGET),APP_KIT_PSE84_EVAL_EPC2 APP_KIT_PSE84_EVAL_EPC4))
else
    ifeq (,$(filter $(strip $(MODEL_SELECTION)),COUGH_MODEL ALARM_MODEL BABYCRY_MODEL DIRECTIONOFARRIVAL_MODEL FALLDETECTION_MODEL SIREN_MODEL))
        $(error Invalid MODEL_SELECTION "$(MODEL_SELECTION)" for TARGET=$(TARGET))
    endif
endif

ifeq (,$(filter $(TARGET),APP_KIT_PSE84_AI))
else
    ifeq (,$(filter $(strip $(MODEL_SELECTION)),COUGH_MODEL ALARM_MODEL BABYCRY_MODEL GESTURE_MODEL DIRECTIONOFARRIVAL_MODEL FALLDETECTION_MODEL SIREN_MODEL))
        $(error Invalid MODEL_SELECTION "$(MODEL_SELECTION)" for TARGET=$(TARGET))
    endif
endif

ifeq (,$(filter $(TARGET),APP_KIT_PSE84_HMI))
else
    ifeq (,$(filter $(strip $(MODEL_SELECTION)),COUGH_MODEL ALARM_MODEL BABYCRY_MODEL GESTURE_MODEL DIRECTIONOFARRIVAL_MODEL SIREN_MODEL))
        $(error Invalid MODEL_SELECTION "$(MODEL_SELECTION)" for TARGET=$(TARGET))
    endif
endif

# Name of application (used to derive name of final linked file).
#
# If APPNAME is edited, ensure to update or regenerate launch
# configurations for your IDE.
APPNAME=proj_cm55

# If set to "true" or "1", display full command-lines when building.
VERBOSE=

# Neural Network Configuration. Options include
#
# float    -- floating point for the input data and weights
# int8x8   -- 8-bit fixed-point for the input data and weights
ifeq (SIREN_MODEL, $(MODEL_SELECTION))
NN_TYPE=float
else
NN_TYPE=int8x8
endif

# Choose the inference engine
# tflm      -- TensorFlow Lite for Microcontrollers inference engine with interpreter
# tflm_less -- TensorFlow Lite for Microcontrollers inference engine interpreter-less
NN_INFERENCE_ENGINE=tflm

################################################################################
# Advanced Configuration
################################################################################

# Enable optional code that is ordinarily disabled by default.
#
# Available components depend on the specific targeted hardware and firmware
# in use. In general, if you have
#
#    COMPONENTS=foo bar
#
# ... then code in directories named COMPONENT_foo and COMPONENT_bar will be
# added to the build
#
COMPONENTS+=FREERTOS RTOS_AWARE

# Like COMPONENTS, but disable optional code that was enabled by default.
DISABLE_COMPONENTS=

# Set this application to run at the CM55
CORE=CM55
CORE_NAME=CM55_0
NPU=U55

# By default the build system automatically looks in the Makefile's directory
# tree for source code and builds it. The SOURCES variable can be used to
# manually add source code to the build process from a location not searched
# by default, or otherwise not found by the build system.
SOURCES+=

# Like SOURCES, but for include directories. Value should be paths to
# directories (without a leading -I).
INCLUDES+=

# Add additional defines to the build process (without a leading -D).
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF

################################################################################
# Display: Waveshare 4.3" DSI LCD (800x480) + FT5406 capacitive touch
#
# ค่าทั้งหมดยกมาจาก mtb-example-psoc-edge-gfx-lvgl-demo ของ Infineon
# ซึ่งรองรับ KIT_PSE84_AI + จอตัวนี้เป็น config เริ่มต้น (CONFIG_DISPLAY=W4P3INCH_DISP)
#
# จอต่อที่ J10 (RPi MIPI DSI, FPC 15 pin) ไม่ต้อง rework ฮาร์ดแวร์
# ตัวคุม panel อยู่ที่ I2C 0x45 / ทัชอยู่ที่ 0x38 ทั้งคู่บน SCB5 (CYBSP_I2C_DISPLAY_CONTROLLER)
################################################################################
DEFINES+=MTB_DISPLAY_W4P3INCH_RPI MTB_CTP_FT5406
DEFINES+=_BAREMETAL=0

ifeq (APP_KIT_PSE84_AI, $(TARGET))
DEFINES+=USE_KIT_PSE84_AI
endif

# LVGL ส่งไดรเวอร์ของแพลตฟอร์มอื่นมาเยอะมาก ตัดทิ้งไม่งั้น build ไม่ผ่าน/ช้ามาก
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/nxp
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/renesas
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/dma2d
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/espressif
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/eve
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/nanovg
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/nema_gfx
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/opengles
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/sdl
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/convert/helium
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/convert/neon
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/sw/blend/neon
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/sw/blend/arm2d
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/sw/blend/riscv_v
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/thorvg
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/FT800-FT813
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/frogfs
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/gltf
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/gstreamer
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/libwebp
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/nanovg
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/svg
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/vg_lite_driver
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/barcode
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/ffmpeg
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/freetype
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/fsdrv
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/gif
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/libjpeg_turbo
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/libpng
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/lz4
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/rlottie
CY_IGNORE+=$(SEARCH_lvgl)/src/libs/tiny_ttf
CY_IGNORE+=$(SEARCH_lvgl)/src/debugging/test
CY_IGNORE+=$(SEARCH_lvgl)/src/debugging/vg_lite_tvg
CY_IGNORE+=$(SEARCH_lvgl)/src/debugging/monkey
CY_IGNORE+=$(SEARCH_lvgl)/src/drivers
CY_IGNORE+=$(SEARCH_lvgl)/src/widgets/property
CY_IGNORE+=$(SEARCH_lvgl)/src/widgets/3dtexture
CY_IGNORE+=$(SEARCH_lvgl)/src/widgets/gif
CY_IGNORE+=$(SEARCH_lvgl)/src/widgets/lottie
CY_IGNORE+=$(SEARCH_lvgl)/src/widgets/arclabel
CY_IGNORE+=$(SEARCH_lvgl)/src/stdlib/micropython
CY_IGNORE+=$(SEARCH_lvgl)/src/stdlib/rtthread
CY_IGNORE+=$(SEARCH_lvgl)/src/stdlib/uefi
CY_IGNORE+=$(SEARCH_lvgl)/src/font/font_manager
CY_IGNORE+=$(SEARCH_lvgl)/tests
CY_IGNORE+=$(SEARCH_lvgl)/examples
CY_IGNORE+=$(SEARCH_lvgl)/env_support
CY_IGNORE+=$(SEARCH_lvgl)/demos

# ไฟล์ 4 ตัวนี้ Infineon แก้เวอร์ชันของตัวเองไว้ อยู่ใน source/ ของเรา
# ต้องกันของเดิมใน lvgl ไม่ให้คอมไพล์ซ้ำ ไม่งั้น symbol ชนกัน
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite.c
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/vg_lite/lv_draw_vg_lite_img.c
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/vg_lite/lv_vg_lite_utils.c
CY_IGNORE+=$(SEARCH_lvgl)/src/draw/convert/lv_draw_buf_convert.c

# Add additional defines related to ARM Helium and DSP extensions
DEFINES+=ARM_MATH_HELIUM ARM_MATH_DSP ARM_MATH_AUTOVECTORIZE


# Add additional components related to MTB ML
COMPONENTS+=U55

# Add additional define/components to select the inference engine
ifeq (tflm, $(NN_INFERENCE_ENGINE))
COMPONENTS+=ML_TFLM
DEFINES+=TF_LITE_STATIC_MEMORY
endif

# Add define to build code for CM55 and include additional files
DEFINES+=CY_ML_MODEL_MEM=.cy_socmem_data
DEFINES+=CY_ML_ARENA_MEM=.cy_socmem_data
COMPONENTS+=CMSIS_DSP

# Depending which Neural Network Type, add a specific DEFINE and COMPONENT
ifeq (float, $(NN_TYPE))
COMPONENTS+=ML_FLOAT32
endif
ifeq (int8x8, $(NN_TYPE))
COMPONENTS+=ML_INT8x8
endif

INCLUDES+=$(SEARCH_cmsis)/COMPONENT_CMSIS_NN/Include

INCLUDES+=./ready_models
INCLUDES+=../shared
CY_IGNORE+=$(SEARCH_CMSIS-DSP)

ifeq (COUGH_MODEL, $(MODEL_SELECTION))
DEFINES+=COUGH_MODEL
# Additional / custom libraries to link in to the application.
LDLIBS+=./ready_models/CONFIG_$(CONFIG)/TOOLCHAIN_$(TOOLCHAIN)/cough_lib_eval.a
endif

ifeq (BABYCRY_MODEL, $(MODEL_SELECTION))
DEFINES+=BABYCRY_MODEL
# Additional / custom libraries to link in to the application.
LDLIBS+=./ready_models/CONFIG_$(CONFIG)/TOOLCHAIN_$(TOOLCHAIN)/babycry_lib_eval.a
endif

ifeq (ALARM_MODEL, $(MODEL_SELECTION))
DEFINES+=ALARM_MODEL
# Additional / custom libraries to link in to the application.
LDLIBS+=./ready_models/CONFIG_$(CONFIG)/TOOLCHAIN_$(TOOLCHAIN)/alarm_siren_lib_eval.a
endif

ifeq (GESTURE_MODEL, $(MODEL_SELECTION))
DEFINES+=GESTURE_MODEL
# Additional / custom libraries to link in to the application.
LDLIBS+=./ready_models/CONFIG_$(CONFIG)/TOOLCHAIN_$(TOOLCHAIN)/gesture_lib_eval.a
endif

ifeq (FALLDETECTION_MODEL, $(MODEL_SELECTION))
DEFINES+=FALLDETECTION_MODEL
# Additional / custom libraries to link in to the application.
LDLIBS+=./ready_models/CONFIG_$(CONFIG)/TOOLCHAIN_$(TOOLCHAIN)/fall_lib_eval.a
endif

ifeq (DIRECTIONOFARRIVAL_MODEL, $(MODEL_SELECTION))
DEFINES+=DIRECTIONOFARRIVAL_MODEL
# Additional / custom libraries to link in to the application.
LDLIBS+=./ready_models/CONFIG_$(CONFIG)/TOOLCHAIN_$(TOOLCHAIN)/doa_lib_eval.a
endif

ifeq (SIREN_MODEL, $(MODEL_SELECTION))
DEFINES+=SIREN_MODEL
# Additional / custom libraries to link in to the application.
LDLIBS+=./ready_models/CONFIG_$(CONFIG)/TOOLCHAIN_$(TOOLCHAIN)/siren_lib_eval.a
endif


ifeq (GESTURE_MODEL, $(MODEL_SELECTION))
  CY_IGNORE+=source/audio.c
  CY_IGNORE+=source/imu.c
  CY_IGNORE+=source/doa.c
else
  ifeq (FALLDETECTION_MODEL, $(MODEL_SELECTION))
    CY_IGNORE+=source/radar.c
    CY_IGNORE+=source/radar
    CY_IGNORE+=source/audio.c
    CY_IGNORE+=source/doa.c
  else
    ifeq (DIRECTIONOFARRIVAL_MODEL, $(MODEL_SELECTION))
      CY_IGNORE+=source/radar.c
      CY_IGNORE+=source/radar
      CY_IGNORE+=source/audio.c
      CY_IGNORE+=source/imu.c
    else
      CY_IGNORE+=source/radar.c
      CY_IGNORE+=source/radar
      CY_IGNORE+=source/imu.c
      CY_IGNORE+=source/doa.c
    endif
  endif
endif

# Option to remap the sensor orienation to align with CY8CKIT-062S2-AI.
#
# ENABLED   - Sensor data is remapped 
#
# DISABLED  - Actual sensor data 
SENSOR_REMAPPING=DISABLED

ifeq ($(SENSOR_REMAPPING),ENABLED)
DEFINES+=USE_SENSOR_REMAPPING
endif

# Check which kit is being used
ifeq (APP_KIT_PSE84_HMI, $(TARGET))
DEFINES+=USE_KIT_PSE84_HMI
endif

# Select softfp or hardfp floating point. Default is softfp.
VFP_SELECT+=hardfp

ifeq ($(VFP_SELECT),softfp)
$(error softfp floating point is not supported for this code example)
endif

# Additional / custom C compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
# CFLAG is added to avoid warning from sensor-dsp library fetched by xensiv-radar-presence library
CFLAGS+=-fno-finite-math-only

# Additional / custom C++ compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CXXFLAGS+=

# Additional / custom assembler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
ASFLAGS+=

# Additional / custom linker flags.
LDFLAGS+=
ifeq ($(TOOLCHAIN), GCC_ARM)
LDFLAGS+=-Wl,--no-warn-execstack
endif

# Additional / custom libraries to link in to the application.
LDLIBS+=

# Path to the linker script to use (if empty, use the default linker script).
LINKER_SCRIPT=

# Custom pre-build commands to run.
PREBUILD=

# Custom post-build commands to run.
POSTBUILD=


################################################################################
# Paths
################################################################################

# Relative path to the project directory (default is the Makefile's directory).
#
# This controls where automatic source code discovery looks for code.
CY_APP_PATH=

# Relative path to the shared repo location.
#
# All .mtb files have the format, <URI>#<COMMIT>#<LOCATION>. If the <LOCATION> 
# field begins with $$ASSET_REPO$$, then the repo is deposited in the path 
# specified by the CY_GETLIBS_SHARED_PATH variable. The default location is one 
# directory level above the current app directory. This is used with 
# CY_GETLIBS_SHARED_NAME variable, which specifies the directory name.
CY_GETLIBS_SHARED_PATH=../..

# Directory name of the shared repo location.
#
CY_GETLIBS_SHARED_NAME=mtb_shared

include $(CY_TOOLS_DIR)/make/start.mk
