#pragma once

#include <string>
#include <vector>

/* A looping video behind Home.
 *
 * The console's own player (Sce.Vsh.Video.VideoPlayer drawn by a PUI
 * VideoView) is put into the system's own background scene (BGLayer), which
 * stays up under Home, Settings and the other system screens while they
 * fade and slide over it, so the video plays on through every change of
 * screen; it fades out only when a game takes the screen (and, unless
 * home_background_global, under the system apps), and goes on from where
 * it was when Home is back. The player's core runs outside ShellUI and cannot
 * open files from our folder, but it can stream, so the file is handed to it
 * over a loopback-only HTTP server that lives here. Everything is found by
 * name at run time.
 *
 * Driven by the Shell Menu settings home_video_enabled, home_video_path,
 * home_video_sound and home_video_volume; home_bg_dim, home_bg_haze,
 * home_bg_vignette and home_bg_edges lay effects over the video or picture.
 * Videos are the .mp4/.m4v files in <data folder>/ShellUI/videos and in
 * <USB drive>/<distribution folder>/videos. A file named no_home_video in
 * the distribution's data folder keeps the feature off whatever is set. */
namespace HomeVideo {
/* UI thread, once a frame. */
void Tick();
/* Drop the player and start over; also clears a "failed" or "left off after
 * a crash" state. Call after the settings were changed by the user. */
void Restart();
/* Full paths of the videos found, sorted by file name. */
const std::vector<std::string> &Videos(bool rescan = false);
/* A still image in the same place, shown when no video plays (none chosen,
 * or the chosen one failed or was left off after a crash): the Shell Menu
 * settings home_image_enabled and home_image_path, and the .png/.jpg files
 * in <data folder>/ShellUI/background. */
const std::vector<std::string> &Images(bool rescan = false);
/* Show the picture anew (after the settings were changed by the user). */
void RestartImage();
}
