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

static SimdLevel SUPPORTED_SIMD;
// Note(Leo): SIMD_WIDTH is the width of simd registers in 32 bit floats
//            So SSE2 which is 128 bits would be 4 wide
static unsigned int SIMD_WIDTH;

#if defined(__ARM_NEON)
    #define CPU_ID(registers, function_id, sub_function) (void)0
    #define ARCH_NEON 1
#elif defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
    #include <intrin.h>
    #define CPU_ID(registers, function_id, sub_function) __cpuidex(registers, function_id, sub_function) 
    #define ARCH_X64 1
#elif defined(__linux__) && !defined(_WIN32)
    #include <cpuid.h>
    #define CPU_ID(registers, function_id, sub_function) __cpuidex(registers, function_id, sub_function)
    #define ARCH_X64 1
#endif
#endif

#if SIMD_IMPLEMENTATION && !SIMD_INCLUDED
#define SIMD_INCLUDED 1
void SimdDetectSupport()
{
    SIMD_WIDTH = 0;
    SUPPORTED_SIMD = SimdLevel::NONE;
    #if defined(ARCH_NEON)
        // Note(Leo): NEON is 128 bits wide 
        SUPPORTED_SIMD = SimdLevel::NEON;
        SIMD_WIDTH = 4;
    #elif defined(ARCH_X64)
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
