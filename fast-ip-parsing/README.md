# Fast IP Parsing

[Jeroen Koekkoek](http://0x80.pl/notesen/2023-04-09-faster-parse-ipv4.html) and [Daniel Lemire](https://lemire.me/blog/2023/06/08/parsing-ip-addresses-crazily-fast) wrote some mind-expanding blog posts on fast parsing of IP addresses using [SIMD](https://en.wikipedia.org/wiki/Single_instruction,_multiple_data). Although SIMD is directly relevant to my work, I mostly rely on auto-vectorization to speed up computation. I hadn't hand-rolled SIMD before, hence the choice of going through these blog posts.

The blog posts were not immediately readable to me due to my lack of knowledge about SIMD intrinsics. So here's a quick primer on the intrinsics used and what they might mean.

## Terms/functions

- `i8` - 8-bit integer
- `pu` - packed unsigned integer, targetting unsigned operations
- `pi` - packed (signed) integer, targetting [64-bit MMX registers](https://en.wikipedia.org/wiki/MMX_(instruction_set))
- `epi` - _extended_ packed (signed) integer, targetting [128-bit XMM registers](https://en.wikipedia.org/wiki/Streaming_SIMD_Extensions#Registers)
- `_mm_` - potentially MMX or Multi Media eXtensions. Officially MMX does not stand for anything, supposedly to allow intel to tradmark it.
- `__m128i` - 
- `_mm_loadu_si128` - 
- `_mm_store_si128` - 
- `_mm_set1_epi8` - 
- `_mm_setr_epi8` - 
- `_mm_movemask_epi8` - 
- `_mm_cmpeq_epi8` - 
- `_mm_sub_epi8` - 
- `_mm_cmplt_epi8` - 
- `_mm_cvtsi128_si32` - copy the lower 32-bit integer from a packed 128-bit integer. This is useful because IPv4 address are 32-bit integers!
- `_mm_shuffle_epi8`/`PSHUFB` - [packed shuffle bytes](https://www.felixcloutier.com/x86/pshufb). This one is a bit involved. More explanation below.

### PSHUFB

Permutes the bytes in a packed integer (`__m128i input`) given a packed "shuffle mask" (`__m128i mask`). For each `i8` in the result, take its index, use the value in the shuffle mask to get another index. This index tells us which `i8` value in the input to place into the result.

For example, if `mask.m128i_i8[a] = x`, then in the returned `__m128i` value (`result`), `result.m128i_i8[a]` has the value of `input.m128_i8[x]`.

There is a special case where if the first bit of an entry in the mask is set to 1 (i.e. the `i8` value is < 0), then set the corresponding entry in the result to 0.

I found [Microsoft Learn's example](https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2010/bb531427(v=vs.100)) pretty elucidating:
 
```
#include <stdio.h>
#include <tmmintrin.h>

int main ()
{
    __m128i a, mask;

    a.m128i_i8[0] = 1;
    a.m128i_i8[1] = 2;
    a.m128i_i8[2] = 4;
    a.m128i_i8[3] = 8;
    a.m128i_i8[4] = 16;
    a.m128i_i8[5] = 32;
    a.m128i_i8[6] = 64;
    a.m128i_i8[7] = 127;
    a.m128i_i8[8] = -2;
    a.m128i_i8[9] = -4;
    a.m128i_i8[10] = -8;
    a.m128i_i8[11] = -16;
    a.m128i_i8[12] = -32;
    a.m128i_i8[13] = -64;
    a.m128i_i8[14] = -128;
    a.m128i_i8[15] = -1;

    mask.m128i_u8[0] = 0x8F;
    mask.m128i_u8[1] = 0x0E;
    mask.m128i_u8[2] = 0x8D;
    mask.m128i_u8[3] = 0x0C;
    mask.m128i_u8[4] = 0x8B;
    mask.m128i_u8[5] = 0x0A;
    mask.m128i_u8[6] = 0x89;
    mask.m128i_u8[7] = 0x08;
    mask.m128i_u8[8] = 0x87;
    mask.m128i_u8[9] = 0x06;
    mask.m128i_u8[10] = 0x85;
    mask.m128i_u8[11] = 0x04;
    mask.m128i_u8[12] = 0x83;
    mask.m128i_u8[13] = 0x02;
    mask.m128i_u8[14] = 0x81;
    mask.m128i_u8[15] = 0x00;

    __m128i res = _mm_shuffle_epi8(a, mask);

    printf_s("Result res:\t%2d\t%2d\t%2d\t%2d\n\t\t%2d\t%2d\t%2d\t%2d\n",
                res.m128i_i8[0], res.m128i_i8[1], res.m128i_i8[2], 
                res.m128i_i8[3], res.m128i_i8[4], res.m128i_i8[5], 
                res.m128i_i8[6], res.m128i_i8[7]);
    printf_s("\t\t%2d\t%2d\t%2d\t%2d\n\t\t%2d\t%2d\t%2d\t%2d\n",
                res.m128i_i8[8],  res.m128i_i8[9], res.m128i_i8[10], 
                res.m128i_i8[11], res.m128i_i8[12], res.m128i_i8[13], 
                res.m128i_i8[14], res.m128i_i8[15]);

    return 0;
}
```
 
```
Result res:      0      -128     0      -32
                 0      -8       0      -2
                 0      64       0      16
                 0       4       0       1
```
 

## Other C++ tidbits I learned during this process

- `alignas` - 
- `__builtin_popcount` - 
- `__builtin_ctz` - 

## The parsing algorithm

Both Jeroen and Daniel summarise the algorithm pretty well in their blog posts. But to check my understanding, this is my summary:

1. Find the positions of dots in the IP address string.
2. Turn this information on positions into a bitmask that we can operate quickly on. Since IP addresses are at most 15 characters long (the largest possible IP address is 255.255.255.255), we know that this will fit into an 16-bit unsigned integer.
3. Hash the bitmask to go to specialised subroutine for parsing the different patterns of dots (we notice that there are only 81 possible combinations of dot positions). The bitmask is fast to operate on because we can quickly find the length of a field by using `__builtin_ctz` to count the number of trailing 0s, then bit shifting by the length of the field plus the dot to move to the next field.
4. Within the specialised subroutine, validate the field (<= 255 and no leading 0s) then set the byte representing the value of the field.

## Reproducing the code

The algorithm described above is reproduced in `parser.cc`. You can run the code as follows:

```
; clang++ parser.cc -std=c++20 -O3 -march=native -o parser
; ./parser
<ip-address>
<ip-address>
...
```

You can also pass in a `--debug` flag to enable debugging output.

## Benchmarking

TODO

## Various Sources

[Intel Intrinsics Guide](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)
[Stack Overflow - What are the names and meanings of the intrinsic vector element types, like epi64x or pi32?](https://stackoverflow.com/questions/70911872/what-are-the-names-and-meanings-of-the-intrinsic-vector-element-types-like-epi6)
[Stack Overflow - In SIMD, SSE2，many instructions named as "_mm_set_epi8"，"_mm_cmpgt_epi8 " and so on，what does "mm" "epi" mean?](https://stackoverflow.com/questions/74831784/in-simd-sse2-many-instructions-named-as-mm-set-epi8-mm-cmpgt-epi8-and-so)
