/*
Copyright (C) 2026 erixabo
SPDX-License-Identifier: GPL-3.0-only
 */

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <librpitx/librpitx.h>

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t release_rf_requested = 0;
static uint64_t sample_remainder = 0;
static ngfmdmasync *modulator = nullptr;
static const uint32_t sample_rate = 100000;
static const int dma_channel = 14;
static const uint32_t fifo_size = 10000;

static void stop_signal(int) { running = false; }
static void release_rf_signal(int) { release_rf_requested = 1; }

static bool wait_before_rf_if_requested() {
    const char *hold = std::getenv("PISSTV_HOLD_BEFORE_RF");
    if (!hold || !std::strcmp(hold, "0")) return running;

    struct sigaction sa{};
    sa.sa_handler = release_rf_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGUSR1, &sa, nullptr) != 0) {
        std::perror("sigaction SIGUSR1");
        return false; // Fail closed: do not transmit without the requested gate.
    }

    std::fprintf(stderr,
        "Waveform ready; RF is still off. PID=%ld. Send SIGUSR1 to start, SIGINT to cancel.\n",
        static_cast<long>(getpid()));
    while (running && !release_rf_requested) pause();
    return running && release_rf_requested;
}

// Timing units are 0.1 us; at 100 ksample/s each sample is 100 such units.
static void append_tone(std::vector<float>& out, double hz, uint32_t time_01us) {
    uint64_t total = sample_remainder + time_01us;
    size_t count = static_cast<size_t>(total / 100);
    sample_remainder = total % 100;
    out.insert(out.end(), count, static_cast<float>(hz));
}

static void send(std::vector<float>& samples) {
    if (!samples.empty()) modulator->SetFrequencySamples(samples.data(), samples.size());
    samples.clear();
}

static bool send_fax480(std::vector<float>& samples) {
    // Keep stop-signal latency bounded. SetFrequencySamples() is blocking and
    // does not observe this program's running flag; one whole FAX frame takes
    // about 139 seconds, so submit it in short, independently interruptible
    // batches while retaining the prebuilt continuous sample stream.
    constexpr size_t batch_size = 5000; // 50 ms at sample_rate = 100 ksample/s
    for (size_t offset = 0; offset < samples.size() && running; offset += batch_size) {
        const size_t count = samples.size() - offset < batch_size
            ? samples.size() - offset : batch_size;
        modulator->SetFrequencySamples(samples.data() + offset, count);
    }
    return running;
}

static void append_vis_header(std::vector<float>& out, int vis) {
    const int data = vis & 0x7f;
    // QSSTV calibration preamble, followed by its standard VIS leader.
    const int calibration[] = {1900, 1500, 1900, 1500, 2300, 1500, 2300, 1500};
    for (int hz : calibration) append_tone(out, hz, 1000000);
    append_tone(out, 1900, 3000000);    // standard 300 ms leader
    append_tone(out, 1200, 100000);     // 10 ms break
    append_tone(out, 1900, 3000000);    // 300 ms leader
    append_tone(out, 1200, 300000);     // 30 ms start
    // VIS values in the table include the parity bit in bit 7. On air they
    // are always sent as seven data bits plus even-parity bit.
    for (int bit = 0; bit < 7; ++bit)
        append_tone(out, (data >> bit) & 1 ? 1100 : 1300, 300000);
    int parity = __builtin_popcount(static_cast<unsigned>(data)) & 1;
    append_tone(out, parity ? 1100 : 1300, 300000);
    append_tone(out, 1200, 300000);     // 30 ms stop
}

static void append_extended_vis_header(std::vector<float>& out, uint32_t vis) {
    const int calibration[] = {1900, 1500, 1900, 1500, 2300, 1500, 2300, 1500};
    for (int hz : calibration) append_tone(out, hz, 1000000);
    append_tone(out, 1900, 300000);
    append_tone(out, 1200, 10000);
    append_tone(out, 1900, 300000);
    append_tone(out, 1200, 300000);
    for (int bit = 0; bit < 16; ++bit)
        append_tone(out, (vis >> bit) & 1 ? 1100 : 1300, 300000);
    append_tone(out, 1200, 300000);
}

static void append_component(std::vector<float>& samples,
                             const std::vector<unsigned char>& rgb,
                             int component, uint32_t pixel_time);
static bool read_rgb_row(int fd, std::vector<unsigned char>& row);

