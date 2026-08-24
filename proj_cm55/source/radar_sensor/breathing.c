/*******************************************************************************
* File: breathing.c   -- see breathing.h for the pipeline overview.
*******************************************************************************/

#include <math.h>
#include <string.h>

#include "arm_math.h"
#include "breathing.h"

/*******************************************************************************
* Radar geometry - MUST match radar_settings.h
*
*   START 58 500 000 000 Hz .. END 62 500 000 000 Hz -> B = 4.000 GHz
*   range bin = c / (2 * B) = 3e8 / 8e9            = 0.0375 m
*   samples per chirp                              = 64  -> 32 range bins
*   chirps per frame                               = 32
*   rx antennas                                    = 3
*   samples per frame = 64 * 32 * 3                = 6144
*   frame_repetition_time = 0.0300446 s            -> 33.284 frames/s
*   lambda at 60.5 GHz                             = 4.955 mm
*
* Max unambiguous range = 32 bins * 0.0375 m = 1.2 m. That is a crib, not a
* room - which is exactly what we want here.
*******************************************************************************/
#define BR_N_FAST               64          /* samples per chirp              */
#define BR_N_CHIRPS             32          /* chirps per frame               */
#define BR_N_RX                 3           /* rx antennas                    */
#define BR_FRAME_LEN            (BR_N_FAST * BR_N_CHIRPS * BR_N_RX)   /* 6144  */

#define BR_FRAME_RATE_HZ        33.284f
#define BR_RANGE_BIN_M          0.0375f
#define BR_LAMBDA_M             0.004955f

/*
 * FIFO sample ordering inside one frame.
 *
 * 1 = antenna interleaved per sample (the documented XENSIV order: the ADC
 *     round-robins across the enabled RX for every sampling instant)
 *       idx = chirp*(N_FAST*N_RX) + sample*N_RX + antenna
 * 0 = each antenna's chirp stored as one contiguous block
 *       idx = chirp*(N_FAST*N_RX) + antenna*N_FAST + sample
 *
 * If the range profile looks like noise with no clear peak when someone is
 * clearly in front of the sensor, flip this to 0 and rebuild. It is the one
 * assumption here that is not verifiable from the headers in this project.
 */
#define BR_LAYOUT_ANT_INTERLEAVED   1

#if BR_LAYOUT_ANT_INTERLEAVED
  #define BR_IDX(c, s, a)   (((c) * BR_N_FAST * BR_N_RX) + ((s) * BR_N_RX) + (a))
#else
  #define BR_IDX(c, s, a)   (((c) * BR_N_FAST * BR_N_RX) + ((a) * BR_N_FAST) + (s))
#endif

/* Slow (breathing) path.
 * 33.284 / 2 = 16.642 Hz, and breathing tops out near 1.5 Hz, so this is still
 * 11x oversampled. 256 samples at 16.642 Hz = 15.4 s of history: long enough
 * to resolve 2 bpm after interpolation, short enough that an apnea alarm can
 * fire inside the 15 s the demo needs. */
#define BR_DECIM                2
#define BR_SLOW_RATE_HZ         (BR_FRAME_RATE_HZ / (float)BR_DECIM)  /* ~16.6 Hz */
#define BR_N_SLOW               256         /* 256 / 16.642 Hz = 15.4 s window */
#define BR_UPDATE_EVERY         17          /* emit a result about every 1 s   */
#define BR_N_SLOW_BINS          (BR_N_SLOW / 2)

/* Length of the moving average subtracted from the window (see the high-pass
 * block below). 51 samples at 16.642 Hz puts the corner near 0.14 Hz = 8.6 bpm,
 * just below BR_BPM_MIN. Longer = gentler = lets more drift through. */
#define BR_MA_LEN               51

