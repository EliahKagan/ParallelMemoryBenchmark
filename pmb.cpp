#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>

namespace {
    using std::cout;
    using std::mt19937;
    using std::numeric_limits;

    const char* program_name;

    [[noreturn]] void die(const char* const message)
    {
        std::cerr << program_name << ": error: " << message << '\n';
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
            
            cout << size << " word" << (size == 1LL ? "" : "s") << " (";
            const auto bytes = size * word;
            if (bytes % mega != 0LL) cout << '~';
            cout << bytes / mega << " MiB)\n";
            
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
        program_name = argv[0];

        if (argc < 2) die("too few arguments");
        if (argc > 2) die("too many arguments");

        return to_size(argv[1]); // e.g., pass 2684354560 for 10 GiB
    }

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
        const auto sum1 = std::accumulate(first, last, 0u);
        cout << sum1 << ".\n";

        cout << "Sorting... " << std::flush;
        std::sort(first, last);
        cout << "Done.\n";

        cout << "Rehashing... " << std::flush;
        const auto sum2 = std::accumulate(first, last, 0u);
        const auto comment = (sum1 == sum2 ? "same" : "DIFFERENT!");
        cout << sum2 << ". (" << comment << ")\n";

        cout << "Checking... " << std::flush;
        cout << (std::is_sorted(first, last) ? "sorted." : "NOT SORTED!")
             << '\n';
    }
}

int main(int argc, char** argv)
{
    std::ios_base::sync_with_stdio(false);
    
    const auto size = get_config(argc, argv);
    auto gen = get_generator();

    const auto ti = std::chrono::steady_clock::now();

    try {
        test(size, gen);
    }
    catch (const std::bad_alloc&) {
        die("out of memory");
    }

    const auto tf = std::chrono::steady_clock::now();
    const auto dt = std::chrono::duration_cast<std::chrono::seconds>(tf - ti);
    cout << "\nTest completed in " << dt.count() << " s.\n";
}
