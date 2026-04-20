#pragma once

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <concepts>
#include <cstdint>
#include <cstddef>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// MARK: - math

namespace audio_bench::math {

template<std::floating_point X>
inline auto db_to_amp(X db) -> X
{
    return std::pow(X{10}, db / 20);
}

template<std::floating_point X>
inline auto amp_to_db(X amp, X noise_floor = -144) -> X
{
    if (amp <= 0) return noise_floor;
    return 20 * std::log10(amp);
}

template<std::floating_point X>
inline auto abs_err(X calc, X ref) -> X
{
    return std::abs(calc - ref);
}

template<std::floating_point X>
inline auto rel_err(X calc, X ref) -> X
{
    if (calc == ref) return 0;
    if (calc == 0 && ref == 0) return std::numeric_limits<X>::infinity();
    const auto ac = std::abs(calc);
    const auto ar = std::abs(ref);
    return std::abs(calc - ref) / std::max(ac, ar);
}

namespace impl {
template<std::floating_point X>
struct fp_traits;

template<>
struct fp_traits<float> {
    using int_type = uint32_t;
    static constexpr auto significand_bits = int_type{23};
    static constexpr auto exponent_bits = int_type{8};
    static constexpr auto significand_mask = (int_type{1} << significand_bits) - 1;
    static constexpr auto exponent_mask = ((int_type{1} << exponent_bits) - 1) << significand_bits;
};

template<>
struct fp_traits<double> {
    using int_type = uint64_t;
    static constexpr auto significand_bits = int_type{52};
    static constexpr auto exponent_bits = int_type{11};
    static constexpr auto significand_mask = (int_type{1} << significand_bits) - 1;
    static constexpr auto exponent_mask = ((int_type{1} << exponent_bits) - 1) << significand_bits;
};
} // namespace impl

template<std::floating_point X>
inline auto bits_prec(X a, X b) -> typename impl::fp_traits<X>::int_type
{
    using traits = impl::fp_traits<X>;
    using I = typename traits::int_type;

    const auto ua = std::bit_cast<I>(a);
    const auto ub = std::bit_cast<I>(b);

    constexpr auto sign_and_exp_mask = ~traits::significand_mask;
    if ((ua & sign_and_exp_mask) != (ub & sign_and_exp_mask)) {
        return 0;
    }

    const auto diff = (ua ^ ub) & traits::significand_mask;
    if (diff == 0) {
        return traits::significand_bits;
    }

    constexpr auto total_bits = static_cast<I>(sizeof(I) * 8);
    constexpr auto slack = total_bits - traits::significand_bits;
    return static_cast<I>(std::countl_zero(diff)) - slack;
}

// see: https://www.emmtrix.com/wiki/ULP_Difference_of_Float_Numbers
template<std::floating_point X>
inline auto ulp_of(X x) -> X
{
    x = std::abs(x);
    return std::nextafter(x, std::numeric_limits<X>::infinity()) - x;
}

// see: https://www.emmtrix.com/wiki/ULP_Difference_of_Float_Numbers
template<std::floating_point X>
inline auto ulp_dist(X f1, X f2) -> typename impl::fp_traits<X>::int_type
{
    if (std::signbit(f1) != std::signbit(f2)) {
        return ulp_dist(std::abs(f1), X{}) + ulp_dist(std::abs(f2), X{}) + 1;
    }

    using I = typename impl::fp_traits<X>::int_type;
    const auto u1 = std::bit_cast<I>(f1);
    const auto u2 = std::bit_cast<I>(f2);

    return u1 > u2 ? u1 - u2 : u2 - u1;
};
} // namespace audio_bench::math

// MARK: - logging

namespace audio_bench {
inline auto log(const std::string& msg) -> void
{
    // Log [INFO] in yellow followed by msg arg
    std::cout << "\033[33m[INFO]\033[0m " << msg << "\n";
}

// log with format string
template<typename... Args>
inline auto log(std::format_string<Args...> fmt, Args&&... args) -> void
{
    log(std::format(fmt, std::forward<Args>(args)...));
}
}

// MARK: - signals

namespace audio_bench::signals {

struct Silence {
    struct Spec {
        double dur{1}; // seconds
    };

    template<typename X>
    static auto make(const Spec& spec, double sr) -> std::vector<X>
    {
        const auto N = static_cast<size_t>(spec.dur * sr);
        return std::vector<X>(N, X{});
    }
};

struct Constant {
    struct Spec {
        double amp{1};
        double dur{1}; // seconds
    };

