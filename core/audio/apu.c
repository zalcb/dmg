#include "apu.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>


/* helper functions */
static const uint8_t duty_table[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1},  // 12.5%
    {1, 0, 0, 0, 0, 0, 0, 1},  // 25%
    {1, 0, 0, 0, 0, 1, 1, 1},  // 50%
    {0, 1, 1, 1, 1, 1, 1, 0},  // 75%
};

// removed complex anti-aliasing that was causing muffled sound

static float get_ch1_output_float(APU *apu) {
    if (!apu->ch1.enabled || !apu->ch1.dac_enabled) {
        return 0.0f;
    }

    // simple duty cycle implementation
    uint8_t duty_output = duty_table[apu->ch1.duty][apu->ch1.duty_position];
    float output        = duty_output ? 1.0f : -1.0f;
    
    // frequency-dependent amplitude reduction for high frequencies
    float frequency_hz = 131072.0f / (2048 - apu->ch1.frequency);
    float amplitude_scale = 1.0f;
    if (frequency_hz > 1500.0f) {
        // gently reduce amplitude for very high frequencies only
        amplitude_scale = 1.0f - fminf(0.2f, (frequency_hz - 1500.0f) / 4000.0f);
    }

    return output * apu->ch1.envelope_volume / 15.0f * amplitude_scale;
}

static float get_ch2_output_float(APU *apu) {
    if (!apu->ch2.enabled || !apu->ch2.dac_enabled) {
        return 0.0f;
    }

    // simple duty cycle implementation
    uint8_t duty_output = duty_table[apu->ch2.duty][apu->ch2.duty_position];
    float output        = duty_output ? 1.0f : -1.0f;
    
    // frequency-dependent amplitude reduction for high frequencies
    float frequency_hz = 131072.0f / (2048 - apu->ch2.frequency);
    float amplitude_scale = 1.0f;
    if (frequency_hz > 1500.0f) {
        // gently reduce amplitude for very high frequencies only
        amplitude_scale = 1.0f - fminf(0.2f, (frequency_hz - 1500.0f) / 4000.0f);
    }

    return output * apu->ch2.envelope_volume / 15.0f * amplitude_scale;
}

static float get_ch3_output_float(APU *apu) {
    if (!apu->ch3.enabled || !apu->ch3.dac_enabled) {
        return 0.0f;
    }

    uint8_t sample = apu->ch3.wave_ram[apu->ch3.wave_position >> 1];

    if (apu->ch3.wave_position & 1) {
        sample &= 0x0F;  // get low nibble
    } else {
        sample >>= 4;  // get high nibble
    }

    float output = (float)sample / 15.0f * 2.0f - 1.0f;  // convert to -1.0 to 1.0

    switch (apu->ch3.output_level) {
        case 0:  return 0.0f;            // mute
        case 1:  return output;          // 100%
        case 2:  return output * 0.5f;   // 50%
        case 3:  return output * 0.25f;  // 25%
        default: return 0.0f;
    }
}

static float get_ch4_output_float(APU *apu) {
    if (!apu->ch4.enabled || !apu->ch4.dac_enabled) {
        return 0.0f;
    }

    // LFSR output is inverted
    uint8_t lfsr_output = (~apu->ch4.lfsr) & 0x01;  // get the least significant bit
    float output        = lfsr_output ? 1.0f : -1.0f;
    return output * apu->ch4.envelope_volume / 15.0f;
}

static void clock_length_counters(APU *apu) {
    if (apu->ch1.length_enabled && apu->ch1.length_counter > 0) {
        apu->ch1.length_counter--;
        if (apu->ch1.length_counter == 0) {
            apu->ch1.enabled = false;  // disable channel if length counter reaches 0
        }
    }

    if (apu->ch2.length_enabled && apu->ch2.length_counter > 0) {
        apu->ch2.length_counter--;
        if (apu->ch2.length_counter == 0) {
            apu->ch2.enabled = false;  // disable channel if length counter reaches 0
        }
    }

    if (apu->ch3.length_enabled && apu->ch3.length_counter > 0) {
        apu->ch3.length_counter--;
        if (apu->ch3.length_counter == 0) {
            apu->ch3.enabled = false;  // disable channel if length counter reaches 0
        }
    }

    if (apu->ch4.length_enabled && apu->ch4.length_counter > 0) {
        apu->ch4.length_counter--;
        if (apu->ch4.length_counter == 0) {
            apu->ch4.enabled = false;  // disable channel if length counter reaches 0
        }
    }
}