/* Range bins to watch: bin 12..28 = 0.45 m .. 1.05 m.
 *
 * This window is deliberately narrow. Reflected power falls off as 1/r^4, so
 * a near-field bin outscores a real target at 0.7 m by orders of magnitude -
 * the first bring-up run locked onto bin 6 (0.23 m) every single time and
 * never looked further out. Restricting the search to where the subject
 * actually is beats trying to normalise that away.
 *
 * Sensor is meant to sit 0.45-1.05 m from the chest: above a crib, or on a
 * desk with you sitting 0.6-0.9 m away (= bin 16-24) for a first test. */
#define BR_BIN_MIN              12
#define BR_BIN_MAX              28
#define BR_N_BINS               (BR_BIN_MAX - BR_BIN_MIN + 1)

/* Search band: 9..90 bpm covers an adult (12-20) and an infant (30-60),
 * so you can sanity-check the code on your own chest before using a phantom. */
#define BR_BPM_MIN              9.0f
#define BR_BPM_MAX              90.0f

/*******************************************************************************
* Tunables - the four numbers you will actually adjust
*******************************************************************************/

/* Clutter tracker. Smaller = slower = treats more of the scene as "static".
 * 0.012 at 33.3 Hz is a time constant of about 2.5 s. */
#define BR_CLUTTER_ALPHA        0.012f

/* Smoothing of the per-bin motion score used to pick the target bin.
 * 0.006 at 33.3 Hz is about 5 s. */
#define BR_SCORE_ALPHA          0.006f

/* Presence gate, in raw ADC counts squared.
 *
 * Measured on this board 18 Aug 2026, sensor on a desk: a person sitting
 * 0.45-0.6 m away reads 3e4 .. 8e5, and an empty room decays to under 1e3.
 * Three orders of magnitude apart, so the threshold is not delicate - 5e3 is
 * comfortably between them.
 *
 * Re-measure this if you change the gain, the mounting, or the range window:
 * the absolute value means nothing on its own. */
#define BR_PRESENCE_MIN         5.0e3f

/* Peak must stand this far above the mean of the search band to be called
 * breathing rather than noise. 3.0 is a reasonable starting point. */
#define BR_SNR_THRESHOLD        3.0f

/* Peak-to-peak unwrapped phase over the window, in radians. 5 mm of chest
 * travel is 4*pi*5/4.955 = 12.7 rad, so anything past ~40 rad is gross body
 * movement, not breathing. */
#define BR_MOVING_PP_RAD        40.0f

/* Hysteresis on the target bin. A challenger must beat the locked bin by this
 * factor, and hold that lead for this many consecutive updates (~1 s each),
 * before we switch. Switching throws the sliding window away, so a twitchy
 * rule means the window never fills - which is exactly what the first run
 * did, cycling bin 6 -> 8 -> 6 -> 7 and restarting the fill every time. */
#define BR_SWITCH_RATIO         2.0f
#define BR_SWITCH_HOLD          5

/* No valid breathing for this long, while a target is still present. */
#define BR_APNEA_SECONDS        15.0f

/* Ignore the first seconds while the clutter EMA settles. */
#define BR_WARMUP_SECONDS       4.0f

#define BR_PI                   3.14159265358979f

/*******************************************************************************
* State
*******************************************************************************/
static arm_rfft_fast_instance_f32 br_fft_fast;
static arm_rfft_fast_instance_f32 br_fft_slow;

static float br_hann_fast[BR_N_FAST];
static float br_hann_slow[BR_N_SLOW];

/* clutter (static scene) estimate per antenna per watched range bin, and the
 * motion score summed over antennas */
static float br_ema_re[BR_N_RX][BR_N_BINS];
static float br_ema_im[BR_N_RX][BR_N_BINS];
static float br_score[BR_N_BINS];

static uint32_t br_frame_count;
static bool     br_locked;
static int      br_sel_bin;             /* index into 0..BR_N_BINS-1          */

/* phase unwrapping */
static bool  br_phase_init;
static float br_phase_prev;
static float br_phase_acc;

