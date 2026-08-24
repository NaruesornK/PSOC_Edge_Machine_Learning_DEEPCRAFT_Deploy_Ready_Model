/*******************************************************************************
* File: breathing.h
*
* BabyMate 2.0 - breathing-rate estimation from BGT60TR13C raw frames.
*
* Pure DSP, no ML. Feeds on the same float buffer that radar_task.c fills from
* xensiv_bgt60trxx_get_fifo_data(), i.e. a WHOLE frame of
* num_samples_per_frame floats, not a single chirp.
*
* Pipeline per frame (~33 Hz for the preset in radar_settings.h):
*   de-interleave -> average the 32 chirps (chest is static over 9.6 ms)
*   -> remove ADC offset -> Hann -> 64-pt range FFT, once per RX antenna
*   -> score each range bin by how far it departs from its clutter EMA
*   -> lock the winning bin -> phase = atan2(Im,Re) on antenna 0
*   -> unwrap -> decimate to ~16.6 Hz -> 15.4 s sliding window
*   -> detrend -> Hann -> 256-pt FFT -> peak inside 9..90 bpm
*   -> parabolic interpolation -> bpm
*
* IMPORTANT: the geometry constants at the top of breathing.c are derived from
* radar_settings.h. If you change the radar preset, change them too - a
* mismatch there is silent and produces a plausible-looking but wrong bpm.
*******************************************************************************/

#ifndef BREATHING_H
#define BREATHING_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    BREATH_STATE_WARMUP = 0,   /* clutter estimate / window still filling     */
    BREATH_STATE_NO_TARGET,    /* nothing reflecting in the watched range     */
    BREATH_STATE_MOVING,       /* target moving too much to measure mm        */
    BREATH_STATE_MEASURING,    /* target present, no clean periodic peak yet  */
    BREATH_STATE_BREATHING,    /* valid rate                                  */
    BREATH_STATE_APNEA_ALARM   /* target present, no breathing for N seconds  */
} breath_state_t;

typedef struct
{
    breath_state_t state;
    float          bpm;                 /* breaths per minute                 */
    float          snr;                 /* peak / mean of the search band.
                                         * MEASURED USELESS as a detector on
                                         * 18 Aug 2026: an empty room scored
                                         * 4.0-4.75, higher than three people
                                         * standing in front of the sensor.
                                         * Kept only for comparison.          */
    float          psr;                 /* peak / next-highest peak outside a
                                         * 2-bin guard. One real periodic
                                         * source should stand clear of the
                                         * rest of the band; noise should not. */
    int            range_bin;           /* selected FFT bin                   */
    float          range_m;             /* its distance in metres             */
    float          motion_energy;       /* clutter-removed energy of that bin
                                         * -> READ THIS FIRST RUN to pick
                                         *   BR_PRESENCE_MIN in breathing.c   */
    float          phase_pp_rad;        /* peak-to-peak phase over the window
                                         * -> used for the "moving" gate      */
    float          seconds_since_valid; /* drives the apnea alarm             */
    float          fill_pct;            /* 0..100, how full the slow window is
                                         * -> 100 before any bpm is trusted   */
} breath_result_t;

/* Call once, after the radar is initialised. */
void breathing_init(void);

/*
 * Call once per radar frame with the same buffer given to the ML preprocessor.
 * frame must hold radar_configs.num_samples_per_frame floats
 * (= samples_per_chirp * chirps_per_frame * rx_antennas).
 *
 * Returns true about once per second, when *out holds a fresh result. While
 * the sliding window is still filling it still returns true once a second with
 * state = WARMUP and fill_pct climbing, so you can watch it come up.
 */
bool breathing_process_frame(const float *frame, breath_result_t *out);

const char *breathing_state_name(breath_state_t s);

#endif /* BREATHING_H */
