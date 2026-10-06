// Original, asset-free FTE dedicated-server acceptance fixture.
// QuakeC API/ABI names follow id Software's GPL-2.0-or-later progdefs.q1
// and FTE's GPL-2.0-or-later engine/server/pr_cmds.c, reviewed 2026-10-05.
// See docs/CREDITS.md. No gamecode implementation or game assets are copied.

entity self, other, world;
float time, frametime;
string mapname;
float trace_allsolid, trace_startsolid, trace_fraction;
vector trace_endpos, trace_plane_normal;
float trace_plane_dist;
entity trace_ent;
vector v_forward, v_right, v_up;
void() main, StartFrame, PlayerPreThink, PlayerPostThink, ClientKill, ClientConnect, PutClientInServer, ClientDisconnect;
void end_sys_globals;

.float modelindex, movetype, solid, frame, skin, flags;
.vector origin, angles, mins, maxs, size;
.string classname, model, wad;
.float frame1time, frame2, frame2time, lerpfrac;
.void end_sys_fields;

void(entity e, vector p) setorigin = #2;
void(entity e, string m) setmodel = #3;
void(entity e, vector lo, vector hi) setsize = #4;
float(vector v) vlen = #12;
entity() spawn = #14;
void(entity e) remove = #15;
void(vector start, vector end, float flags, entity ignore) traceline = #16;
string(string name) precache_model = #20;
void(string text) dprint = #25;
string(float n) ftos = #26;
string(vector v) vtos = #27;
float(float yaw, float distance) walkmove = #32;
void(string text) localcmd = #46;
float(float n) fabs = #43;
string(...) strcat = #0;
float(float index) modelframecount = #0;
string(float index, float frame) frametoname = #0;
float(float index, float frame) frameduration = #0;
float(entity e, string name) gettagindex = #0;
vector(entity e, float index) gettaginfo = #0;
void(vector start, vector lo, vector hi, vector end, float flags, entity ignore) tracebox = #0;

float passed, failed, finished;

void(float condition, string label) expect =
{
	if (condition)
	{
		passed = passed + 1;
		dprint(strcat("VSMODEL PASS ", label, "\n"));
	}
	else
	{
		failed = failed + 1;
		dprint(strcat("VSMODEL FAIL ", label, "\n"));
	}
};

void(string path, string label, string first, string second) model_test =
{
	local entity item;
	local float pose;
	item = spawn();
	setmodel(item, path);
	expect(modelframecount(item.modelindex) == 2, strcat(label, "-frame-count"));
	expect(frametoname(item.modelindex, 0) == first, strcat(label, "-frame-0-name"));
	expect(frametoname(item.modelindex, 1) == second, strcat(label, "-frame-1-name"));
	dprint(strcat("VSMODEL SAMPLE ", label, "-names ", frametoname(item.modelindex, 0), " ", frametoname(item.modelindex, 1), "\n"));
	item.solid = 2;
	setsize(item, '-32 -32 -32', '32 32 32');
	setorigin(item, '180 0 64');
	for (pose = 0; pose < 2; pose = pose + 1)
	{
		item.frame = pose;
		item.frame2 = pose;
		item.lerpfrac = 0;
		// MOVE_HITMODEL (4) asks the engine to trace the decoded pose's mesh.
		traceline('182 3 110', '182 3 40', 4, world);
		dprint(strcat("VSMODEL SAMPLE ", label, "-pose-", ftos(pose), " ", vtos(trace_endpos), "\n"));
		expect(trace_ent == item && trace_fraction > 0 && trace_fraction < 1 &&
			fabs(trace_endpos_z - (64 + 8 * pose)) < 0.2, strcat(label, "-pose-", ftos(pose)));
		traceline('182 3 40', '182 3 110', 4, world);
		expect(trace_fraction == 1, strcat(label, "-backface-", ftos(pose)));
	}
	remove(item);
};

