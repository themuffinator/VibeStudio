/* Original MIT host glue. Stock game, server-world, CM and Pmove implementations
 * remain unmodified external GPL inputs. The private combined executable is GPL.
 * No engine main loop, socket code, client, renderer or input-device subsystem. */
#include "g_local.h"
#include <errno.h>
#include <stddef.h>

/* The 1997 public headers have no include guards. Import the game view once
 * and declare only the collision API used by this independent host. */
extern void Swap_Init(void), Pmove(pmove_t *);
extern cmodel_t *CM_LoadMap(char *, qboolean, unsigned *), *CM_InlineModel(char *);
extern char *CM_EntityString(void);
extern int CM_NumInlineModels(void), CM_PointLeafnum(vec3_t), CM_LeafCluster(int), CM_LeafArea(int);
extern byte *CM_ClusterPVS(int), *CM_ClusterPHS(int);
extern qboolean CM_AreasConnected(int, int);
extern void CM_SetAreaPortalState(int, qboolean);

extern game_export_t *GetGameAPI(game_import_t *);
extern void QMG_BindWorld(cmodel_t *, game_export_t *);
extern void QMG_BindInline(int, cmodel_t *);
extern void QMG_WorldReady(void);
extern size_t QMG_ServerOwnerOffset(void), QMG_ServerClientOffset(void), QMG_ServerAreaOffset(void);
extern trace_t SV_Trace(vec3_t, vec3_t, vec3_t, vec3_t, edict_t *, int);
extern int SV_PointContents(vec3_t);
extern void SV_LinkEdict(edict_t *), SV_UnlinkEdict(edict_t *);
extern int SV_AreaEdicts(vec3_t, vec3_t, edict_t **, int, int);

static const char *bsp_path;
static game_export_t *module;
static edict_t *actor, *victim, *pad, *destination, *trigger;
static unsigned command_frame, elapsed_ms, trace_calls, area_calls, network_message_calls, link_calls;
static int observing;
static cvar_t cvars[128];
static char cvar_names[128][64], cvar_values[128][256];
static unsigned cvar_count;
static char registry[3][256][MAX_QPATH];
static unsigned registry_count[3];

typedef struct allocation_s { struct allocation_s *next; void *pointer; int tag; } allocation_t;
static allocation_t *allocations;

static void fatal(char *format, ...) {
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
    fputc('\n', stderr); exit(2);
}
void Com_Error(int code, char *format, ...) {
    va_list args; fprintf(stderr, "stock error %d: ", code);
    va_start(args, format); vfprintf(stderr, format, args); va_end(args); fputc('\n', stderr); exit(3);
}
void Com_Printf(char *format, ...) { va_list args; va_start(args,format); vfprintf(stderr,format,args); va_end(args); }
void Com_DPrintf(char *format, ...) { va_list args; va_start(args,format); vfprintf(stderr,format,args); va_end(args); }
void Sys_Error(char *format, ...) { va_list args; va_start(args,format); vfprintf(stderr,format,args); va_end(args); exit(3); }

cvar_t *Cvar_Get(char *name, char *value, int flags) {
    unsigned i;
    if (strlen(name)>=64 || strlen(value)>=256) fatal("cvar text cap");
    for(i=0;i<cvar_count;i++) if(!strcmp(name,cvar_names[i])) return &cvars[i];
    if(cvar_count==128) fatal("cvar count cap");
    i=cvar_count++; strcpy(cvar_names[i],name); strcpy(cvar_values[i],value);
    cvars[i].name=cvar_names[i]; cvars[i].string=cvar_values[i]; cvars[i].value=(float)atof(value); cvars[i].flags=flags;
    return &cvars[i];
}
float Cvar_VariableValue(char *name) { return !strcmp(name,"flushmap") ? 1 : Cvar_Get(name,"0",0)->value; }
static cvar_t *set_cvar(char *name,char *value) {
    cvar_t *c=Cvar_Get(name,value,0); if(strlen(value)>=256) fatal("cvar value cap");
    strcpy(c->string,value); c->value=(float)atof(value); return c;
}

