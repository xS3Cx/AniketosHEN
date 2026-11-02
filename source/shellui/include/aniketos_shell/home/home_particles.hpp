#pragma once

/* Drifting particles behind Home: PlayStation symbols and soft dots.
 *
 * The console's own background particles are drawn by native code from
 * textures it loads itself, with nothing to hold on to from here. These are
 * ours: PUI image widgets in a layer under Home's React tree (above the
 * wallpaper or video), moved a little every frame.
 *
 * The pictures are the .png files in ShellUI/particles of the user data root.
 * The five defaults (triangle, circle, cross, square, dot) are written there
 * on first use and never overwritten, so they can be replaced, and any other
 * .png put there joins them. Driven by the Shell Menu settings
 * home_particles_enabled and home_particles_count. */
namespace HomeParticles {
/* UI thread, once a frame. */
void Tick();
}