static void clock_envelope(APU *apu) {
    if (apu->ch1.envelope_period != 0) {
        if (apu->ch1.envelope_timer > 0) {
            apu->ch1.envelope_timer--;
        }
        if (apu->ch1.envelope_timer == 0) {
            apu->ch1.envelope_timer = apu->ch1.envelope_period;
            if (apu->ch1.envelope_direction && apu->ch1.envelope_volume < 15) {
                apu->ch1.envelope_volume++;
            } else if (!apu->ch1.envelope_direction && apu->ch1.envelope_volume > 0) {
                apu->ch1.envelope_volume--;
            }
        }
    }

    if (apu->ch2.envelope_period != 0) {
        if (apu->ch2.envelope_timer > 0) {
            apu->ch2.envelope_timer--;
        }
        if (apu->ch2.envelope_timer == 0) {
            apu->ch2.envelope_timer = apu->ch2.envelope_period;
            if (apu->ch2.envelope_direction && apu->ch2.envelope_volume < 15) {
                apu->ch2.envelope_volume++;
            } else if (!apu->ch2.envelope_direction && apu->ch2.envelope_volume > 0) {
                apu->ch2.envelope_volume--;
            }
        }
    }

    if (apu->ch4.envelope_period != 0) {
        if (apu->ch4.envelope_timer > 0) {
            apu->ch4.envelope_timer--;
        }
        if (apu->ch4.envelope_timer == 0) {
            apu->ch4.envelope_timer = apu->ch4.envelope_period;
            if (apu->ch4.envelope_direction && apu->ch4.envelope_volume < 15) {
                apu->ch4.envelope_volume++;
            } else if (!apu->ch4.envelope_direction && apu->ch4.envelope_volume > 0) {
                apu->ch4.envelope_volume--;
            }
        }
    }
}

static int calculate_sweep_frequency(APU *apu) {
    int new_frequency = apu->ch1.sweep_shadow_frequency >> apu->ch1.sweep_shift;

    if (apu->ch1.sweep_negate) {
        new_frequency = apu->ch1.sweep_shadow_frequency - new_frequency;
    } else {
        new_frequency = apu->ch1.sweep_shadow_frequency + new_frequency;
    }

    if (new_frequency > 2047) {
        apu->ch1.enabled = false;  // disable channel if frequency exceeds limit
    }

    return new_frequency;
}

static void clock_sweep(APU *apu) {
    if (apu->ch1.sweep_timer > 0) {
        apu->ch1.sweep_timer--;
    }

    if (apu->ch1.sweep_timer == 0) {
        apu->ch1.sweep_timer = apu->ch1.sweep_period ? apu->ch1.sweep_period : 8;

        if (apu->ch1.sweep_enabled && apu->ch1.sweep_period > 0) {
            int new_frequency = calculate_sweep_frequency(apu);
            if (new_frequency < 2048 && apu->ch1.sweep_shift > 0) {
                apu->ch1.frequency              = new_frequency;
                apu->ch1.sweep_shadow_frequency = new_frequency;
                calculate_sweep_frequency(apu);
            }
        }
    }
}

static int get_noise_period(APU *apu) {
    static const int divisors[8] = {4, 8, 16, 24, 32, 40, 48, 56};
    int divider_index            = apu->ch4.clock_divider & 0x07;  // mask to get the last 3 bits
    return divisors[divider_index] << apu->ch4.clock_shift;
}