int FS_LoadFile(char *name, void **buffer) {
    FILE *f; long size;
    if(strcmp(name,"teleport.bsp")) fatal("unexpected file request: %s",name);
    f=fopen(bsp_path,"rb"); if(!f) fatal("cannot read declared BSP");
    if(fseek(f,0,SEEK_END)) fatal("BSP seek failed"); size=ftell(f); rewind(f);
    if(size<160 || size>8*1024*1024) fatal("BSP byte cap");
    *buffer=malloc(size); if(!*buffer || fread(*buffer,1,size,f)!=(size_t)size) fatal("BSP read failed");
    fclose(f); return (int)size;
}
void FS_FreeFile(void *p) { free(p); }
void FS_Read(void *p,int n,FILE *f) { if(n<0 || fread(p,1,n,f)!=(size_t)n) fatal("native portal read failed"); }

static void *tag_malloc(int size,int tag) {
    allocation_t *a; if(size<=0 || size>16*1024*1024) fatal("tag allocation cap");
    a=malloc(sizeof(*a)); if(!a) fatal("allocation failed"); a->pointer=calloc(1,size);
    if(!a->pointer) fatal("allocation failed"); a->tag=tag; a->next=allocations; allocations=a; return a->pointer;
}
static void tag_free(void *p) {
    allocation_t **at=&allocations,*old; while(*at && (*at)->pointer!=p) at=&(*at)->next;
    if(!*at) fatal("unknown allocation release"); old=*at; *at=old->next; free(old->pointer); free(old);
}
static void free_tags(int tag) {
    allocation_t **at=&allocations,*old;
    while(*at) { if((*at)->tag!=tag) { at=&(*at)->next; continue; } old=*at; *at=old->next; free(old->pointer); free(old); }
}

static int index_resource(int type,char *name) {
    unsigned i; if(!name || !*name) return 0; if(strlen(name)>=MAX_QPATH) fatal("resource name cap");
    for(i=1;i<=registry_count[type];i++) if(!strcmp(registry[type][i],name)) return (int)i;
    if(registry_count[type]>=255) fatal("resource index cap"); i=++registry_count[type]; strcpy(registry[type][i],name); return (int)i;
}
static int model_index(char *s) { return index_resource(0,s); }
static int sound_index(char *s) { return index_resource(1,s); }
static int image_index(char *s) { return index_resource(2,s); }
static void set_model(edict_t *e,char *name) {
    e->s.modelindex=model_index(name);
    if(name[0]=='*') { cmodel_t *m=CM_InlineModel(name); QMG_BindInline(e->s.modelindex,m);
        VectorCopy(m->mins,e->mins); VectorCopy(m->maxs,e->maxs); SV_LinkEdict(e); }
}
static void config_string(int index,char *value) {
    /* Stock PF_Configstring accepts NULL as an empty string (sv_game.c). */
    if(index<0 || index>=MAX_CONFIGSTRINGS || (value && strlen(value)>4096)) fatal("configstring cap");
}
static void bprint(int level,char *format,...) { va_list a; (void)level; va_start(a,format); vfprintf(stderr,format,a); va_end(a); }
static void cprint(edict_t *e,int level,char *format,...) { va_list a; (void)e;(void)level;va_start(a,format);vfprintf(stderr,format,a);va_end(a); }
static void centerprint(edict_t *e,char *format,...) { va_list a;(void)e;va_start(a,format);vfprintf(stderr,format,a);va_end(a); }
static void sound_event(edict_t *e,int c,int s,float v,float a,float t) { (void)e;(void)c;(void)s;(void)v;(void)a;(void)t; ++network_message_calls; }
static void positioned_sound(vec3_t p,edict_t *e,int c,int s,float v,float a,float t) { (void)p;sound_event(e,c,s,v,a,t); }
static void multicast_event(vec3_t p,multicast_t to) { (void)p;(void)to;++network_message_calls; }
static void unicast_event(edict_t *e,qboolean reliable) { (void)e;(void)reliable;++network_message_calls; }
static void write_integer(int v) { (void)v;++network_message_calls; }
static void write_float(float v) { (void)v;++network_message_calls; }
static void write_string(char *v) { (void)v;++network_message_calls; }
static void write_vector(vec3_t v) { (void)v;++network_message_calls; }
static int command_argc(void) { return 0; }
static char *command_argv(int i) { (void)i;return ""; }
static char *command_args(void) { return ""; }
static void add_command(char *s) { fatal("unexpected game console request: %s",s); }
static void debug_graph(float value,int color) { (void)value;(void)color; }
static qboolean visibility(vec3_t a,vec3_t b) {
    int la=CM_PointLeafnum(a),lb=CM_PointLeafnum(b),ca=CM_LeafCluster(la),cb=CM_LeafCluster(lb);
    byte *bits; if(ca<0 || cb<0) return false; bits=CM_ClusterPVS(ca);
    return (bits[cb>>3] & (1<<(cb&7))) && CM_AreasConnected(CM_LeafArea(la),CM_LeafArea(lb));
}
static qboolean hearability(vec3_t a,vec3_t b) {
    int la=CM_PointLeafnum(a),lb=CM_PointLeafnum(b),ca=CM_LeafCluster(la),cb=CM_LeafCluster(lb);
    byte *bits; if(ca<0 || cb<0) return false; bits=CM_ClusterPHS(ca);
    return (bits[cb>>3] & (1<<(cb&7))) && CM_AreasConnected(CM_LeafArea(la),CM_LeafArea(lb));
}
static trace_t trace_world(vec3_t a,vec3_t mi,vec3_t ma,vec3_t b,edict_t *pass,int mask) {
    ++trace_calls; return SV_Trace(a,mi,ma,b,pass,mask);
}
static int box_entities(vec3_t mi,vec3_t ma,edict_t **list,int cap,int kind) {
    ++area_calls; return SV_AreaEdicts(mi,ma,list,cap,kind);
}

