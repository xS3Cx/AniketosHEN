#pragma once

#include <string>
#include <vector>

/* A looping video behind Home.
 *
 * The console's own player (Sce.Vsh.Video.VideoPlayer drawn by a PUI
 * VideoView) is put under Home's React tree, in Home's scene, so the video
 * fades and moves with Home like everything else in it. The player's core
 * runs outside ShellUI and cannot open files from our folder, but it can
 * stream, so the file is handed to it over a loopback-only HTTP server that
 * lives here. Everything is found by name at run time.
 *
 * Driven by the Shell Menu settings home_video_enabled, home_video_path and
 * home_video_sound. Videos are the .mp4/.m4v files in Directory() and in
 * <USB drive>/<distribution folder>/videos. A file named no_home_video in
 * the distribution's data folder keeps the feature off whatever is set. */
namespace HomeVideo {
/* UI thread, once a frame. */
void Tick();
/* Drop the player and start over; also clears a "failed" or "left off after
 * a crash" state. Call after the settings were changed by the user. */
void Restart();
/* The folder users put videos in (as ShellUI sees it). */
const char *Directory();
/* Full paths of the videos found, sorted by file name. */
const std::vector<std::string> &Videos(bool rescan = false);
/* The file being played, or empty. */
std::string Current();
std::string FileName(const std::string &path);
/* A still image in the same place, shown when no video is: the Shell Menu
 * settings home_image_enabled and home_image_path, and the .png/.jpg files
 * in ImageDirectory(). */
const char *ImageDirectory();
const std::vector<std::string> &Images(bool rescan = false);
/* Show the picture anew (after the settings were changed by the user). */
void RestartImage();
/* One line about where things stand, for the debug console. */
std::string Status();
}
