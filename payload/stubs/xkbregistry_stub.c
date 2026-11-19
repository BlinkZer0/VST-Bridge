// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
//
// Stand-in for libxkbregistry.so.0 (x86_64, run under Box64). Wine's Wayland driver links it only to
// name keyboard layouts when a compositor sends a keymap; our compositor (shared/wl) never does, and
// the real library would pull in libxml2 + ICU (~30 MB). Wine checks for failure: rxkb_context_new()
// returning NULL makes it log "Failed to parse default Xkb ruleset" and carry on.
#include <stdbool.h>
#include <stddef.h>

struct rxkb_context;
struct rxkb_layout;

struct rxkb_context* rxkb_context_new(int flags) { (void)flags; return NULL; }
bool rxkb_context_parse_default_ruleset(struct rxkb_context* c) { (void)c; return false; }
struct rxkb_context* rxkb_context_unref(struct rxkb_context* c) { (void)c; return NULL; }
struct rxkb_layout* rxkb_layout_first(struct rxkb_context* c) { (void)c; return NULL; }
struct rxkb_layout* rxkb_layout_next(struct rxkb_layout* l) { (void)l; return NULL; }
const char* rxkb_layout_get_name(struct rxkb_layout* l) { (void)l; return NULL; }
const char* rxkb_layout_get_variant(struct rxkb_layout* l) { (void)l; return NULL; }
const char* rxkb_layout_get_description(struct rxkb_layout* l) { (void)l; return NULL; }