    template<typename X>
    static auto make(const Spec& spec, double sr) -> std::vector<X>
    {
        const auto N = static_cast<size_t>(spec.dur * sr);
        return std::vector<X>(N, static_cast<X>(spec.amp));
    }
};

struct Sine {
    struct Spec {
        double freq{1000};
        double amp{1};
        double phase{};
        double dur{1}; // seconds
    };

    template<typename X>
    static auto make(const Spec& spec, double sr) -> std::vector<X>
    {
        const auto N = static_cast<size_t>(spec.dur * sr);
        auto x = std::vector<X>(N, X{});
        for (size_t i = 0; i < N; ++i) {
            const auto arg = i * spec.freq / sr;
            const auto xi = spec.amp * std::sin(2 * std::numbers::pi_v<double> * arg + spec.phase);
            x[i] = static_cast<X>(xi);
        }
        return x;
    }
};

struct Noise {
    struct Spec {
        double amp{1};
        double dur{1}; // seconds
    };

    template<typename X>
    static auto make(const Spec& spec, double sr) -> std::vector<X>
    {
        const auto N = static_cast<size_t>(spec.dur * sr);
        auto x = std::vector<X>(N, X{});
        auto device = std::random_device{};
        auto gen = std::mt19937{device()};
        auto dist = std::uniform_real_distribution<X>{-1, 1};
        for (size_t i = 0; i < N; ++i) {
            x[i] = static_cast<X>(spec.amp) * dist(gen);
        }
        return x;
    }
};

struct Impulse {
    struct Spec {
        double amp{1};
        double dur{1}; // seconds
    };

    template<typename X>
    static auto make(const Spec& spec, double sr) -> std::vector<X>
    {
        const auto N = static_cast<size_t>(spec.dur * sr);
        auto x = std::vector<X>(N, X{});
        if (!x.empty()) {
            x[0] = static_cast<X>(spec.amp);
        }
        return x;
    }
};

struct Range {
    struct Spec {
        double start{0};
        double end{1};
        size_t n{100};
        bool inclusive{true}; // Whether the end value should be included in the range.
    };

    template<typename X>
    static auto make(const Spec& spec) -> std::vector<X>
    {
        if (spec.n == 0) return {};
        if (spec.n == 1) return {static_cast<X>(spec.start)};
        auto x = std::vector<X>(spec.n);
        const auto denom = spec.inclusive ? (spec.n - 1) : spec.n;
        const auto step = (spec.end - spec.start) / denom;
        for (size_t n = 0; n < spec.n; ++n) {
            x[n] = static_cast<X>(spec.start + n * step);
        }
        if (spec.inclusive) {
            x.back() = static_cast<X>(spec.end);
        }
        return x;
    }
};

} // namespace audio_bench::signals

// MARK: - analysis

namespace audio_bench::analysis {

template<typename X>
inline auto freq_power_for(const std::vector<X>& x, X freq, X sr) -> X
{
    // See: https://en.wikipedia.org/wiki/Goertzel_algorithm (thanks, Grok)
    if (x.empty()) return {};
    if (freq <= 0 || freq > sr / 2) return {};

    const auto N = x.size();
    const auto omega = 2 * std::numbers::pi_v<X> *freq / sr;
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

} // namespace audio_bench::analysis

// MARK: - tests

namespace audio_bench {

struct Error {
    // Absolute error.
    struct Absolute {
        double value{1e-4f}; // Default: 0.0001 (-80 dB)
    };

    // Relative error.
    struct Relative {
        double value{1e-2f}; // Default: 1%
    };

    // ULP distance.
    struct Ulp_distance {
        size_t value{10};
    };

    // Bits of precision.
    struct Bits_precision {
        size_t value{5};
    };

    using Any = std::variant<Absolute, Relative, Bits_precision, Ulp_distance>;
};

template<std::floating_point X>
struct Error_tracker {
    
    struct Accum {
        X max{std::numeric_limits<X>::lowest()};
        X sum{};
        X sum_sq{};
        size_t count{};
        X arg_max{};
        X calc_max{};
        X ref_max{};
    };

