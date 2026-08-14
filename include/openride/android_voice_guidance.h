#ifndef OPENRIDE_ANDROID_VOICE_GUIDANCE_H
#define OPENRIDE_ANDROID_VOICE_GUIDANCE_H

#include "openride/voice_guidance.h"

#include <stdbool.h>

bool openride_android_voice_guidance_init(void);
bool openride_android_voice_guidance_ready(void);
bool openride_android_voice_guidance_speak(const char *text, bool flush);
void openride_android_voice_guidance_stop(void);
void openride_android_voice_guidance_shutdown(void);

OpenRideVoiceGuidanceBackend openride_android_voice_guidance_backend(void);

#endif