static void trigger_ch1(APU *apu) {
    apu->ch1.enabled = apu->ch1.dac_enabled;

    if (apu->ch1.length_counter == 0) {
        apu->ch1.length_counter = 64;
    }

    apu->ch1.frequency_timer        = (2048 - apu->ch1.frequency) * 4;
    apu->ch1.envelope_volume        = apu->ch1.enevelope_init_volume;
    apu->ch1.envelope_timer         = apu->ch1.envelope_period;

    apu->ch1.sweep_shadow_frequency = apu->ch1.frequency;
    apu->ch1.sweep_timer            = apu->ch1.sweep_period ? apu->ch1.sweep_period : 8;
    apu->ch1.sweep_enabled          = apu->ch1.sweep_period > 0 || apu->ch1.sweep_shift > 0;

    if (apu->ch1.sweep_shift > 0) {
        calculate_sweep_frequency(apu);
    }
}

static void trigger_ch2(APU *apu) {
    apu->ch2.enabled = apu->ch2.dac_enabled;

    if (apu->ch2.length_counter == 0) {
        apu->ch2.length_counter = 64;
    }

    apu->ch2.frequency_timer = (2048 - apu->ch2.frequency) * 4;
    apu->ch2.envelope_volume = apu->ch2.enevelope_init_volume;
    apu->ch2.envelope_timer  = apu->ch2.envelope_period;
}

static void trigger_ch3(APU *apu) {
    apu->ch3.enabled = apu->ch3.dac_enabled;

    if (apu->ch3.length_counter == 0) {
        apu->ch3.length_counter = 255;
    }

    apu->ch3.frequency_timer = (2048 - apu->ch3.frequency) * 2;  // frequency timer for wave channel
    apu->ch3.wave_position   = 0;                                // reset wave position
}

static void trigger_ch4(APU *apu) {
    apu->ch4.enabled = apu->ch4.dac_enabled;

    if (apu->ch4.length_counter == 0) {
        apu->ch4.length_counter = 64;
    }

    apu->ch4.frequency_timer = get_noise_period(apu);
    apu->ch4.envelope_volume = apu->ch4.enevelope_init_volume;
    apu->ch4.envelope_timer  = apu->ch4.envelope_period;
    apu->ch4.lfsr            = 0x7FFF;  // reset LFSR to a known state
}

static void frame_sequencer_step(APU *apu) {
    // clock length counters on steps 0, 2, 4, 6
    // clock envelope on step 7
    // clock sweep on steps 2 and 6

    switch (apu->frame_sequencer_step) {
        case 0:
        case 4: clock_length_counters(apu); break;
        case 2:
        case 6:
            clock_length_counters(apu);
            clock_sweep(apu);
            break;
        case 7: clock_envelope(apu); break;
    }

    apu->frame_sequencer_step = (apu->frame_sequencer_step + 1) & 7;
}

static void update_channel_timers(APU *apu, int cycles) {
    if (apu->ch1.enabled) {
        int period = (2048 - apu->ch1.frequency) * 4;
        int timer  = apu->ch1.frequency_timer - cycles;

        if (timer <= 0) {
            int ticks = 1 + (-timer) / period; /* how many steps we missed */
            timer += ticks * period;           /* catch up in one go      */
            apu->ch1.duty_position = (apu->ch1.duty_position + ticks) & 7;
        }
        apu->ch1.frequency_timer = timer;
    }

    if (apu->ch2.enabled) {
        int period = (2048 - apu->ch2.frequency) * 4;
        int timer  = apu->ch2.frequency_timer - cycles;

        if (timer <= 0) {
            int ticks = 1 + (-timer) / period;
            timer += ticks * period;
            apu->ch2.duty_position = (apu->ch2.duty_position + ticks) & 7;
        }
        apu->ch2.frequency_timer = timer;
    }

    if (apu->ch3.enabled) {
        int period = (2048 - apu->ch3.frequency) * 2; /* wave is x1/2 */
        int timer  = apu->ch3.frequency_timer - cycles;

        if (timer <= 0) {
            int ticks = 1 + (-timer) / period;
            timer += ticks * period;
            apu->ch3.wave_position = (apu->ch3.wave_position + ticks) & 31;
        }
        apu->ch3.frequency_timer = timer;
    }

    if (apu->ch4.enabled) {
        int period = get_noise_period(apu); /* 4 … 895 CPU cycles */
        int timer  = apu->ch4.frequency_timer - cycles;

        if (timer <= 0) {
            int steps = 1 + (-timer) / period;
            timer += steps * period;

            /* advance the 15-bit or 7-bit LFSR exactly <steps> times */
            uint16_t lfsr = apu->ch4.lfsr;
            for (int s = 0; s < steps; ++s) {
                uint8_t bit = (lfsr ^ (lfsr >> 1)) & 1;
                lfsr        = (lfsr >> 1) | (bit << 14); /* 15-bit variant */
                if (!apu->ch4.width_mode)                /* 7-bit mode */
                    lfsr = (lfsr & ~0x40) | (bit << 6);
            }
            apu->ch4.lfsr = lfsr;
        }
        apu->ch4.frequency_timer = timer;
    }
}

