/* SPDX-License-Identifier: MIT */
#ifndef TDVP_STARTUP_TRACE_H
#define TDVP_STARTUP_TRACE_H

/* Optional diagnostic extension in owner.reserved[0..2]. It is covered by
 * the existing sequence protocol and cookies, but NEVER grants resources or
 * advances the liveness heartbeat. Old firmware leaves it zero/unavailable.
 * A result of 1 means the call was entered; 0 means completed; negative means
 * returned failure. A frozen entry identifies the last call, not its cause.
 */
#define TDVP_STARTUP_TRACE_MAGIC 0x31545354U /* TST1 */
#define TDVP_STARTUP_PENDING 1
enum tdvp_startup_stage {
    TDVP_STARTUP_NONE,
    TDVP_STARTUP_GRANT,
    TDVP_STARTUP_I2C_CLOCK,
    TDVP_STARTUP_I2C_MAP,
    TDVP_STARTUP_I2C_REGISTERS,
    TDVP_STARTUP_I2C_SPEED,
    TDVP_STARTUP_I2C_REGISTER,
    TDVP_STARTUP_CAMERA_CLOCKS,
    TDVP_STARTUP_CMPI,
    TDVP_STARTUP_LOG,
    TDVP_STARTUP_MMZ,
    TDVP_STARTUP_MMZ_USERDEV,
    TDVP_STARTUP_SYSCTRL,
    TDVP_STARTUP_VB,
    TDVP_STARTUP_CAMERA_PINS,
    TDVP_STARTUP_VICAP,
    TDVP_STARTUP_AI_CLOCKS,
    TDVP_STARTUP_GNNE,
    TDVP_STARTUP_AI2D,
    TDVP_STARTUP_FFT,
    TDVP_STARTUP_LAUNCH,
    TDVP_STARTUP_COMPLETE
};

static inline const char *tdvp_startup_stage_name(unsigned int stage)
{
    switch (stage) {
    case TDVP_STARTUP_GRANT: return "grant";
    case TDVP_STARTUP_I2C_CLOCK: return "i2c4-clock";
    case TDVP_STARTUP_I2C_MAP: return "i2c4-map";
    case TDVP_STARTUP_I2C_REGISTERS: return "i2c4-registers";
    case TDVP_STARTUP_I2C_SPEED: return "i2c4-speed";
    case TDVP_STARTUP_I2C_REGISTER: return "i2c4-register-bus";
    case TDVP_STARTUP_CAMERA_CLOCKS: return "camera-clocks";
    case TDVP_STARTUP_CMPI: return "cmpi";
    case TDVP_STARTUP_LOG: return "log";
    case TDVP_STARTUP_MMZ: return "mmz";
    case TDVP_STARTUP_MMZ_USERDEV: return "mmz-userdev";
    case TDVP_STARTUP_SYSCTRL: return "sysctrl";
    case TDVP_STARTUP_VB: return "vb";
    case TDVP_STARTUP_CAMERA_PINS: return "camera-pins";
    case TDVP_STARTUP_VICAP: return "vicap";
    case TDVP_STARTUP_AI_CLOCKS: return "ai-clocks";
    case TDVP_STARTUP_GNNE: return "gnne";
    case TDVP_STARTUP_AI2D: return "ai2d";
    case TDVP_STARTUP_FFT: return "fft";
    case TDVP_STARTUP_LAUNCH: return "worker-launch";
    case TDVP_STARTUP_COMPLETE: return "complete";
    default: return "unavailable";
    }
}

/* CPU1 startup thread ONLY. stage=NONE retains the most recent stage while
 * recording a top-level failure. No printing, allocation, locks or MMIO to
 * media engines; publish only into our granted ownership control half.
 */
void tdvp_cpu1_startup_trace(unsigned int stage, int result);
#endif
