#include "dsp/fm_quality.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include <liquid/liquid.h>

#include "core/logging.h"

/* Provisional engineering endpoints, not calibrated reception thresholds. */
static const double FM_QUALITY_CLEAN_NOISE_DB = -50.0;
static const double FM_QUALITY_POOR_NOISE_DB = -20.0;
static const double FM_QUALITY_POWER_FLOOR = 1.0e-20;
static const double FM_QUALITY_AVERAGE_SECONDS = 0.5;

/* Reproduce with scripts/generate-fm-quality-filter.py.
 * 240 kHz; passband 16.5-17.5 kHz (1 dB); stopbands <=15 / >=19 kHz (60 dB).
 * Generated Butterworth bandpass: order 12, 6 biquads. */
#define FM_QUALITY_SOS_COUNT 6u
static const float fm_quality_b[FM_QUALITY_SOS_COUNT * 3u] = {
    1.451337151e-02f,  2.902674302e-02f,  1.451337151e-02f, 1.451337151e-02f,  2.902674302e-02f,
    1.451337151e-02f,  1.451337151e-02f,  2.902674302e-02f, 1.451337151e-02f,  1.451337151e-02f,
    -2.902674302e-02f, 1.451337151e-02f,  1.451337151e-02f, -2.902674302e-02f, 1.451337151e-02f,
    1.451337151e-02f,  -2.902674302e-02f, 1.451337151e-02f,
};
static const float fm_quality_a[FM_QUALITY_SOS_COUNT * 3u] = {
    1.000000000e+00f,  -1.776690722e+00f, 9.718729258e-01f,  1.000000000e+00f,  -1.783526063e+00f,
    9.723109007e-01f,  1.000000000e+00f,  -1.777398944e+00f, 9.790541530e-01f,  1.000000000e+00f,
    -1.795853853e+00f, 9.799361825e-01f,  1.000000000e+00f,  -1.785807610e+00f, 9.922230244e-01f,
    1.000000000e+00f,  -1.810488462e+00f, 9.926695824e-01f,
};

void fm_quality_init(struct fm_quality_state *state, double report_interval_seconds) {
    /* liquid's constructor accepts mutable arrays and copies the coefficients. */
    float b[FM_QUALITY_SOS_COUNT * 3u];
    float a[FM_QUALITY_SOS_COUNT * 3u];
    memcpy(b, fm_quality_b, sizeof(b));
    memcpy(a, fm_quality_a, sizeof(a));
    memset(state, 0, sizeof(*state));
    state->report_interval_seconds = report_interval_seconds;
    state->filter = iirfilt_rrrf_create_sos(b, a, FM_QUALITY_SOS_COUNT);
    fm_quality_reset(state);
}

void fm_quality_destroy(struct fm_quality_state *state) {
    if (state->filter != NULL) {
        iirfilt_rrrf_destroy((iirfilt_rrrf)state->filter);
    }
    memset(state, 0, sizeof(*state));
}

void fm_quality_reset(struct fm_quality_state *state) {
    if (state->filter != NULL) {
        iirfilt_rrrf_reset((iirfilt_rrrf)state->filter);
    }
    state->settling_samples = FM_QUALITY_SAMPLE_RATE_HZ / 10u;
    state->smoothed_power = 0.0;
    state->result =
        (struct fm_quality_result){.valid = false, .noise_db = NAN, .quality_percent = NAN};
}

void fm_quality_process_block(struct fm_quality_state *state, const float *mpx, size_t count) {
    double power = 0.0;
    if (state->filter == NULL || count == 0u) {
        fm_quality_reset(state);
        return;
    }

    for (size_t i = 0u; i < count; ++i) {
        float filtered;
        if (!isfinite(mpx[i])) {
            fm_quality_reset(state);
            return;
        }
        iirfilt_rrrf_execute((iirfilt_rrrf)state->filter, mpx[i], &filtered);
        if (!isfinite(filtered)) {
            fm_quality_reset(state);
            return;
        }
        power += (double)filtered * filtered;
    }

    /* Discard the entire block that crosses the settling boundary. */
    if (state->settling_samples != 0u) {
        state->settling_samples =
            count >= state->settling_samples ? 0u : state->settling_samples - count;
        return;
    }

    power /= (double)count;
    const double alpha =
        -expm1(-(double)count / (FM_QUALITY_SAMPLE_RATE_HZ * FM_QUALITY_AVERAGE_SECONDS));
    if (!state->result.valid) {
        state->smoothed_power = power;
    } else {
        state->smoothed_power += alpha * (power - state->smoothed_power);
    }
    if (!isfinite(state->smoothed_power) || state->smoothed_power < 0.0) {
        fm_quality_reset(state);
        return;
    }

    state->result.noise_db = 10.0 * log10(fmax(state->smoothed_power, FM_QUALITY_POWER_FLOOR));
    const double fraction = (FM_QUALITY_POOR_NOISE_DB - state->result.noise_db) /
                            (FM_QUALITY_POOR_NOISE_DB - FM_QUALITY_CLEAN_NOISE_DB);
    state->result.quality_percent = 100.0 * fmax(0.0, fmin(1.0, fraction));
    state->result.valid = true;
}

const struct fm_quality_result *fm_quality_get_result(const struct fm_quality_state *state) {
    return &state->result;
}

void fm_quality_report(struct fm_quality_state *state, uint32_t frequency_hz) {
    const struct fm_quality_result *result = fm_quality_get_result(state);
    struct timespec time;
    if (!result->valid) {
        if (!state->have_reported || state->reported_valid) {
            INFO("FM %.1f MHz: quality=unavailable (estimated)", (double)frequency_hz / 1000000.0);
        }
        state->have_reported = true;
        state->reported_valid = false;
        return;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
        return;
    }
    const double now = (double)time.tv_sec + (double)time.tv_nsec * 1.0e-9;
    if (state->have_reported && state->reported_valid &&
        now - state->last_report_time < state->report_interval_seconds) {
        return;
    }
    INFO("FM %.1f MHz: quality=%.0f%% (estimated), mpx_noise_db=%.2f dB",
         (double)frequency_hz / 1000000.0, result->quality_percent, result->noise_db);
    state->have_reported = true;
    state->reported_valid = true;
    state->last_report_time = now;
}
