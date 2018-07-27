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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
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
#include <boost/program_options.hpp>
#include <fmt/format.h>

// Use this to mark places a compiler might wrongly think are possible to reach.
#if defined(_MSC_VER)
#define NOT_REACHED() __assume(false)
#elif defined(__GNUC__)
#define NOT_REACHED() __builtin_unreachable()
#else
#define NOT_REACHED() assert(false)
#endif

namespace {
    using namespace std::string_view_literals;
    namespace po = boost::program_options;
    using std::mt19937;
    using std::numeric_limits;
    using std::size_t;
    using std::uintmax_t;

    std::string program_name;

    [[noreturn]]
    void die(const std::string_view message)
    {
        fmt::print(stderr, "{}: error : {}\n", program_name, message);
        std::exit(EXIT_FAILURE);
    }

    enum class ParallelMode { seq, par, par_unseq };

    std::array parallel_mode_summaries { // TODO: make const or constxpr
        "std::seq (do not parallelize)"sv,
        "std::par (parallelize)"sv,
        "std::par_unseq (parallelize/vectorize/migrate)"sv
    };
}

// http://fmtlib.net/dev/api.html#formatting-user-defined-types for ParallelMode
namespace fmt {
    template<>
    struct formatter<ParallelMode> {
        template<typename ParseContext>
        constexpr auto parse(ParseContext& ctx) { return std::begin(ctx); }

        template<typename FormatContext>
        auto format(const ParallelMode mode, FormatContext& ctx)
        {
            const auto s = parallel_mode_summaries[static_cast<size_t>(mode)]);
            return format_to(std::begin(ctx), "{}", s);
        }
    };
}

namespace {
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

    struct Parameters {
        size_t length;
        unsigned seed;
        std::string_view seed_origin;
        ParallelMode mode;
        int inplace_reps;
        bool show_start_time;
    };

    // FIXME: remove or replace when the operator<< it helps is replaced
    std::ostream& timestamp(std::ostream& out)
    {
        using std::chrono::system_clock;

        const auto ticks = system_clock::to_time_t(system_clock::now());
        const auto local = std::localtime(&ticks);
        return out << std::put_time(local, "Current time is %T%z.\n");
    }

    // FIXME: remove or replace when the operator<< it helps is replaced
    void report_length(std::ostream& out, const std::size_t length)
    {
        static constexpr size_t kilo {1024u}, mega {kilo * kilo};

        const auto bytes = length * sizeof(unsigned);

        out << format{"   length:  %u word%s (%s%u MiB)\n"} 
                % length % (length == 1u ? "" : "s")
                % (bytes % mega == 0u ? "" : "~") % (bytes / mega);
    }

    // FIXME: remove after replacing with an fmt::formatter specialization
    std::ostream& operator<<(std::ostream& out, const Parameters& params)
    {
        // Print the human-readable current time (and blank line), if requested.
        if (params.show_start_time) out << timestamp << '\n';

        // Show the specified length and about how much space it will use.
        report_length(out, params.length);

        // Show the seed the PRNG will use, and say where it came from.
        out << format{"     seed:  %u  (%s)\n"}
                % params.seed % params.seed_origin;

        // Name and "explain" the execution policy and if the sort is repeated.
        out << format{"sort mode:  %s"} % params.mode;
        if (params.inplace_reps > 1)
            out << format {"  [repeating %dx]"} % params.inplace_reps;
        return out << '\n';
    }

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
                ("par-unseq,U", "try to parallelize, may migrate thread and vectorize");

        po::positional_options_description pos_desc;
        pos_desc.add("length", 1);

        return {desc, pos_desc};
    }

    [[nodiscard]]
    po::variables_map
    parse_cmdline_args(const int argc, char* const* const argv)
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
            desc.print(std::cout); // FIXME: print this some other way
            std::exit(EXIT_SUCCESS);
        }

        return vm;
    }

    [[nodiscard]]
    size_t extract_length(const po::variables_map& vm)
    {
        if (!vm.count("length")) die("no length specified");

        const auto length = vm.at("length").as<size_t>();
        if (length >= numeric_limits<size_t>::max() / sizeof(unsigned))
            die("length is representable but too big to meaningfully try");

        return length;
    }

    [[nodiscard]]
    std::tuple<unsigned, std::string_view>
    obtain_seed_info(const po::variables_map& vm)
    {
        if (vm.count("seed"))
            return {vm.at("seed").as<unsigned>(), "provided by the user"};

        std::random_device rd;
        return {rd(), "generated by the system"};
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
    Parameters configure(const int argc, char* const* const argv)
    {
        // Avoid needless buffer-flushing. (Remove if using any C-style IO.)
        std::ios_base::sync_with_stdio(false);

        // Set the program name for error messages to the Unix-style basename.
        assert(argc > 0);
        program_name = std::filesystem::path{argv[0]}.filename().string();

        // Fetch operating parameters from command-line arguments and defaults.
        return extract_operating_parameters(parse_cmdline_args(argc, argv));
    }

    // TODO: Extract the number-generating stanza (and accompanying static
    //       assertion) into its own function, and also implement a trivial
    //       alternative with std::iota to get more insight into adaptivity.
    //
    // TODO: Time each step in the test, as well as the whole thing. Put shared
    //       timing and reporting logic in a function template called with
    //       message arguments (std::string or boost::format) and action
    //       arguments of template parameter type, passing lambdas. Or write an
    //       RAII timing/reporting class (designed similar to std::lock_guard).
    void test(const Parameters& params, mt19937& gen)
    {
        static_assert(mt19937::min() == numeric_limits<unsigned>::min()
                        && mt19937::max() == numeric_limits<unsigned>::max(),
                "the PRNG does not have the same range as the output type");

        cout << "Generating... " << std::flush;
        std::vector<unsigned> a (params.length);
        const auto first = std::begin(a), last = std::end(a);
        std::generate(first, last, gen);
        cout << "Done.\n";

        cout << "Hashing... " << std::flush;
        const auto s1 = std::accumulate(first, last, 0u);
        cout << format{"%x.\n"} % s1;

        for (auto i = params.inplace_reps; i > 0; --i) {
            cout << "Sorting... " << std::flush;
            sort(params.mode, first, last);
            cout << "Done.\n";
        }

        cout << "Rehashing... " << std::flush;
        const auto s2 = std::accumulate(first, last, 0u);
        cout << format{"%x. (%s)\n"} % s2 % (s1 == s2 ? "same" : "DIFFERENT!");

        cout << "Checking... " << std::flush;
        cout << format{"%s\n"} % (std::is_sorted(first, last) ? "sorted."
                                                              : "NOT SORTED!");
    }
}

int main(int argc, char** argv)
{
    const auto params = configure(argc, argv);
    std::cout << params << '\n'; // this extra newline is intended
    mt19937 gen {params.seed};

    using namespace std::chrono;
    const auto ti = steady_clock::now();

    try {
        test(params, gen);
    }
    catch (const std::bad_alloc&) {
        cout << '\n'; // end the "Generating..." line
        die("not enough memory");
    }

    const auto tf = steady_clock::now();
    const auto dt = tf - ti;

    cout << format{"\nTest completed in about %d s (%d ms).\n"}
                % duration_cast<seconds>(dt).count()
                % duration_cast<milliseconds>(dt).count();
}