static int entity_id(edict_t *e) { return e ? (int)(e-g_edicts) : -1; }
static void vector_json(const float *v) { printf("[%.9g,%.9g,%.9g]",v[0],v[1],v[2]); }
static void short_json(const short *v) { printf("[%d,%d,%d]",v[0],v[1],v[2]); }
static void state_json(edict_t *e) {
    trace_t t;
    if(!e) { printf("null");return; }
    t=SV_Trace(e->s.origin,e->mins,e->maxs,e->s.origin,e,MASK_PLAYERSOLID);
    printf("{\"id\":%d,\"inuse\":%d,\"health\":%d,\"solid\":%d,\"deadflag\":%d,\"takedamage\":%d,\"origin\":",entity_id(e),e->inuse,e->health,e->solid,e->deadflag,e->takedamage);vector_json(e->s.origin);
    printf(",\"old_origin\":");vector_json(e->s.old_origin); printf(",\"velocity\":");vector_json(e->velocity);
    printf(",\"angles\":");vector_json(e->s.angles);printf(",\"mins\":");vector_json(e->mins);printf(",\"maxs\":");vector_json(e->maxs);
    printf(",\"absmin\":");vector_json(e->absmin);printf(",\"absmax\":");vector_json(e->absmax);
    printf(",\"event\":%d,\"groundentity\":%d,\"waterlevel\":%d,\"post_startsolid\":%d,\"post_allsolid\":%d,\"post_trace_fraction\":%.9g,\"post_trace_entity\":%d",e->s.event,entity_id(e->groundentity),e->waterlevel,t.startsolid,t.allsolid,t.fraction,entity_id(t.ent));
    if(e->client) {
        printf(",\"pm_origin\":");short_json(e->client->ps.pmove.origin);printf(",\"pm_velocity\":");short_json(e->client->ps.pmove.velocity);
        printf(",\"pm_time\":%u,\"pm_flags\":%u,\"delta_angles\":",e->client->ps.pmove.pm_time,e->client->ps.pmove.pm_flags);short_json(e->client->ps.pmove.delta_angles);
        printf(",\"viewangles\":");vector_json(e->client->ps.viewangles);printf(",\"v_angle\":");vector_json(e->client->v_angle);printf(",\"cmd_angles\":");vector_json(e->client->resp.cmd_angles);
    }
    printf("}");
}
static void snapshot(const char *kind,edict_t *changed) {
    printf("{\"type\":\"%s\",\"frame\":%u,\"time_ms\":%u,\"game_frame\":%d,\"game_time\":%.9g,\"changed_entity\":%d,\"means_of_death\":%d,\"actor\":",kind,command_frame,elapsed_ms,level.framenum,level.time,entity_id(changed),meansOfDeath);
    state_json(actor);printf(",\"victim\":");state_json(victim);
    printf(",\"source_event\":%d,\"trace_calls\":%u,\"box_queries\":%u,\"link_calls\":%u,\"recorded_message_calls\":%u}\n",pad ? pad->s.event : 0,trace_calls,area_calls,link_calls,network_message_calls);
}
static void link_entity(edict_t *e) {
    SV_LinkEdict(e);++link_calls;
    if(observing && (e==actor || e==victim)) snapshot("link",e);
}
static void unlink_entity(edict_t *e) {
    if(observing && (e==actor || e==victim)) snapshot("before_unlink",e);
    SV_UnlinkEdict(e);
}