static float soft_clip(float x) {
    // gentle soft clipping to prevent harsh distortion
    if (x > 0.9f)
        return 0.9f + 0.1f * tanhf((x - 0.9f) * 10.0f);
    if (x < -0.9f)
        return -0.9f + 0.1f * tanhf((x + 0.9f) * 10.0f);
    return x;
}

static void update_channel_fades(APU *apu) {
    // ch1
    if (apu->ch1.enabled && apu->ch1.dac_enabled) {
        apu->ch1_fade = fminf(1.0f, apu->ch1_fade + apu->fade_rate);
    } else {
        apu->ch1_fade = fmaxf(0.0f, apu->ch1_fade - apu->fade_rate);
    }

    // ch2
    if (apu->ch2.enabled && apu->ch2.dac_enabled) {
        apu->ch2_fade = fminf(1.0f, apu->ch2_fade + apu->fade_rate);
    } else {
        apu->ch2_fade = fmaxf(0.0f, apu->ch2_fade - apu->fade_rate);
    }

    // ch3
    if (apu->ch3.enabled && apu->ch3.dac_enabled) {
        apu->ch3_fade = fminf(1.0f, apu->ch3_fade + apu->fade_rate);
    } else {
        apu->ch3_fade = fmaxf(0.0f, apu->ch3_fade - apu->fade_rate);
    }

    // ch4
    if (apu->ch4.enabled && apu->ch4.dac_enabled) {
        apu->ch4_fade = fminf(1.0f, apu->ch4_fade + apu->fade_rate);
    } else {
        apu->ch4_fade = fmaxf(0.0f, apu->ch4_fade - apu->fade_rate);
    }
}

