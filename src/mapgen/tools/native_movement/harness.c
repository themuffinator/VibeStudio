/* Original MIT-licensed orchestration glue. The optional local executable links
 * unmodified external GPL Quake II code; it is not a qmg_core or map deliverable.
 * No client, game module, renderer, network, input device or engine main loop. */
#include "qcommon.h"
#include <errno.h>
#include <limits.h>
_Static_assert(sizeof(short) == 2 && sizeof(int) == 4 && sizeof(unsigned long) == 4,
               "Pinned native 3.19 source/MD4 baseline requires the Windows integer widths");

static const char *input_bsp;
static int world_headnode;
static int world_identity;
static unsigned trace_calls, point_calls, startsolid_calls;
static cvar_t no_areas;

static void fatal(const char *message) {
    fprintf(stderr, "native_movement: %s\n", message);
    exit(2);
}

void Com_Error(int code, char *format, ...) {
    va_list args;
    fprintf(stderr, "native CM/Pmove error %d: ", code);
    va_start(args, format); vfprintf(stderr, format, args); va_end(args);
    fputc('\n', stderr);
    exit(3);
}

void Com_Printf(char *format, ...) {
    va_list args;
    va_start(args, format); vfprintf(stderr, format, args); va_end(args);
}

void Com_DPrintf(char *format, ...) {
    va_list args;
    va_start(args, format); vfprintf(stderr, format, args); va_end(args);
}

cvar_t *Cvar_Get(char *name, char *value, int flags) {
    if (strcmp(name, "map_noareas") || strcmp(value, "0") || flags)
        fatal("unexpected native Cvar_Get dependency");
    memset(&no_areas, 0, sizeof(no_areas));
    no_areas.name = "map_noareas"; no_areas.string = "0";
    return &no_areas;
}

float Cvar_VariableValue(char *name) {
    if (strcmp(name, "flushmap")) fatal("unexpected native cvar dependency");
    return 1; /* One map per process; force the native loader to read it. */
}

int FS_LoadFile(char *name, void **buffer) {
    FILE *file;
    long length;
    dheader_t *header;
    int i;
    if (strcmp(name, "movement.bsp")) fatal("unexpected virtual file request");
    file = fopen(input_bsp, "rb");
    if (!file) fatal("cannot open requested BSP");
    if (fseek(file, 0, SEEK_END)) fatal("BSP seek failed");
    length = ftell(file);
    if (length < sizeof(dheader_t) || length > 8 * 1024 * 1024) fatal("BSP outside bounded file size");
    rewind(file);
    *buffer = malloc((size_t)length);
    if (!*buffer || fread(*buffer, 1, (size_t)length, file) != (size_t)length) fatal("BSP read failed");
    fclose(file);
    header = (dheader_t *)*buffer;
    if (LittleLong(header->ident) != IDBSPHEADER || LittleLong(header->version) != BSPVERSION)
        fatal("only native IBSP38 is supported");
    for (i = 0; i < HEADER_LUMPS; ++i) {
        int offset = LittleLong(header->lumps[i].fileofs), size = LittleLong(header->lumps[i].filelen);
        if (offset < 0 || size < 0 || offset > length || size > length - offset)
            fatal("BSP lump lies outside bounded input");
    }
    return (int)length;
}

void FS_FreeFile(void *buffer) { free(buffer); }
void FS_Read(void *buffer, int length, FILE *file) {
    if (length < 0 || fread(buffer, 1, (size_t)length, file) != (size_t)length)
        fatal("native portal-state read failed");
}

static trace_t movement_trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end) {
    trace_t result = CM_BoxTrace(start, end, mins, maxs, world_headnode, MASK_PLAYERSOLID);
    ++trace_calls;
    if (result.startsolid) ++startsolid_calls;
    /* CM reports world geometry; the server trace wrapper normally associates
     * a hit with the world edict. Pmove only tests/stores this pointer. */
    if (result.fraction < 1 || result.startsolid || result.allsolid)
        result.ent = (struct edict_s *)&world_identity;
    return result;
}

static int movement_contents(vec3_t point) {
    ++point_calls;
    return CM_PointContents(point, world_headnode);
}

static double number(const char *text, double low, double high) {
    char *end;
    double value;
    errno = 0;
    value = strtod(text, &end);
    if (errno || *end || end == text || !isfinite(value) || value < low || value > high)
        fatal("invalid bounded numeric argument");
    return value;
}

