#include "qmg/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace qmg {
namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t rotate_right(const std::uint32_t value, const unsigned amount) {
    return (value >> amount) | (value << (32U - amount));
}

class Sha256State {
  public:
    void update(const char *data, const std::size_t size) {
        if (finalized_)
            throw std::logic_error("cannot update a finalized SHA-256 digest");
        constexpr std::uint64_t maximum_bytes = std::numeric_limits<std::uint64_t>::max() / 8U;
        if (byte_count_ > maximum_bytes || size > maximum_bytes - byte_count_)
            throw std::overflow_error("SHA-256 input is too large");
        byte_count_ += static_cast<std::uint64_t>(size);
        std::size_t consumed{};
        while (consumed < size) {
            const std::size_t copied = std::min(block_.size() - block_size_, size - consumed);
            for (std::size_t index = 0; index < copied; ++index)
                block_[block_size_ + index] = static_cast<std::uint8_t>(data[consumed + index]);
            block_size_ += copied;
            consumed += copied;
            if (block_size_ == block_.size()) {
                transform(block_);
                block_size_ = 0;
            }
        }
    }

    [[nodiscard]] std::string finish() {
        if (finalized_)
            throw std::logic_error("SHA-256 digest was already finalized");
        const std::uint64_t bit_count = byte_count_ * 8U;
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56) {
            while (block_size_ < block_.size())
                block_[block_size_++] = 0;
            transform(block_);
            block_size_ = 0;
        }
        while (block_size_ < 56)
            block_[block_size_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8)
            block_[block_size_++] = static_cast<std::uint8_t>((bit_count >> static_cast<unsigned>(shift)) & 0xFFU);
        transform(block_);
        finalized_ = true;

        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::hex << std::setfill('0');
        for (const std::uint32_t word : state_)
            output << std::setw(8) << word;
        return output.str();
    }

  private:
    void transform(const std::array<std::uint8_t, 64> &block) {
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t offset = index * 4;
            schedule[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                              (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
                              (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
                              static_cast<std::uint32_t>(block[offset + 3]);
        }
        for (std::size_t index = 16; index < schedule.size(); ++index) {
            const std::uint32_t x = schedule[index - 15];
            const std::uint32_t y = schedule[index - 2];
            const std::uint32_t sigma0 = rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3U);
            const std::uint32_t sigma1 = rotate_right(y, 17) ^ rotate_right(y, 19) ^ (y >> 10U);
            schedule[index] = schedule[index - 16] + sigma0 + schedule[index - 7] + sigma1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t index = 0; index < schedule.size(); ++index) {
            const std::uint32_t big_sigma1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            const std::uint32_t choose = (e & f) ^ ((~e) & g);
            const std::uint32_t temporary1 = h + big_sigma1 + choose + round_constants[index] + schedule[index];
            const std::uint32_t big_sigma0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temporary2 = big_sigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_{};
    std::uint64_t byte_count_{};
    bool finalized_{};
};

} // namespace

std::string sha256(const std::string_view input) {
    Sha256State state;
    state.update(input.data(), input.size());
    return state.finish();
}

std::string sha256_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open file for SHA-256: " + path.string());
    Sha256State state;
    std::vector<char> buffer(1024 * 1024);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0)
            state.update(buffer.data(), static_cast<std::size_t>(count));
    }
    if (!input.eof())
        throw std::runtime_error("failed while hashing file: " + path.string());
    return state.finish();
}

} // namespace qmg