static void generate_sample(APU *apu) {
    update_channel_fades(apu);

    // get raw channel outputs
    float ch1_raw             = get_ch1_output_float(apu);
    float ch2_raw             = get_ch2_output_float(apu);
    float ch3_raw             = get_ch3_output_float(apu);
    float ch4_raw             = get_ch4_output_float(apu);

    // apply gentle interpolation to reduce sudden changes
    const float interp_factor = 0.96f; // very light smoothing to avoid muffling
    ch1_raw              = apu->ch1_last_output * (1.0f - interp_factor) + ch1_raw * interp_factor;
    ch2_raw              = apu->ch2_last_output * (1.0f - interp_factor) + ch2_raw * interp_factor;
    ch3_raw              = apu->ch3_last_output * (1.0f - interp_factor) + ch3_raw * interp_factor;
    ch4_raw              = apu->ch4_last_output * (1.0f - interp_factor) + ch4_raw * interp_factor;

    // store for next sample
    apu->ch1_last_output = ch1_raw;
    apu->ch2_last_output = ch2_raw;
    apu->ch3_last_output = ch3_raw;
    apu->ch4_last_output = ch4_raw;

    // apply channel fades
    float ch1            = ch1_raw * apu->ch1_fade;
    float ch2            = ch2_raw * apu->ch2_fade;
    float ch3            = ch3_raw * apu->ch3_fade;
    float ch4            = ch4_raw * apu->ch4_fade;

    float left = 0.0f, right = 0.0f;

    // apply panning and volume
    if (apu->channel_panning & 0x10)
        left += ch1;
    if (apu->channel_panning & 0x01)
        right += ch1;

    if (apu->channel_panning & 0x20)
        left += ch2;
    if (apu->channel_panning & 0x02)
        right += ch2;

    if (apu->channel_panning & 0x40)
        left += ch3;
    if (apu->channel_panning & 0x04)
        right += ch3;

    if (apu->channel_panning & 0x80)
        left += ch4;
    if (apu->channel_panning & 0x08)
        right += ch4;

    // apply master volume with smoother curve
    float vol_left  = (apu->master_volume_left + 1) / 8.0f;
    float vol_right = (apu->master_volume_right + 1) / 8.0f;
    // apply slight curve to master volume for more natural feel
    vol_left        = vol_left * vol_left;
    vol_right       = vol_right * vol_right;
    left *= vol_left;
    right *= vol_right;

    left *= apu->master_fade;
    right *= apu->master_fade;

    if (apu->sound_disabling) {
        apu->master_fade -= apu->master_fade_rate;
        if (apu->master_fade <= 0.0f) {
            apu->master_fade     = 0.0f;
            apu->sound_disabling = false;
            apu->sound_enabled   = false;
        }
    } else if (apu->sound_enabling) {
        apu->master_fade += apu->master_fade_rate;
        if (apu->master_fade >= 1.0f) {
            apu->master_fade    = 1.0f;
            apu->sound_enabling = false;
        }
    }

    // normalize with improved scaling (channels now output -1.0 to 1.0)
    left /= 4.0f;  // 4 channels max
    right /= 4.0f;

    // very light low-pass filter to reduce only the harshest edges
    const float lp_alpha = 0.5f;  // minimal smoothing
    left                 = apu->lp_left + lp_alpha * (left - apu->lp_left);
    right                = apu->lp_right + lp_alpha * (right - apu->lp_right);
    apu->lp_left         = left;
    apu->lp_right        = right;

    // soft clipping instead of hard limiting
    left                 = soft_clip(left);
    right                = soft_clip(right);

    // high-pass filter to remove DC offset
    float out_l = apu->hp_alpha * (apu->hp_last_output_left + left - apu->hp_last_input_left);
    float out_r = apu->hp_alpha * (apu->hp_last_output_right + right - apu->hp_last_input_right);

    apu->hp_last_input_left                   = left;
    apu->hp_last_input_right                  = right;
    apu->hp_last_output_left                  = out_l;
    apu->hp_last_output_right                 = out_r;

    // store in buffer
    apu->audio_buffer[apu->buffer_position++] = out_l;
    apu->audio_buffer[apu->buffer_position++] = out_r;

    // wrap buffer position if necessary
    if (apu->buffer_position >= apu->buffer_size * 2) {
        apu->buffer_position = 0;
    }
}

static void power_off(APU *apu) {
    if (apu->sound_enabled) {
        apu->sound_disabling = true;
        return;  // don't reset channels yet
    }

    // disable all channels
    apu->ch1                 = (ch1_t){0};  // reset channel 1
    apu->ch2                 = (ch2_t){0};  // reset channel 2
    apu->ch3                 = (ch3_t){0};  // reset channel 3
    apu->ch4                 = (ch4_t){0};  // reset channel 4

    // reset master control
    apu->sound_enabled       = false;
    apu->master_volume_left  = 0;
    apu->master_volume_right = 0;
    apu->channel_panning     = 0;
}

static void init_highpass_filter(APU *apu, float cutoff_hz) {
    float sample_rate = 48000.0f;
    float rc          = 1.0f / (2.0f * 3.14159265358979323846 * cutoff_hz);
    float dt          = 1.0f / sample_rate;
    apu->hp_alpha     = rc / (rc + dt);
}

/* APU API functions */
void apu_init(APU *apu, struct CPU *cpu, struct MMU *mmu) {
    apu->cpu          = cpu;
    apu->mmu          = mmu;

    apu->buffer_size  = 2048;
    apu->audio_buffer = malloc(apu->buffer_size * sizeof(float) * 2);  // * 2 for stereo
    if (!apu->audio_buffer) {
        fprintf(stderr, "Failed to allocate audio buffer\n");
        exit(EXIT_FAILURE);
    }

    apu->fade_rate        = 0.001f;   // much faster channel fades
    apu->master_fade_rate = 0.0005f;  // faster master fade

    // initialize high-pass filter with moderate cutoff
    init_highpass_filter(apu, 15.0f);

    apu_reset(apu);
}

