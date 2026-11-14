/*
 * timing.h - frame delta time and frame-time statistics.
 */
#ifndef TIMING_H
#define TIMING_H

void  timing_init(void);

/* Call once per frame. Returns seconds since the previous call,
 * clamped to TIMING_MAX_DT to avoid huge steps after stalls. */
float timing_tick(void);

/* Statistics over the most recent ~0.5 s window. */
float timing_avg_ms(void);
float timing_max_ms(void);
float timing_fps(void);

#define TIMING_MAX_DT (1.0f / 15.0f)

#endif /* TIMING_H */
