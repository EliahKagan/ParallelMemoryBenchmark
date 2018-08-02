// This is a simple memory benchmarking tool that makes an array of pseudorandom
// numbers and sorts them using an execution policy. The length and policy are
// specified by the user. The flags --seq, --par, and --par-unseq specify the
// policy. By default, it is as if --par were passed. These options correspond
// to https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t. Use
// --help for a description of all options. 64-bit builds are recommended. This
// is an outgrowth of a program meant to reproduce a vexing system stability
// problem; it is not really well-suited to use as a general-purpose benchmark.

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <execution>
#include <filesystem>
#include <iterator>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant> // for std::monostate
#include <boost/program_options.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h> // to print boost::format_options::options_description
#include <fmt/time.h>
#include <gsl/gsl>

// Use this to mark places a compiler might wrongly think are possible to reach.
#if defined(_MSC_VER)
#define NOT_REACHED() __assume(false)
#elif defined(__GNUC__)
#define NOT_REACHED() __builtin_unreachable()
#else
#define NOT_REACHED() assert(false)
#endif

namespace {
    using namespace std::literals;
    namespace po = boost::program_options;
    using std::forward;
    using std::size_t;

    template<typename T, typename U>
    constexpr auto same_range_v = T::min() == U::min() && T::max() == U::max();

    std::string program_name;

    [[noreturn]]
    void die(const std::string_view message)
    {
        fmt::print(stderr, "{}: error : {}\n", program_name, message);
        std::exit(EXIT_FAILURE);
    }

    enum class ParallelMode { seq, par, par_unseq };

    constexpr std::array parallel_mode_summaries {
        "std::execution::seq (do not parallelize)"sv,
        "std::execution::par (parallelize)"sv,
        "std::execution::par_unseq (parallelize/vectorize/migrate)"sv
    };
}

// ParallelMode http://fmtlib.net/dev/api.html#formatting-user-defined-types
namespace fmt {
    template<>
    struct formatter<ParallelMode> {
        template<typename ParseContext>
        constexpr auto parse(ParseContext& ctx) { return std::begin(ctx); }

        template<typename FormatContext>
        auto format(const ParallelMode mode, FormatContext& ctx)
        {
            return format_to(std::begin(ctx), "{}",
                    parallel_mode_summaries.at(static_cast<size_t>(mode)));
        }
    };
}

namespace {
    // TODO: Consider making a template that itself binds a function template as
    //       a template parameter, for run-time selction of execution policy for
    //       any algorithm that supports it. The algorithm would still be
    //       selected at compile-time, but the execution policy at run-time.
    template<typename RandomIt>
    void
    sort(const ParallelMode mode, const RandomIt first, const RandomIt last)
    {
        switch (mode) {
        case ParallelMode::seq:
            std::sort(std::execution::seq, first, last);
            return;

        case ParallelMode::par:
            std::sort(std::execution::par, first, last);
            return;

        case ParallelMode::par_unseq:
            std::sort(std::execution::par_unseq, first, last);
            return;
        }

        NOT_REACHED();
    }

    // Formattable names of specific configuration parameters (see Parameters).
    struct ParameterLabel {
        static constexpr auto width = 9;

        std::string_view name;
    };

    [[nodiscard]]
    constexpr ParameterLabel
    operator""_pl(const char* const s, const size_t count)
    {
        return {{s, count}};
    }
}

// ParameterLabel http://fmtlib.net/dev/api.html#formatting-user-defined-types
namespace fmt {
    template<>
    struct formatter<ParameterLabel> {
        template<typename ParseContext>
        constexpr auto parse(ParseContext& ctx) { return std::begin(ctx); }

        template<typename FormatContext>
        auto format(const ParameterLabel label, FormatContext& ctx)
        {
            return format_to(std::begin(ctx), "{:>{}}:  ", // right-justified
                             label.name, ParameterLabel::width);
        }
    };
}

namespace {
    // Configuration parameters that control a run.
    struct Parameters {
        static constexpr auto label_width = 9;

        size_t length;
        unsigned seed;
        std::string_view seed_origin;
        ParallelMode mode;
        int inplace_reps;
        bool show_start_time;
    };

    // Helper for fmt::formatter<Parameters>::format. Prints timestamp.
    template<typename OutputIt>
    [[nodiscard]]
    OutputIt format_localnow_to(const OutputIt out)
    {
        // Obtain the current time.
        using clock = std::chrono::system_clock;
        const auto time = fmt::localtime(clock::to_time_t(clock::now()));

        // Convert it to a string, then use that string (see below for why).
        const auto repr = fmt::format("{:%T%z}", time);
        return fmt::format_to(out, "Current time is {}.\n", repr);

        // The code shown below crashes or behaves erratically. It appears to
        // (try to) write past the end of a buffer. Perhaps someone will point
        // out my mistake or a bug in fmtlib. For now, I am working around it
        // with the ugly hack that appears above.
        //
        // return fmt::format_to(out, "Current time is {:%T%z}.\n", time);
    }

