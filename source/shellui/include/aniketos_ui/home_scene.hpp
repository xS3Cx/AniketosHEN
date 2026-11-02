/* Home's scene and the widget tree around it: where our widgets hang, found by
 * name for every firmware. UI thread. */
#pragma once

#include "monodef.h"

#include <string>

namespace AniketosUI::HomeScene {

/* Root widget of Home's scene, else of the scene the system clock is in, or
 * null. Children of it are faded and animated by PUI together with that scene. */
MonoObject *Root();
/* Home's root and no other scene's: null while Home is not there. */
MonoObject *OwnRoot();
bool Contains(MonoObject *widget);
/* Calls `visit` with the root widget of every scene in ShellUI's scene tree,
 * sub-scenes and modal scenes included. */
void VisitSceneRoots(void (*visit)(MonoObject *root, void *context),
                     void *context);

/* Gives IsDisposed the getter of `widget_class` or a base of it. Until then
 * nothing counts as disposed. */
void ResolveDisposedGetter(MonoClass *widget_class);
bool IsDisposed(MonoObject *widget);

MonoObject *Parent(MonoObject *widget);
/* The top of the widget's parent chain, the widget itself if it has no parent. */
MonoObject *TopWidget(MonoObject *widget);

/* A property's value by name, null if it is missing or its getter throws. */
MonoObject *PropertyObject(MonoObject *object, const char *name);
/* The same as text, at most 60 bytes: "-" missing, "!" thrown, "null", "?". */
std::string PropertyText(MonoObject *object, const char *name);

void RemoveStaleChildren(MonoObject *root);

} // namespace AniketosUI::HomeScene