    // Accumulate worst case errors.
    auto update_with(X a, X c, X r) -> void
    {
        update_accum(_abs, a, c, r, math::abs_err(c, r));
        update_accum(_rel, a, c, r, math::rel_err(c, r));
        update_accum(_ulp, a, c, r, static_cast<X>(math::ulp_dist(c, r)));

        const auto bits = -static_cast<X>(math::bits_prec(c, r)); // Invert.
        if (bits != 0) {
            update_accum(_bit, a, c, r, bits); 
        } else {
            _bit_zeros++;
        }
    }

    auto report() const -> void
    {
        const auto abs = finalize(_abs);
        const auto rel = finalize(_rel);
        const auto ulp = finalize(_ulp);
        const auto bit = finalize(_bit);

        log("---- Error Report ----");

        log("Absolute error:");
        log("  max: {:.2e}, mean: {:.2e}, rms: {:.2e}", abs.max, abs.mean, abs.rms);
        log("  worst @ arg: {}, calc: {}, ref: {}", abs.arg, abs.calc, abs.ref);

        log("Relative error:");
        log("  max: {:.2e}, mean: {:.2e}, rms: {:.2e}", rel.max, rel.mean, rel.rms);
        log("  worst @ arg: {}, calc: {}, ref: {}", rel.arg, rel.calc, rel.ref);

        log("ULP distance:");
        log("  max: {}, mean: {:.2e}, rms: {:.2e}", ulp.max, ulp.mean, ulp.rms);
        log("  worst @ arg: {}, calc: {}, ref: {}", ulp.arg, ulp.calc, ulp.ref);

        // remember: stored as negative bits
        const auto min_bits = -bit.max;

        log("Bits precision (non-zero):");
        log("  min: {}, mean: {:.2e}, rms: {:.2e}", 
            min_bits,
            -bit.mean,
            -bit.rms);
        log("  worst @ arg: {}, calc: {}, ref: {}", bit.arg, bit.calc, bit.ref);
        log("  zero count: {}", _bit_zeros);

        log("----------------------");
    }

private:

    Accum _abs{};
    Accum _rel{};
    Accum _ulp{};
    Accum _bit{};
    size_t _bit_zeros{};

    auto update_accum(Accum& accum, X a, X c, X r, X err) -> void
    {
        accum.count++;
        accum.sum += err;
        accum.sum_sq += err * err;
        if (err > accum.max) {
            accum.max = err;
            accum.arg_max = a;
            accum.calc_max = c;
            accum.ref_max = r;
        }
    }

    auto finalize(const Accum& a) const
    {
        struct Stats {
            X max;
            X mean;
            X rms;
            X arg;
            X calc;
            X ref;
        };

        if (a.count == 0) {
            return Stats{};
        }

        return Stats{
            .max = a.max,
            .mean = a.sum / X(a.count),
            .rms = std::sqrt(a.sum_sq / X(a.count)),
            .arg = a.arg_max,
            .calc = a.calc_max,
            .ref = a.ref_max
        };
    }
};

struct Tests {

    struct Test {
        std::string name{};
        std::function<void()> func{};
    };

    static auto add(std::string name, std::function<void()> func) -> void
    {
        all().push_back(Test{std::move(name), std::move(func)});
    }

