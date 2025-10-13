#pragma once

#include <chrono>
#include <cmath>
#include <complex>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

namespace audio_bench {

// MARK: - utils

template<typename... Ts>
struct Inline_visitor : Ts... {
    using Ts::operator()...;
};

template<typename X>
inline auto db_to_amp(X db) -> X
{
    return std::pow(X{10}, db / 20);
}

template<typename X>
inline auto amp_to_db(X amp) -> X
{
    if (amp <= 0) return -std::numeric_limits<X>::infinity();
    return 20 * std::log10(amp);
}

// MARK: - signals

template<typename X>
struct Sine_spec {
    X freq{1000};
    X amp{1};
    X phase{};
    X dur{1};
    X sr{48000};
};

template<typename X>
inline auto make_sine(const Sine_spec<X>& spec) -> std::vector<X>
{
    const auto N = static_cast<size_t>(spec.dur * spec.sr);
    auto x = std::vector<X>(N);
    for (size_t n = 0; n < N; ++n) {
        const auto arg = n * spec.freq / spec.sr;
        x[n] = spec.amp * std::sin(2 * std::numbers::pi_v<X> * arg + spec.phase);
    }
    return x;
}

template<typename X>
struct Noise_spec {
    X amp{1};
    X dur{1};
    X sr{48000};
};

template<typename X>
inline auto make_noise(const Noise_spec<X>& spec) -> std::vector<X>
{
    auto device = std::random_device{};
    auto gen = std::mt19937{device()};
    auto dist = std::uniform_real_distribution<X>{-1, 1};

    const auto N = static_cast<size_t>(spec.dur * spec.sr);
    auto x = std::vector<X>(N);

    for (size_t n = 0; n < N; ++n) {
        x[n] = spec.amp * dist(gen);
    }

    return x;
}

template<typename X>
struct Dc_spec {
    X amp{1};
    X dur{1};
    X sr{48000};
};

template<typename X>
inline auto make_dc(const Dc_spec<X>& spec) -> std::vector<X>
{
    const auto N = static_cast<size_t>(spec.dur * spec.sr);
    return std::vector<X>(N, spec.amp);
}

// MARK: - analysis

template<typename X>
inline auto freq_power_for(const std::vector<X>& x, X freq, X sr) -> X
{
    // See: https://en.wikipedia.org/wiki/Goertzel_algorithm (thanks, Grok)
    if (x.empty()) return {};
    if (freq <= 0 || freq > sr / 2) return {};

    const auto N = x.size();
    const auto omega = 2 * std::numbers::pi_v<X> * freq / sr;
    const auto target = std::polar(X{1}, -omega);

    auto s0 = X{};
    auto s1 = X{};
    const auto coeff = 2 * std::cos(omega);

    for (auto sample : x) {
        const auto s = sample + coeff * s0 - s1;
        s1 = s0;
        s0 = s;
    }

    const auto y = std::complex<X>{s0, 0} - target * std::complex<X>{s1, 0};

    const auto mag_sq = std::norm(y);
    return 2 * mag_sq / (N * N);
}

template<typename X>
inline auto peak_for(const std::vector<X>& x) -> X
{
    if (x.empty()) return {};
    const auto peak = std::ranges::max_element(x, std::less{}, [](auto v) { return std::abs(v); });
    return std::abs(*peak);
}

template<typename X>
inline auto rms_for(const std::vector<X>& x) -> X
{
    if (x.empty()) return {};
    const auto N = x.size();
    const auto sum_sq = std::accumulate(x.begin(), x.end(), X{}, [](X acc, X val) { return acc + val * val; });
    return std::sqrt(sum_sq / N);
}

// MARK: - error

template<typename X>
inline auto abs_err(X calc, X ref) -> X
{
    return std::abs(calc - ref);
}

template<typename X>
inline auto rel_err(X calc, X ref) -> X
{
    if (calc == ref) return 0;
    if (ref == 0) return std::numeric_limits<X>::infinity();
    return std::abs((calc - ref) / ref);
}

template<typename X>
struct Abs_err { X value{1e-4f}; }; // Default: 0.0001 (-80 dB)

template<typename X>
struct Rel_err { X value{1e-2f}; }; // Default: 1%

template<typename X>
using Err_value = std::variant<Abs_err<X>, Rel_err<X>>;

namespace impl {
template<typename X>
inline auto get_err_value(const Err_value<X>& err) -> X
{
    return std::visit([](const auto& e) { return e.value; }, err);
}
} // namespace impl

// MARK: - tests

struct Test {
    std::string name{};
    std::function<void()> func{};
};

namespace impl {
inline auto all_tests() -> std::vector<Test>&
{
    static auto tests = std::vector<Test>{};
    return tests;
}
} // namespace impl

inline auto add_test(std::string name, std::function<void()> func) -> void
{
    impl::all_tests().push_back(Test{std::move(name), std::move(func)});
}

inline auto run_all_tests() -> size_t
{
    std::cout << std::format("[TEST] Running {} tests...\n", impl::all_tests().size());
    auto num_failed = size_t{};
    for (auto& [name, func] : impl::all_tests()) {
        try {
            func();
            std::cout << "[PASS] " << name << "\n";
        }
        catch (const std::exception& e) {
            std::cout << "[FAIL] " << name << " " << e.what() << "\n";
            ++num_failed;
        }
    }
    std::cout << (num_failed == 0 ? "[TEST] All tests passed!\n" : std::format("{} tests failed.\n", num_failed));
    return num_failed;
}

// MARK: - expect

inline auto expect_true(bool cond, const std::string& msg = {}) -> void
{
    if (!cond) {
        throw std::runtime_error(msg.empty() ? "Condition is false." : msg);
    }
}

template<typename X>
inline auto expect_close(X calc, X ref, const Err_value<X>& tol, const std::string& msg = {}) -> void
{
    const auto tol_val = impl::get_err_value(tol);
    const auto err_val = std::visit(Inline_visitor{
        [&](const Abs_err<X>&) { return abs_err(calc, ref); },
        [&](const Rel_err<X>&) { return rel_err(calc, ref); }
    }, tol);
    if (err_val > tol_val) {
        throw std::runtime_error(std::format(
            "{}Got: {}, Ref: {}, Err: {}, Tol: {}",
            msg.empty() ? "" : std::format("{} ", msg), calc, ref, err_val, tol_val
        ));
    }
}

template<typename X>
inline auto expect_close(X calc, X ref, const Abs_err<X>& tol, const std::string& msg = {}) -> void
{
    expect_close(calc, ref, Err_value<X>{tol}, msg);
}

template<typename X>
inline auto expect_close(X calc, X ref, const Rel_err<X>& tol, const std::string& msg = {}) -> void
{
    expect_close(calc, ref, Err_value<X>{tol}, msg);
}

template<typename X>
inline auto expect_sinusoidal(const std::vector<X>& x, X freq, X amp, X sr, const Rel_err<X>& tol = {}, const std::string& msg = {}) -> void
{
    if (x.empty()) { throw std::runtime_error("Signal is empty."); };

    const auto power_at_freq = freq_power_for(x, freq, sr);
    if (power_at_freq <= 0) { throw std::runtime_error("Signal is not periodic."); };

    const auto rms = rms_for(x);
    if (rms <= 0) { throw std::runtime_error("Signal RMS is zero."); };

    const auto calc_amp = std::sqrt(2 * power_at_freq);
    const auto err = rel_err(calc_amp, amp); 

    if (err > tol.value) {
        throw std::runtime_error(std::format(
            "{}Found amplitude: {:<.2f} for frequency: {:<.2f}, Expected: {:<.2f}, Err: {:<.2f}%, Tol: {:<.2f}%",
            msg.empty() ? "" : std::format("{} ", msg), calc_amp, freq, amp, 100 * err, 100 * tol.value
        ));
    }
}

template<typename X>
inline auto expect_silent(const std::vector<X>& x, const Abs_err<X>& tol = {}, const std::string& msg = {}) -> void
{
    if (x.empty()) { throw std::runtime_error("Signal is empty."); };

    const auto rms = rms_for(x);
    if (rms <= 0) return;

    if (rms > tol.value) {
        throw std::runtime_error(std::format(
            "{}Found RMS: {:<.6f}, Tol: {:<.6f}",
            msg.empty() ? "" : std::format("{} ", msg), rms, tol.value
        ));
    }
}

// MARK: - benchmarks

// See: https://theunixzoo.co.uk/blog/2021-10-14-preventing-optimisations.html
template<typename T>
inline auto do_not_optimize(T& value) -> void
{
#if defined(_MSC_VER)
    (void)value;
    _ReadWriteBarrier();
#else
    asm volatile("" : "+r,m"(value) : : "memory");
#endif
}

enum class Bench_units { seconds, milliseconds, microseconds, nanoseconds };

namespace impl {
inline auto nanos_to(double x, Bench_units dur) -> double
{
    switch (dur) {
        case Bench_units::seconds: return x * 1e-9;
        case Bench_units::milliseconds: return x * 1e-6;
        case Bench_units::microseconds: return x * 1e-3;
        case Bench_units::nanoseconds: return x;
    }
    return x;
}

inline auto units_name(Bench_units dur) -> std::string
{
    switch (dur) {
        case Bench_units::seconds: return "s";
        case Bench_units::milliseconds: return "ms";
        case Bench_units::microseconds: return "us";
        case Bench_units::nanoseconds: return "ns";
    }
    return "";
}

inline auto build_type_name() -> std::string
{
#ifdef NDEBUG
    return "Release";
#else
    return "Debug";
#endif
}
} // namespace impl

struct Bench_spec {
    size_t reps{1000};
    size_t warmup{1000};
    Bench_units units{Bench_units::microseconds};
};

struct Bench_stats {
    std::string name{};
    size_t reps{};
    size_t warmup{};
    double mean{};
    double stddev{};
    double min{};
    double median{};
    double max{};
    Bench_units units{Bench_units::microseconds};
};

inline auto print_bench_stats(const Bench_stats& stats) -> void
{
    std::cout << std::format(
        "Stats for benchmark: '{}' (build type: {})\n"
        "---- Reps: {}, Warmup: {}\n"
        "---- Mean: {:<.3f} {}, Std. Dev.: {:<.3f} {}, Min: {:<.3f} {}, Median: {:<.3f} {}, Max: {:<.3f} {}\n\n",
        stats.name, impl::build_type_name(),
        stats.reps, stats.warmup,
        stats.mean, impl::units_name(stats.units),
        stats.stddev, impl::units_name(stats.units),
        stats.min, impl::units_name(stats.units),
        stats.median, impl::units_name(stats.units),
        stats.max, impl::units_name(stats.units)
    );
}

template<typename L>
struct Lambda_timer {

