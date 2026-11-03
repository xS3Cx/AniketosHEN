#pragma once

/* Drifting particles behind Home.
 *
 * The console's own background particles are drawn by native code from
 * textures it loads itself, with nothing to hold on to from here. These are
 * ours: PUI image widgets in a layer under Home's React tree (above the
 * wallpaper or video), moved a little every frame.
 *
 * Style 0 shows the .png files in ShellUI/particles of the user data root.
 * The five defaults (triangle, circle, cross, square, dot) are written there
 * on first use and never overwritten, so they can be replaced, and any other
 * .png put there joins them. The other styles are built in (particle_sim):
 * their textures are embedded and written to ShellUI/particles/builtin.
 * Driven by the Shell Menu settings home_particles_enabled,
 * home_particles_style, home_particles_count, home_particles_hue (built-in
 * styles) and home_particles_speed. */
namespace HomeParticles {
/* UI thread, once a frame. */
void Tick();
/* The menu's styles: the folder's pictures, then the built-in ones. */
int StyleCount();
const char *StyleName(int style); /* an i18n key */
}
