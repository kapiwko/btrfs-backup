// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <config/ProfileFingerprint.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <core/Errors.hpp>

namespace fs = std::filesystem;

namespace {

constexpr std::array<std::uint32_t, 64> sha256_k = {
    0x428a2f98U,
    0x71374491U,
    0xb5c0fbcfU,
    0xe9b5dba5U,
    0x3956c25bU,
    0x59f111f1U,
    0x923f82a4U,
    0xab1c5ed5U,
    0xd807aa98U,
    0x12835b01U,
    0x243185beU,
    0x550c7dc3U,
    0x72be5d74U,
    0x80deb1feU,
    0x9bdc06a7U,
    0xc19bf174U,
    0xe49b69c1U,
    0xefbe4786U,
    0x0fc19dc6U,
    0x240ca1ccU,
    0x2de92c6fU,
    0x4a7484aaU,
    0x5cb0a9dcU,
    0x76f988daU,
    0x983e5152U,
    0xa831c66dU,
    0xb00327c8U,
    0xbf597fc7U,
    0xc6e00bf3U,
    0xd5a79147U,
    0x06ca6351U,
    0x14292967U,
    0x27b70a85U,
    0x2e1b2138U,
    0x4d2c6dfcU,
    0x53380d13U,
    0x650a7354U,
    0x766a0abbU,
    0x81c2c92eU,
    0x92722c85U,
    0xa2bfe8a1U,
    0xa81a664bU,
    0xc24b8b70U,
    0xc76c51a3U,
    0xd192e819U,
    0xd6990624U,
    0xf40e3585U,
    0x106aa070U,
    0x19a4c116U,
    0x1e376c08U,
    0x2748774cU,
    0x34b0bcb5U,
    0x391c0cb3U,
    0x4ed8aa4aU,
    0x5b9cca4fU,
    0x682e6ff3U,
    0x748f82eeU,
    0x78a5636fU,
    0x84c87814U,
    0x8cc70208U,
    0x90befffaU,
    0xa4506cebU,
    0xbef9a3f7U,
    0xc67178f2U,
};

std::uint32_t rotr(std::uint32_t value, unsigned shift) {
    return (value >> shift) | (value << (32U - shift));
}

std::uint32_t load_be32(const unsigned char* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) | (static_cast<std::uint32_t>(data[1]) << 16U) | (static_cast<std::uint32_t>(data[2]) << 8U) | static_cast<std::uint32_t>(data[3]);
}

void store_be64(std::vector<unsigned char>& data, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        data.push_back(static_cast<unsigned char>((value >> shift) & 0xffU));
    }
}

void store_hex32(std::string& output, std::uint32_t value) {
    constexpr char digits[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        output.push_back(digits[(value >> shift) & 0x0fU]);
    }
}

std::string read_file_bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw btrfsbackup::ValidationError("cannot read " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void append_record(std::string& data, const std::string& key, const std::string& value) {
    data.append(key);
    data.push_back('=');
    data.append(value);
    data.push_back('\0');
}

std::string sha256_bytes(const std::string& data) {
    std::vector<unsigned char> padded(data.begin(), data.end());
    std::uint64_t bit_size = static_cast<std::uint64_t>(padded.size()) * 8U;
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U) {
        padded.push_back(0U);
    }
    store_be64(padded, bit_size);

    std::array<std::uint32_t, 8> h = {
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };

    for (std::size_t offset = 0; offset < padded.size(); offset += 64U) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16U; ++i) {
            w[i] = load_be32(padded.data() + offset + (i * 4U));
        }
        for (std::size_t i = 16U; i < 64U; ++i) {
            std::uint32_t s0 = rotr(w[i - 15U], 7U) ^ rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
            std::uint32_t s1 = rotr(w[i - 2U], 17U) ^ rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
            w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
        }

        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        std::uint32_t f = h[5];
        std::uint32_t g = h[6];
        std::uint32_t hh = h[7];

        for (std::size_t i = 0; i < 64U; ++i) {
            std::uint32_t s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
            std::uint32_t ch = (e & f) ^ ((~e) & g);
            std::uint32_t temp1 = hh + s1 + ch + sha256_k[i] + w[i];
            std::uint32_t s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = s0 + maj;

            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::string digest;
    digest.reserve(64);
    for (std::uint32_t value : h) {
        store_hex32(digest, value);
    }
    return digest;
}

} // namespace

namespace btrfsbackup::config {

std::string compute_config_fingerprint_from_bytes(
    std::string_view version,
    const fs::path& config_file,
    std::string_view contents
) {
    std::string data;
    append_record(data, "version", std::string(version));
    append_record(data, "main", config_file.filename().string());
    data.append(contents);
    data.push_back('\0');
    return sha256_bytes(data);
}

std::string compute_config_fingerprint(
    const std::string& version,
    const fs::path& config_file,
    const std::vector<fs::path>& source_files
) {
    std::string data;
    append_record(data, "version", version);
    append_record(data, "main", config_file.filename().string());
    data.append(read_file_bytes(config_file));
    data.push_back('\0');

    for (const fs::path& source_file : source_files) {
        append_record(data, "source", source_file.filename().string());
        data.append(read_file_bytes(source_file));
        data.push_back('\0');
    }

    return sha256_bytes(data);
}

} // namespace btrfsbackup::config