void apu_reset(APU *apu) {
    // reset channels
    memset(&apu->ch1, 0, sizeof(ch1_t));
    memset(&apu->ch2, 0, sizeof(ch2_t));
    memset(&apu->ch3, 0, sizeof(ch3_t));
    memset(&apu->ch4, 0, sizeof(ch4_t));

    apu->ch4.lfsr                = 0x7FFF;  // initialize noise channel LFSR to a known state

    // reset frame sequencer
    apu->frame_sequencer_counter = 0;
    apu->frame_sequencer_step    = 0;

    // reset master control
    apu->sound_enabled           = false;
    apu->master_volume_left      = 0;
    apu->master_volume_right     = 0;
    apu->channel_panning         = 0;

    // reset audio buffer
    apu->buffer_position         = 0;
    apu->buffer_read_position    = 0;

    // reset high-pass filter state
    apu->hp_last_input_left      = 0.0f;
    apu->hp_last_input_right     = 0.0f;
    apu->hp_last_output_left     = 0.0f;
    apu->hp_last_output_right    = 0.0f;

    // reset low-pass filter state
    apu->lp_left                 = 0.0f;
    apu->lp_right                = 0.0f;

    // reset fade states
    apu->ch1_fade                = 0.0f;
    apu->ch2_fade                = 0.0f;
    apu->ch3_fade                = 0.0f;
    apu->ch4_fade                = 0.0f;
    apu->master_fade             = 0.0f;
    apu->sound_enabling          = false;
    apu->sound_disabling         = false;

    // for buffer underrun handling
    apu->last_output_left        = 0.0f;
    apu->last_output_right       = 0.0f;

    // reset channel interpolation state
    apu->ch1_last_output         = 0.0f;
    apu->ch2_last_output         = 0.0f;
    apu->ch3_last_output         = 0.0f;
    apu->ch4_last_output         = 0.0f;

    apu->cycles                  = 0;
    apu->cycles_per_sample       = 4194304.0 / 48000.0;
    apu->sample_counter          = 0.0;

    for (int i = 0; i < 16; i++) {
        apu->ch3.wave_ram[i] = (i << 4) | i;  // initialize wave RAM with a simple pattern
    }
}

void apu_step(APU *apu, int cycles) {
    if (!apu->sound_enabled && !apu->sound_disabling) {
        return;
    }

    apu->cycles += cycles;

    apu->frame_sequencer_counter += cycles;
    if (apu->frame_sequencer_counter >= 8192) {
        apu->frame_sequencer_counter -= 8192;
        frame_sequencer_step(apu);
    }

    update_channel_timers(apu, cycles);

    // generate audio samples
    apu->sample_counter += (double)cycles;
    while (apu->sample_counter >= apu->cycles_per_sample) {
        apu->sample_counter -= apu->cycles_per_sample;
        generate_sample(apu);
    }
}

void apu_get_samples(APU *apu, float *buffer, int num_samples) {
    for (int i = 0; i < num_samples * 2; i += 2) {
        int read_pos  = apu->buffer_read_position;
        int write_pos = apu->buffer_position;

        int available = (write_pos - read_pos + apu->buffer_size * 2) % (apu->buffer_size * 2);

        if (available >= 2) {
            // copy samples from audio buffer
            buffer[i]                 = apu->audio_buffer[read_pos];
            buffer[i + 1]             = apu->audio_buffer[(read_pos + 1) % (apu->buffer_size * 2)];

            apu->last_output_left     = buffer[i];
            apu->last_output_right    = buffer[i + 1];
            apu->buffer_read_position = (read_pos + 2) % (apu->buffer_size * 2);
        } else {
            // if buffer is empty, fade to silence
            buffer[i]              = apu->last_output_left * 0.95f;
            buffer[i + 1]          = apu->last_output_right * 0.95f;
            apu->last_output_left  = buffer[i];
            apu->last_output_right = buffer[i + 1];
        }
    }
}

