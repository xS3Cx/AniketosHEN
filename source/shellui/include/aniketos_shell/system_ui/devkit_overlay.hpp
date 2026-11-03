#pragma once

#include "external_symbols.hpp"

/* The DevKit/TestKit overlays on Home, drawn by the system's own widget
 * (ReactNative.Components.ShellUI.HomeUI.DebugInfoView in
 * Sce.Vsh.ShellUI.ReactNativeShellApp): the "System Software / Release Check
 * Mode / HDD / ..." panel and the red "This DevKit/TestKit is Expired"
 * banner. Retail firmware carries the widget but never builds those panels;
 * this module makes an instance of its own under Home's root and asks it to.
 * Runtime only, both off by default. */
namespace DevkitOverlay {
void Install();
/* UI thread, once a frame. */
void Tick();
/* A setting changed: show, hide or drop the overlay at the next tick. */
void Refresh();
/* True for the overlay's widget, its panels and their labels (and anything
   made while its create methods run): the theme hooks leave them as the
   system draws them. UI thread; costs nothing while the overlay is off. */
bool Owns(MonoObject *widget);
}
