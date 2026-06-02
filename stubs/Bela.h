#pragma once

// Local IntelliSense fallback for machines without Bela headers installed.
// Real Bela builds use <Bela.h> on target and ignore this file.

#include <cstdint>

struct BelaContext {
	unsigned int audioFrames = 0;
	unsigned int audioSampleRate = 48000;
	const char* projectName = "kosc";
};

inline float audioRead(BelaContext*, unsigned int, unsigned int) { return 0.0f; }
inline void audioWrite(BelaContext*, unsigned int, unsigned int, float) {}
inline int rt_printf(const char*, ...) { return 0; }