    Lambda_timer(const std::string& name, L&& l) : _name{name}, _lambda{l} {}

    auto run(const Bench_spec& spec) -> Bench_stats
    {
        _trials.assign(spec.reps, 0);

        // warmup
        for (size_t i = 0; i < spec.warmup; ++i) {
            _lambda();
        }

        for (size_t i = 0; i < spec.reps; ++i) {
            const auto start = std::chrono::high_resolution_clock::now();
            _lambda();
            const auto end = std::chrono::high_resolution_clock::now();
            const auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            _trials[i] = static_cast<double>(dur);
        }

        std::ranges::sort(_trials);
        const auto sum = std::accumulate(_trials.begin(), _trials.end(), double{});
        const auto mean = sum / spec.reps;

        const auto stddev = [&]() {
            auto result = double{};
            for (const auto t : _trials) {
                const auto dt = t - mean;
                result += dt * dt;
            }
            return std::sqrt(result / spec.reps);
        }();

        const auto median = _trials[spec.reps / 2];
        const auto min_val = _trials[0];
        const auto max_val = _trials[spec.reps - 1];

        return {
            .name = _name,
            .reps = spec.reps,
            .warmup = spec.warmup,
            .mean = impl::nanos_to(mean, spec.units),
            .stddev = impl::nanos_to(stddev, spec.units),
            .min = impl::nanos_to(min_val, spec.units),
            .median = impl::nanos_to(median, spec.units),
            .max = impl::nanos_to(max_val, spec.units),
            .units = spec.units
        };
    }

private:

    std::string _name{};
    L _lambda{};
    std::vector<double> _trials{};

};

} // namespace audio_bench