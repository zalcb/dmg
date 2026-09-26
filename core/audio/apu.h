#ifndef APU_HEADER
#define APU_HEADER

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* APU registers (to interact with the MMU) */
// global control registers
#define NR50 0xFF24  // sound volume control (and VIN panning, unused)
#define NR51 0xFF25  // sound panning (left/center/right)
#define NR52 0xFF26  // sound on/off

// CH1
// - square wave pulse (with sweep)
// - duty cycle
// - envelope volume (automatic)
#define NR10 0xFF10  // sweep register
#define NR11 0xFF11  // sound length and duty cycle
#define NR12 0xFF12  // envelope register
#define NR13 0xFF13  // frequency low byte
#define NR14 0xFF14  // frequency high byte and control

// CH2
// - square wave pulse (no sweep)
// - duty cycle
// - envelope volume (automatic)
#define NR21 0xFF16  // sound length and duty cycle
#define NR22 0xFF17  // envelope register
#define NR23 0xFF18  // frequency low byte
#define NR24 0xFF19  // frequency high byte and control

// CH3
// - wave channel (user-supplied wave pattern)
// - 4-bit
// - no envelope
#define NR30 0xFF1A  // sound on/off
#define NR31 0xFF1B  // sound length
#define NR32 0xFF1C  // output level (2-bit)
#define NR33 0xFF1D  // frequency low byte
#define NR34 0xFF1E  // frequency high byte and control

// CH4
// - noise channel (randomly switched frequency)
#define NR41 0xFF20  // sound length
#define NR42 0xFF21  // envelope register
#define NR43 0xFF22  // frequency control
#define NR44 0xFF23  // control register

struct CPU;
struct MMU;

/* channel 1 type */
typedef struct {
    // sweep
    uint8_t sweep_period;        // sweep period (0-7)
    uint8_t sweep_shift;         // sweep shift (0-7)
    bool sweep_negate;           // sweep negate (true/false)
    bool sweep_enabled;          // sweep enabled (true/false)
    int sweep_timer;             // sweep timer (in CPU cycles)
    int sweep_shadow_frequency;  // shadow frequency (for sweep calculations)

    // duty and length
    uint8_t duty;            // duty cycle (0-3)
    uint8_t length_counter;  // length counter (0-63)
    bool length_enabled;     // length enabled (true/false)

    // envelope
    uint8_t envelope_volume;        // envelope volume (0-15)
    uint8_t enevelope_init_volume;  // initial envelope volume (0-15)
    uint8_t envelope_period;        // envelope period (0-7)
    bool
        envelope_direction;  //  direction (true -> increase, false -> decrease)
    int envelope_timer;      // envelope timer (in CPU cycles)

    // frequency
    uint16_t frequency;   // frequency (0-2047)
    int frequency_timer;  // frequency timer (in CPU cycles)

    // control and output
    bool enabled;          // channel enabled (true/false)
    bool dac_enabled;      // DAC enabled (true/false)
    uint8_t output_level;  // output level (0-3)
    int duty_position;     // current duty position (0-7)
} ch1_t;

/* channel 2 type (same as ch1 but no sweep) */
typedef struct {
    // duty and length
    uint8_t duty;            // duty cycle (0-3)
    uint8_t length_counter;  // length counter (0-63)
    bool length_enabled;     // length enabled (true/false)

    // envelope
    uint8_t envelope_volume;        // envelope volume (0-15)
    uint8_t enevelope_init_volume;  // initial envelope volume (0-15)
    uint8_t envelope_period;        // envelope period (0-7)
    bool
        envelope_direction;  //  direction (true -> increase, false -> decrease)
    int envelope_timer;      // envelope timer (in CPU cycles)

    // frequency
    uint16_t frequency;   // frequency (0-2047)
    int frequency_timer;  // frequency timer (in CPU cycles)

    // control and output
    bool enabled;          // channel enabled (true/false)
    bool dac_enabled;      // DAC enabled (true/false)
    uint8_t output_level;  // output level (0-3)
    int duty_position;     // current duty position (0-7)
} ch2_t;

