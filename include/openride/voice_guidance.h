#ifndef OPENRIDE_VOICE_GUIDANCE_H
#define OPENRIDE_VOICE_GUIDANCE_H

#include "openride/navigation_engine.h"
#include "openride/navigation_instructions.h"

#include <stdbool.h>
#include <stdint.h>

typedef bool (*OpenRideVoiceGuidanceReadyFn)(void *userdata);
typedef bool (*OpenRideVoiceGuidanceSpeakFn)(void *userdata,
                                             const char *text,
                                             bool flush);
typedef void (*OpenRideVoiceGuidanceStopFn)(void *userdata);

typedef struct OpenRideVoiceGuidanceBackend {
    void *userdata;
    OpenRideVoiceGuidanceReadyFn ready;
    OpenRideVoiceGuidanceSpeakFn speak;
    OpenRideVoiceGuidanceStopFn stop;
} OpenRideVoiceGuidanceBackend;

typedef struct OpenRideVoiceGuidance {
    OpenRideVoiceGuidanceBackend backend;
    bool enabled;
    bool initial_instruction_announced;
    bool has_instruction;
    uint32_t instruction_index;
    bool announced_far;
    bool announced_near;
    bool announced_now;
    bool off_route_announced;
    bool arrival_announced;
} OpenRideVoiceGuidance;

void openride_voice_guidance_init(OpenRideVoiceGuidance *voice);
void openride_voice_guidance_set_backend(OpenRideVoiceGuidance *voice,
                                         OpenRideVoiceGuidanceBackend backend);
void openride_voice_guidance_set_enabled(OpenRideVoiceGuidance *voice,
                                         bool enabled);
bool openride_voice_guidance_enabled(const OpenRideVoiceGuidance *voice);

void openride_voice_guidance_reset(OpenRideVoiceGuidance *voice);

bool openride_voice_guidance_update(
    OpenRideVoiceGuidance *voice,
    const OpenRideNavigationInstructionList *instructions,
    const OpenRideNavigationState *navigation);

bool openride_voice_guidance_announce_reroute(OpenRideVoiceGuidance *voice);

#endif
