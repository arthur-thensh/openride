#include "openride/navigation_session.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static OpenRideNavigationState state(OpenRideNavigationStatus status)
{
    OpenRideNavigationState value = {0};
    value.valid = true;
    value.status = status;
    value.remaining_m = 5000.0;
    return value;
}

int main(void)
{
    OpenRideNavigationSession session;
    openride_navigation_session_init(&session);
    session.config.off_route_confirm_s = 2.0;
    session.config.reroute_cooldown_s = 5.0;

    OpenRideNavigationState nav = state(OPENRIDE_NAVIGATION_ON_ROUTE);
    openride_navigation_session_update(&session, &nav, 50.0, 3.0, 10.0, 1.0);
    openride_navigation_session_update(&session, &nav, 50.00005, 3.0, 10.0, 1.0);
    assert(session.stats.elapsed_s == 2.0);
    assert(session.stats.moving_s == 2.0);
    assert(session.stats.gps_distance_m > 4.0);
    assert(session.stats.max_speed_mps == 10.0);
    assert(!openride_navigation_session_take_reroute_request(&session));

    nav.status = OPENRIDE_NAVIGATION_OFF_ROUTE;
    openride_navigation_session_update(&session, &nav, 50.00010, 3.0, 10.0, 1.0);
    assert(!openride_navigation_session_take_reroute_request(&session));
    openride_navigation_session_update(&session, &nav, 50.00015, 3.0, 10.0, 1.1);
    assert(openride_navigation_session_take_reroute_request(&session));

    openride_navigation_session_mark_rerouted(&session);
    assert(session.stats.reroute_count == 1U);
    openride_navigation_session_update(&session, &nav, 50.00020, 3.0, 10.0, 3.0);
    assert(!openride_navigation_session_take_reroute_request(&session));

    nav.status = OPENRIDE_NAVIGATION_ON_ROUTE;
    openride_navigation_session_update(&session, &nav, 50.00025, 3.0, 0.0, 3.0);
    assert(session.reroute_cooldown_remaining_s <= 0.0);

    openride_navigation_session_set_auto_reroute(&session, false);
    nav.status = OPENRIDE_NAVIGATION_OFF_ROUTE;
    openride_navigation_session_update(&session, &nav, 50.00030, 3.0, 10.0, 3.0);
    assert(!openride_navigation_session_take_reroute_request(&session));

    printf("Navigation session tests: OK\n");
    return 0;
}
