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

template <bool kUseChar = false, typename kType = int8_t>
void PrintXMM(const __m128i value) {
  size_t kSize = sizeof(value) / sizeof(kType);
  alignas(16) kType v[kSize];
  _mm_store_si128(reinterpret_cast<__m128i*>(v), value);
  for (int i = 0; i < kSize; ++i) {
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

constexpr int kMaxUInt16 = std::numeric_limits<uint16_t>::max();
constexpr int kMaxIPLength = 16;
constexpr int kMaxDotmasks = 81;

consteval std::tuple<std::array<int8_t, kMaxUInt16>,
                     std::array<std::array<int8_t, kMaxIPLength>, kMaxDotmasks + 1>>
EvalMaskToId() {
  std::array<int8_t, kMaxUInt16> mapping;
  std::array<std::array<int8_t, kMaxIPLength>, kMaxDotmasks + 1> patterns_builder;
  int8_t cur_id = 0;
  for (uint i = 0; i < kMaxUInt16; ++i) {
    bool bailed = false;
    for (int x = 0; x < kMaxIPLength; ++x) patterns_builder[cur_id][x] = -1;
    if (std::popcount(i) != 4) {
      mapping[i] = -1;
      bailed = true;
      continue;
    }
    uint value = i;
    int previous_dot = -1;
    for (int count = 0; count < 4 && !bailed; ++count) {
      unsigned int dot_pos = std::countr_zero(value);
      unsigned int field_length = dot_pos - previous_dot - 1;
      switch (field_length) {
        case 1:
          patterns_builder[cur_id][count * 2] = previous_dot + 1;
          break;
        case 2:
          patterns_builder[cur_id][count * 2] = previous_dot + 2;
          patterns_builder[cur_id][count * 2 + 1] = previous_dot + 1;
          patterns_builder[cur_id][12 + count] = previous_dot + 1;
          break;
        case 3:
          patterns_builder[cur_id][count * 2] = previous_dot + 3;
          patterns_builder[cur_id][count * 2 + 1] = previous_dot + 2;
          patterns_builder[cur_id][8 + count * 2] = previous_dot + 1;
          patterns_builder[cur_id][8 + count * 2 + 1] = previous_dot + 1;
          break;
        default:
          mapping[i] = -1;
          bailed = true;
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
constexpr std::array<int8_t, kMaxUInt16> kMaskToId = std::get<0>(_kMaskEval);
constexpr std::array<std::array<int8_t, kMaxIPLength>, kMaxDotmasks + 1> kPatterns =
    std::get<1>(_kMaskEval);

void Parse(std::string address) {
  const __m128i input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(address.data()));
#ifdef _DEBUG
  PrintXMM</*kUseChar=*/true>(input);
#endif

  uint16_t dotmask;
  {
    const __m128i dots = _mm_set1_epi8('.');
    dotmask = _mm_movemask_epi8((_mm_cmpeq_epi8(input, dots)));
    const uint16_t end_pos = 1 << address.size();
    dotmask = (dotmask & (end_pos - 1)) + end_pos;
  }
  const int maskId = kMaskToId[dotmask];
  if (maskId < 0) {
    std::cout << "Error: Invalid IP Address." << std::endl;
    return;
  }
  const __m128i pattern =
      _mm_loadu_si128(reinterpret_cast<const __m128i*>(kPatterns[maskId].data()));

  const __m128i ascii0 = _mm_set1_epi8('0');
  const __m128i shuffled_input = _mm_shuffle_epi8(input, pattern);
  const __m128i normalized_input = _mm_subs_epu8(shuffled_input, ascii0);

  const __m128i weights = _mm_setr_epi8(1, 10, 1, 10, 1, 10, 1, 10, 100, 0, 100, 0, 100, 0, 100, 0);
  const __m128i multiplied_input = _mm_maddubs_epi16(normalized_input, weights);
  const __m128i t5 = _mm_alignr_epi8(multiplied_input, multiplied_input, 8);
  const __m128i t6 = _mm_add_epi16(multiplied_input, t5);
  const __m128i output = _mm_packus_epi16(t6, t6);
  PrintIPAddress(_mm_cvtsi128_si32(output));
}

int main(int argc, char* argv[]) {
  std::string address;
  while (std::cin >> address) {
    Parse(address);
  }
  return 0;
}
