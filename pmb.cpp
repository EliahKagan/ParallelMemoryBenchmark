#include <algorithm>
#include <cassert>
#include <chrono> // for time measurement, *not* seeding the PRNG,  we get it
#include <cstdint>
#include <cstdlib>
#include <execution>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <boost/format.hpp>

namespace {
    using std::cout;
    using std::mt19937;
    using std::numeric_limits;
    using std::uintmax_t;
    using boost::format;

    std::string program_name;

    [[noreturn]] void die(const char* const message)
    {
        std::cerr << format{"%s: error: %s\n"} % program_name % message;
        std::exit(EXIT_FAILURE);
    }

    size_t to_size(const std::string str) // helper for get_config()
    {
        static_assert(static_cast<uintmax_t>(numeric_limits<long long>::max())
                       <= static_cast<uintmax_t>(numeric_limits<size_t>::max()),
                "word size too small (32-bit build?)");

        static constexpr auto kilo = 1024LL, mega = kilo * kilo;
        static constexpr auto word = static_cast<long long>(sizeof(unsigned));

        try {
            const auto size = std::stoll(str);
            if (size < 0) die("size argument is negative");
            if (size >= numeric_limits<long long>::max() / word)
                die("size argument is too big");

            const auto bytes = size * word;
            cout << format{"%d word%s (%s%d MiB)\n"}
                        % size % (size == 1LL ? "" : "s")
                        % (bytes % mega == 0LL ? "" : "~") % (bytes / mega);
            
            return static_cast<size_t>(size);
        }
        catch (const std::invalid_argument&) {
            die("size argument is non-numeric");
        }
        catch (const std::out_of_range&) {
            die("size argument is way too big");
        }
    }

    // Reads command-line arguments. Returns the user-specified vector size.
    size_t get_config(const int argc, char* const* const argv)
    {
        assert(argc > 0);
        program_name = std::filesystem::path{argv[0]}.filename().string();


		if (argc < 2) die("too few arguments");									// argv[0] is the name, argv[1] is the size 
		if (argc > 2) die("too many arguments");

      //  if (argc < 3) die("too few arguments");									// argv[0] is the name, argv[1] is the size, argv[2] is the parrallel flag,
      //  if (argc > 3) die("too many arguments");

        return to_size(argv[1]); // e.g., pass 2684354560 for 10 GiB
    }

    // Obtains a random number generator.
    // TODO: Redesign get_config() so it returns a seed, so that a command-line
    //       option can be added for a user-given seed to reproduce a run.
    mt19937 get_generator()
    {
        std::random_device rd;
        const auto seed = rd();
        cout << "seed: " << seed << '\n';
        return mt19937{seed};
    }

    void test(const size_t size, mt19937& gen)
    {
        static_assert(mt19937::min() == numeric_limits<unsigned>::min()
                        && mt19937::max() == numeric_limits<unsigned>::max(),
                "the PRNG does not have the same range as the output type");

        cout << "\nGenerating... " << std::flush;
        std::vector<unsigned> a (size);
        const auto first = std::begin(a), last = std::end(a);
        std::generate(first, last, gen);
        cout << "Done.\n";

        cout << "Hashing... " << std::flush;
        const auto s1 = std::accumulate(first, last, 0u);
        cout << format{"%x.\n"} % s1;

        cout << "Sorting... " << std::flush;
        std::sort(std::execution::par_unseq, first, last);
        cout << "Done.\n";

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
    std::ios_base::sync_with_stdio(false); //Performance optimization
    const auto size = get_config(argc, argv); 
    auto gen = get_generator();

    using namespace std::chrono;
    const auto ti = steady_clock::now();

    try {
        test(size, gen);
    }
    catch (const std::bad_alloc&) {
        cout << '\n'; // end the "Generating" line
        die("out of memory");
    }

    const auto tf = steady_clock::now();
    const auto dt = tf - ti;

    cout << format{"\nTest completed in about %d s (%d ms).\n"}
                % duration_cast<seconds>(dt).count()
                % duration_cast<milliseconds>(dt).count();


}
