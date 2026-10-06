// Original FTE menu-VM render fixture, using public interfaces from pr_menu.c,
// pr_common.h and pr_csqc.c, reviewed 2026-10-05. See docs/CREDITS.md.
// The MenuQC system ABI contains self and no system fields (CRC 10020).
entity self;
void end_sys_globals;
void end_sys_fields;
.string model;
.vector origin, angles;
.float modelindex, frame, frame2, lerpfrac, frame1time, frame2time, skin;

entity() spawn = #0;
void(string text) print = #0;
string(...) strcat = #0;
void(string text) localcmd = #0;
void(string name) precache_model = #0;
void(entity e, string name) setmodel = #0;
float(float index) modelframecount = #0;
string(float index, float frame) frametoname = #0;
float(float index, float frame) frameduration = #0;
float(float value) fabs = #0;
void() clearscene = #0;
void(float property, ...) setproperty = #0;
void(entity e, vector direction, vector ambient, vector diffuse) addentity_lighting = #0;
void() renderscene = #0;
void(vector position, vector size, vector rgb, float alpha, float flags) drawfill = #0;

entity mdl, md2, md3, grouped;
float phase, draws;
void(float condition, string label) expect =
{
	if (condition) print(strcat("VSMODEL RENDER PASS ", label, "\n"));
	else print(strcat("VSMODEL RENDER FAIL ", label, "\n"));
};
void(entity panel, string path, string label, string first, string second) load_panel =
{
	// setmodel waits for loading; frame identities catch substituted models.
	setmodel(panel, path);
	expect(modelframecount(panel.modelindex) == 2, strcat(label, "-frame-count"));
	expect(frametoname(panel.modelindex, 0) == first, strcat(label, "-frame-0-name"));
	expect(frametoname(panel.modelindex, 1) == second, strcat(label, "-frame-1-name"));
};
string() phase_name =
{
	if (phase == 0) return "pose-0";
	if (phase == 1) return "pose-1";
	if (phase == 2) return "blend";
	if (phase == 3) return "group-0";
	return "group-1";
};
void(entity panel) draw_panel =
{
	panel.frame = 0;
	panel.frame2 = 0;
	panel.lerpfrac = 0;
	panel.frame1time = 0;
	panel.frame2time = 0;
	if (phase == 1)
	{
		panel.frame = 1;
		panel.frame2 = 1;
	}
	if (phase == 2)
	{
		panel.frame2 = 1;
		panel.lerpfrac = 0.5;
	}
	if (panel == grouped)
	{
		if (phase == 3) panel.frame1time = 0.05;
		else panel.frame1time = 0.25;
		panel.frame2time = panel.frame1time;
	}
	addentity_lighting(panel, '0 0 1', '1 1 1', '0 0 0');
};
void() m_init =
{
	precache_model("progs/panel_mdl.mdl");
	precache_model("progs/panel_md2.md2");
	precache_model("progs/panel_md3.md3");
	precache_model("progs/grouped.mdl");
	mdl = spawn(); mdl.origin = '-48 0 0';
	md2 = spawn();
	md3 = spawn(); md3.origin = '48 0 0';
	grouped = spawn(); grouped.origin = '-48 0 0';
	load_panel(mdl, "progs/panel_mdl.mdl", "mdl", "rest", "lift");
	load_panel(md2, "progs/panel_md2.md2", "md2", "rest", "lift");
	load_panel(md3, "progs/panel_md3.md3", "md3", "frame0", "frame1");
	setmodel(grouped, "progs/grouped.mdl");
	expect(modelframecount(grouped.modelindex) == 1, "group-frame-count");
	expect(fabs(frameduration(grouped.modelindex, 0) - 0.4) < 0.001, "group-duration");
	print("VSMODEL RENDER INIT\n");
};
void(vector size) m_draw =
{
	if (phase >= 5) return;
	drawfill('0 0 0', size, '0.05 0.05 0.05', 1, 0);
	clearscene();
	setproperty(4, size); // VF_SIZE
	setproperty(8, '90 73.739795 0'); // VF_FOV: square pixels at 640x480.
	setproperty(11, '0 0 96'); // VF_ORIGIN
	setproperty(15, '90 90 0'); // VF_ANGLES: +X screen-right, +Y screen-up.
	if (phase >= 3) draw_panel(grouped);
	else draw_panel(mdl);
	draw_panel(md2);
	draw_panel(md3);
	renderscene();
	draws = draws + 1;
	if (draws == 40)
	{
		print(strcat("VSMODEL RENDER CAPTURE ", phase_name(), "\n"));
		localcmd(strcat("screenshot renders/", phase_name(), ".png\n"));
	}
	// Hold the scene unchanged while the queued screenshot reads its buffer.
	if (draws == 60)
	{
		phase = phase + 1;
		draws = 0;
		if (phase == 5)
		{
			print("VSMODEL RENDER COMPLETE\n");
			localcmd("quit\n");
		}
	}
};
void() m_shutdown = {};
void(float mode) m_toggle = {};