/* slow ring buffer of unwrapped phase */
static float br_slow_buf[BR_N_SLOW];
static int   br_slow_head;
static int   br_slow_filled;
static int   br_decim_count;
static int   br_since_update;

/* bin-switch hysteresis */
static int   br_challenger;
static int   br_challenge_count;

/* scratch */
static float br_chirp_avg[BR_N_FAST];
static float br_fast_in[BR_N_FAST];
static float br_fast_spec[BR_N_RX][BR_N_FAST];
static float br_slow_in[BR_N_SLOW];
static float br_slow_hp[BR_N_SLOW];
static float br_ps[BR_N_SLOW + 1];      /* prefix sums for the moving average */
static float br_slow_spec[BR_N_SLOW];
static float br_mag[BR_N_SLOW_BINS];

static float br_sec_since_valid;

/*******************************************************************************
* Helpers
*******************************************************************************/
static void br_make_hann(float *w, int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        w[i] = 0.5f * (1.0f - cosf(2.0f * BR_PI * (float)i / (float)(n - 1)));
    }
}

const char *breathing_state_name(breath_state_t s)
{
    switch (s)
    {
        case BREATH_STATE_WARMUP:      return "WARMUP";
        case BREATH_STATE_NO_TARGET:   return "NO TARGET";
        case BREATH_STATE_MOVING:      return "MOVING";
        case BREATH_STATE_MEASURING:   return "MEASURING";
        case BREATH_STATE_BREATHING:   return "BREATHING";
        case BREATH_STATE_APNEA_ALARM: return "*** APNEA ALARM ***";
        default:                       return "?";
    }
}

/*******************************************************************************
* breathing_init
*******************************************************************************/
void breathing_init(void)
{
    arm_rfft_fast_init_f32(&br_fft_fast, BR_N_FAST);
    arm_rfft_fast_init_f32(&br_fft_slow, BR_N_SLOW);

    br_make_hann(br_hann_fast, BR_N_FAST);
    br_make_hann(br_hann_slow, BR_N_SLOW);

    memset(br_ema_re, 0, sizeof(br_ema_re));
    memset(br_ema_im, 0, sizeof(br_ema_im));
    memset(br_score,  0, sizeof(br_score));
    memset(br_slow_buf, 0, sizeof(br_slow_buf));

    br_frame_count     = 0u;
    br_locked          = false;
    br_sel_bin         = 0;
    br_phase_init      = false;
    br_phase_prev      = 0.0f;
    br_phase_acc       = 0.0f;
    br_slow_head       = 0;
    br_slow_filled     = 0;
    br_decim_count     = 0;
    br_since_update    = 0;
    br_challenger      = -1;
    br_challenge_count = 0;
    br_sec_since_valid = 0.0f;
}

