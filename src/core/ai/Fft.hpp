#pragma once

// Port of Voice CTX's Fft.cs (MIT): in-place iterative radix-2 complex FFT
// with precomputed twiddles. Allocation-free after construction.

#include <cmath>
#include <cstdint>
#include <vector>

namespace vd {

class Fft {
public:
    explicit Fft(int size) : size_(size) {
        bits_ = 0;
        while ((std::size_t(1) << (bits_ + 1)) <= std::size_t(size)) ++bits_;
        bitReverse_.resize(std::size_t(size));
        for (int i = 0; i < size; ++i) {
            int r = 0;
            for (int b = 0; b < bits_; ++b) r |= ((i >> b) & 1) << (bits_ - 1 - b);
            bitReverse_[std::size_t(i)] = r;
        }
        cos_.resize(std::size_t(size / 2));
        sin_.resize(std::size_t(size / 2));
        for (int i = 0; i < size / 2; ++i) {
            cos_[std::size_t(i)] = std::cos(2.0 * kPi * i / size);
            sin_[std::size_t(i)] = -std::sin(2.0 * kPi * i / size);
        }
    }

    int size() const { return size_; }

    void forward(double* re, double* im) {
        for (int i = 0; i < size_; ++i) {
            const int j = bitReverse_[std::size_t(i)];
            if (j > i) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        for (int len = 2; len <= size_; len <<= 1) {
            const int half = len >> 1;
            const int step = size_ / len;
            for (int start = 0; start < size_; start += len) {
                for (int k = 0; k < half; ++k) {
                    const double wr = cos_[std::size_t(k * step)];
                    const double wi = sin_[std::size_t(k * step)];
                    const int a = start + k;
                    const int b = a + half;
                    const double tr = re[b] * wr - im[b] * wi;
                    const double ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    int size_ = 0;
    int bits_ = 0;
    std::vector<int> bitReverse_;
    std::vector<double> cos_;
    std::vector<double> sin_;
};

} // namespace vd