    static auto run_all() -> size_t
    {
        std::cout << std::format("\033[34m[TEST]\033[0m Running {} tests...\n", all().size());
        auto num_failed = size_t{};
        for (auto& [name, func] : all()) {
            try {
                func();
                std::cout << "\033[32m[PASS]\033[0m " << name << "\n";
            }
            catch (const std::exception& e) {
                std::cout << "\033[31m[FAIL]\033[0m " << name << " " << e.what() << "\n";
                ++num_failed;
            }
        }
        std::cout << "\033[34m[TEST]\033[0m " << (num_failed == 0 ? "All tests passed!\n" : std::format("{} tests failed.\n", num_failed));
        return num_failed;
    }


private:
    // All tests.
    static auto all() -> std::vector<Test>&
    {
        static auto tests = std::vector<Test>{};
        return tests;
    }
};

// MARK: - expect

inline auto expect_true(bool cond, const std::string& msg = {}) -> void
{
    if (!cond) {
        const auto out = msg.empty() ? "Condition is false." : msg;
        throw std::runtime_error(out);
    }
}

namespace impl {
template<typename... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
}

template<typename X>
inline auto expect_close(X calc, X ref, const Error::Any& tol, const std::string& msg = {}) -> void
{
    const auto tol_val = std::visit([](const auto& e) { return static_cast<X>(e.value); }, tol);
    
    const auto err_val = std::visit(impl::overloaded{
        [&](const Error::Absolute&) { return math::abs_err(calc, ref); },
        [&](const Error::Relative&) { return math::rel_err(calc, ref); },
        [&](const Error::Ulp_distance&) { return static_cast<X>(math::ulp_dist(calc, ref)); },
        [&](const Error::Bits_precision&) { return static_cast<X>(math::bits_prec(calc, ref)); }
    }, tol);

    const auto success = std::visit(impl::overloaded{
        [&](const Error::Bits_precision&) { return err_val >= tol_val; },
        [&](const auto&) { return err_val <= tol_val; }
    }, tol);

    if (!success) {
        const auto out = std::format(
            "{}Got: {}, Ref: {}, Err: {}, Tol: {}",
            msg.empty() ? "" : std::format("{} ", msg),
            calc, ref, err_val, tol_val
        );
        throw std::runtime_error(out);
    }
}

// ...

template<typename X>
inline auto expect_close(X calc, X ref, const Error::Absolute& tol, const std::string& msg = {}) -> void
{
    expect_close(calc, ref, Error::Any{tol}, msg);
}

template<typename X>
inline auto expect_close(X calc, X ref, const Error::Relative& tol, const std::string& msg = {}) -> void
{
    expect_close(calc, ref, Error::Any{tol}, msg);
}

template<typename X>
inline auto expect_close(X calc, X ref, const Error::Bits_precision& tol, const std::string& msg = {}) -> void
{
    expect_close(calc, ref, Error::Any{tol}, msg);
}

template<typename X>
inline auto expect_close(X calc, X ref, const Error::Ulp_distance& tol, const std::string& msg = {}) -> void
{
    expect_close(calc, ref, Error::Any{tol}, msg);
}

template<typename X>
inline auto expect_sinusoidal(const std::vector<X>& x, X freq, X amp, X sr, const Error::Relative& tol = {}, const std::string& msg = {}) -> void
{
    if (x.empty()) { throw std::runtime_error("Signal is empty."); };

    const auto power_at_freq = freq_power_for(x, freq, sr);
    if (power_at_freq <= 0) { throw std::runtime_error("Signal is not periodic."); };

    const auto rms = rms_for(x);
    if (rms <= 0) { throw std::runtime_error("Signal RMS is zero."); };

    const auto calc_amp = std::sqrt(2 * power_at_freq);
    const auto err = rel_err(calc_amp, amp);

    if (err > tol.value) {
        const auto out = std::format(
            "{}Found amplitude: {:<.2f} for frequency: {:<.2f}, Expected: {:<.2f}, Err: {:<.2f}%, Tol: {:<.2f}%",
            msg.empty() ? "" : std::format("{} ", msg), calc_amp, freq, amp, 100 * err, 100 * tol.value
        );
        throw std::runtime_error(out);
    }
}

template<typename X>
inline auto expect_silent(const std::vector<X>& x, const Error::Absolute& tol = {}, const std::string& msg = {}) -> void
{
    if (x.empty()) { throw std::runtime_error("Signal is empty."); };

    const auto rms = rms_for(x);
    if (rms <= 0) return;

    if (rms > tol.value) {
        const auto out = std::format(
            "{}Found RMS: {:<.6f}, Tol: {:<.6f}",
            msg.empty() ? "" : std::format("{} ", msg), rms, tol.value
        );
        throw std::runtime_error(out);
    }
}

} // namespace audio_bench

// MARK: - benchmarks

namespace audio_bench {

// See: https://github.com/google/benchmark/blob/main/src/benchmark.cc
namespace {
void const volatile* volatile global_force_escape_pointer;
}

// See: https://theunixzoo.co.uk/blog/2021-10-14-preventing-optimisations.html
template<typename T>
inline auto do_not_optimize(T& value) -> void
{
#if defined(_MSC_VER)
    global_force_escape_pointer = &reinterpret_cast<char const volatile&>(value);
    _ReadWriteBarrier();
#else
    asm volatile("" : "+r,m"(value) : : "memory");
#endif
}

struct Benchmark {
    // Display units.
    enum class Units { Seconds, Milliseconds, Microseconds, Nanoseconds };

