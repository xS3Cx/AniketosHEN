/* Keyboard & mouse for games: names used in profile JSON and in the editor.
 *
 * Inputs:  "key:W", "key:LShift", "key:0x87" (any HID usage), "mouse:left",
 *          "mouse:right", "mouse:middle", "mouse:back", "mouse:forward",
 *          "mouse:6".."mouse:8", "wheel:up|down|left|right",
 *          "move:right|left|down|up" (mouse motion as a digital input).
 * Outputs: "none", "pad:cross" (several: "pad:l3+r3"), "axis:lx-" .. "axis:ry+"
 *          (Y down positive, so "axis:ly-" is stick up), "trigger:l2|r2",
 *          "layer:hold", "layer:toggle", "macro", "scale:left|right",
 *          "suspend". Layer and macro names are separate fields.
 * Parsing ignores case; the writers emit the canonical spelling above.
 * Display names are English; the Toolbox may translate them. */
#pragma once

#include <onion/kbm_profile.hpp>

#include <string>

namespace onion::kbm {

/* "" for kInputNone or ids out of range. */
std::string InputName(Input input);
bool ParseInput(const char *name, Input *out);
/* "W", "Left Shift", "Left mouse button", "Wheel up" ... */
std::string InputDisplayName(Input input);

/* Button masks: "cross+l1" (no "pad:" prefix). Unknown words fail. */
std::string ButtonsName(uint32_t mask);
bool ParseButtons(const char *names, uint32_t *out);

/* Name of the output's kind and target; value, ramp, layer and macro are not
 * part of the name. ParseOutput resets `out` and sets kind, buttons, axis,
 * trigger and the default value (full deflection or pull, signed for axes). */
std::string OutputName(const OutputDef &output);
bool ParseOutput(const char *name, OutputDef *out);

const char *GestureName(Gesture g);
bool ParseGesture(const char *name, Gesture *out);
const char *ModeName(Mode m);
bool ParseMode(const char *name, Mode *out);
const char *MouseModeName(MouseMode m);
bool ParseMouseMode(const char *name, MouseMode *out);
const char *DeadzoneShapeName(DeadzoneShape s);
bool ParseDeadzoneShape(const char *name, DeadzoneShape *out);
const char *OpposingName(Opposing o);
bool ParseOpposing(const char *name, Opposing *out);

} // namespace onion::kbm
