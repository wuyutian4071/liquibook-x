#include <benchmark/benchmark.h>

#include <sstream>
#include <vector>

#include "decode.hpp"
#include "synth.hpp"

using namespace liquibook::itch;

namespace {

// Generated once per process (not timed) and reused across every benchmark iteration --
// benchmark iterations must not measure stream generation, only decode() throughput.
std::vector<std::byte> generate_fixture(std::size_t num_orders) {
    SynthConfig config;
    config.seed = 42;
    config.num_orders = num_orders;
    config.symbols = {"AAPL", "MSFT", "GOOGL", "AMZN", "TSLA"};

    std::ostringstream oss(std::ios::binary);
    generate(config, oss);
    const std::string data = oss.str();

    std::vector<std::byte> bytes(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        bytes[i] = static_cast<std::byte>(data[i]);
    }
    return bytes;
}

} // namespace

static void BM_DecodeThroughput(benchmark::State& state) {
    static const std::vector<std::byte> fixture = generate_fixture(200'000);
    std::size_t total_messages = 0;

    for (auto _ : state) {
        std::size_t offset = 0;
        while (offset + 2 <= fixture.size()) {
            const auto len =
                static_cast<std::uint16_t>((std::to_integer<unsigned>(fixture[offset]) << 8) |
                                           std::to_integer<unsigned>(fixture[offset + 1]));
            const std::size_t msg_start = offset + 2;
            if (msg_start + len > fixture.size()) {
                break;
            }

            const std::span<const std::byte> raw(fixture.data() + msg_start, len);
            auto msg = decode(raw);
            benchmark::DoNotOptimize(msg);

            ++total_messages;
            offset = msg_start + len;
        }
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(total_messages));
    state.SetLabel("messages/sec");
}
BENCHMARK(BM_DecodeThroughput)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
