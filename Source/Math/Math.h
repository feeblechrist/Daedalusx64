#ifndef DAEDMATHS_H__
#define DAEDMATHS_H__

#include <math.h>

// VFPU Math :D
//
// Todo : Move to SysPSP ?
//
// Note : Matrix math => check Matrix4x4.cpp
//
#define PI   3.141592653589793f
//
inline float vfpu_invSqrt(float x)
{
	float result;
	
	// return 1.0f/sqrtf(x);
	
	__asm__ volatile (
		"mtv		%0, S000\n"
		"vrsq.s		S000, S000\n"
		"mfv		%0, S000\n"
	: "=r"(result): "r"(x));
	return result;
}

inline float vfpu_sqrtf(float x) {
	float result;
	__asm__ volatile (
		"mtv     %1, S000\n"
		"vsqrt.s S000, S000\n"
		"mfv     %0, S000\n"
	: "=r"(result) : "r"(x));
	return result;
}

//Below Function taken from PGE - Phoenix Game Engine - Greets InsertWittyName !
inline float vfpu_abs(float x) {
    float result;

	__asm__ volatile (
		"mtv      %1, S000\n"
		"vabs.s   S000, S000\n"
		"mfv      %0, S000\n"
	: "=r"(result) : "r"(x));

	return result;
}

inline float vfpu_cosf(float rad) {
    float result;
    __asm__ volatile (
        "mtv     %1, S000\n"
        "vcst.s  S001, VFPU_2_PI\n"
        "vmul.s  S000, S000, S001\n"
        "vcos.s  S000, S000\n"
        "mfv     %0, S000\n"
        : "=r"(result) : "r"(rad));
    return result;
}

inline float vfpu_sinf(float rad) {
    float result;
    __asm__ volatile (
        "mtv     %1, S000\n"
        "vcst.s  S001, VFPU_2_PI\n"
        "vmul.s  S000, S000, S001\n"
        "vsin.s  S000, S000\n"
        "mfv     %0, S000\n"
        : "=r"(result) : "r"(rad));
    return result;
}

//Below function taken from PGE
inline float vfpu_round(float x)
{
	float result;

	__asm__ volatile (
		"mtv      %1, S000\n"
		"vf2in.s  S000, S000, 0\n"
		"vi2f.s	  S000, S000, 0\n"
		"mfv      %0, S000\n"
	: "=r"(result) : "r"(x));
	
	return result;
}

inline float vfpu_fmaxf(float x, float y) {
	float result;
	__asm__ volatile (
		"mtv      %1, S000\n"
		"mtv      %2, S001\n"
		"vmax.s   S002, S000, S001\n"
		"mfv      %0, S002\n"
	: "=r"(result) : "r"(x), "r"(y));
	return result;
}

inline float vfpu_fminf(float x, float y) {
	float result;
	__asm__ volatile (
		"mtv      %1, S000\n"
		"mtv      %2, S001\n"
		"vmin.s   S002, S000, S001\n"
		"mfv      %0, S002\n"
	: "=r"(result) : "r"(x), "r"(y));
	return result;
}

inline float vfpu_powf(float x, float y) {
	float result;
	// result = exp2f(y * log2f(x));
	__asm__ volatile (
		"mtv      %1, S000\n"
		"mtv      %2, S001\n"
		"vlog2.s  S001, S001\n"
		"vmul.s   S000, S000, S001\n"
		"vexp2.s  S000, S000\n"
		"mfv      %0, S000\n"
	: "=r"(result) : "r"(x), "r"(y));
	return result;
}

#endif // DAEDMATHS_H__