void apu_cleanup(APU *apu) {
    if (apu->audio_buffer) {
        free(apu->audio_buffer);
        apu->audio_buffer = NULL;
    }
}

/* MMU handlers */
void apu_write(APU *apu, uint16_t addr, uint8_t value) {
    if (!apu->sound_enabled && addr != NR52)
        return;

    switch (addr) {
        case NR10:
            apu->ch1.sweep_period = (value >> 4) & 0x07;  // 0-7
            apu->ch1.sweep_negate = (value >> 3) & 0x01;  // 0 or 1
            apu->ch1.sweep_shift  = value & 0x07;         // 0-7
            break;
        case NR11:
            apu->ch1.duty           = (value >> 6) & 0x03;  // 0-3
            apu->ch1.length_counter = 64 - (value & 0x3F);  // 0-64
            break;
        case NR12:
            apu->ch1.enevelope_init_volume = (value >> 4) & 0x0F;
            apu->ch1.envelope_direction    = (value >> 3) & 0x01;
            apu->ch1.envelope_period       = value & 0x07;
            apu->ch1.dac_enabled           = (value & 0xF8) != 0;
            if (!apu->ch1.dac_enabled) {
                apu->ch1.enabled = false;  // disable channel if DAC is disabled
            }
            break;
        case NR13: apu->ch1.frequency = (apu->ch1.frequency & 0x700) | value; break;
        case NR14:
            apu->ch1.frequency      = (apu->ch1.frequency & 0xFF) | ((value & 0x07) << 8);
            apu->ch1.length_enabled = (value >> 6) & 0x01;
            if (value & 0x80) {
                trigger_ch1(apu);
            }
            break;

        case NR21:
            apu->ch2.duty           = (value >> 6) & 0x03;  // 0-3
            apu->ch2.length_counter = 64 - (value & 0x3F);  // 0-64
            break;
        case NR22:
            apu->ch2.enevelope_init_volume = (value >> 4) & 0x0F;
            apu->ch2.envelope_direction    = (value >> 3) & 0x01;
            apu->ch2.envelope_period       = value & 0x07;
            apu->ch2.dac_enabled           = (value & 0xF8) != 0;
            if (!apu->ch2.dac_enabled) {
                apu->ch2.enabled = false;  // disable channel if DAC is disabled
            }
            break;
        case NR23: apu->ch2.frequency = (apu->ch2.frequency & 0x700) | value; break;
        case NR24:
            apu->ch2.frequency      = (apu->ch2.frequency & 0xFF) | ((value & 0x07) << 8);
            apu->ch2.length_enabled = (value >> 6) & 0x01;
            if (value & 0x80) {
                trigger_ch2(apu);
            }
            break;

        case NR30:
            apu->ch3.dac_enabled = (value >> 7) & 0x01;  // enable/disable DAC
            if (!apu->ch3.dac_enabled) {
                apu->ch3.enabled = false;  // disable channel if DAC is disabled
            }
            break;
        case NR31: apu->ch3.length_counter = 256 - value; break;
        case NR32: apu->ch3.output_level = (value >> 5) & 0x03; break;
        case NR33: apu->ch3.frequency = (apu->ch3.frequency & 0x700) | value; break;
        case NR34:
            apu->ch3.frequency      = (apu->ch3.frequency & 0xFF) | ((value & 0x07) << 8);
            apu->ch3.length_enabled = (value >> 6) & 0x01;
            if (value & 0x80) {
                trigger_ch3(apu);
            }
            break;

        case NR41:
            apu->ch4.length_counter = 64 - (value & 0x3F);  // 0-64
            break;
        case NR42:
            apu->ch4.enevelope_init_volume = (value >> 4) & 0x0F;
            apu->ch4.envelope_direction    = (value >> 3) & 0x01;
            apu->ch4.envelope_period       = value & 0x07;
            apu->ch4.dac_enabled           = (value & 0xF8) != 0;
            if (!apu->ch4.dac_enabled) {
                apu->ch4.enabled = false;  // disable channel if DAC is disabled
            }
            break;
        case NR43:
            apu->ch4.clock_shift   = (value >> 4) & 0x0F;  // 0-15
            apu->ch4.width_mode    = (value >> 3) & 0x01;  // 0 or 1
            apu->ch4.clock_divider = value & 0x07;         // 0-7
            break;
        case NR44:
            apu->ch4.length_enabled = (value >> 6) & 0x01;  // length enabled
            if (value & 0x80) {
                trigger_ch4(apu);
            }
            break;

        case NR50:
            apu->master_volume_left  = (value >> 4) & 0x07;  // left volume (0-7)
            apu->master_volume_right = value & 0x07;         // right volume (0-7)
            break;
        case NR51:
            apu->channel_panning = value;  // channel panning (0-255)
            break;
        case NR52:
            if (!(value & 0x80)) {
                power_off(apu);
            } else if (!apu->sound_enabled && (value & 0x80)) {
                apu->sound_enabled        = true;  // enable sound
                apu->sound_enabling       = true;  // start fade-in
                apu->frame_sequencer_step = 0;     // reset frame sequencer step
            }
            break;
        default:  // wave ram
            if (addr >= 0xFF30 && addr <= 0xFF3F) {
                // write to wave RAM (0xFF30 - 0xFF3F)
                apu->ch3.wave_ram[addr - 0xFF30] = value;
            } else {
                fprintf(stderr, "APU: Invalid write to address 0x%04X with value 0x%02X\n", addr,
                        value);
            }
            break;
    }
}

