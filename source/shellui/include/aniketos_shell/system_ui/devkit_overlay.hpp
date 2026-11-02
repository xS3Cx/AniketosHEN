#pragma once

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
}
