# audio_bench

A single-header C++20 library for testing and benchmarking audio DSP code.

## Contents

**`audio_bench::math`** — numeric utilities for DSP.
- `db_to_amp`, `amp_to_db` — decibel/amplitude conversions
- `abs_err`, `rel_err` — absolute and relative error between two values
- `bits_prec` — number of matching significand bits between two floating-point values
- `ulp_of` — ULP (unit in the last place) of a value
- `ulp_dist` — ULP distance between two floating-point values

**`audio_bench::signals`** — test signal generators. Each type exposes a `Spec` struct and a templated `make(spec, sr)` static function returning `std::vector<X>`. `Range::make` takes no sample rate.
- `Sine` — sinusoid at a given frequency, amplitude, and phase
- `Noise` — uniform white noise
- `Impulse` — unit impulse (Dirac delta)
- `Constant` — DC offset
- `Silence` — zero-valued buffer
- `Range` — linearly spaced sequence (inclusive or exclusive endpoint)

**`audio_bench::analysis`** — signal measurement functions.
- `freq_power_for` — power at a specific frequency via the Goertzel algorithm
- `rms_for` — root mean square
- `peak_for` — peak absolute value

**`audio_bench::Error`** — tolerance types used by the expectation functions.
- `Error::Absolute` — absolute difference threshold
- `Error::Relative` — relative difference threshold
- `Error::Ulp_distance` — ULP distance threshold
- `Error::Bits_precision` — minimum matching significand bits
- `Error::Any` — variant holding any of the above

**`audio_bench::Error_tracker<X>`** — accumulates absolute, relative, ULP, and bits-precision errors across many samples and reports aggregate stats (max, mean, RMS, worst-case argument).

**`audio_bench::Tests`** — lightweight test registry and runner.
- `Tests::add(name, func)` — registers a named test
- `Tests::run_all()` — runs all registered tests, reports pass/fail to stdout, returns failure count

**Expectations** — assertion functions that throw `std::runtime_error` on failure.
- `expect_true` — boolean condition check
- `expect_close` — numeric closeness check; accepts `Error::Absolute`, `Relative`, `Bits_precision`, or `Ulp_distance`
- `expect_sinusoidal` — verifies a signal contains a sinusoid at the expected frequency and amplitude
- `expect_silent` — verifies a signal's RMS is within an absolute tolerance of zero

**`audio_bench::Benchmark`** — timing and comparison utilities.
- `Benchmark::Spec` — run configuration: `reps`, `warmup`
- `Benchmark::Stats` — timing results stored in nanoseconds: mean, stddev, min, median, max
- `Benchmark::print(stats, units)` — prints stats formatted in the requested `Benchmark::Units`
- `Benchmark::compare(b1, b2)` — Welch's t-test comparison with significance reporting
- `Benchmark::Units` — `Seconds`, `Milliseconds`, `Microseconds`, `Nanoseconds`

**`audio_bench::Timer<L>`** — runs a callable with warmup and timed repetitions, returns `Benchmark::Stats`.

**`audio_bench::do_not_optimize`** — prevents the compiler from optimizing away benchmark subject code.

**`audio_bench::log`** — formatted logging to stdout with `[INFO]` prefix.

## Requirements

- C++20
- CMake 3.30+

## Integration

```cmake
add_subdirectory(audio_bench)
target_link_libraries(your_target PRIVATE audio_bench)
```

## License

MIT License. Copyright (c) 2026 Sketch Audio, LLC.