int main(int argc, char **argv) {
    pmove_t movement;
    vec3_t origin, mins = {-16, -16, -24}, maxs = {16, 16, 32};
    cmodel_t *model;
    unsigned checksum, frame = 0, elapsed = 0;
    char line[256], extra;
    int i, msec, forward, side, up, buttons;
    double yaw, pitch, fields[7];
    trace_t occupancy;
    if (argc != 9) fatal("usage: qmg_native_pmove <bsp> <x> <y> <feet_z> <vx> <vy> <vz> <gravity>");
    input_bsp = argv[1];
    memset(&movement, 0, sizeof(movement));
    movement.s.pm_type = PM_NORMAL;
    for (i = 0; i < 3; ++i) {
        double coordinate = number(argv[2 + i], -3000, 3000) + (i == 2 ? 24 : 0);
        double velocity = number(argv[5 + i], -1000, 1000);
        if (coordinate * 8 != round(coordinate * 8) || velocity * 8 != round(velocity * 8))
            fatal("initial position and velocity must be on the native eighth-unit grid");
        movement.s.origin[i] = (short)(coordinate * 8);
        movement.s.velocity[i] = (short)(velocity * 8);
        origin[i] = (float)coordinate;
    }
    {
        double gravity = number(argv[8], 1, 2000);
        if (gravity != floor(gravity)) fatal("gravity must be an integer");
        movement.s.gravity = (short)gravity;
    }
    Swap_Init();
    model = CM_LoadMap("movement.bsp", false, &checksum);
    world_headnode = model->headnode;
    occupancy = movement_trace(origin, mins, maxs, origin);
    if (occupancy.startsolid || occupancy.allsolid) fatal("initial standing hull is startsolid");
    movement.trace = movement_trace;
    movement.pointcontents = movement_contents;
    movement.snapinitial = true;
    printf("{\"type\":\"initial\",\"native_map_checksum\":%u,\"inline_models\":%d,"
           "\"standing_origin\":[%.9g,%.9g,%.9g],\"world_only_collision\":true}\n",
           checksum, CM_NumInlineModels(), origin[0], origin[1], origin[2]);
    while (fgets(line, sizeof(line), stdin)) {
        if (!strchr(line, '\n') && !feof(stdin)) fatal("command line exceeds cap");
        if (sscanf(line, "%lf %lf %lf %lf %lf %lf %lf %c", &fields[0], &fields[1], &fields[2], &fields[3],
                   &fields[4], &fields[5], &fields[6], &extra) != 7)
            fatal("command requires msec forward side up yaw pitch buttons");
        for (i = 0; i < 7; ++i)
            if (!isfinite(fields[i]) || ((i < 4 || i == 6) && fields[i] != floor(fields[i])))
                fatal("command needs finite integer movement/time/buttons fields");
        if (fields[0] < 8 || fields[0] > 50 || fabs(fields[1]) > 400 || fabs(fields[2]) > 400 ||
            fabs(fields[3]) > 400 || fabs(fields[4]) > 360 || fabs(fields[5]) > 89 || fields[6] < 0 || fields[6] > 255)
            fatal("command outside deterministic harness bounds");
        msec = (int)fields[0]; forward = (int)fields[1]; side = (int)fields[2]; up = (int)fields[3];
        yaw = fields[4]; pitch = fields[5]; buttons = (int)fields[6];
        if (++frame > 4096 || elapsed + (unsigned)msec > 120000) fatal("trial frame/time budget exceeded");
        elapsed += (unsigned)msec;
        memset(&movement.cmd, 0, sizeof(movement.cmd));
        movement.cmd.msec = (byte)msec;
        movement.cmd.forwardmove = (short)forward;
        movement.cmd.sidemove = (short)side;
        movement.cmd.upmove = (short)up;
        movement.cmd.buttons = (byte)buttons;
        movement.cmd.angles[YAW] = (short)ANGLE2SHORT(yaw);
        movement.cmd.angles[PITCH] = (short)ANGLE2SHORT(pitch);
        trace_calls = point_calls = startsolid_calls = 0;
        Pmove(&movement);
        movement.snapinitial = false;
        for (i = 0; i < 3; ++i) origin[i] = movement.s.origin[i] * .125f;
        occupancy = CM_BoxTrace(origin, origin, movement.mins, movement.maxs, world_headnode, MASK_PLAYERSOLID);
        printf("{\"type\":\"frame\",\"frame\":%u,\"time_ms\":%u,\"msec\":%d,"
               "\"origin\":[%.9g,%.9g,%.9g],\"velocity\":[%.9g,%.9g,%.9g],\"feet_z\":%.9g,"
               "\"hull_mins\":[%.9g,%.9g,%.9g],\"hull_maxs\":[%.9g,%.9g,%.9g],"
               "\"waterlevel\":%d,\"watertype\":%d,\"viewheight\":%.9g,\"grounded\":%s,"
               "\"pm_flags\":%u,\"pm_time\":%u,\"touches\":%d,\"trace_calls\":%u,"
               "\"pointcontents_calls\":%u,\"trace_startsolid_calls\":%u,\"post_startsolid\":%s,\"post_allsolid\":%s}\n",
               frame, elapsed, msec, origin[0], origin[1], origin[2], movement.s.velocity[0] * .125,
               movement.s.velocity[1] * .125, movement.s.velocity[2] * .125, origin[2] + movement.mins[2],
               movement.mins[0], movement.mins[1], movement.mins[2], movement.maxs[0], movement.maxs[1], movement.maxs[2],
               movement.waterlevel, movement.watertype, movement.viewheight, movement.groundentity ? "true" : "false",
               movement.s.pm_flags, movement.s.pm_time, movement.numtouch, trace_calls, point_calls, startsolid_calls,
               occupancy.startsolid ? "true" : "false", occupancy.allsolid ? "true" : "false");
    }
    if (ferror(stdin) || !frame) fatal("empty or unreadable command stream");
    return 0;
}
