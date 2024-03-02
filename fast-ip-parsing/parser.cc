// This algorithm is not my idea. The code here is me working through Jeroen Koekkoek's [1] and
// Daniel Lemire's [2] blog posts on fast parsing of IP addresses using SIMD.
//
// [1]: http://0x80.pl/notesen/2023-04-09-faster-parse-ipv4.html
// [2]: https://lemire.me/blog/2023/06/08/parsing-ip-addresses-crazily-fast

#ifdef __x86_64__
#include <immintrin.h>
#else
#include "../sse2neon/sse2neon.h"
#endif

#include <array>
#include <bit>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>

template <bool kUseChar = false>
void PrintXMM(const __m128i value) {
  alignas(16) int8_t v[16];
  _mm_store_si128(reinterpret_cast<__m128i*>(v), value);
  for (int i = 0; i < sizeof(value); ++i) {
    if constexpr (kUseChar) {
      std::cout << v[i] << " ";
    } else {
      printf("%x ", v[i]);
    }
  }
  std::cout << std::endl;
}

template <typename T>
void PrintBinary(const T value) requires std::is_integral_v<T> {
  constexpr size_t kNumBits = sizeof(T) * 8;
  std::bitset<kNumBits> b(value);
  std::cout << b << std::endl;
}

void PrintIPAddress(uint32_t address) {
  std::cout << (address & 0xFF) << '.' << ((address >> 8) & 0xFF) << '.' << ((address >> 16) & 0xFF)
            << '.' << ((address >> 24) & 0xFF) << std::endl;
}

void PrintIPAddress2(uint32_t address) {
  printf("%x.%x.%x.%x\n", (address & 0xFF), ((address >> 8) & 0xFF), ((address >> 16) & 0xFF),
         ((address >> 24) & 0xFF));
}

constexpr int kTwoTo16 = 65536;

consteval std::tuple<std::array<int8_t, kTwoTo16>, std::array<std::array<int8_t, 16>, 82>>
EvalMaskToId() {
  std::array<int8_t, kTwoTo16> mapping;
  std::array<std::array<int8_t, 16>, 82> patterns_builder;
  int8_t cur_id = 0;
  for (uint i = 0; i < kTwoTo16; ++i) {
    bool bailed = false;
    for (int x = 0; x < 16; ++x) patterns_builder[cur_id][x] = -1;
    if (std::popcount(i) != 4) {
      mapping[i] = -1;
      bailed = true;
      continue;
    }
    uint value = i;
    int previous_dot = -1;
    for (int count = 0; count < 4; ++count) {
      unsigned int dot_pos = std::countr_zero(value);
      unsigned int field_length = dot_pos - previous_dot - 1;
      if (field_length > 3 || field_length < 1) {
        mapping[i] = -1;
        bailed = true;
        break;
      }
      patterns_builder[cur_id][count] = previous_dot + 1;
      if (field_length > 1) {
        patterns_builder[cur_id][4 + count] = previous_dot + 2;
        patterns_builder[cur_id][12 + count] = previous_dot + 2;
      }
      if (field_length == 3) {
        patterns_builder[cur_id][8 + count] = previous_dot + 3;
        patterns_builder[cur_id][12 + count] = previous_dot + 3;
      }
      previous_dot = dot_pos;
      value -= 1 << dot_pos;
    }
    if (!bailed) {
      mapping[i] = cur_id++;
    }
  }
  return std::make_tuple(mapping, patterns_builder);
}

constexpr auto _kMaskEval = EvalMaskToId();
constexpr std::array<int8_t, kTwoTo16> kMaskToId = std::get<0>(_kMaskEval);
constexpr std::array<std::array<int8_t, 16>, 82> patterns = std::get<1>(_kMaskEval);

template <bool kDebug>
void Parse(std::string address) {
  const __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(address.data()));
  PrintXMM</*kUseChar=*/true>(input);

  const __m128i dots = _mm_set1_epi8('.');
  __m128i his_t0 = _mm_cmpeq_epi8(input, dots);
  uint16_t dotmask = _mm_movemask_epi8((_mm_cmpeq_epi8(input, dots)));
  PrintBinary(dotmask);

  const uint16_t mask = ~(0xffff << address.size());
  dotmask &= mask;
  PrintBinary(dotmask);

  const __m128i less_than_0 = _mm_set1_epi8('0' + std::numeric_limits<int8_t>::min());
  const __m128i greater_than_9 =
      _mm_set1_epi8('9' - '0' + 1 + (std::numeric_limits<int8_t>::min()));

  // Force ASCII values less than '0' to underflow.
  const __m128i t0 = _mm_sub_epi8(input, less_than_0);
  // Get the non-underflowed values that are less than '9'.
  const __m128i t1 = _mm_cmplt_epi8(t0, greater_than_9);
  // Check that every character is either a valid numerical value or a dot.
  uint16_t in_range = _mm_movemask_epi8(t1) & mask;
  uint16_t valid_positions = in_range | dotmask;
  if (valid_positions != mask) {
    std::cout << "Invalid IP address!" << std::endl;
    return;
  }

  const auto num_dots = __builtin_popcount(dotmask);

  if (num_dots != 3) {
    std::cout << ((num_dots > 3) ? "Invalid IP address: too many fields!"
                                 : "Invalid IP address: too few fields!")
              << std::endl;
    return;
  }

  std::cout << "special cases" << std::endl;

  // Mixed 1-byte, 2-byte case XX.X.XX.X.
  // Rearrange 12.3.45.6 into a 128-bit register storing '6''5''3''2''0''4''0''1'.
  const __m128i pattern = _mm_setr_epi8(0, -1, 5, -1, 1, 3, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1);
  const __m128i shufed = _mm_shuffle_epi8(input, pattern);
  PrintXMM</*kUseChar=*/false>(shufed);
  // Take the lower 64-bits and convert ascii numericals into integers.
  const uint64_t ascii = _mm_cvtsi128_si64(shufed);
  const uint64_t w01 = ascii & 0x0f0f0f0f0f0f0f0f;
  // Combine the lower and higher bits by multiplying the lower bits by 10.
  const uint32_t w0 = w01 >> 32;
  const uint32_t w1 = w01 & 0xffffffff;
  PrintIPAddress(10 * w1 + w0);

  return;
  // Special 1-byte fields case.
  // 1.1.1.1
  // '1''1''1''1' & 0x0F0F0F0F
  // 1111
  // const __m128i pattern =
  //   _mm_setr_epi8(0, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  // const __m128i his_t1 = _mm_shuffle_epi8(input, pattern);
  // PrintXMM(input);
  // PrintIPAddress2(_mm_cvtsi128_si32(his_t1) & 0x0f0f0f0f);
  // PrintIPAddress2(_mm_cvtsi128_si32(his_t1));
  // PrintXMM(his_t1);
}

int main(int argc, char* argv[]) {
  for (int i = 0; i < argc; ++i) {
    if (!strcmp(argv[i], "--debug")) {
      std::cout << "whee" << std::endl;
    }
  }
  std::string address;
  while (std::cin >> address) {
    Parse</*kDebug=*/false>(address);
  }
  return 0;
}
