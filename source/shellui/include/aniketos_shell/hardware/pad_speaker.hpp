#pragma once

/* Everything the console plays, also on the foreground user's DualSense
 * speaker. The audio server keeps a copy of its final mix in a ring for the
 * capture service (Share, Remote Play); a thread here reads that ring and
 * plays it on a pad speaker port, which ShellUI may open because it holds
 * the controller. The TV keeps playing; nothing is routed or changed in the
 * server, and the port closes when the switch is off or ShellUI restarts.
 *
 * Driven by the Shell Menu setting pad_speaker_mirror. */
namespace PadSpeaker {

/* UI thread, once a frame: starts or stops the relay for the setting. */
void Tick();

} // namespace PadSpeaker
