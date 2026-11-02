#pragma once

#include <string>

/* Fetches a website's icon for its Home tile on a thread of its own and
 * writes it under the user's data folder (or, with no usable icon, a plain
 * tile with the title's first letter): <tile id>.png, or .svg for an SVG.
 * The Shell Menu's side waits for the file. False: the values were refused
 * or the thread did not start. */
bool web_tile_icon_start(const std::string &id, const std::string &url,
                         const std::string &title);
