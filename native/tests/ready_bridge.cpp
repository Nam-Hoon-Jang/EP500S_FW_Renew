// Linux PTY용 시험 실행파일. 제품의 실제 protocol/commands/state를 그대로 사용한다.
// 보드 시계/UART만 PC 시계와 파일 descriptor로 대체한다. ARM FW에는 연결하지 않는다.
#include "klippy_commands.h"
#include "mcu_clock.h"
#include <chrono>
#include <cstdlib>
#include <poll.h>
#include <unistd.h>
#include <cerrno>

namespace {
const auto started = std::chrono::steady_clock::now();
bool send_bytes(void* context, const uint8_t* data, uint32_t length)
{
    const int fd = *static_cast<int*>(context);
    while (length) {
        const auto count = write(fd, data, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        data += count;
        length -= count;
    }
    return true;
}
}

namespace McuClock {
uint64_t ticks64()
{
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
    return static_cast<uint64_t>(ns) * kFrequencyHz / 1000000000u;
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    int fd = std::atoi(argv[1]);
    KlippyCommands commands;
    Protocol protocol(send_bytes, &fd, KlippyCommands::receive, &commands);
    pollfd descriptor{fd, POLLIN, 0};
    for (;;) {
        const int ready = poll(&descriptor, 1, 10);
        const auto now = static_cast<uint32_t>(McuClock::ticks64() / (McuClock::kFrequencyHz / 1000));
        if (ready > 0 && (descriptor.revents & POLLIN)) {
            uint8_t bytes[256];
            const auto count = read(fd, bytes, sizeof(bytes));
            if (count <= 0) return 0;
            for (ssize_t i = 0; i < count; ++i) protocol.receive_byte(bytes[i], now);
        }
        protocol.poll(now);
        commands.poll(protocol);
    }
}
