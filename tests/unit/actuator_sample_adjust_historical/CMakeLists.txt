cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_actuator_sample_adjust_historical)

# HISTORICAL / RETIRED suite: compiles the retired sample insert/drop
# actuator moved out of production src/ into this suite (R2).  The
# historical source and its test-local header declare the retired API
# (init/apply_ppm/consume_sample_adjustment/reset); production
# actuators (APLL, NONE) are tested in their own suites.
target_sources(app PRIVATE
  src/test_actuator.c
  src/audio_clock_actuator_sample_adjust_historical.c
)
