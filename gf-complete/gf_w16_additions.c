
int cpu_detect_run = 0;
int has_ssse3 = 0;
size_t has_slow_shuffle = 0;
int has_pclmul = 0;
int has_avx2 = 0;
int has_avxslow = 0;
int has_avx512bw = 0;
int has_gfni = 0;
int has_htt = 0;

int has_neon = 0;

#if !defined(_MSC_VER) && defined(INTEL_SSE2)
#include <cpuid.h>
#endif

#ifdef _MSC_VER
	#define _cpuid __cpuid
	#define _cpuidX __cpuidex
#else
	/* GCC seems to support this, I assume everyone else does too? */
	#define _cpuid(ar, eax) __cpuid(eax, ar[0], ar[1], ar[2], ar[3])
	#define _cpuidX(ar, eax, ecx) __cpuid_count(eax, ecx, ar[0], ar[1], ar[2], ar[3])
#endif

#ifdef ARM_NEON
# ifdef __ANDROID__
#  include <cpu-features.h>
# elif defined(__linux__)
#  include <sys/auxv.h>
#  include <asm/hwcap.h>
# endif
#endif

void detect_cpu(void) {
	if(cpu_detect_run) return;
	cpu_detect_run = 1;
#ifdef INTEL_SSE2 /* if we can't compile SSE, there's not much point in checking CPU capabilities; we use this to eliminate ARM :P */
	int cpuInfo[4];
	int family, model, hasMulticore;
	_cpuid(cpuInfo, 1);
	hasMulticore = (cpuInfo[3] & (1<<28));
	#ifdef INTEL_SSSE3
	has_ssse3 = (cpuInfo[2] & 0x200);
	#endif
	#ifdef INTEL_SSE4_PCLMUL
	has_pclmul = (cpuInfo[2] & 0x2);
	#endif
	
	family = ((cpuInfo[0]>>8) & 0xf) + ((cpuInfo[0]>>20) & 0xff);
	model = ((cpuInfo[0]>>4) & 0xf) + ((cpuInfo[0]>>12) & 0xf0);
	
	has_slow_shuffle = 131072; // it seems like XOR JIT is always faster than shuffle at ~128KB sizes
	
	if(family == 6) {
		/* from handy table at http://a4lg.com/tech/x86/database/x86-families-and-models.en.html */
		if(model == 0x1C || model == 0x26 || model == 0x27 || model == 0x35 || model == 0x36 || model == 0x37 || model == 0x4A || model == 0x4D) {
			/* we have a Bonnell/Silvermont CPU with a really slow pshufb instruction; pretend SSSE3 doesn't exist, as XOR_DEPENDS is much faster */
			has_slow_shuffle = 2048;
		}
		if(model == 0x0F || model == 0x16) {
			/* Conroe CPU with relatively slow pshufb; pretend SSSE3 doesn't exist, as XOR_DEPENDS is generally faster */
			has_slow_shuffle = 16384;
		}
	}
	
	has_avxslow = (family == 0x6f || family == 0x7f || family == 0x8f); // AMD CPUs currently have 128b FPUs

#if !defined(_MSC_VER) || _MSC_VER >= 1600
	_cpuidX(cpuInfo, 7, 0);
	
	#ifdef INTEL_AVX2
	has_avx2 = (cpuInfo[1] & 0x20);
	#endif
	#ifdef INTEL_AVX512BW
	has_avx512bw = (cpuInfo[1] & 0x40010000) == 0x40010000;
	#endif
	#ifdef INTEL_GFNI
	has_gfni = (cpuInfo[2] & 0x100) == 0x100;
	#endif
#endif

	/* try to detect hyper-threading */
	has_htt = 0;
	if(hasMulticore) {
		/* only Intel CPUs have HT (VMs which obscure CPUID -> too bad); we won't include AMD Zen here */
		_cpuid(cpuInfo, 0);
		if(cpuInfo[1] == 0x756E6547 && cpuInfo[2] == 0x6C65746E && cpuInfo[3] == 0x49656E69 && cpuInfo[0] >= 11) {
			_cpuidX(cpuInfo, 11, 0);
			if(((cpuInfo[2] >> 8) & 0xFF) == 1 // SMT level
			&& (cpuInfo[1] & 0xFFFF) > 1) // multiple threads per core
				has_htt = 1;
		}
	}
#endif /* INTEL_SSE2 */
	
#ifdef ARM_NEON
# if defined(AT_HWCAP)
#  ifdef ARCH_AARCH64
	has_neon = getauxval(AT_HWCAP) & HWCAP_ASIMD;
#  else
	has_neon = getauxval(AT_HWCAP) & HWCAP_NEON;
#  endif
# elif defined(ANDROID_CPU_FAMILY_ARM)
#  ifdef ARCH_AARCH64
	has_neon = android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_ASIMD;
#  else
	has_neon = android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON;
#  endif
# endif
#endif
}



/* refers to log_val, ltd and xor */
/* TODO: are we going to make use of these? */
#define _GF_W16_LOG_MULTIPLY_REGION(op, src, dest, srcto) { \
  uint16_t *s16 = (uint16_t *)src, *d16 = (uint16_t *)dest; \
  while (s16 < (uint16_t *)(srcto)) { \
    *d16 op (*s16 == 0) ? 0 : GF_ANTILOG((int) ltd->log_tbl[*s16] + log_val); \
    s16++; \
    d16++; \
  } \
}
#define GF_W16_LOG_MULTIPLY_REGION(src, dest, srcto) \
  if(xor) _GF_W16_LOG_MULTIPLY_REGION(^=, src, dest, srcto) \
  else _GF_W16_LOG_MULTIPLY_REGION(=, src, dest, srcto)


#include "gf_w16/xor.h"
#include "gf_w16/affine.h"
#include "gf_w16/shuffle.h"

