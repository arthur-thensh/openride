#include "openride/voice_guidance.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct FakeVoiceBackend {
    bool ready;
    unsigned speak_count;
    unsigned stop_count;
    bool last_flush;
    char last_text[192];
} FakeVoiceBackend;

static bool fake_ready(void *userdata)
{
    const FakeVoiceBackend *fake = userdata;
    return fake && fake->ready;
}

static bool fake_speak(void *userdata, const char *text, bool flush)
{
    FakeVoiceBackend *fake = userdata;
    if (!fake || !fake->ready || !text) return false;
    ++fake->speak_count;
    fake->last_flush = flush;
    snprintf(fake->last_text, sizeof(fake->last_text), "%s", text);
    return true;
}

static void fake_stop(void *userdata)
{
    FakeVoiceBackend *fake = userdata;
    if (fake) ++fake->stop_count;
}

static OpenRideVoiceGuidanceBackend fake_backend(FakeVoiceBackend *fake)
{
    OpenRideVoiceGuidanceBackend backend = {0};
    backend.userdata = fake;
    backend.ready = fake_ready;
    backend.speak = fake_speak;
    backend.stop = fake_stop;
    return backend;
}

static OpenRideNavigationInstructionList instruction_list(void)
{
    static OpenRideNavigationInstruction items[3];
    memset(items, 0, sizeof(items));
    items[0].maneuver = OPENRIDE_MANEUVER_DEPART;
    items[0].distance_from_start_m = 0.0;
    items[1].maneuver = OPENRIDE_MANEUVER_RIGHT;
    items[1].distance_from_start_m = 300.0;
    items[2].maneuver = OPENRIDE_MANEUVER_ARRIVE;
    items[2].distance_from_start_m = 1000.0;

    OpenRideNavigationInstructionList list = {0};
    list.items = items;
    list.count = 3U;
    list.route_distance_m = 1000.0;
    return list;
}

static OpenRideNavigationState navigation_state(void)
{
    OpenRideNavigationState navigation = {0};
    navigation.valid = true;
    navigation.status = OPENRIDE_NAVIGATION_ON_ROUTE;
    navigation.speed_mps = 15.0;
    navigation.traveled_m = 0.0;
    navigation.remaining_m = 1000.0;
    return navigation;
}

static void test_phased_announcements_and_deduplication(void)
{
    FakeVoiceBackend fake = {0};
    fake.ready = true;

    OpenRideVoiceGuidance voice;
    openride_voice_guidance_init(&voice);
    openride_voice_guidance_set_backend(&voice, fake_backend(&fake));

    const OpenRideNavigationInstructionList list = instruction_list();
    OpenRideNavigationState navigation = navigation_state();

    assert(openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 1U);
    assert(strcmp(fake.last_text, "Dans 300 metres, Tournez a droite") == 0);
    assert(fake.last_flush);

    assert(!openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 1U);

    navigation.traveled_m = 200.0;
    assert(openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 2U);
    assert(strcmp(fake.last_text, "Dans 100 metres, Tournez a droite") == 0);

    navigation.traveled_m = 278.0;
    assert(openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 3U);
    assert(strcmp(fake.last_text, "Tournez a droite") == 0);

    assert(!openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 3U);
}

static void test_status_announcements(void)
{
    FakeVoiceBackend fake = {0};
    fake.ready = true;

    OpenRideVoiceGuidance voice;
    openride_voice_guidance_init(&voice);
    openride_voice_guidance_set_backend(&voice, fake_backend(&fake));

    const OpenRideNavigationInstructionList list = instruction_list();
    OpenRideNavigationState navigation = navigation_state();
    navigation.status = OPENRIDE_NAVIGATION_OFF_ROUTE;

    assert(openride_voice_guidance_update(&voice, &list, &navigation));
    assert(strcmp(fake.last_text, "Hors itineraire") == 0);
    assert(!openride_voice_guidance_update(&voice, &list, &navigation));

    navigation.status = OPENRIDE_NAVIGATION_ON_ROUTE;
    navigation.traveled_m = 500.0;
    assert(!openride_voice_guidance_update(&voice, &list, &navigation));

    navigation.status = OPENRIDE_NAVIGATION_ARRIVED;
    assert(openride_voice_guidance_update(&voice, &list, &navigation));
    assert(strcmp(fake.last_text, "Vous etes arrive") == 0);
    assert(!openride_voice_guidance_update(&voice, &list, &navigation));

    assert(openride_voice_guidance_announce_reroute(&voice));
    assert(strcmp(fake.last_text, "Recalcul de l'itineraire") == 0);
}

static void test_initial_instruction_is_announced_even_when_far(void)
{
    FakeVoiceBackend fake = {0};
    fake.ready = true;

    OpenRideVoiceGuidance voice;
    openride_voice_guidance_init(&voice);
    openride_voice_guidance_set_backend(&voice, fake_backend(&fake));

    OpenRideNavigationInstructionList list = instruction_list();
    list.items[1].distance_from_start_m = 1500.0;
    list.items[2].distance_from_start_m = 3000.0;
    list.route_distance_m = 3000.0;
    OpenRideNavigationState navigation = navigation_state();
    navigation.remaining_m = 3000.0;

    assert(openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 1U);
    assert(strcmp(fake.last_text, "Dans 1500 metres, Tournez a droite") == 0);

    assert(!openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 1U);
}

static void test_not_ready_is_retried(void)
{
    FakeVoiceBackend fake = {0};

    OpenRideVoiceGuidance voice;
    openride_voice_guidance_init(&voice);
    openride_voice_guidance_set_backend(&voice, fake_backend(&fake));

    const OpenRideNavigationInstructionList list = instruction_list();
    OpenRideNavigationState navigation = navigation_state();

    assert(!openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 0U);

    fake.ready = true;
    assert(openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 1U);
}

static void test_disable_stops_and_silences(void)
{
    FakeVoiceBackend fake = {0};
    fake.ready = true;

    OpenRideVoiceGuidance voice;
    openride_voice_guidance_init(&voice);
    openride_voice_guidance_set_backend(&voice, fake_backend(&fake));
    const unsigned stops_after_backend = fake.stop_count;

    openride_voice_guidance_set_enabled(&voice, false);
    assert(!openride_voice_guidance_enabled(&voice));
    assert(fake.stop_count == stops_after_backend + 1U);

    const OpenRideNavigationInstructionList list = instruction_list();
    OpenRideNavigationState navigation = navigation_state();
    assert(!openride_voice_guidance_update(&voice, &list, &navigation));
    assert(fake.speak_count == 0U);
}

int main(void)
{
    test_phased_announcements_and_deduplication();
    test_status_announcements();
    test_initial_instruction_is_announced_even_when_far();
    test_not_ready_is_retried();
    test_disable_stops_and_silences();
    puts("Voice guidance tests: OK");
    return 0;
}
