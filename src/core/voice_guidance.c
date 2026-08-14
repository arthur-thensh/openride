#include "openride/voice_guidance.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static double clampd(double value, double minimum, double maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static bool backend_ready(const OpenRideVoiceGuidance *voice)
{
    if (!voice || !voice->backend.speak) return false;
    return !voice->backend.ready
        || voice->backend.ready(voice->backend.userdata);
}

static bool speak(OpenRideVoiceGuidance *voice,
                  const char *text,
                  bool flush)
{
    if (!voice || !voice->enabled || !text || !text[0]) return false;
    if (!backend_ready(voice)) return false;
    return voice->backend.speak(voice->backend.userdata, text, flush);
}

static void reset_announcements(OpenRideVoiceGuidance *voice)
{
    if (!voice) return;
    voice->initial_instruction_announced = false;
    voice->has_instruction = false;
    voice->instruction_index = UINT32_MAX;
    voice->announced_far = false;
    voice->announced_near = false;
    voice->announced_now = false;
    voice->off_route_announced = false;
    voice->arrival_announced = false;
}

void openride_voice_guidance_init(OpenRideVoiceGuidance *voice)
{
    if (!voice) return;
    memset(voice, 0, sizeof(*voice));
    voice->enabled = true;
    voice->instruction_index = UINT32_MAX;
}

void openride_voice_guidance_set_backend(OpenRideVoiceGuidance *voice,
                                         OpenRideVoiceGuidanceBackend backend)
{
    if (!voice) return;
    if (voice->backend.stop) {
        voice->backend.stop(voice->backend.userdata);
    }
    voice->backend = backend;
    reset_announcements(voice);
}

void openride_voice_guidance_set_enabled(OpenRideVoiceGuidance *voice,
                                         bool enabled)
{
    if (!voice || voice->enabled == enabled) return;
    if (!enabled && voice->backend.stop) {
        voice->backend.stop(voice->backend.userdata);
    }
    voice->enabled = enabled;
    reset_announcements(voice);
}

bool openride_voice_guidance_enabled(const OpenRideVoiceGuidance *voice)
{
    return voice && voice->enabled;
}

void openride_voice_guidance_reset(OpenRideVoiceGuidance *voice)
{
    if (!voice) return;
    if (voice->backend.stop) {
        voice->backend.stop(voice->backend.userdata);
    }
    reset_announcements(voice);
}

static uint32_t instruction_index(
    const OpenRideNavigationInstructionList *instructions,
    const OpenRideNavigationInstruction *instruction)
{
    if (!instructions || !instructions->items || !instruction) return UINT32_MAX;
    if (instruction < instructions->items
        || instruction >= instructions->items + instructions->count) {
        return UINT32_MAX;
    }
    return (uint32_t)(instruction - instructions->items);
}

static void announcement_thresholds(double speed_mps,
                                    double *far_m,
                                    double *near_m,
                                    double *now_m)
{
    const double speed = clampd(isfinite(speed_mps) ? speed_mps : 0.0,
                                0.0,
                                60.0);
    if (far_m) *far_m = clampd(speed * 20.0, 200.0, 600.0);
    if (near_m) *near_m = clampd(speed * 8.0, 60.0, 180.0);
    if (now_m) *now_m = clampd(speed * 2.0, 20.0, 45.0);
}

static unsigned rounded_distance_m(double distance_m)
{
    if (!isfinite(distance_m) || distance_m <= 0.0) return 0U;
    const double step = distance_m < 100.0 ? 10.0 : 50.0;
    return (unsigned)(floor((distance_m + step * 0.5) / step) * step);
}

static void instruction_announcement(
    const OpenRideNavigationInstruction *instruction,
    double distance_m,
    bool immediate,
    char *text,
    size_t text_size)
{
    if (!text || text_size == 0U) return;
    text[0] = '\0';

    char maneuver[128];
    openride_navigation_instruction_text_fr(instruction,
                                            maneuver,
                                            sizeof(maneuver));
    if (immediate) {
        snprintf(text, text_size, "%s", maneuver);
        return;
    }

    const unsigned metres = rounded_distance_m(distance_m);
    snprintf(text,
             text_size,
             "Dans %u metres, %s",
             metres,
             maneuver);
}