static bool send_martin_m1(int image_fd, size_t rows) {
    std::vector<unsigned char> rgb(320 * 3);
    std::vector<float> samples;
    samples.reserve(46000);

    append_vis_header(samples, 44); // Martin M1
    send(samples);

    for (size_t row = 0; row < rows && running; ++row) {
        size_t got = 0;
        while (got < rgb.size()) {
            ssize_t n = read(image_fd, rgb.data() + got, rgb.size() - got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            got += static_cast<size_t>(n);
        }

        samples.reserve(46000);
        append_tone(samples, 1200, 48620); // line sync 4.862 ms
        append_tone(samples, 1500, 5720);  // porch 0.572 ms
        for (int x = 0; x < 320; ++x) append_tone(samples, 1500 + rgb[x*3+1] * 800.0 / 256.0, 4576); // G
        append_tone(samples, 1500, 5720);
        for (int x = 0; x < 320; ++x) append_tone(samples, 1500 + rgb[x*3+2] * 800.0 / 256.0, 4576); // B
        append_tone(samples, 1500, 5720);
        for (int x = 0; x < 320; ++x) append_tone(samples, 1500 + rgb[x*3] * 800.0 / 256.0, 4576);   // R
        append_tone(samples, 1500, 5720);
        send(samples); // stream one complete scan line without per-pixel scheduler gaps
    }
    return running;
}

static bool send_martin_m2(int image_fd, size_t rows) {
    std::vector<unsigned char> rgb(320 * 3);
    std::vector<float> samples;
    samples.reserve(24000);
    append_vis_header(samples, 40); // Martin M2
    send(samples);
    for (size_t row = 0; row < rows && running; ++row) {
        if (!read_rgb_row(image_fd, rgb)) return false;
        samples.reserve(24000);
        append_tone(samples, 1200, 50000); // 5 ms sync
        append_component(samples, rgb, 1, 2292); // G
        append_tone(samples, 1500, 5000); // 0.5 ms colour blank
        append_component(samples, rgb, 2, 2292); // B
        append_tone(samples, 1500, 5000);
        append_component(samples, rgb, 0, 2292); // R
        append_tone(samples, 1500, 8000); // 0.8 ms front porch
        send(samples);
    }
    return running;
}

static void append_component(std::vector<float>& samples,
                             const std::vector<unsigned char>& rgb,
                             int component, uint32_t pixel_time) {
    for (int x = 0; x < 320; ++x)
        append_tone(samples, 1500 + rgb[x*3+component] * 800.0 / 256.0, pixel_time);
}

static void robot24_pixel(const unsigned char *rgb, unsigned char& y,
                          unsigned char& ry, unsigned char& by) {
    const int r = rgb[0], g = rgb[1], b = rgb[2];
    const int luma = (59 * g + 30 * r + 11 * b) / 100;
    y = static_cast<unsigned char>(luma);
    const int red_difference = (10 * r - 10 * luma + 7 * 255) / 14;
    const int blue_difference = (100 * b - 100 * luma + 89 * 255) / 178;
    ry = static_cast<unsigned char>(red_difference < 0 ? 0 : red_difference > 255 ? 255 : red_difference);
    by = static_cast<unsigned char>(blue_difference < 0 ? 0 : blue_difference > 255 ? 255 : blue_difference);
}

static void robot36_pixel(const unsigned char *even_rgb, const unsigned char *odd_rgb,
                          bool even_line, unsigned char& y,
                          unsigned char& ry, unsigned char& by) {
    auto luminance = [](const unsigned char *p) {
        return (59 * static_cast<int>(p[1]) + 30 * static_cast<int>(p[0]) +
                11 * static_cast<int>(p[2])) / 100;
    };
    const int ye = luminance(even_rgb);
    const int yo = luminance(odd_rgb);
    const int red = (static_cast<int>(even_rgb[0]) + odd_rgb[0]) / 2;
    const int blue = (static_cast<int>(even_rgb[2]) + odd_rgb[2]) / 2;
    const int red_chroma = (10 * red - 5 * (yo + ye) + 7 * 255) / 14;
    const int blue_chroma = (100 * blue - 50 * (yo + ye) + 89 * 255) / 178;
    y = static_cast<unsigned char>(even_line ? ye : yo);
    ry = static_cast<unsigned char>(red_chroma < 0 ? 0 : red_chroma > 255 ? 255 : red_chroma);
    by = static_cast<unsigned char>(blue_chroma < 0 ? 0 : blue_chroma > 255 ? 255 : blue_chroma);
}

static void append_robot36_scan(std::vector<float>& samples,
                                const std::vector<unsigned char>& even,
                                const std::vector<unsigned char>& odd,
                                int component, uint32_t scan_time,
                                bool even_line = true) {
    constexpr size_t width = 320;
    for (size_t x = 0; x < width; ++x) {
        unsigned char y, ry, by;
        robot36_pixel(&even[x * 3], &odd[x * 3], even_line, y, ry, by);
        const unsigned char value = component == 0 ? y : component == 1 ? ry : by;
        const uint32_t t0 = static_cast<uint32_t>((static_cast<uint64_t>(x) * scan_time) / width);
        const uint32_t t1 = static_cast<uint32_t>((static_cast<uint64_t>(x + 1) * scan_time) / width);
        append_tone(samples, 1500 + value * 800.0 / 255.0, t1 - t0);
    }
}

static void append_robot24_scan(std::vector<float>& samples,
                                const std::vector<unsigned char>& rgb,
                                int component, uint32_t scan_time) {
    constexpr size_t width = 160;
    for (size_t x = 0; x < width; ++x) {
        unsigned char y, ry, by;
        robot24_pixel(&rgb[x * 3], y, ry, by);
        const unsigned char value = component == 0 ? y : component == 1 ? ry : by;
        const uint32_t t0 = static_cast<uint32_t>((static_cast<uint64_t>(x) * scan_time) / width);
        const uint32_t t1 = static_cast<uint32_t>((static_cast<uint64_t>(x + 1) * scan_time) / width);
        append_tone(samples, 1500 + value * 800.0 / 255.0, t1 - t0);
    }
}

static void append_ycc_scan(std::vector<float>& samples,
                            const std::vector<unsigned char>& rgb,
                            size_t width, int component, uint32_t scan_time,
                            bool average_with = false,
                            const std::vector<unsigned char>& rgb2 = {}) {
    for (size_t x = 0; x < width; ++x) {
        unsigned char y1, ry1, by1, y2 = 0, ry2 = 0, by2 = 0;
        robot24_pixel(&rgb[x*3], y1, ry1, by1);
        if (average_with) robot24_pixel(&rgb2[x*3], y2, ry2, by2);
        unsigned char value = component == 0 ? y1 : component == 1 ? ry1 : by1;
        if (average_with) {
            unsigned char second = component == 0 ? y2 : component == 1 ? ry2 : by2;
            value = static_cast<unsigned char>((static_cast<unsigned>(value) + second + 1) / 2);
        }
        uint32_t t0 = static_cast<uint32_t>((static_cast<uint64_t>(x) * scan_time) / width);
        uint32_t t1 = static_cast<uint32_t>((static_cast<uint64_t>(x + 1) * scan_time) / width);
        append_tone(samples, 1500 + value * 800.0 / 256.0, t1-t0);
    }
}

static void append_pd240_scan(std::vector<float>& samples,
                              const std::vector<unsigned char>& rgb,
                              const std::vector<unsigned char>& rgb2,
                              int component, uint32_t scan_time,
                              bool average_with = false) {
    constexpr size_t width = 640;
    for (size_t x = 0; x < width; ++x) {
        unsigned char y1, ry1, by1, y2 = 0, ry2 = 0, by2 = 0;
        robot24_pixel(&rgb[x * 3], y1, ry1, by1);
        if (average_with) robot24_pixel(&rgb2[x * 3], y2, ry2, by2);
        unsigned char value = component == 0 ? y1 : component == 1 ? ry1 : by1;
        if (average_with) {
            const unsigned char second = component == 0 ? y2 : component == 1 ? ry2 : by2;
            value = static_cast<unsigned char>((static_cast<unsigned>(value) + second + 1) / 2);
        }
        // QSSTV places pixel boundaries at (x+1)*visibleLineLength/width.
        // Keep the duration in 0.1 us units and carry fractional samples
        // across every component and line, instead of rounding each scan.
        const uint32_t t0 = static_cast<uint32_t>((static_cast<uint64_t>(x) * scan_time) / width);
        const uint32_t t1 = static_cast<uint32_t>((static_cast<uint64_t>(x + 1) * scan_time) / width);
        append_tone(samples, 1500 + value * 800.0f / 256.0f, t1 - t0);
    }
}

static void append_rgb_scan(std::vector<float>& samples,
                            const std::vector<unsigned char>& rgb,
                            int component, uint32_t scan_time,
                            size_t width = 320) {
    for (size_t x = 0; x < width; ++x) {
        const uint32_t t0 = static_cast<uint32_t>((static_cast<uint64_t>(x) * scan_time) / width);
        const uint32_t t1 = static_cast<uint32_t>((static_cast<uint64_t>(x + 1) * scan_time) / width);
        append_tone(samples, 1500 + rgb[x * 3 + component] * 800.0 / 256.0, t1 - t0);
    }
}

static int transmit_baseband_zero_10s(double frequency_hz) {
    struct sigaction sa{};
    sa.sa_handler = stop_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    std::fprintf(stderr, "Baseband 0 Hz (unmodulated center carrier), 10 s at %.0f Hz\n", frequency_hz);
    modulator = new ngfmdmasync(static_cast<uint64_t>(frequency_hz), sample_rate, dma_channel, fifo_size);
    std::vector<float> samples(static_cast<size_t>(sample_rate) * 10, 0.0f);
    modulator->SetFrequencySamples(samples.data(), samples.size());
    usleep(250000); // let the final queued samples leave the DMA buffer
    delete modulator;
    modulator = nullptr;
    std::fprintf(stderr, "10 s baseband-zero test complete\n");
    return 0;
}

static bool read_rgb_row(int fd, std::vector<unsigned char>& row) {
    size_t got = 0;
    while (got < row.size()) {
        ssize_t n = read(fd, row.data() + got, row.size() - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

static bool send_robot8(int fd, size_t rows) {
    std::vector<unsigned char> rgb(160 * 3);
    std::vector<float> samples;
    samples.reserve(11000);
    append_vis_header(samples, 2);
    send(samples);
    for (size_t y = 0; y < rows && running; ++y) {
        if (!read_rgb_row(fd, rgb)) return false;
        samples.reserve(11000);
        append_tone(samples, 1200, 60000); // 6 ms sync
        append_tone(samples, 1500, 5000);  // 0.5 ms porch
        append_ycc_scan(samples, rgb, 160, 0, 600000); // 60 ms luminance
        append_tone(samples, 1500, 5000);
        send(samples);
    }
    return running;
}

static bool send_bw12(int fd, size_t rows) {
    std::vector<unsigned char> rgb(160 * 3);
    std::vector<float> samples;
    samples.reserve(11000);
    append_vis_header(samples, 6);
    send(samples);
    for (size_t y = 0; y < rows && running; ++y) {
        if (!read_rgb_row(fd, rgb)) return false;
        samples.reserve(11000);
        append_tone(samples, 1200, 60000); // 6 ms sync
        append_tone(samples, 1500, 10000); // 1 ms back porch
        for (size_t x = 0; x < 160; ++x) {
            const int gray = (11 * rgb[x * 3] + 16 * rgb[x * 3 + 1] + 5 * rgb[x * 3 + 2]) / 32;
            const uint32_t t0 = static_cast<uint32_t>((static_cast<uint64_t>(x) * 920080) / 160);
            const uint32_t t1 = static_cast<uint32_t>((static_cast<uint64_t>(x + 1) * 920080) / 160);
            append_tone(samples, 1500 + gray * 800.0 / 255.0, t1 - t0);
        } // 92.008 ms grayscale scan
        append_tone(samples, 1500, 10000); // 1 ms front porch
        send(samples);
    }
    return running;
}

static bool send_robot36(int fd, size_t rows) {
    std::vector<unsigned char> row(320 * 3), next(320 * 3);
    std::vector<float> samples;
    samples.reserve(19000);
    if (!std::getenv("PISSTV_NO_VIS")) {
        append_vis_header(samples, 0x88);
        send(samples);
    }
    for (size_t y = 0; y < rows && running; y += 2) {
        if (!read_rgb_row(fd, row) || !read_rgb_row(fd, next)) return false;
        for (int parity = 0; parity < 2; ++parity) {
            samples.reserve(19000);
            // QSSTV starts with a back porch; subsequent syncs follow each
            // chroma half-line and its back porch precedes the next Y line.
            if (y == 0 && parity == 0) append_tone(samples, 1500, 30000);
            append_robot36_scan(samples, row, next, 0, 884053, parity == 0); // 88.4053 ms Y
            append_tone(samples, parity == 0 ? 1500 : 2300, 36000);
            append_tone(samples, 1900, 18000);
            append_robot36_scan(samples, row, next, parity == 0 ? 1 : 2, 442027);
            append_tone(samples, 1200, 90000); // 9 ms sync
            append_tone(samples, 1500, 30000); // 3 ms back porch
            send(samples);
        }
    }
    return running;
}

static bool send_robot24(int fd, size_t rows) {
    std::vector<unsigned char> rgb(160 * 3);
    std::vector<float> samples;
    samples.reserve(24000);
    append_vis_header(samples, 0x84);
    send(samples);
    for (size_t y = 0; y < rows && running; ++y) {
        if (!read_rgb_row(fd, rgb)) return false;
        samples.reserve(24000);
        // QSSTV Robot 24 TX layout: 200 ms line = 1.2 ms BP, 2Y,
        // 3.8 ms blank (2/3 lower tone + 1/3 1900 Hz), R-Y,
        // 3.8 ms blank (2/3 2300 Hz + 1/3 1900 Hz), B-Y, no FP, 6 ms sync.
        append_tone(samples, 1500, 12000);
        append_robot24_scan(samples, rgb, 0, 926000); // 2x luminance, 92.6 ms
        append_tone(samples, 1500, 25330);
        append_tone(samples, 1900, 12670);
        append_robot24_scan(samples, rgb, 1, 463000); // R-Y, 46.3 ms
        append_tone(samples, 2300, 25330);
        append_tone(samples, 1900, 12670);
        append_robot24_scan(samples, rgb, 2, 463000); // B-Y, 46.3 ms
        append_tone(samples, 1200, 60000);
        send(samples);
    }
    return running;
}

static bool send_robot72(int fd, size_t rows) {
    std::vector<unsigned char> rgb(320 * 3);
    std::vector<float> samples;
    samples.reserve(25000);
    append_vis_header(samples, 12);
    send(samples);
    for (size_t y = 0; y < rows && running; ++y) {
        if (!read_rgb_row(fd, rgb)) return false;
        samples.reserve(25000);
        append_tone(samples, 1200, 90000);
        append_tone(samples, 1500, 30000);
        append_ycc_scan(samples, rgb, 320, 0, 1380000);
        append_tone(samples, 1500, 45000);
        append_tone(samples, 1900, 15000);
        append_ycc_scan(samples, rgb, 320, 1, 690000);
        append_tone(samples, 2300, 45000);
        append_tone(samples, 1900, 15000);
        append_ycc_scan(samples, rgb, 320, 2, 690000);
        send(samples);
    }
    return running;
}

static bool send_pd120(int fd, size_t rows) {
    std::vector<unsigned char> odd(640 * 3), even(640 * 3);
    std::vector<float> samples;
    samples.reserve(80000);
    append_vis_header(samples, 95);
    send(samples);
    for (size_t y = 0; y < rows && running; y += 2) {
        if (!read_rgb_row(fd, odd) || !read_rgb_row(fd, even)) return false;
        samples.reserve(80000);
        append_tone(samples, 1200, 200000); // 20 ms sync
        append_tone(samples, 1500, 20800);  // 2.08 ms porch
        append_ycc_scan(samples, odd, 640, 0, 1216000); // odd-line Y
        append_ycc_scan(samples, odd, 640, 1, 1216000, true, even); // averaged R-Y
        append_ycc_scan(samples, odd, 640, 2, 1216000, true, even); // averaged B-Y
        append_ycc_scan(samples, even, 640, 0, 1216000); // even-line Y
        send(samples);
    }
    return running;
}

// PD modes transmit two image rows as four sequential components: Y(odd),
// averaged R-Y, averaged B-Y, Y(even). PD50 uses 320x256 and 128 pairs.
static bool send_pd50(int fd, size_t rows) {
    constexpr size_t width = 320;
    constexpr uint32_t line_time = 3881850; // 49.6877 s / 128, in 0.1 us
    constexpr uint32_t sync_time = 200000;
    constexpr uint32_t back_porch = 23000;
    constexpr uint32_t scan_time = (line_time - sync_time - back_porch) / 4;
    std::vector<unsigned char> odd(width * 3), even(width * 3);
    std::vector<float> samples;
    samples.reserve(4 * 100000);
    append_vis_header(samples, 0xDD);
    send(samples);
    for (size_t y = 0; y < rows && running; y += 2) {
        if (!read_rgb_row(fd, odd) || !read_rgb_row(fd, even)) return false;
        append_ycc_scan(samples, odd, width, 0, scan_time);
        append_ycc_scan(samples, odd, width, 1, scan_time, true, even);
        append_ycc_scan(samples, odd, width, 2, scan_time, true, even);
        append_ycc_scan(samples, even, width, 0, scan_time);
        append_tone(samples, 1500, 0); // preserve fractional sample timing
        append_tone(samples, 1200, sync_time);
        append_tone(samples, 1500, back_porch);
        send(samples);
    }
    return running;
}

static bool send_pd90(int fd, size_t rows) {
    constexpr size_t width = 320;
    constexpr uint32_t line_time = 7030860; // 89.995 s / 128, in 0.1 us
    constexpr uint32_t sync_time = 200000;
    constexpr uint32_t back_porch = 23000;
    constexpr uint32_t scan_time = (line_time - sync_time - back_porch) / 4;
    std::vector<unsigned char> odd(width * 3), even(width * 3);
    std::vector<float> samples;
    samples.reserve(4 * 200000);
    append_vis_header(samples, 0x63);
    send(samples);
    for (size_t y = 0; y < rows && running; y += 2) {
        if (!read_rgb_row(fd, odd) || !read_rgb_row(fd, even)) return false;
        append_ycc_scan(samples, odd, width, 0, scan_time);
        append_ycc_scan(samples, odd, width, 1, scan_time, true, even);
        append_ycc_scan(samples, odd, width, 2, scan_time, true, even);
        append_ycc_scan(samples, even, width, 0, scan_time);
        append_tone(samples, 1200, sync_time);
        append_tone(samples, 1500, back_porch);
        send(samples);
    }
    return running;
}

static bool send_pd160(int fd, size_t rows) {
    constexpr size_t width = 512;
    constexpr uint32_t line_time = 8044710; // 160.8942 s / 200, in 0.1 us
    constexpr uint32_t sync_time = 200000;
    constexpr uint32_t back_porch = 23000;
    constexpr uint32_t scan_time = (line_time - sync_time - back_porch) / 4;
    std::vector<unsigned char> odd(width * 3), even(width * 3);
    std::vector<float> samples;
    samples.reserve(4 * 250000);
    append_vis_header(samples, 0xE2);
    send(samples);
    for (size_t y = 0; y < rows && running; y += 2) {
        if (!read_rgb_row(fd, odd) || !read_rgb_row(fd, even)) return false;
        append_ycc_scan(samples, odd, width, 0, scan_time);
        append_ycc_scan(samples, odd, width, 1, scan_time, true, even);
        append_ycc_scan(samples, odd, width, 2, scan_time, true, even);
        append_ycc_scan(samples, even, width, 0, scan_time);
        append_tone(samples, 1200, sync_time);
        append_tone(samples, 1500, back_porch);
        send(samples);
    }
    return running;
}

static bool send_pd180(int fd, size_t rows) {
    constexpr size_t width = 640;
    constexpr uint32_t line_time = 7542923; // 187.0645 s / 248, in 0.1 us
    constexpr uint32_t sync_time = 200000;
    constexpr uint32_t back_porch = 23000;
    constexpr uint32_t scan_time = (line_time - sync_time - back_porch) / 4;
    std::vector<unsigned char> odd(width * 3), even(width * 3);
    std::vector<float> samples;
    samples.reserve(4 * 300000);
    append_vis_header(samples, 0x60);
    send(samples);
    for (size_t y = 0; y < rows && running; y += 2) {
        if (!read_rgb_row(fd, odd) || !read_rgb_row(fd, even)) return false;
        append_ycc_scan(samples, odd, width, 0, scan_time);
        append_ycc_scan(samples, odd, width, 1, scan_time, true, even);
        append_ycc_scan(samples, odd, width, 2, scan_time, true, even);
        append_ycc_scan(samples, even, width, 0, scan_time);
        append_tone(samples, 1200, sync_time);
        append_tone(samples, 1500, back_porch);
        send(samples);
    }
    return running;
}

static bool build_pd240(int fd, size_t rows, std::vector<float>& samples) {
    constexpr size_t width = 640;
    // Match QSSTV's PD240 RX timing table: 248.017 s / 248 line pairs,
    // 20 ms sync, 2 ms front porch and 2 ms back porch.
    constexpr uint32_t line_time = 10000685; // 1.0000685 s in 0.1 us units
    constexpr uint32_t sync_time = 200000;
    constexpr uint32_t back_porch = 23000; // QSSTV transmit table uses 2.3 ms
    constexpr uint32_t front_porch = 20000;
    constexpr uint32_t scan_time_base = (line_time - sync_time - back_porch - front_porch) / 4;
    constexpr uint32_t scan_time_remainder = (line_time - sync_time - back_porch - front_porch) % 4;
    std::vector<unsigned char> odd(width * 3), even(width * 3);
    // Build the complete frame before transmitting. Feeding librpitx one
    // row-pair at a time makes host-side work part of the real-time path;
    // a single continuous buffer keeps every line clocked by the DMA stream.
    samples.reserve(rows / 2 * 100100);
    append_vis_header(samples, 0xE1);
    // Each QSSTV line pair is BP, Y, R-Y, B-Y, Y, FP, sync.
    // Distribute the fractional 0.1-us scan remainder so every pair totals
    // exactly 1.0000685 s, the PD240 timing used by QSSTV.
    sample_remainder = 0;
    for (size_t y = 0; y < rows && running; y += 2) {
        if (!read_rgb_row(fd, even) || !read_rgb_row(fd, odd)) return false;
        append_tone(samples, 1500, back_porch);
        for (int component = 0; component < 4; ++component) {
            const uint32_t scan_time = scan_time_base + (component < static_cast<int>(scan_time_remainder));
            if (component == 0) append_pd240_scan(samples, even, odd, 0, scan_time);
            else if (component == 1) append_pd240_scan(samples, even, odd, 1, scan_time, true);
            else if (component == 2) append_pd240_scan(samples, even, odd, 2, scan_time, true);
            else append_pd240_scan(samples, odd, even, 0, scan_time);
        }
        append_tone(samples, 1500, front_porch);
        append_tone(samples, 1200, sync_time);
    }
    return running;
}

static bool build_pd290(int fd, size_t rows, std::vector<float>& samples) {
    constexpr size_t width = 800;
    // QSSTV PD290: 288.702 s, 308 row pairs, 20 ms sync, 2.3 ms TX back porch.
    constexpr uint32_t line_time = 9373440; // 0.937344 s per row pair, in 0.1 us units
    constexpr uint32_t sync_time = 200000;
    constexpr uint32_t back_porch = 23000;
    constexpr uint32_t scan_time = (line_time - sync_time - back_porch) / 4;
    std::vector<unsigned char> even(width * 3), odd(width * 3);
    samples.reserve(rows / 2 * 93735);
    append_vis_header(samples, 0x5E); // PD290 standard VIS is decimal 94
    sample_remainder = 0;
    for (size_t y = 0; y < rows && running; y += 2) {
        if (!read_rgb_row(fd, even) || !read_rgb_row(fd, odd)) return false;
        append_tone(samples, 1500, back_porch);
        append_ycc_scan(samples, even, width, 0, scan_time);
        append_ycc_scan(samples, even, width, 1, scan_time, true, odd);
        append_ycc_scan(samples, even, width, 2, scan_time, true, odd);
        append_ycc_scan(samples, odd, width, 0, scan_time);
        append_tone(samples, 1200, sync_time);
    }
    return running;
}

static bool build_p7(int fd, size_t rows, std::vector<float>& samples) {
    constexpr size_t width = 640;
    constexpr uint64_t total_image_time = 4061280000ULL; // 406.128 s in 0.1 us units
    constexpr uint32_t sync_time = 104000;
    constexpr uint32_t front_porch = 21000;
    constexpr uint32_t back_porch = 21000;
    constexpr uint32_t blank = 21000;
    std::vector<unsigned char> rgb(width * 3);
    samples.reserve(static_cast<size_t>(total_image_time * sample_rate / 10000000ULL) + 500000);
    append_vis_header(samples, 0xF3); // P7: eight on-air VIS bits, including parity
    sample_remainder = 0;
    uint64_t line_remainder = 0;
    for (size_t y = 0; y < rows && running; ++y) {
        if (!read_rgb_row(fd, rgb)) return false;
        const uint64_t line_total = line_remainder + total_image_time;
        const uint32_t line_time = static_cast<uint32_t>(line_total / rows);
        line_remainder = line_total % rows;
        const uint32_t scan_total = line_time - sync_time - front_porch - back_porch - 2 * blank;
        const uint32_t scan_base = scan_total / 3;
        const uint32_t scan_rem = scan_total % 3;
        append_tone(samples, 1500, back_porch);
        append_rgb_scan(samples, rgb, 0, scan_base, width);
        append_tone(samples, 1500, blank);
        append_rgb_scan(samples, rgb, 1, scan_base + (scan_rem > 0), width);
        append_tone(samples, 1500, blank);
        append_rgb_scan(samples, rgb, 2, scan_base + (scan_rem > 1), width);
        append_tone(samples, 1500, front_porch);
        append_tone(samples, 1200, sync_time);
    }
    return running;
}

static bool build_pasokon_rgb(int fd, size_t rows, std::vector<float>& samples,
                             uint64_t total_image_time, int vis,
                             uint32_t sync_time, uint32_t porch, uint32_t blank) {
    constexpr size_t width = 640;
    const uint32_t front_porch = porch;
    const uint32_t back_porch = porch;
    std::vector<unsigned char> rgb(width * 3);
    samples.reserve(static_cast<size_t>(total_image_time * sample_rate / 10000000ULL) + 500000);
    append_vis_header(samples, vis);
    sample_remainder = 0;
    uint64_t line_remainder = 0;
    for (size_t y = 0; y < rows && running; ++y) {
        if (!read_rgb_row(fd, rgb)) return false;
        const uint64_t line_total = line_remainder + total_image_time;
        const uint32_t line_time = static_cast<uint32_t>(line_total / rows);
        line_remainder = line_total % rows;
        const uint32_t scan_total = line_time - sync_time - front_porch - back_porch - 2 * blank;
        const uint32_t scan_base = scan_total / 3;
        const uint32_t scan_rem = scan_total % 3;
        append_tone(samples, 1500, back_porch);
        append_rgb_scan(samples, rgb, 0, scan_base, width);
        append_tone(samples, 1500, blank);
        append_rgb_scan(samples, rgb, 1, scan_base + (scan_rem > 0), width);
        append_tone(samples, 1500, blank);
        append_rgb_scan(samples, rgb, 2, scan_base + (scan_rem > 1), width);
        append_tone(samples, 1500, front_porch);
        append_tone(samples, 1200, sync_time);
    }
    return running;
}

static bool build_robot2(int fd, size_t width, size_t rows,
                         std::vector<float>& samples, uint64_t total_image_time,
                         uint32_t vis, uint32_t sync_time, uint32_t front_porch,
                         uint32_t back_porch, uint32_t blank) {
    std::vector<unsigned char> rgb(width * 3);
    samples.reserve(static_cast<size_t>(total_image_time * sample_rate / 10000000ULL) + 500000);
    append_extended_vis_header(samples, vis);
    sample_remainder = 0;
    uint64_t line_remainder = 0;
    for (size_t y = 0; y < rows && running; ++y) {
        if (!read_rgb_row(fd, rgb)) return false;
        const uint64_t line_total = line_remainder + total_image_time;
        const uint32_t line_time = static_cast<uint32_t>(line_total / rows);
        line_remainder = line_total % rows;
        const uint32_t overhead = sync_time + front_porch + back_porch + 2 * blank;
        const uint32_t scan_total = line_time - overhead;
        const uint32_t scan_base = scan_total / 4;
        const uint32_t scan_rem = scan_total % 4;
        const uint32_t y_scan = 2 * scan_base + (scan_rem >= 2);
        const uint32_t red_scan = scan_base + (scan_rem == 3);
        const uint32_t blue_scan = scan_base + (scan_rem == 1 || scan_rem == 2);
        append_tone(samples, 1500, back_porch);
        append_ycc_scan(samples, rgb, width, 0, y_scan);
        const uint32_t blank_low = (2 * blank) / 3;
        append_tone(samples, 1500, blank_low);
        append_tone(samples, 1900, blank - blank_low);
        append_ycc_scan(samples, rgb, width, 1, red_scan);
        append_tone(samples, 2300, blank_low);
        append_tone(samples, 1900, blank - blank_low);
        append_ycc_scan(samples, rgb, width, 2, blue_scan);
        append_tone(samples, 1500, front_porch);
        append_tone(samples, 1200, sync_time);
    }
    return running;
}

// QSSTV routes MP modes through modePD: each data line carries two image rows
// as Y(even), averaged R-Y, averaged B-Y, Y(odd), then front porch, sync and
// back porch. MP has 128 data lines for a 320x256 image.
static bool build_mp(int fd, size_t rows, std::vector<float>& samples,
                     uint64_t total_image_time, uint32_t vis) {
    constexpr size_t width = 320;
    constexpr uint32_t sync_time = 90000;
    constexpr uint32_t back_porch = 10000;
    constexpr uint32_t front_porch = 0;
    constexpr uint32_t blank = 0;
    const size_t line_count = rows / 2;
    std::vector<unsigned char> even(width * 3), odd(width * 3);
    samples.reserve(static_cast<size_t>(total_image_time * sample_rate / 10000000ULL) + 500000);
    append_extended_vis_header(samples, vis);
    sample_remainder = 0;
    uint64_t line_remainder = 0;
    for (size_t y = 0; y < rows && running; y += 2) {
        if (!read_rgb_row(fd, even) || !read_rgb_row(fd, odd)) return false;
        const uint64_t line_total = line_remainder + total_image_time;
        const uint32_t line_time = static_cast<uint32_t>(line_total / line_count);
        line_remainder = line_total % line_count;
        const uint32_t scan_total = line_time - sync_time - back_porch - front_porch - 3 * blank;
        const uint32_t scan_base = scan_total / 4;
        const uint32_t scan_rem = scan_total % 4;
        const uint32_t scan0 = scan_base + (scan_rem > 0);
        const uint32_t scan1 = scan_base + (scan_rem > 1);
        const uint32_t scan2 = scan_base + (scan_rem > 2);
        const uint32_t scan3 = scan_base;
        append_ycc_scan(samples, even, width, 0, scan0);
        append_tone(samples, 1500, blank);
        append_ycc_scan(samples, even, width, 1, scan1, true, odd);
        append_tone(samples, 1500, blank);
        append_ycc_scan(samples, even, width, 2, scan2, true, odd);
        append_tone(samples, 1500, blank);
        append_ycc_scan(samples, odd, width, 0, scan3);
        append_tone(samples, 1500, front_porch);
        append_tone(samples, 1200, sync_time);
        append_tone(samples, 1500, back_porch);
    }
    return running;
}

static bool build_fax480(int fd, std::vector<float>& samples) {
    constexpr size_t width = 512;
    constexpr size_t rows = 500;
    constexpr uint64_t image_time = 1336330000ULL; // 133.633 s, 0.1 us units
    constexpr uint32_t phasing_pulse = 20500;       // 2.05 ms
    constexpr uint32_t sync_time = 51200;           // 5.12 ms
    const uint32_t line_time = static_cast<uint32_t>(image_time / rows);
    const uint32_t scan_time = line_time - sync_time;
    std::vector<unsigned char> rgb(width * 3);
    samples.reserve(static_cast<size_t>(image_time * sample_rate / 10000000ULL) + 600000);
    sample_remainder = 0;
    for (int i = 0; i < 1220; ++i) {
        append_tone(samples, 1500, phasing_pulse);
        append_tone(samples, 2300, phasing_pulse);
    }
    for (size_t y = 0; y < rows && running; ++y) {
        if (!read_rgb_row(fd, rgb)) return false;
        for (size_t x = 0; x < width; ++x) {
            const unsigned r = rgb[x * 3 + 0];
            const unsigned g = rgb[x * 3 + 1];
            const unsigned b = rgb[x * 3 + 2];
            const unsigned gray = (11 * r + 16 * g + 5 * b) / 32;
            const uint32_t t0 = static_cast<uint32_t>((static_cast<uint64_t>(x) * scan_time) / width);
            const uint32_t t1 = static_cast<uint32_t>((static_cast<uint64_t>(x + 1) * scan_time) / width);
            append_tone(samples, 1500 + gray * 800.0 / 255.0, t1 - t0);
        }
        append_tone(samples, 1900, sync_time);
    }
    return running;
}

static bool send_scottie(int image_fd, size_t rows, int vis, uint32_t pixel_time) {
    std::vector<unsigned char> rgb(320 * 3);
    std::vector<float> samples;
    samples.reserve(43000);
    append_vis_header(samples, vis);
    send(samples);

    for (size_t row = 0; row < rows && running; ++row) {
        size_t got = 0;
        while (got < rgb.size()) {
            ssize_t n = read(image_fd, rgb.data() + got, rgb.size() - got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            got += static_cast<size_t>(n);
        }

        samples.reserve(43000);
        if (row == 0) {
            append_tone(samples, 1200, 90000); // initial 9 ms line sync
            append_tone(samples, 1500, 15000); // 1.5 ms porch
        }
        append_component(samples, rgb, 1, pixel_time); // Green
        append_tone(samples, 1500, 15000);
        append_component(samples, rgb, 2, pixel_time); // Blue
        append_tone(samples, 1200, 90000);     // 9 ms mid-line sync
        append_tone(samples, 1500, 15000);     // sync porch
        append_component(samples, rgb, 0, pixel_time); // Red
        append_tone(samples, 1500, 15000);     // separator before next line
        send(samples);
    }
    return running;
}

static bool send_sc2(int fd, size_t rows, int vis, uint32_t scan_time) {
    std::vector<unsigned char> rgb(320 * 3);
    std::vector<float> samples;
    samples.reserve(48000);
    append_vis_header(samples, vis);
    send(samples);
    for (size_t row = 0; row < rows && running; ++row) {
        if (!read_rgb_row(fd, rgb)) return false;
        samples.reserve(48000);
        append_rgb_scan(samples, rgb, 0, scan_time); // Red
        append_rgb_scan(samples, rgb, 1, scan_time); // Green
        append_rgb_scan(samples, rgb, 2, scan_time); // Blue
        append_tone(samples, 1200, 50000); // SC2 sync, 5 ms
        send(samples);
    }
    return running;
}

int main(int argc, char **argv) {
    if (argc == 3 && !std::strcmp(argv[1], "--baseband-zero")) {
        char *end = nullptr;
        double frequency_hz = std::strtod(argv[2], &end);
        if (!end || *end || frequency_hz < 50000 || frequency_hz > 1500000000.0) {
            std::fprintf(stderr, "invalid frequency\n");
            return 2;
        }
        return transmit_baseband_zero_10s(frequency_hz);
    }
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr,
            "usage: pisstv image.rgb frequency_hz [M1|M2|S1|S2|DX|SC2-60|SC2-120|SC2-180|P3|P5|P7|MP73|MP115|MP140|MP175|MR73|MR90|MR115|MR140|MR175|ML180|ML240|ML280|ML320|ROBOT8|ROBOT24|ROBOT36|ROBOT72|BW12|FAX480|PD50|PD90|PD120|PD160|PD180|PD240|PD290]\n"
            "       pisstv --baseband-zero frequency_hz  (10-second center carrier)\n"
            "  M1  Martin M1   VIS 44 (0x2C)\n"
            "  M2  Martin M2   VIS 40 (0x28)\n"
            "  S1  Scottie S1  VIS 60 (0x3C)\n"
            "  S2  Scottie S2  VIS 56 (0x38)\n"
            "  DX  Scottie DX  VIS 76 (0x4C)\n"
            "  SC2-60/120/180 RGB 320x256 VIS 0xBB/0x3F/0xB7\n"
            "  P3/P5  Pasokon RGB, VIS 0x71/0x72, 640x496\n"
            "  P7      Pasokon P7, VIS 243 (0xF3), 640x496\n"
            "  MP/MR/ML modes  Robot2 Y/R-Y/B-Y, extended 16-bit VIS\n"
            "  ROBOT8  B/W 8  VIS 2 (0x02), 160x120\n"
            "  ROBOT24 Robot 24 VIS 4 (0x04), 160x120\n"
            "  ROBOT36 Robot 36 VIS 8 (0x08), 320x240\n"
            "  ROBOT72 Robot 72 VIS 12 (0x0C), 320x240\n"
            "  BW12  B/W 12   VIS 6 (0x06), 160x120\n"
            "  FAX480 FAX480  512x500, no VIS, 5-second alternating-tone phasing sequence\n"
            "  PD50   PD 50    VIS 221 (0xDD), 320x256\n"
            "  PD90   PD 90    VIS 99 (0x63), 320x256\n"
            "  PD160  PD 160   VIS 226 (0xE2), 512x400\n"
            "  PD180  PD 180   VIS 96 (0x60), 640x496\n"
            "  PD240  PD 240   VIS 225 (0xE1), 640x496\n"
            "  PD290  PD 290   VIS 94 (0x5E), 800x616\n"
            "  PD120  PD 120   VIS 95 (0x5F), 640x496\n");
        return 2;
    }
    const char *mode = argc == 4 ? argv[3] : "M1";
    if (!std::strcmp(mode, "R8")) mode = "ROBOT8";
    if (!std::strcmp(mode, "R36")) mode = "ROBOT36";
    if (!std::strcmp(mode, "R72")) mode = "ROBOT72";
    if (std::strcmp(mode, "M1") && std::strcmp(mode, "M2") && std::strcmp(mode, "S1") &&
        std::strcmp(mode, "S2") && std::strcmp(mode, "DX") &&
        std::strcmp(mode, "SC2-60") && std::strcmp(mode, "SC2-120") && std::strcmp(mode, "SC2-180") &&
        std::strcmp(mode, "P3") && std::strcmp(mode, "P5") && std::strcmp(mode, "P7") &&
        std::strcmp(mode, "MP73") && std::strcmp(mode, "MP115") && std::strcmp(mode, "MP140") && std::strcmp(mode, "MP175") &&
        std::strcmp(mode, "MR73") && std::strcmp(mode, "MR90") && std::strcmp(mode, "MR115") && std::strcmp(mode, "MR140") && std::strcmp(mode, "MR175") &&
        std::strcmp(mode, "ML180") && std::strcmp(mode, "ML240") && std::strcmp(mode, "ML280") && std::strcmp(mode, "ML320") &&
        std::strcmp(mode, "ROBOT8") && std::strcmp(mode, "ROBOT24") &&
        std::strcmp(mode, "ROBOT36") &&
        std::strcmp(mode, "ROBOT72") && std::strcmp(mode, "BW12") && std::strcmp(mode, "FAX480") &&
        std::strcmp(mode, "PD50") && std::strcmp(mode, "PD90") && std::strcmp(mode, "PD120") && std::strcmp(mode, "PD160") && std::strcmp(mode, "PD180") && std::strcmp(mode, "PD240") && std::strcmp(mode, "PD290")) {
        std::fprintf(stderr, "unknown mode; use M1, M2, S1, S2, DX, ROBOT8, ROBOT24, ROBOT36, ROBOT72, BW12, or PD120\n");
        return 2;
    }
    char *end = nullptr;
    double frequency = std::strtod(argv[2], &end);
    if (!end || *end || frequency < 50000 || frequency > 1500000000.0) {
        std::fprintf(stderr, "invalid frequency\n");
        return 2;
    }
    const bool is_martin = !std::strcmp(mode, "M1");
    const bool is_martin2 = !std::strcmp(mode, "M2");
    const bool is_scottie = !std::strcmp(mode, "S1") || !std::strcmp(mode, "S2") || !std::strcmp(mode, "DX");
    const bool is_sc2 = !std::strcmp(mode, "SC2-60") || !std::strcmp(mode, "SC2-120") || !std::strcmp(mode, "SC2-180");
    const bool is_robot8 = !std::strcmp(mode, "ROBOT8");
    const bool is_robot24 = !std::strcmp(mode, "ROBOT24");
    const bool is_robot36 = !std::strcmp(mode, "ROBOT36");
    const bool is_robot72 = !std::strcmp(mode, "ROBOT72");
    const bool is_bw12 = !std::strcmp(mode, "BW12");
    const bool is_fax480 = !std::strcmp(mode, "FAX480");
    const bool is_pd50 = !std::strcmp(mode, "PD50");
    const bool is_pd90 = !std::strcmp(mode, "PD90");
    const bool is_pd160 = !std::strcmp(mode, "PD160");
    const bool is_pd180 = !std::strcmp(mode, "PD180");
    const bool is_pd240 = !std::strcmp(mode, "PD240");
    const bool is_pd290 = !std::strcmp(mode, "PD290");
    const bool is_p3 = !std::strcmp(mode, "P3");
    const bool is_p5 = !std::strcmp(mode, "P5");
    const bool is_p7 = !std::strcmp(mode, "P7");
    const bool is_mp = !std::strcmp(mode, "MP73") || !std::strcmp(mode, "MP115") || !std::strcmp(mode, "MP140") || !std::strcmp(mode, "MP175");
    const bool is_mr = !std::strcmp(mode, "MR73") || !std::strcmp(mode, "MR90") || !std::strcmp(mode, "MR115") || !std::strcmp(mode, "MR140") || !std::strcmp(mode, "MR175");
    const bool is_ml = !std::strcmp(mode, "ML180") || !std::strcmp(mode, "ML240") || !std::strcmp(mode, "ML280") || !std::strcmp(mode, "ML320");
    const bool is_robot2 = is_mr || is_ml;
    const bool is_pd120 = !std::strcmp(mode, "PD120");
    const size_t width = is_robot8 || is_robot24 || is_bw12 ? 160 : is_pd290 ? 800 : is_pd160 || is_fax480 ? 512 : (is_pd120 || is_pd180 || is_pd240 || is_p3 || is_p5 || is_p7 || is_ml) ? 640 : 320;
    const size_t expected_rows = is_fax480 ? 500 : (is_robot8 || is_robot24 || is_bw12) ? 120 : is_robot36 || is_robot72 ? 240 : is_pd290 ? 616 : is_pd160 ? 400 : (is_pd120 || is_pd180 || is_pd240 || is_p3 || is_p5 || is_p7 || is_ml) ? 496 : 256;
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { std::perror("open image"); return 1; }
    struct stat st{};
    if (fstat(fd, &st) || st.st_size <= 0 || st.st_size % (width * 3) != 0) {
        std::fprintf(stderr, "image must be raw %zuxN RGB for %s\n", width, mode); close(fd); return 2;
    }
    size_t rows = st.st_size / (width * 3);
    if (rows != expected_rows) {
        std::fprintf(stderr, "%s expects %zux%zu RGB, got %zux%zu\n", mode, width, expected_rows, width, rows); close(fd); return 2;
    }
    struct sigaction sa{};
    sa.sa_handler = stop_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    const int vis = is_fax480 ? 0 : is_mp ? (!std::strcmp(mode, "MP73") ? 0x2523 : !std::strcmp(mode, "MP115") ? 0x2923 : !std::strcmp(mode, "MP140") ? 0x2A23 : 0x2C23) :
                    is_mr ? (!std::strcmp(mode, "MR73") ? 0x4523 : !std::strcmp(mode, "MR90") ? 0x4623 : !std::strcmp(mode, "MR115") ? 0x4923 : 0x4A23) :
                    is_ml ? (!std::strcmp(mode, "ML180") ? 0x8523 : !std::strcmp(mode, "ML240") ? 0x8623 : !std::strcmp(mode, "ML280") ? 0x8923 : 0x8A23) :
                    is_martin ? 44 : is_martin2 ? 40 : is_robot24 ? 0x84 : is_bw12 ? 6 :
                    !std::strcmp(mode, "SC2-60") ? 0xBB : !std::strcmp(mode, "SC2-120") ? 0x3F : !std::strcmp(mode, "SC2-180") ? 0xB7 :
                    !std::strcmp(mode, "S1") ? 60 :
                    !std::strcmp(mode, "S2") ? 56 : !std::strcmp(mode, "DX") ? 76 :
        is_robot8 ? 2 : is_robot36 ? 0x88 : is_robot72 ? 12 : is_pd50 ? 0xDD : is_pd90 ? 0x63 : is_pd160 ? 0xE2 : is_pd180 ? 0x60 : is_pd240 ? 0xE1 : is_pd290 ? 0x5E : is_p3 ? 0x71 : is_p5 ? 0x72 : is_p7 ? 0xF3 : 95;
    const uint32_t pixel_time = !std::strcmp(mode, "S2") ? 2752 :
                                !std::strcmp(mode, "DX") ? 10800 : 4320;
    const double line_seconds = !std::strcmp(mode, "SC2-60") ? 61.54350 / 256.0 :
                                !std::strcmp(mode, "SC2-120") ? 121.74250 / 256.0 :
                                !std::strcmp(mode, "SC2-180") ? 182.03850 / 256.0 : 0.0;
    const uint32_t sc2_scan_time = static_cast<uint32_t>((line_seconds - 0.005) * 10000000.0 / 3.0);
    const char *mode_name = is_martin ? "Martin M1" : is_martin2 ? "Martin M2" : is_robot24 ? "Robot 24" : is_bw12 ? "B/W 12" :
        is_mp || is_robot2 || is_fax480 ? mode : is_sc2 ? mode : !std::strcmp(mode, "S1") ? "Scottie S1" :
        !std::strcmp(mode, "S2") ? "Scottie S2" : !std::strcmp(mode, "DX") ? "Scottie DX" :
        is_robot8 ? "Robot B/W 8" : is_robot36 ? "Robot 36" : is_robot72 ? "Robot 72" : is_pd50 ? "PD 50" : is_pd90 ? "PD 90" : is_pd160 ? "PD 160" : is_pd180 ? "PD 180" : is_pd240 ? "PD 240" : is_pd290 ? "PD 290" : is_p3 ? "Pasokon P3" : is_p5 ? "Pasokon P5" : is_p7 ? "Pasokon P7" : "PD 120";
    std::fprintf(stderr, "%s (VIS %d / 0x%02X): %zux%zu at %.0f Hz\n",
                 mode_name, vis, vis, width, rows, frequency);
    std::vector<float> pd240_samples;
    if (is_pd240) {
        std::fprintf(stderr, "Preparing continuous PD240 frame before RF start\n");
        if (!build_pd240(fd, rows, pd240_samples)) {
            std::fprintf(stderr, "PD240 frame preparation failed\n");
            close(fd);
            return 1;
        }
    }
    std::vector<float> pd290_samples;
    if (is_pd290) {
        std::fprintf(stderr, "Preparing continuous PD290 frame before RF start\n");
        if (!build_pd290(fd, rows, pd290_samples)) {
            std::fprintf(stderr, "PD290 frame preparation failed\n");
            close(fd);
            return 1;
        }
    }
    std::vector<float> p7_samples;
    if (is_p7) {
        std::fprintf(stderr, "Preparing continuous P7 frame before RF start\n");
        if (!build_p7(fd, rows, p7_samples)) {
            std::fprintf(stderr, "P7 frame preparation failed\n");
            close(fd);
            return 1;
        }
    }
    std::vector<float> pasokon_samples;
    if (is_p3 || is_p5) {
        const uint64_t total_image_time = is_p3 ? 2030642000ULL : 3045960000ULL;
        const uint32_t sync_time = is_p3 ? 52000 : 78000;
        const uint32_t porch = is_p3 ? 10400 : 16000;
        const uint32_t blank = is_p3 ? 10400 : 16000;
        std::fprintf(stderr, "Preparing continuous %s frame before RF start\n", mode);
        if (!build_pasokon_rgb(fd, rows, pasokon_samples, total_image_time, vis, sync_time, porch, blank)) {
            std::fprintf(stderr, "%s frame preparation failed\n", mode);
            close(fd);
            return 1;
        }
    }
    std::vector<float> mp_samples;
    if (is_mp) {
        const uint64_t image_time =
            !std::strcmp(mode, "MP73") ? 729642000ULL :
            !std::strcmp(mode, "MP115") ? 1154640000ULL :
            !std::strcmp(mode, "MP140") ? 1395280000ULL : 1753730000ULL;
        std::fprintf(stderr, "Preparing continuous %s frame before RF start\n", mode);
        if (!build_mp(fd, rows, mp_samples, image_time, static_cast<uint32_t>(vis))) {
            std::fprintf(stderr, "%s frame preparation failed\n", mode);
            close(fd);
            return 1;
        }
    }
    std::vector<float> fax_samples;
    if (is_fax480) {
        std::fprintf(stderr, "Preparing continuous FAX480 frame before RF start\n");
        if (!build_fax480(fd, fax_samples)) {
            std::fprintf(stderr, "FAX480 frame preparation failed\n");
            close(fd);
            return 1;
        }
    }
    std::vector<float> robot2_samples;
    if (is_robot2) {
        const uint64_t image_time =
            !std::strcmp(mode, "MR73") ? 732980000ULL :
            !std::strcmp(mode, "MR90") ? 901950000ULL :
            !std::strcmp(mode, "MR115") ? 1152850000ULL :
            !std::strcmp(mode, "MR140") ? 1403740000ULL :
            !std::strcmp(mode, "MR175") ? 1751920000ULL :
            !std::strcmp(mode, "ML180") ? 1802085000ULL :
            !std::strcmp(mode, "ML240") ? 2397328000ULL :
            !std::strcmp(mode, "ML280") ? 2804060000ULL : 3200910000ULL;
        std::fprintf(stderr, "Preparing continuous %s frame before RF start\n", mode);
        if (!build_robot2(fd, width, rows, robot2_samples, image_time,
                          static_cast<uint32_t>(vis), 90000, 0, 10000, 0)) {
            std::fprintf(stderr, "%s frame preparation failed\n", mode);
            close(fd);
            return 1;
        }
    }
    if (!wait_before_rf_if_requested()) {
        std::fprintf(stderr, "Transmission cancelled before RF start.\n");
        close(fd);
        return 130;
    }
    modulator = new ngfmdmasync(static_cast<uint64_t>(frequency), sample_rate, dma_channel, fifo_size);
    int image_fd = fd;
    bool ok;
    if (is_fax480) {
        ok = send_fax480(fax_samples);
    } else if (is_mp) {
        send(mp_samples);
        ok = running;
    } else if (is_robot2) {
        send(robot2_samples);
        ok = running;
    } else if (is_scottie) {
        ok = send_scottie(image_fd, rows, vis, pixel_time);
    } else if (is_sc2) {
        ok = send_sc2(image_fd, rows, vis, sc2_scan_time);
    } else if (is_martin2) {
        ok = send_martin_m2(image_fd, rows);
    } else if (is_robot8) {
        ok = send_robot8(image_fd, rows);
    } else if (is_robot24) {
        ok = send_robot24(image_fd, rows);
    } else if (is_bw12) {
        ok = send_bw12(image_fd, rows);
    } else if (is_robot36) {
        ok = send_robot36(image_fd, rows);
    } else if (is_robot72) {
        ok = send_robot72(image_fd, rows);
    } else if (is_pd50) {
        ok = send_pd50(image_fd, rows);
    } else if (is_pd90) {
        ok = send_pd90(image_fd, rows);
    } else if (is_pd160) {
        ok = send_pd160(image_fd, rows);
    } else if (is_pd180) {
        ok = send_pd180(image_fd, rows);
    } else if (is_pd240) {
        send(pd240_samples);
        ok = running;
    } else if (is_pd290) {
        send(pd290_samples);
        ok = running;
    } else if (is_p7) {
        send(p7_samples);
        ok = running;
    } else if (is_p3 || is_p5) {
        send(pasokon_samples);
        ok = running;
    } else if (is_pd120) {
        ok = send_pd120(image_fd, rows);
    } else {
        ok = send_martin_m1(image_fd, rows);
    }
    close(fd);
    delete modulator;
    modulator = nullptr;
    std::fprintf(stderr, ok ? "frame sent\n" : "stopped/short image\n");
    return ok ? 0 : 1;
}