void() tags_test =
{
	local entity item;
	local float tag;
	local vector position;
	item = spawn();
	setmodel(item, "progs/tagged.md3");
	tag = gettagindex(item, "tag_mount");
	expect(tag > 0 && gettagindex(item, "missing_tag") == 0, "md3-tag-identity");
	position = gettaginfo(item, tag);
	expect(vlen(position - '2 3 4') < 0.001, "md3-tag-pose-0-origin");
	expect(vlen(v_forward - '1 0 0') < 0.001 && vlen(v_up - '0 0 1') < 0.001, "md3-tag-pose-0-axes");
	item.frame = 1;
	item.frame2 = 1;
	position = gettaginfo(item, tag);
	expect(vlen(position - '5 7 9') < 0.001, "md3-tag-pose-1-origin");
	dprint(strcat("VSMODEL SAMPLE tag-forward ", vtos(v_forward), " up ", vtos(v_up), "\n"));
	expect(vlen(v_forward - '0 1 0') < 0.001 && vlen(v_up - '0 0 1') < 0.001, "md3-tag-pose-1-axes");
	remove(item);
};

void() collision_test =
{
	local entity pawn, saved;
	local float small_end;
	pawn = spawn();
	pawn.flags = 1; // FL_FLY: exercise walkmove without gravity or stair stepping.
	// SOLID_SLIDEBOX selects the engine's player-clip mask for sized traces.
	// A zero-sized trace still passes clip. See FTE server/world.c and credits.
	pawn.solid = 3;
	setsize(pawn, '-16 -16 -24', '16 16 32');
	setorigin(pawn, '180 16 64');
	traceline('180 16 64', '72 16 64', 0, pawn);
	expect(trace_fraction == 1 && !trace_startsolid, "clip-point-pass-through");
	tracebox('180 16 64', '-16 -16 -24', '16 16 32', '72 16 64', 0, pawn);
	small_end = trace_endpos_x;
	dprint(strcat("VSMODEL SAMPLE player-hull ", vtos(trace_endpos), "\n"));
	expect(!trace_startsolid && trace_fraction > 0 && trace_fraction < 1 &&
		small_end > 90 && small_end < 140, "clip-player-hull-blocked");
	tracebox('180 16 64', '-32 -32 -24', '32 32 64', '72 16 64', 0, pawn);
	expect(!trace_startsolid && trace_fraction > 0 && trace_fraction < 1 &&
		trace_endpos_x > small_end + 8, "clip-large-hull-blocked-earlier");
	tracebox('72 16 64', '-16 -16 -24', '16 16 32', '72 16 64', 0, pawn);
	expect(trace_startsolid, "clip-player-hull-start-solid");
	tracebox('180 120 64', '-16 -16 -24', '16 16 32', '72 120 64', 0, pawn);
	expect(trace_fraction == 1 && !trace_startsolid, "clip-clear-path");
	tracebox('180 120 100', '-16 -16 -24', '16 16 32', '180 120 -100', 0, pawn);
	expect(trace_fraction > 0 && trace_fraction < 1 && fabs(trace_endpos_z - 24) < 0.2, "room-floor");
	saved = self;
	self = pawn;
	expect(walkmove(180, 20) == 1 && fabs(pawn.origin_x - 160) < 0.001, "walkmove-clear");
	expect(walkmove(180, 88) == 0 && fabs(pawn.origin_x - 160) < 0.001, "walkmove-clip-blocked");
	self = saved;
	remove(pawn);
};

void() worldspawn =
{
	precache_model("progs/flat.mdl");
	precache_model("progs/grouped.mdl");
	precache_model("progs/flat.md2");
	precache_model("progs/flat.md3");
	precache_model("progs/tagged.md3");
};

void() StartFrame =
{
	local entity item;
	if (finished)
		return;
	finished = 1;
	model_test("progs/flat.mdl", "mdl", "rest", "lift");
	model_test("progs/flat.md2", "md2", "rest", "lift");
	// FTE synthesizes MD3 frame names; the runner audits the file's actual names.
	model_test("progs/flat.md3", "md3", "frame0", "frame1");
	item = spawn();
	setmodel(item, "progs/grouped.mdl");
	expect(modelframecount(item.modelindex) == 1, "mdl-group-frame-count");
	expect(fabs(frameduration(item.modelindex, 0) - 0.4) < 0.001, "mdl-group-duration");
	remove(item);
	tags_test();
	collision_test();
	dprint(strcat("VSMODEL COMPLETE ", ftos(passed), " ", ftos(failed), "\n"));
	localcmd("quit\n");
};

void() main = {};
void() PlayerPreThink = {};
void() PlayerPostThink = {};
void() ClientKill = {};
void() ClientConnect = {};
void() PutClientInServer = {};
void() ClientDisconnect = {};
void() info_player_start = {};
void() info_player_deathmatch = {};