static double numeric(char *text,double low,double high) {
    char *end;double v;errno=0;v=strtod(text,&end);
    if(errno || *end || end==text || !isfinite(v) || v<low || v>high) fatal("numeric argument outside bounds");return v;
}
static void place_client(edict_t *e,const vec3_t origin,float yaw) {
    SV_UnlinkEdict(e);VectorCopy(origin,e->s.origin);VectorCopy(origin,e->s.old_origin);VectorClear(e->velocity);
    memset(&e->client->ps.pmove,0,sizeof(e->client->ps.pmove));memset(&e->client->old_pmove,0,sizeof(e->client->old_pmove));
    VectorClear(e->client->resp.cmd_angles);VectorClear(e->client->v_angle);VectorClear(e->client->ps.viewangles);
    e->client->resp.cmd_angles[YAW]=yaw;e->client->v_angle[YAW]=yaw;e->client->ps.viewangles[YAW]=yaw;
    e->groundentity=NULL;e->s.event=0;SV_LinkEdict(e);
}

int main(int argc,char **argv) {
    game_import_t api={0};cmodel_t *world_model;unsigned checksum;int i,occupancy,missing;
    vec3_t start,at;float initial_yaw,dest_yaw;char line[256],extra;double fields[7];
    _Static_assert(sizeof(short)==2 && sizeof(int)==4 && sizeof(unsigned long)==4,"Windows 3.19 integer ABI required");
    if(argc!=10) fatal("usage: native_teleport bsp target startX startY startFeet cmdYaw destYawOr999 occupancy0_1_2 missing0_1");
    if(QMG_ServerOwnerOffset()!=offsetof(edict_t,owner) || QMG_ServerClientOffset()!=offsetof(edict_t,client) || QMG_ServerAreaOffset()!=offsetof(edict_t,area)) fatal("game/server edict prefix ABI differs");
    bsp_path=argv[1]; for(i=0;i<3;i++) start[i]=(float)numeric(argv[3+i],-3000,3000)+(i==2 ? 24.125f : 0);
    initial_yaw=(float)numeric(argv[6],-360,360);dest_yaw=(float)numeric(argv[7],-360,999);
    occupancy=(int)numeric(argv[8],0,2);missing=(int)numeric(argv[9],0,1);
    Cvar_Get("deathmatch","1",0);Cvar_Get("coop","0",0);Cvar_Get("maxclients","2",0);Cvar_Get("maxentities","1024",0);Cvar_Get("dedicated","1",0);Cvar_Get("skill","1",0);Cvar_Get("dmflags","0",0);
    Swap_Init();world_model=CM_LoadMap("teleport.bsp",false,&checksum);
    api.bprintf=bprint;api.dprintf=Com_Printf;api.cprintf=cprint;api.centerprintf=centerprint;api.error=fatal;
    api.sound=sound_event;api.positioned_sound=positioned_sound;api.configstring=config_string;
    api.modelindex=model_index;api.soundindex=sound_index;api.imageindex=image_index;api.setmodel=set_model;
    api.trace=trace_world;api.pointcontents=SV_PointContents;api.inPVS=visibility;api.inPHS=hearability;
    api.SetAreaPortalState=CM_SetAreaPortalState;api.AreasConnected=CM_AreasConnected;
    api.linkentity=link_entity;api.unlinkentity=unlink_entity;api.BoxEdicts=box_entities;api.Pmove=Pmove;
    api.multicast=multicast_event;api.unicast=unicast_event;api.WriteChar=write_integer;api.WriteByte=write_integer;api.WriteShort=write_integer;api.WriteLong=write_integer;api.WriteFloat=write_float;api.WriteString=write_string;api.WritePosition=write_vector;api.WriteDir=write_vector;api.WriteAngle=write_float;
    api.TagMalloc=tag_malloc;api.TagFree=tag_free;api.FreeTags=free_tags;api.cvar=Cvar_Get;api.cvar_set=set_cvar;api.cvar_forceset=set_cvar;
    api.argc=command_argc;api.argv=command_argv;api.args=command_args;api.AddCommandString=add_command;api.DebugGraph=debug_graph;
    module=GetGameAPI(&api);if(module->apiversion!=3 || module->edict_size!=sizeof(edict_t)) fatal("stock game API/edict size differs");
    module->Init();QMG_BindWorld(world_model,module);model_index("maps/teleport.bsp");
    module->SpawnEntities("qmg_fixture",CM_EntityString(),"");QMG_WorldReady();srand(1);
    module->RunFrame();module->RunFrame();
    for(i=1;i<globals.num_edicts;i++) {
        edict_t *e=g_edicts+i;if(!e->inuse) continue;
        if(e->classname && !strcmp(e->classname,"misc_teleporter") && e->target && !strcmp(e->target,argv[2])) { if(pad) fatal("ambiguous teleporter source");pad=e; }
        if(e->targetname && !strcmp(e->targetname,argv[2])) { if(destination) fatal("ambiguous teleporter target");destination=e; }
    }
    if(!pad || !destination) fatal("declared teleporter pair absent");
    for(i=1;i<globals.num_edicts;i++) if(g_edicts[i].inuse && g_edicts[i].owner==pad && g_edicts[i].solid==SOLID_TRIGGER) { if(trigger) fatal("ambiguous generated trigger");trigger=g_edicts+i; }
    if(!trigger) fatal("native teleporter trigger absent");
    if(dest_yaw!=999) destination->s.angles[YAW]=dest_yaw;
    actor=g_edicts+1;{char info[]="\\name\\qmg_actor\\skin\\male/grunt\\ip\\localhost\\fov\\90";if(!module->ClientConnect(actor,info)) fatal("fixture actor connect rejected");module->ClientBegin(actor);}
    if(occupancy==1) { victim=g_edicts+2;{char info[]="\\name\\qmg_occupant\\skin\\male/grunt\\ip\\localhost\\fov\\90";if(!module->ClientConnect(victim,info)) fatal("fixture occupant rejected");module->ClientBegin(victim);} }
    if(actor->health<=0 || (victim && victim->health<=0)) fatal("fixture creation killed a client before placement");
    place_client(actor,start,initial_yaw);
    VectorCopy(destination->s.origin,at);at[2]+=10;
    if(victim) place_client(victim,at,0);
    if(occupancy==2) { victim=G_Spawn();victim->classname="qmg_unkillable_fixture";victim->solid=SOLID_BBOX;victim->movetype=MOVETYPE_NONE;victim->health=100;victim->takedamage=DAMAGE_NO;VectorSet(victim->mins,-16,-16,-24);VectorSet(victim->maxs,16,16,32);VectorCopy(at,victim->s.origin);SV_LinkEdict(victim); }
    if(missing) trigger->target="qmg_missing_destination";
    printf("{\"type\":\"fixture\",\"native_map_checksum\":%u,\"inline_models\":%d,\"game_api\":%d,\"edict_size\":%d,\"source_id\":%d,\"destination_id\":%d,\"trigger_id\":%d,\"destination_origin\":",checksum,CM_NumInlineModels(),module->apiversion,module->edict_size,entity_id(pad),entity_id(destination),entity_id(trigger));vector_json(destination->s.origin);printf(",\"destination_angles\":");vector_json(destination->s.angles);printf(",\"occupancy_mode\":%d,\"missing_target\":%d,\"source_pad\":",occupancy,missing);state_json(pad);printf(",\"destination_pad\":");state_json(destination);printf(",\"trigger\":");state_json(trigger);printf("}\n");
    snapshot("initial",NULL);observing=1;
    while(fgets(line,sizeof(line),stdin)) {
        usercmd_t cmd={0};if(sscanf(line,"%lf %lf %lf %lf %lf %lf %lf %c",fields,fields+1,fields+2,fields+3,fields+4,fields+5,fields+6,&extra)!=7) fatal("command arity");
        for(i=0;i<7;i++) if(!isfinite(fields[i]) || ((i<4 || i==6) && fields[i]!=floor(fields[i]))) fatal("nonfinite/noninteger command");
        if(fields[0]!=16 || fabs(fields[1])>400 || fields[2]!=0 || fields[3]!=0 || fabs(fields[4])>360 || fields[5]!=0 || fields[6]!=0) fatal("unsupported fixed command");
        if(++command_frame>1024) fatal("command cap");elapsed_ms+=16;cmd.msec=16;cmd.forwardmove=(short)fields[1];cmd.angles[YAW]=(short)ANGLE2SHORT(fields[4]);
        trace_calls=area_calls=link_calls=network_message_calls=0;
        module->ClientThink(actor,&cmd);snapshot("after_clientthink",actor);
        while((unsigned)(level.framenum-2)*100<=(elapsed_ms-100) && elapsed_ms>=100) {module->RunFrame();snapshot("after_gameframe",NULL);}
    }
    if(ferror(stdin) || !command_frame) fatal("empty or failed input");observing=0;module->Shutdown();return 0;
}