/* channel 3 type */
typedef struct {
    uint8_t length_counter;  // length counter (0-256)
    bool length_enabled;     // length enabled (true/false)
    uint8_t output_level;    // output level (0-3)

    uint16_t frequency;   // frequency (0-2047)
    int frequency_timer;  // frequency timer (in CPU cycles)

    bool enabled;           // channel enabled (true/false)
    bool dac_enabled;       // DAC enabled (true/false)
    uint8_t wave_position;  // current wave position (0-31)
    uint8_t wave_ram[16];   // wave RAM (32 4-bit samples, 16 bytes)
} ch3_t;

/* channel 4 type */
typedef struct {
    uint8_t length_counter;  // length counter (0-64)
    bool length_enabled;     // length enabled (true/false)

    uint8_t envelope_volume;        // envelope volume (0-15)
    uint8_t enevelope_init_volume;  // initial envelope volume (0-15)
    uint8_t envelope_period;        // envelope period (0-7)
    bool
        envelope_direction;  //  direction (true -> increase, false -> decrease)
    int envelope_timer;      // envelope timer (in CPU cycles)

    uint8_t clock_shift;    // clock shift (0-7)
    uint8_t clock_divider;  // clock divider (0-15)
    bool width_mode;  // width mode (true -> 15-bit lsfr, false -> 7-bit lsfr)
    uint16_t lfsr;    // linear feedback shift register
    int frequency_timer;  // frequency timer (in CPU cycles)

    bool enabled;          // channel enabled (true/false)
    bool dac_enabled;      // DAC enabled (true/false)
    uint8_t output_level;  // output level (0-3)
} ch4_t;

typedef struct APU {
    struct CPU *cpu;
    struct MMU *mmu;

    ch1_t ch1;  // channel 1 (square wave with sweep)
    ch2_t ch2;  // channel 2 (square wave without sweep)
    ch3_t ch3;  // channel 3 (wave channel)
    ch4_t ch4;  // channel 4 (noise channel)

    // frame sequencer
    int frame_sequencer_counter;   // frame sequencer counter
    uint8_t frame_sequencer_step;  // current step in the frame sequencer (0-7)

    // master control
    bool sound_enabled;
    uint8_t master_volume_left;
    uint8_t master_volume_right;
    uint8_t channel_panning;  // (NR51)

    // audio output buffer
    float *audio_buffer;
    int buffer_position;  // current position in the audio buffer
    int buffer_read_position;
    int buffer_size;  // size of the audio buffer
    double sample_counter;

    float hp_alpha;              // filter coefficient
    float hp_last_input_left;    // previous input sample (left)
    float hp_last_input_right;   // previous input sample (right)
    float hp_last_output_left;   // previous output sample (left)
    float hp_last_output_right;  // previous output sample (right)

    float lp_left;   // low-pass filter state (left)
    float lp_right;  // low-pass filter state (right)

    // channel fade states
    float ch1_fade;   // 0.0 to 1.0
    float ch2_fade;   // 0.0 to 1.0
    float ch3_fade;   // 0.0 to 1.0
    float ch4_fade;   // 0.0 to 1.0
    float fade_rate;  // how fast channels fade in/out

    // master volume fade
    float master_fade;       // 0.0 to 1.0
    float master_fade_rate;  // how fast master fades
    bool sound_enabling;     // currently fading in
    bool sound_disabling;    // currently fading out

    // Buffer underrun handling
    float last_output_left;   // for smooth underrun recovery
    float last_output_right;  // for smooth underrun recovery

    // Channel state interpolation to reduce popping
    float ch1_last_output;    // previous sample for smoothing
    float ch2_last_output;    // previous sample for smoothing
    float ch3_last_output;    // previous sample for smoothing
    float ch4_last_output;    // previous sample for smoothing

    uint64_t cycles;
    double cycles_per_sample;  // number of CPU cycles per audio sample

} APU;

void apu_init(APU *apu, struct CPU *cpu, struct MMU *mmu);
void apu_reset(APU *apu);
void apu_cleanup(APU *apu);
void apu_step(APU *apu, int cycles);

/* mmu write/read handlers */
void apu_write(APU *apu, uint16_t addr, uint8_t value);
uint8_t apu_read(APU *apu, uint16_t addr);

/* audio output */
void apu_get_samples(APU *apu, float *buffer, int num_samples);

#endif