bool openride_voice_guidance_update(
    OpenRideVoiceGuidance *voice,
    const OpenRideNavigationInstructionList *instructions,
    const OpenRideNavigationState *navigation)
{
    if (!voice || !voice->enabled || !navigation || !navigation->valid) {
        return false;
    }

    if (navigation->status == OPENRIDE_NAVIGATION_ARRIVED) {
        voice->off_route_announced = false;
        if (!voice->arrival_announced
            && speak(voice, "Vous etes arrive", true)) {
            voice->arrival_announced = true;
            return true;
        }
        return false;
    }
    voice->arrival_announced = false;

    if (navigation->status == OPENRIDE_NAVIGATION_OFF_ROUTE) {
        if (!voice->off_route_announced
            && speak(voice, "Hors itineraire", true)) {
            voice->off_route_announced = true;
            return true;
        }
        return false;
    }
    voice->off_route_announced = false;

    if (navigation->status != OPENRIDE_NAVIGATION_ON_ROUTE
        || !instructions || !instructions->items
        || instructions->count == 0U) {
        return false;
    }

    double distance_m = 0.0;
    const OpenRideNavigationInstruction *instruction =
        openride_navigation_instructions_next(instructions,
                                              navigation->traveled_m,
                                              &distance_m);
    if (!instruction || instruction->maneuver == OPENRIDE_MANEUVER_ARRIVE) {
        return false;
    }

    const uint32_t index = instruction_index(instructions, instruction);
    if (index == UINT32_MAX) return false;
    if (!voice->has_instruction || voice->instruction_index != index) {
        voice->has_instruction = true;
        voice->instruction_index = index;
        voice->announced_far = false;
        voice->announced_near = false;
        voice->announced_now = false;
    }

    double far_m = 0.0;
    double near_m = 0.0;
    double now_m = 0.0;
    announcement_thresholds(navigation->speed_mps,
                            &far_m,
                            &near_m,
                            &now_m);

    char text[192];
    if (!voice->initial_instruction_announced) {
        const bool immediate = distance_m <= now_m;
        instruction_announcement(instruction,
                                 distance_m,
                                 immediate,
                                 text,
                                 sizeof(text));
        if (speak(voice, text, true)) {
            voice->initial_instruction_announced = true;
            if (distance_m <= far_m) voice->announced_far = true;
            if (distance_m <= near_m) voice->announced_near = true;
            if (distance_m <= now_m) voice->announced_now = true;
            return true;
        }
        return false;
    }
    if (distance_m <= now_m && !voice->announced_now) {
        instruction_announcement(instruction,
                                 distance_m,
                                 true,
                                 text,
                                 sizeof(text));
        if (speak(voice, text, true)) {
            voice->announced_far = true;
            voice->announced_near = true;
            voice->announced_now = true;
            return true;
        }
        return false;
    }

    if (distance_m <= near_m && !voice->announced_near) {
        instruction_announcement(instruction,
                                 distance_m,
                                 false,
                                 text,
                                 sizeof(text));
        if (speak(voice, text, true)) {
            voice->announced_far = true;
            voice->announced_near = true;
            return true;
        }
        return false;
    }

    if (distance_m <= far_m && !voice->announced_far) {
        instruction_announcement(instruction,
                                 distance_m,
                                 false,
                                 text,
                                 sizeof(text));
        if (speak(voice, text, true)) {
            voice->announced_far = true;
            return true;
        }
    }

    return false;
}

bool openride_voice_guidance_announce_reroute(OpenRideVoiceGuidance *voice)
{
    if (!voice) return false;
    reset_announcements(voice);
    return speak(voice, "Recalcul de l'itineraire", true);
}