/*******************************************************************************
* breathing_process_frame
*******************************************************************************/
bool breathing_process_frame(const float *frame, breath_result_t *out)
{
    int   i, k, a, c, s;
    float mean;
    float best_score;
    int   best_bin;

    /*--------------------------------------------------------------------------
    * 1. Per antenna: average the 32 chirps of this frame, strip the ADC offset,
    *    window, and take the range FFT.
    *
    *    Averaging first is free SNR: the chirps are 0.3 ms apart and the whole
    *    frame spans 9.6 ms, over which a chest simply does not move. 32 chirps
    *    averaged is about 15 dB on the noise floor.
    *------------------------------------------------------------------------*/
    for (a = 0; a < BR_N_RX; a++)
    {
        for (s = 0; s < BR_N_FAST; s++)
        {
            br_chirp_avg[s] = 0.0f;
        }

        for (c = 0; c < BR_N_CHIRPS; c++)
        {
            for (s = 0; s < BR_N_FAST; s++)
            {
                br_chirp_avg[s] += frame[BR_IDX(c, s, a)];
            }
        }

        mean = 0.0f;
        for (s = 0; s < BR_N_FAST; s++)
        {
            br_chirp_avg[s] /= (float)BR_N_CHIRPS;
            mean += br_chirp_avg[s];
        }
        mean /= (float)BR_N_FAST;

        /* Removing the offset matters: skipping it dumps a huge spike into
         * bin 0 that leaks into the bins we care about. */
        for (s = 0; s < BR_N_FAST; s++)
        {
            br_fast_in[s] = (br_chirp_avg[s] - mean) * br_hann_fast[s];
        }

        /* Output packing of arm_rfft_fast_f32 for N real inputs:
         *   spec[0]         = real part of bin 0    (DC)
         *   spec[1]         = real part of bin N/2  (Nyquist)
         *   spec[2k],[2k+1] = real, imag of bin k   for k = 1 .. N/2-1
         * With N = 64 that gives usable bins 1..31, and BR_BIN_MAX is 30. */
        arm_rfft_fast_f32(&br_fft_fast, br_fast_in, br_fast_spec[a], 0);
    }

    /*--------------------------------------------------------------------------
    * 2. Per bin: track the static scene with an EMA per antenna, and score how
    *    much the bin departs from it, summed over antennas. That score answers
    *    "which range bin is moving?" -- the wall is bright but never moves, so
    *    it scores near zero.
    *------------------------------------------------------------------------*/
    best_score = -1.0f;
    best_bin   = 0;

    for (i = 0; i < BR_N_BINS; i++)
    {
        float e = 0.0f;

        k = BR_BIN_MIN + i;

        for (a = 0; a < BR_N_RX; a++)
        {
            float re, im, dre, dim;

            re = br_fast_spec[a][2 * k];
            im = br_fast_spec[a][2 * k + 1];

            br_ema_re[a][i] += BR_CLUTTER_ALPHA * (re - br_ema_re[a][i]);
            br_ema_im[a][i] += BR_CLUTTER_ALPHA * (im - br_ema_im[a][i]);

            dre = re - br_ema_re[a][i];
            dim = im - br_ema_im[a][i];
            e  += (dre * dre) + (dim * dim);
        }

        br_score[i] += BR_SCORE_ALPHA * (e - br_score[i]);

        if (br_score[i] > best_score)
        {
            best_score = br_score[i];
            best_bin   = i;
        }
    }

    br_frame_count++;

    if (!br_locked)
    {
        if ((float)br_frame_count >= (BR_WARMUP_SECONDS * BR_FRAME_RATE_HZ))
        {
            br_sel_bin    = best_bin;
            br_locked     = true;
            br_phase_init = false;
        }
        return false;
    }

    /*--------------------------------------------------------------------------
    * 3. Phase of the selected bin on antenna 0, from the RAW complex value.
    *
    *    We deliberately do not use the clutter-removed value here. Even when
    *    static clutter dominates the bin, the phase of the sum still oscillates
    *    at the breathing rate -- and the rate is all we need. Clutter removal
    *    is used only to CHOOSE the bin.
    *
    *    Chest travel of 5 mm is 4*pi*5/4.955 = 12.7 rad, i.e. two full wraps,
    *    so unwrapping is mandatory, not optional.
    *------------------------------------------------------------------------*/
    {
        float re, im, ph, d;

        k  = BR_BIN_MIN + br_sel_bin;
        re = br_fast_spec[0][2 * k];
        im = br_fast_spec[0][2 * k + 1];

        ph = atan2f(im, re);

        if (!br_phase_init)
        {
            br_phase_prev = ph;
            br_phase_acc  = 0.0f;
            br_phase_init = true;
        }

        d = ph - br_phase_prev;
        while (d >  BR_PI) { d -= 2.0f * BR_PI; }
        while (d < -BR_PI) { d += 2.0f * BR_PI; }

        br_phase_acc += d;
        br_phase_prev = ph;
    }

    /*--------------------------------------------------------------------------
    * 4. Decimate 33.3 Hz -> ~16.6 Hz and push into the sliding window.
    *------------------------------------------------------------------------*/
    br_decim_count++;
    if (br_decim_count < BR_DECIM)
    {
        return false;
    }
    br_decim_count = 0;

    br_slow_buf[br_slow_head] = br_phase_acc;
    br_slow_head = (br_slow_head + 1) % BR_N_SLOW;
    if (br_slow_filled < BR_N_SLOW)
    {
        br_slow_filled++;
    }
    br_since_update++;

    if (br_since_update < BR_UPDATE_EVERY)
    {
        return false;
    }
    br_since_update = 0;

    /* Still filling: report progress rather than going silent, so a bring-up
     * run shows something moving instead of a frozen WARMUP line. */
    if (br_slow_filled < BR_N_SLOW)
    {
        if (out != NULL)
        {
            memset(out, 0, sizeof(*out));
            out->state         = BREATH_STATE_WARMUP;
            out->range_bin     = BR_BIN_MIN + br_sel_bin;
            out->range_m       = (float)(BR_BIN_MIN + br_sel_bin) * BR_RANGE_BIN_M;
            out->motion_energy = br_score[br_sel_bin];
            out->fill_pct      = 100.0f * (float)br_slow_filled / (float)BR_N_SLOW;
        }
        return true;
    }

    /*--------------------------------------------------------------------------
    * 5. Once a second: copy the window out in time order, remove mean and slope
    *    (the unwrapped phase drifts), measure peak-to-peak for the movement
    *    gate, window, transform.
    *------------------------------------------------------------------------*/
    {
        float m = 0.0f;
        float num = 0.0f, den = 0.0f, slope, x;
        float mn, mx, pp;
        int   kmin, kmax, peak_k;
        float peak, sum, meanmag, snr, psr, delta, freq, bpm, dt;
        breath_state_t st;

        for (i = 0; i < BR_N_SLOW; i++)
        {
            br_slow_in[i] = br_slow_buf[(br_slow_head + i) % BR_N_SLOW];
            m += br_slow_in[i];
        }
        m /= (float)BR_N_SLOW;

        for (i = 0; i < BR_N_SLOW; i++)
        {
            x    = (float)i - ((float)(BR_N_SLOW - 1) * 0.5f);
            num += x * (br_slow_in[i] - m);
            den += x * x;
        }
        slope = num / den;

        mn = 1e30f;
        mx = -1e30f;
        for (i = 0; i < BR_N_SLOW; i++)
        {
            x = (float)i - ((float)(BR_N_SLOW - 1) * 0.5f);
            br_slow_in[i] = br_slow_in[i] - m - (slope * x);
            if (br_slow_in[i] < mn) { mn = br_slow_in[i]; }
            if (br_slow_in[i] > mx) { mx = br_slow_in[i]; }
        }
        pp = mx - mn;   /* measured on the detrended phase, before high-pass */

        /*----------------------------------------------------------------------
        * High-pass by subtracting a centred moving average.
        *
        * Removing mean and slope is not enough: unwrapped phase wanders, and
        * the leftover curvature piles up at the bottom of the search band. The
        * first run showed that as a constant 7.8 bpm sitting exactly on the
        * lowest allowed FFT bin.
        *
        * A first difference fixed that but broke the other end: its gain rises
        * with frequency, which tilted the noise floor upward and parked the
        * peak on the TOP bin instead, reporting a constant 89.7 bpm. Both
        * readings were the band edge, not a rate.
        *
        * A moving-average high-pass has no such tilt: it is flat above its
        * corner and rolls off below it. BR_MA_LEN 51 puts the corner near
        * 0.14 Hz = 8.6 bpm, just under BR_BPM_MIN, so drift is attenuated and
        * everything we actually search for passes at full gain.
        *
        * Prefix sums keep this O(N), and the window is clamped at the edges -
        * where the Hann window is about to suppress everything anyway.
        *--------------------------------------------------------------------*/
        {
            const int half = BR_MA_LEN / 2;

            br_ps[0] = 0.0f;
            for (i = 0; i < BR_N_SLOW; i++)
            {
                br_ps[i + 1] = br_ps[i] + br_slow_in[i];
            }

            for (i = 0; i < BR_N_SLOW; i++)
            {
                int lo = i - half;
                int hi = i + half;
                float ma;

                if (lo < 0)             { lo = 0; }
                if (hi > BR_N_SLOW - 1) { hi = BR_N_SLOW - 1; }

                ma = (br_ps[hi + 1] - br_ps[lo]) / (float)(hi - lo + 1);
                br_slow_hp[i] = br_slow_in[i] - ma;
            }
        }

        for (i = 0; i < BR_N_SLOW; i++)
        {
            br_slow_in[i] = br_slow_hp[i] * br_hann_slow[i];
        }

        arm_rfft_fast_f32(&br_fft_slow, br_slow_in, br_slow_spec, 0);

        /*----------------------------------------------------------------------
        * 6. Peak inside the breathing band.
        *    bin k <-> k * BR_SLOW_RATE_HZ / BR_N_SLOW Hz (0.065 Hz = 3.9 bpm),
        *    which parabolic interpolation below brings down to about 1 bpm.
        *--------------------------------------------------------------------*/
        kmin = (int)(((BR_BPM_MIN / 60.0f) * (float)BR_N_SLOW / BR_SLOW_RATE_HZ) + 0.5f);
        kmax = (int)(((BR_BPM_MAX / 60.0f) * (float)BR_N_SLOW / BR_SLOW_RATE_HZ) + 0.5f);
        if (kmin < 1)                   { kmin = 1; }
        if (kmax > BR_N_SLOW_BINS - 2)  { kmax = BR_N_SLOW_BINS - 2; }

        peak   = 0.0f;
        peak_k = kmin;
        sum    = 0.0f;

        for (k = kmin; k <= kmax; k++)
        {
            float rr = br_slow_spec[2 * k];
            float ii = br_slow_spec[2 * k + 1];
            float mg = sqrtf((rr * rr) + (ii * ii));

            br_mag[k] = mg;
            sum += mg;

            if (mg > peak)
            {
                peak   = mg;
                peak_k = k;
            }
        }

        meanmag = sum / (float)(kmax - kmin + 1);
        snr     = (meanmag > 1e-12f) ? (peak / meanmag) : 0.0f;

        /*----------------------------------------------------------------------
        * Peak-to-sidelobe ratio: the peak against the highest OTHER peak in the
        * band, skipping a 2-bin guard around it.
        *
        * peak/mean (snr above) was measured useless on 18 Aug 2026 - an empty
        * room scored 4.0-4.75 while three people standing in front of the
        * sensor scored 3.5-4.4. With only ~22 bins in the band, a random noise
        * maximum routinely sits several times above the mean, so the statistic
        * has no separating power.
        *
        * peak/next-peak should behave better: one genuine periodic source
        * leaves a single dominant line, whereas noise leaves several lines of
        * similar height. Printed alongside snr so the two can be compared on
        * real data before either is used as a gate.
        *--------------------------------------------------------------------*/
        {
            float second = 0.0f;

            for (k = kmin; k <= kmax; k++)
            {
                int d = k - peak_k;
                if (d < 0) { d = -d; }
                if (d <= 2) { continue; }

                if (br_mag[k] > second) { second = br_mag[k]; }
            }

            psr = (second > 1e-12f) ? (peak / second) : 0.0f;
        }

        /* Parabolic interpolation: the true peak rarely lands exactly on a bin,
         * and without this the readout quantises to 3.9 bpm steps. */
        delta = 0.0f;
        if ((peak_k > kmin) && (peak_k < kmax))
        {
            float pa = br_mag[peak_k - 1];
            float pb = br_mag[peak_k];
            float pc = br_mag[peak_k + 1];
            float dn = pa - (2.0f * pb) + pc;

            if (fabsf(dn) > 1e-12f)
            {
                delta = 0.5f * (pa - pc) / dn;
            }
            if (delta >  0.5f) { delta =  0.5f; }
            if (delta < -0.5f) { delta = -0.5f; }
        }

        freq = ((float)peak_k + delta) * BR_SLOW_RATE_HZ / (float)BR_N_SLOW;
        bpm  = freq * 60.0f;

        /*----------------------------------------------------------------------
        * 7. State machine.
        *--------------------------------------------------------------------*/
        dt = (float)BR_UPDATE_EVERY / BR_SLOW_RATE_HZ;   /* about 1 s */

        /* A peak sitting exactly on a band edge is the signature of something
         * leaking in from outside the band, not of a rate inside it - both
         * bring-up runs produced one (7.8 bpm at the bottom, 89.7 at the top).
         * Refuse to call that breathing however good its SNR looks. */
        if ((peak_k <= kmin) || (peak_k >= kmax))
        {
            st = (br_score[br_sel_bin] < BR_PRESENCE_MIN)
                     ? BREATH_STATE_NO_TARGET
                     : BREATH_STATE_MEASURING;
        }
        else if (br_score[br_sel_bin] < BR_PRESENCE_MIN)
        {
            st = BREATH_STATE_NO_TARGET;
        }
        else if (pp > BR_MOVING_PP_RAD)
        {
            st = BREATH_STATE_MOVING;
        }
        else if (snr >= BR_SNR_THRESHOLD)
        {
            st = BREATH_STATE_BREATHING;
        }
        else
        {
            st = BREATH_STATE_MEASURING;
        }

        /* A squirming baby is a living baby: movement resets the apnea clock.
         * So does an empty room - otherwise the timer runs up while nobody is
         * there and the alarm fires the instant someone walks back in. */
        if ((st == BREATH_STATE_BREATHING) ||
            (st == BREATH_STATE_MOVING) ||
            (st == BREATH_STATE_NO_TARGET))
        {
            br_sec_since_valid = 0.0f;
        }
        else
        {
            br_sec_since_valid += dt;
        }

        if ((st != BREATH_STATE_NO_TARGET) &&
            (br_sec_since_valid >= BR_APNEA_SECONDS))
        {
            st = BREATH_STATE_APNEA_ALARM;
        }

        /*----------------------------------------------------------------------
        * 8. Allow the target bin to move, but only on a clear win, and reset the
        *    phase history when it does - the old unwrapped phase belongs to a
        *    different reflector.
        *--------------------------------------------------------------------*/
        if ((best_bin != br_sel_bin) &&
            (best_score > (BR_SWITCH_RATIO * br_score[br_sel_bin])))
        {
            if (best_bin == br_challenger)
            {
                br_challenge_count++;
            }
            else
            {
                br_challenger      = best_bin;
                br_challenge_count = 1;
            }
        }
        else
        {
            br_challenger      = -1;
            br_challenge_count = 0;
        }

        if (br_challenge_count >= BR_SWITCH_HOLD)
        {
            br_sel_bin         = br_challenger;
            br_challenger      = -1;
            br_challenge_count = 0;
            br_phase_init      = false;
            br_slow_filled     = 0;
            br_slow_head       = 0;
        }

        if (out != NULL)
        {
            out->state               = st;
            /* Do not hand back a rate for an empty room. The first logs printed
             * a plausible 22-23 bpm through the whole time nobody was there,
             * which is an easy thing to misread as a working measurement. */
            out->bpm                 = (st == BREATH_STATE_NO_TARGET) ? 0.0f : bpm;
            out->snr                 = snr;
            out->psr                 = psr;
            out->range_bin           = BR_BIN_MIN + br_sel_bin;
            out->range_m             = (float)(BR_BIN_MIN + br_sel_bin) * BR_RANGE_BIN_M;
            out->motion_energy       = br_score[br_sel_bin];
            out->phase_pp_rad        = pp;
            out->seconds_since_valid = br_sec_since_valid;
            out->fill_pct            = 100.0f;
        }

        return true;
    }
}
