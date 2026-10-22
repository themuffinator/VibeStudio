/* Original MIT API glue. Links unmodified external GPL stock Quake II 3.19
 * world queries; the private combined executable is GPL and not distributed. */
#include "server.h"
#include <stddef.h>

server_t sv;
game_export_t *ge;

void QMG_BindWorld(cmodel_t *model, game_export_t *module) {
    memset(&sv, 0, sizeof(sv));
    sv.state = ss_loading;
    sv.models[1] = model;
    ge = module;
    SV_ClearWorld();
}

void QMG_BindInline(int index, cmodel_t *model) {
    if (index < 1 || index >= MAX_MODELS) Com_Error(ERR_FATAL, "inline model index outside cap");
    sv.models[index] = model;
}

void QMG_WorldReady(void) { sv.state = ss_game; }
size_t QMG_ServerOwnerOffset(void) { return offsetof(edict_t, owner); }
size_t QMG_ServerClientOffset(void) { return offsetof(edict_t, client); }
size_t QMG_ServerAreaOffset(void) { return offsetof(edict_t, area); }