uint8_t apu_read(APU *apu, uint16_t addr) {
    /* wave RAM is always readable */
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        return apu->ch3.wave_ram[addr - 0xFF30];
    }

    switch (addr) {
        /* CH1 registers */
        case NR10:
            return ((apu->ch1.sweep_period << 4) |
                    (apu->ch1.sweep_negate << 3) |
                    apu->ch1.sweep_shift) | 0x80;
        case NR11: return (apu->ch1.duty << 6) | 0x3F;
        case NR12:
            return (apu->ch1.enevelope_init_volume << 4) |
                   (apu->ch1.envelope_direction << 3) |
                   apu->ch1.envelope_period;
        case NR13: return 0xFF;  // write-only
        case NR14: return (apu->ch1.length_enabled ? 0x40 : 0x00) | 0xBF;

        /* CH2 registers */
        case NR21: return (apu->ch2.duty << 6) | 0x3F;
        case NR22:
            return (apu->ch2.enevelope_init_volume << 4) |
                   (apu->ch2.envelope_direction << 3) |
                   apu->ch2.envelope_period;
        case NR23: return 0xFF;  // write-only
        case NR24: return (apu->ch2.length_enabled ? 0x40 : 0x00) | 0xBF;

        /* CH3 registers */
        case NR30: return (apu->ch3.dac_enabled ? 0x80 : 0x00) | 0x7F;
        case NR31: return 0xFF;  // write-only
        case NR32: return (apu->ch3.output_level << 5) | 0x9F;
        case NR33: return 0xFF;  // write-only
        case NR34: return (apu->ch3.length_enabled ? 0x40 : 0x00) | 0xBF;

        /* CH4 registers */
        case NR41: return 0xFF;  // write-only
        case NR42:
            return (apu->ch4.enevelope_init_volume << 4) |
                   (apu->ch4.envelope_direction << 3) |
                   apu->ch4.envelope_period;
        case NR43:
            return (apu->ch4.clock_shift << 4) |
                   (apu->ch4.width_mode << 3) |
                   apu->ch4.clock_divider;
        case NR44: return (apu->ch4.length_enabled ? 0x40 : 0x00) | 0xBF;

        /* master control registers */
        case NR50:
            return (apu->master_volume_left << 4) | apu->master_volume_right;
        case NR51: return apu->channel_panning;
        case NR52:
            return (apu->sound_enabled ? 0x80 : 0x00) | (apu->ch1.enabled ? 0x01 : 0x00) |
                   (apu->ch2.enabled ? 0x02 : 0x00) | (apu->ch3.enabled ? 0x04 : 0x00) |
                   (apu->ch4.enabled ? 0x08 : 0x00) | 0x70;  // bits 4-6 always 1

        default: return 0xFF;  // unmapped APU address
    }
}
