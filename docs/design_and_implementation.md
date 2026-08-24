[Click here](../README.md) to view the README.

## Design and implementation

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

<br>

In this code example, at device reset, the secure boot process starts from the ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. Resource initialization for this example is performed by this CM33 non-secure project. It configures the system clocks, pins, clock to peripheral connections, and other platform resources. It then enables the CM55 core using the `Cy_SysEnableCM55()` function.

In the CM33 non-secure application, the clocks and system resources are initialized by the BSP initialization function. The retarget-io middleware is configured to use the debug UART. The debug UART prints a message (as shown in [Terminal output on program startup](../images/audio-model-detection.png)) on the terminal emulator, the onboard KitProg3 acts the USB-UART bridge to create the virtual COM port.

Once CM55 core is enabled it configures system clocks, pins, clock to peripheral connections, and other platform resources. This code example provides the Ready Model library for the following use cases using microphone and XENSIV&trade; radar sensor.

> **Note:** The ready models included in this code example can run for a maximum of one hour. To extend the usage, purchase the license from [DEEPCRAFT&trade;](https://www.infineon.com/design-resources/embedded-software/deepcraft-edge-ai-solutions/deepcraft-ready-models).


### Audio detection

There are five models that detect different sounds such as baby cry, cough, alarm, and siren. The models use data from pulse-density modulation (PDM) to pulse-code modulation (PCM), which is then sent to the model for detection.

The data consists of PDM/PCM data. The PDM/PCM is sampled at 16 kHz and an interrupt is generated after 1024 samples are collected. After 1024 samples are collected, the data is fed to the DEEPCRAFT&trade; model through the `IMAI_AED_enqueue` function. The `IMAI_AED_dequeue` function is used to read the model output and results are displayed on the UART terminal.

- **Cough Detection:** This model gathers PDM/PCM audio data to detect cough audio

- **Factory Alarm Detection:** This model gathers PDM/PCM audio data to detect alarm (for example, fire alarm) audio

- **Baby Cry Detection:** This model gathers PDM/PCM audio data to detect baby crying audio

- **Siren Detection:** This model gathers PDM/PCM audio data to detect siren audio

- **Direction of Arrival (Sound):** This model uses the sample audio data detect the direction of sound source

> **Note:**  The Direction of Arrival (Sound) model requires PDM data from four different microphones pointing to four different directions. As the PSOC&trade; Edge AI kit hardware does not support this, the sample audio data is being passed to the model instead of real-time data for detecting the direction of the sound source.

The sample audio data only shows the sound coming from "South" direction.


### Gesture detection

The Gesture Classification model gathers preprocessed radar data to detect different gestures, such as left, right, up, down, and push using the Ready Model library.

The data consist of raw radar sensor data. After data collection, preprocessing is done on the raw data. It is then fed to the DEEPCRAFT&trade; model through the `IMAI_AED_enqueue` function. The `IMAI_AED_dequeue` function is used to read Gesture Classification model output and the results are displayed on the UART terminal.


### Fall detection

The Fall Detection model gathers accelerometer data from the BMI270 sensor and detects the fall condition using the Ready Model library.

The data consist of raw accelerometer sensor data, sampled at 50 Hz, with a range of +/- 8g, and 16-bit resolution. After data collection, it is then fed to the DEEPCRAFT&trade; model through the `IMAI_FED_enqueue` function. The `IMAI_FED_dequeue` function is used to read Fall Detection model output and the results are displayed on the UART terminal.
