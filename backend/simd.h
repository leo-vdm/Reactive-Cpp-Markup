
#if !SIMD_HEADER
#define SIMD_HEADER 1

enum class SimdLevel
{
    NONE,
    SSE2,
    AVX2,
    AVX512,
    NEON,
};

extern unsigned int SIMD_WIDTH;
extern SimdLevel SUPPORTED_SIMD;

#if defined(__ARM_NEON)
    #include "third_party/sse2neon/sse2neon.h"
    #define CPU_ID(registers, function_id, sub_function) (void)0
    #define ARCH_NEON 1
#elif defined(__arm__) || defined(__i386__)
#elif defined(__ANDROID__) && defined(__x86_64__)
    #include <immintrin.h>
    #include <emmintrin.h>
    #include <xmmintrin.h>
    #define CPU_ID(registers, function_id, sub_function) {registers[0] = 0;registers[1] = 0;registers[2] = 0;registers[3] = 0;}
    #define ARCH_X64_SSE 1
#elif defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
    #include <intrin.h>
    #include <immintrin.h>
    #include <emmintrin.h>
    #include <xmmintrin.h>
    #define CPU_ID(registers, function_id, sub_function) __cpuidex(registers, function_id, sub_function) 
    #define ARCH_X64 1
#elif defined(__linux__) && !defined(_WIN32)
    #include <cpuid.h>
    #include <immintrin.h>
    #include <emmintrin.h>
    #include <xmmintrin.h>
    #define CPU_ID(registers, function_id, sub_function) __cpuidex(registers, function_id, sub_function)
    #define ARCH_X64 1
#endif


#if ARCH_X64 || ARCH_NEON || ARCH_X64_SSE
    // SSE2
    #define get_i8_128(A, index) _mm_extract_epi8(A, index)
    #define insert_i8_128(A, value, index) _mm_insert_epi8 (A, value, index)
    #define cmp_i8_128(A, B) _mm_cmpeq_epi8(A, B)
    #define set_i8_128(value) _mm_set1_epi8(value)
    #define add_i8_128(A, B) _mm_add_epi8(A, B)
    #define and_128(A, B) _mm_and_si128(A, B)
    #define or_128(A, B) _mm_or_si128(A, B)
    #define andnot_128(A, B) _mm_andnot_si128(A, B)
    #define load_i128(ptr) _mm_loadu_si128((i128*)(ptr))
    #define store_i128(A, ptr) _mm_storeu_si128((i128*)(ptr), A)
    #define lshift_i128(A, BYTES) _mm_bslli_si128(A, BYTES)
    #define rshift_i128(A, BYTES) _mm_bsrli_si128(A, BYTES)
    #define test_all_ones_i128(A) _mm_test_all_ones(A)
    typedef __m128i i128;

    // Note(Leo): Inserts value into the lanes in A where the value of the corresponding lane in mask == index
    #define dyn_insert_i8_128(A, mask, value, index) {  \
    i128 tmp_reg1 = set_i8_128(index);                  \
    tmp_reg1 = cmp_i8_128(tmp_reg1, mask);              \
    A = andnot_128(tmp_reg1, A);                        \
    i128 tmp_reg2 = set_i8_128(value);                  \
    tmp_reg2 = and_128(tmp_reg2, tmp_reg1);             \
    A = or_128(A, tmp_reg2);                            \
    }
#endif
#if ARCH_X64
    // AVX2
    #define get_i8_256(A, index) _mm256_extract_epi8(A, index)
    #define insert_i8_256(A, value, index) _mm256_insert_epi8(A, value, index)
    #define cmp_i8_256(A, B) _mm256_cmpeq_epi8(A, B)
    #define set_i8_256(value) _mm256_set1_epi8(value)
    #define add_i8_256(A, B) _mm256_add_epi8(A, B)
    #define and_256(A, B) _mm256_and_si256(A, B)
    #define or_256(A, B) _mm256_or_si256(A, B)
    #define xor_256(A, B) _mm256_xor_si256(A, B)
    #define andnot_256(A, B) _mm256_andnot_si256(A, B)
    #define load_i256(ptr) _mm256_loadu_si256((i256*)(ptr))
    #define store_i256(A, ptr) _mm256_storeu_si256 ((i256*)(ptr), A)
    #define lshift_i256(A, BYTES) _mm256_slli_si256(A, BYTES)
    #define rshift_i256(A, BYTES) _mm256_srli_si256(A, BYTES)
    #define test_equal_i256(A, B) _mm256_testc_si256 (A, B)
    typedef __m256i i256;
    
    // Note(Leo): Inserts value into the lanes in A where the value of the corresponding lane in mask == index
    #define dyn_insert_i8_256(A, mask, value, index) {  \
    i256 tmp_reg1 = set_i8_256(index);                  \
    tmp_reg1 = cmp_i8_256(tmp_reg1, mask);              \
    A = andnot_256(tmp_reg1, A);                        \
    i256 tmp_reg2 = set_i8_256(value);                  \
    tmp_reg2 = and_256(tmp_reg2, tmp_reg1);             \
    A = or_256(A, tmp_reg2);                            \
    }

#endif

#endif

#if SIMD_IMPLEMENTATION && !SIMD_INCLUDED
#define SIMD_INCLUDED 1

SimdLevel SUPPORTED_SIMD;
// Note(Leo): SIMD_WIDTH is the width of simd registers in 32 bit floats
//            So SSE2 which is 128 bits would be 4 wide
unsigned int SIMD_WIDTH;

void SimdDetectSupport()
{
    SIMD_WIDTH = 0;
    SUPPORTED_SIMD = SimdLevel::NONE;
    #if defined(ARCH_NEON)
        // Note(Leo): NEON is 128 bits wide 
        SUPPORTED_SIMD = SimdLevel::NEON;
        SIMD_WIDTH = 4;
    #elif defined(ARCH_X64) || defined(ARCH_X64_SSE)
        int regs[4];
        CPU_ID(regs, 7,0);
        
        if(regs[1] & (1 << 16))
        {
            SUPPORTED_SIMD = SimdLevel::AVX512;
            SIMD_WIDTH = 16;
        }
        else if(regs[1] & (1 << 5))
        {
            SUPPORTED_SIMD = SimdLevel::AVX2;
            SIMD_WIDTH = 8;
        }
        else
        {
            // all x86-64 CPUs have SSE2
            SUPPORTED_SIMD = SimdLevel::SSE2;
            SIMD_WIDTH = 4;
        }
    #else
        SUPPORTED_SIMD = SimdLevel::NONE;
        SIMD_WIDTH = 1;
    #endif
}
#endif
