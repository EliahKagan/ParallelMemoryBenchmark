#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
    const char* program_name;

    [[noreturn]] void die(const char* const message)
    {
        std::cerr << program_name << ": error: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    std::size_t to_size(const std::string str) // helper for get_config()
    {
        static_assert(static_cast<unsigned long long>(
                            std::numeric_limits<long long>::max())
                        <= std::numeric_limits<std::size_t>::max(),
                "word size too small (32-bit build?)");

        try {
            const auto size = std::stoll(str);
            if (size < 0) die("size argument is negative");
            std::cout << "size: " << size << '\n';
            return static_cast<std::size_t>(size);
        }
        catch (const std::invalid_argument&) {
            die("size argument is non-numeric");
        }
        catch (const std::out_of_range&) {
            die("size argument is too large");
        }
    }

    // Reads command-line arguments. Returns the user-specified vector size.
    std::size_t get_config(const int argc, char* const* const argv)
    {
        assert(argc > 0);
        program_name = argv[0];

        if (argc < 2) die("too few arguments");
        if (argc > 2) die("too many arguments");

        return to_size(argv[1]); // e.g., pass 2684354560 for 10 GiB
    }

    std::mt19937 get_generator()
    {
        std::random_device rd;
        const auto seed = rd();
        std::cout << "seed: " << seed << '\n';
        return std::mt19937{seed};
    }

    unsigned sum(const std::vector<unsigned>& a) noexcept // helper for test()
    {
        return std::accumulate(std::cbegin(a), std::cend(a), 0u);
    }

    void test(const std::size_t size, std::mt19937& gen)
    {
        static_assert(std::mt19937::min() == std::numeric_limits<unsigned>::min()
                        && std::mt19937::max() == std::numeric_limits<unsigned>::max(),
                "the PRNG does not have the same range as the output type");

        std::cout << "Generating... " << std::flush;
        std::vector<unsigned> a (size);
        for (auto& e : a) e = gen();
        std::cout << "Done.\n";

        std::cout << "Hashing... " << std::flush;
        const auto sum1 = sum(a);
        std::cout << sum1 << ".\n";

        std::cout << "Sorting... " << std::flush;
        std::sort(std::begin(a), std::end(a));
        std::cout << "Done.\n";

        std::cout << "Rehashing... " << std::flush;
        const auto sum2 = sum(a);
        const auto comment = (sum1 == sum2 ? "same" : "DIFFERENT!");
        std::cout << sum2 << ". (" << comment << ")\n";
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
    std::cout << "\nTest completed in " << dt.count() << " s.\n";
}