    // Helper for fmt::formatter<Parameters>::format. Prints array length.
    template<typename OutputIt>
    [[nodiscard]]
    OutputIt format_length_to(const OutputIt out, const size_t length)
    {
        static constexpr size_t kilo {1024u}, mega {kilo * kilo};

        const auto bytes = length * sizeof(unsigned);

        return fmt::format_to(out, "{}{} element{} ({}{} MiB)\n",
                              "length"_pl, length, (length == 1u ? "" : "s"),
                              (bytes % mega == 0u ? "" : "~"), bytes / mega);
    }
}

// Parameters http://fmtlib.net/dev/api.html#formatting-user-defined-types
namespace fmt {
    template<>
    struct formatter<Parameters> {
        template<typename ParseContext>
        constexpr auto parse(ParseContext& ctx) { return std::cbegin(ctx); }

        template<typename FormatContext>
        auto format(const Parameters& params, FormatContext& ctx)
        {
            auto out = std::begin(ctx);

            // Print human-readable current time (and blank line), if requested.
            if (params.show_start_time) out = format_localnow_to(out);

            // Show the specified length and about how much space it will use.
            out = format_length_to(out, params.length);

            // Show the seed the PRNG will use, and say where it came from.
            out = format_to(out, "{}{}  ({})\n", "seed"_pl,
                            params.seed, params.seed_origin);

            // Name and "explain" the execution policy and if we rerun the sort.
            out = format_to(out, "{}{}", "sort mode"_pl, params.mode);
            if (params.inplace_reps > 1) 
                out = format_to(out, "  [repeating {}x]", params.inplace_reps);
            return format_to(out, "\n");
        }
    };
}

namespace {
    [[nodiscard]]
    std::tuple<po::options_description, po::positional_options_description>
    describe_options()
    {
        po::options_description desc {"Options to configure the benchmark"};
        desc.add_options()
                ("help,h", "show this message") // TODO: list --help separately
                ("length,l", po::value<size_t>(),
                             "specify how many elements to generate and sort")
                ("seed,s", po::value<unsigned>(),
                           "custom seed for PRNG (omit to use system entropy)")
                ("twice,2", "after sorting, sort again (may test adaptivity)")
                ("time,t", "display human-readable start time")
                ("seq,S", "don't try to parallelize")
                ("par,P", "try to parallelize (default)")
                ("par-unseq,U",
                        "try to parallelize, may migrate thread and vectorize");

        po::positional_options_description pos_desc;
        pos_desc.add("length", 1);

        return {desc, pos_desc};
    }

    [[nodiscard]]
    po::variables_map
    parse_cmdline_args(const int argc,
                       const gsl::not_null<const char* const*> argv)
    {
        const auto [desc, pos_desc] = describe_options();

        po::variables_map vm;
        try {
            po::store(po::command_line_parser{argc, argv}
                        .options(desc).positional(pos_desc).run(), vm);
        }
        catch (const po::error& e) {
            die(e.what());
        }
        po::notify(vm);

        if (vm.count("help")) {
            fmt::print("{}", desc);
            std::exit(EXIT_SUCCESS);
        }

        return vm;
    }

    [[nodiscard]]
    size_t extract_length(const po::variables_map& vm)
    {
        if (!vm.count("length")) die("no length specified");

        const auto length = vm.at("length").as<size_t>();
        if (length >= std::numeric_limits<size_t>::max() / sizeof(unsigned))
            die("length is representable but too big to meaningfully try");

        return length;
    }

    [[nodiscard]]
    std::tuple<unsigned, std::string_view>
    obtain_seed_info(const po::variables_map& vm)
    {
        if (vm.count("seed"))
            return {vm.at("seed").as<unsigned>(), "provided by the user"};

        return {std::random_device{}(), "generated by the system"};
    }

    [[nodiscard]]
    ParallelMode extract_dynamic_execution_policy(const po::variables_map& vm)
    {
        const auto got_seq = vm.count("seq");
        const auto got_par = vm.count("par");
        const auto got_par_unseq = vm.count("par-unseq");

        switch (got_seq + got_par + got_par_unseq) {
        case 0u:
            return ParallelMode::par;

        case 1u:
            if (got_seq) return ParallelMode::seq;
            if (got_par) return ParallelMode::par;
            if (got_par_unseq) return ParallelMode::par_unseq;
            NOT_REACHED();

        default:
            die("at most one of (--seq, --par, --par-unseq) is accepted");
        }
    }

