#pragma once

#include "external_symbols.hpp"

/* The one list of what the shell installs, starts and runs per frame. The
   order inside each function is part of the behaviour. */
namespace ShellFeatures {

void Install(MonoImage *react_pui, MonoImage *pui);
void StartWorkers();
void BeforeNativeFrame();

/* Sony rewrites the tile focus settings while it renders Home. As a stack
   object in the render hook this runs the tile pass after the native frame,
   on every exit path, so ours is the value the outline renderer finally
   consumes. */
struct AfterNativeFrame {
  ~AfterNativeFrame();
};

} // namespace ShellFeatures
