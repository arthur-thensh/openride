#ifndef OPENRIDE_APP_RUNTIME_H
#define OPENRIDE_APP_RUNTIME_H

/*
 * High-level OpenRide application runtime.
 *
 * Platform entrypoints stay deliberately tiny. All application lifetime
 * orchestration (startup, event/async runtimes, navigation/frame updates,
 * rendering and shutdown) lives behind this function.
 */
int openride_app_run(int argc, char **argv);

#endif