    [[nodiscard]]
    Parameters extract_operating_parameters(const po::variables_map& vm)
    {
        Parameters params {};

        params.length = extract_length(vm);
        std::tie(params.seed, params.seed_origin) = obtain_seed_info(vm);
        params.mode = extract_dynamic_execution_policy(vm);
        params.inplace_reps = (vm.count("twice") ? 2 : 1);
        params.show_start_time = vm.count("time");

        return params;
    }
    
    [[nodiscard]]
    Parameters
    configure(const int argc, const gsl::not_null<const char* const*> argv)
    {
        // Set the program name for error messages to the Unix-style basename.
        assert(argc > 0);
        program_name = std::filesystem::path{*argv}.filename().string();

        // Fetch operating parameters from command-line arguments and defaults.
        return extract_operating_parameters(parse_cmdline_args(argc, argv));
    }

    // Reporters for the bench() function templates.
    namespace report {
        constexpr auto time_only = [](const auto dt) {
            fmt::print(" ({} ms)\n", dt / 1ms);
        };

        constexpr auto compact = [](const auto dt) {
            fmt::print("Done.");
            time_only(dt);
        };

        constexpr auto full = [](const auto dt) {
            fmt::print("\nTest completed in about {:.1f} seconds ({} ms).\n",
                       dt / 1.0s, dt / 1ms);
        };
    }

    // Calls a medadic functor and returns its result or, if void, a monostate.
    template<typename Action>
    decltype(auto) call(Action&& action)
    {
        using Ret = decltype(forward<Action>(action)());

        static_assert(!std::is_same_v<Ret, std::monostate>,
                      "monostate as a real result would be ambiguous");

        if constexpr (std::is_same_v<Ret, void>) {
            forward<Action>(action)();
            return std::monostate{};
        }
        else return forward<Action>(action)();
    }

    // Times an action and passes its duration to a reporter. (I don't *want*
    // results to be reported if the task throws an excepetion, which is why I
    // used this approach rather than an RAII class whose destructor reports.)
    template<typename Reporter, typename Action>
    decltype(auto) bench(Reporter&& reporter, Action&& action)
    {
        using clock = std::chrono::steady_clock;

        const auto ti = clock::now();
#pragma warning(suppress: 26496) // https://stackoverflow.com/a/48263092
        decltype(auto) ret = call(forward<Action>(action));
        const auto tf = clock::now();

        forward<Reporter>(reporter)(tf - ti);
        return ret;
    }

    // Prints an action's name, times it, and passes its duration to a reporter.
    template<typename Reporter, typename Action>
    decltype(auto) bench(const std::string_view label,
                         Reporter&& reporter, Action&& action)
    {
        fmt::print("{}... ", label);
        std::fflush(stdout);
        return bench(forward<Reporter>(reporter), forward<Action>(action));
    }

    // TODO: Extract the number-generating stanza (and accompanying static
    //       assertion) into its own function, and also implement a trivial
    //       alternative with std::iota to get more insight into adaptivity.
    void test(const Parameters& params, std::mt19937& gen)
    {
        static_assert(same_range_v<std::mt19937, std::numeric_limits<unsigned>>,
                      "the PRNG and the output type have different ranges");

        std::vector<unsigned> a;

        bench("Allocating/zeroing", report::compact, [&]() {
            a.resize(params.length);
        });

        bench("Generating", report::compact, [&]() {
            std::generate(begin(a), end(a), gen);
        });

        const auto s1 = bench("Hashing", report::time_only, [&]() {
            auto s = std::accumulate(cbegin(a), cend(a), 0u);
            fmt::print("{:x}.", s);
            return s;
        });

        for (auto i = params.inplace_reps; i > 0; --i) {
            bench("Sorting", report::compact, [&]() {
                sort(params.mode, begin(a), end(a));
            });
        }

        bench("Rehashing", report::time_only, [&]() {
            const auto s2 = std::accumulate(cbegin(a), cend(a), 0u);
            fmt::print("{:x}, {}", s2, (s1 == s2 ? "same." : "DIFFERENT!"));
        });

        bench("Checking", report::time_only, [&]() {
            const auto ok = std::is_sorted(cbegin(a), cend(a));
            fmt::print("{}", (ok ? "sorted." : "NOT SORTED!"));
        });
    }
}

int main(int argc, char** argv)
{
    const auto params = configure(argc, gsl::not_null{argv});
    fmt::print("{}\n", params); // the extra newline is intended
    std::mt19937 gen {params.seed};

    try {
        bench(report::full, [&]() {
            test(params, gen);
        });
    }
    catch (const std::bad_alloc&) {
        fmt::print("\n"); // end the "Allocating/zeroing..." line
        die("not enough memory");
    }
}