    // Run specification.
    struct Spec {
        size_t reps{100};
        size_t warmup{100};
    };

    // Run statistics.
    struct Stats {
        std::string name{};
        size_t reps{};
        size_t warmup{};
        double mean{};
        double stddev{};
        double min{};
        double median{};
        double max{};
    };

    static auto print(const Stats& stats, Units units) -> void
    {
        std::cout << std::format(
            "Stats for benchmark: '{}' (build type: {})\n"
            "---- Reps: {}, Warmup: {}\n"
            "---- Mean: {:<.3f} {}, Std. Dev.: {:<.3f} {}, Min: {:<.3f} {}, Median: {:<.3f} {}, Max: {:<.3f} {}\n\n",
            stats.name, build_type_name(),
            stats.reps, stats.warmup,
            nanos_to(stats.mean, units), units_name(units),
            nanos_to(stats.stddev, units), units_name(units),
            nanos_to(stats.min, units), units_name(units),
            nanos_to(stats.median, units), units_name(units),
            nanos_to(stats.max, units), units_name(units)
        );
    }

    static auto compare(const Stats& b1, const Stats& b2) -> void
    {
        // Check for sufficient repetitions
        if (b1.reps < 2 || b2.reps < 2) {
            std::cerr << "Error: Insufficient repetitions (b1: " << b1.reps
                << ", b2: " << b2.reps << "). Need at least 2 for stddev.\n";
            return;
        }

        // Welch's t-test
        const auto mean_diff = b1.mean - b2.mean;
        const auto var1 = b1.stddev * b1.stddev;
        const auto var2 = b2.stddev * b2.stddev;
        const auto se = std::sqrt(var1 / b1.reps + var2 / b2.reps); // Standard error

        if (se == 0.0) {
            std::cerr << "Error: Standard error is zero (possibly zero variance).\n";
            return;
        }

        const auto t_stat = mean_diff / se;

        // Approximate degrees of freedom (Satterthwaite)
        const auto df = std::pow(var1 / b1.reps + var2 / b2.reps, 2) /
            (std::pow(var1 / b1.reps, 2) / (b1.reps - 1) +
                std::pow(var2 / b2.reps, 2) / (b2.reps - 1));

        // Critical value for 95% confidence (~1.96 for large df)
        const auto critical_t = (df > 100) ? 1.96 : 2.0;
        const auto significant = std::abs(t_stat) > critical_t;

        // Print simplified output
        if (significant) {
            const auto faster = mean_diff < 0 ? b1.name : b2.name;
            const auto slower = mean_diff < 0 ? b2.name : b1.name;
            const auto ratio = mean_diff < 0 ? b2.mean / b1.mean : b1.mean / b2.mean;
            std::cout << std::format(
                "Benchmark '{}' is {:.2f}x faster than '{}' (t = {:.2f}, df = {:.0f}, p < 0.05)\n\n",
                faster, ratio, slower, std::abs(t_stat), df
            );
        }
        else {
            std::cout << std::format(
                "No significant difference between benchmarks (t = {:.2f}, df = {:.0f}, p >= 0.05)\n\n",
                std::abs(t_stat), df
            );
        }
    }

private:

    static auto nanos_to(double x, Units dur) -> double
    {
        switch (dur) {
            case Units::Seconds: return x * 1e-9;
            case Units::Milliseconds: return x * 1e-6;
            case Units::Microseconds: return x * 1e-3;
            case Units::Nanoseconds: return x;
        }
        return x;
    }

    static auto units_name(Units dur) -> std::string
    {
        switch (dur) {
            case Units::Seconds: return "s";
            case Units::Milliseconds: return "ms";
            case Units::Microseconds: return "us";
            case Units::Nanoseconds: return "ns";
        }
        return "";
    }

    static auto build_type_name() -> std::string
    {
#ifdef NDEBUG
        return "Release";
#else
        return "Debug";
#endif
    }
};

template<typename L>
class Timer {
public:

    Timer(const std::string& name, L&& l) : _name{name}, _lambda{l} {}

    auto run(const Benchmark::Spec& spec) -> Benchmark::Stats
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
            .mean = mean,
            .stddev = stddev,
            .min = min_val,
            .median = median,
            .max = max_val,
        };
    }

private:

    std::string _name{};
    L _lambda{};
    std::vector<double> _trials{};

};

} // namespace audio_bench