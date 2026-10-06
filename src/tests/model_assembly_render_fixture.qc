// Original baked-assembly MenuQC fixture using FTE's public pr_menu.c,
// pr_common.h and pr_csqc.c ABI, reviewed 2026-10-06. See docs/CREDITS.md.
// MenuQC's system ABI contains self and no system fields (CRC 10020).
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
void() clearscene = #0;
void(float property, ...) setproperty = #0;
void(entity e, vector direction, vector ambient, vector diffuse) addentity_lighting = #0;
void() renderscene = #0;
void(vector position, vector size, vector rgb, float alpha, float flags) drawfill = #0;

entity md2, md3;
float phase, draws;
void(float condition, string label) expect =
{
	if (condition) print(strcat("VSASSEMBLY RENDER PASS ", label, "\n"));
	else print(strcat("VSASSEMBLY RENDER FAIL ", label, "\n"));
};
void(entity panel, string path, string label, string first, string last) load_panel =
{
	setmodel(panel, path);
	expect(modelframecount(panel.modelindex) == 5, strcat(label, "-frame-count"));
	expect(frametoname(panel.modelindex, 0) == first, strcat(label, "-first-frame"));
	expect(frametoname(panel.modelindex, 4) == last, strcat(label, "-last-frame"));
};
string() phase_name =
{
	if (phase == 0) return "pose-0";
	if (phase == 1) return "pose-1";
	if (phase == 2) return "pose-2";
	if (phase == 3) return "pose-3";
	if (phase == 4) return "pose-4";
	if (phase == 5) return "blend-0-1";
	return "blend-1-2";
};
void(entity panel) draw_panel =
{
	panel.frame = phase;
	panel.frame2 = phase;
	panel.lerpfrac = 0;
	panel.frame1time = 0;
	panel.frame2time = 0;
	if (phase >= 5)
	{
		panel.frame = phase - 5;
		panel.frame2 = phase - 4;
		panel.lerpfrac = 0.5;
	}
	addentity_lighting(panel, '0 0 1', '1 1 1', '0 0 0');
};
void() m_init =
{
	precache_model("progs/assembly_md2.md2");
	precache_model("progs/assembly_md3.md3");
	md2 = spawn(); md2.origin = '-60 0 0';
	md3 = spawn(); md3.origin = '60 0 0';
	load_panel(md2, "progs/assembly_md2.md2", "md2", "bake0000", "bake0004");
	load_panel(md3, "progs/assembly_md3.md3", "md3", "frame0", "frame4");
};
void(vector size) m_draw =
{
	if (phase >= 7) return;
	drawfill('0 0 0', size, '0.05 0.05 0.05', 1, 0);
	clearscene();
	setproperty(4, size); // VF_SIZE
	setproperty(8, '90 73.739795 0'); // VF_FOV, square pixels at 640x480.
	setproperty(11, '0 0 140'); // VF_ORIGIN
	setproperty(15, '90 90 0'); // VF_ANGLES: +X screen-right, +Y screen-up.
	draw_panel(md2);
	draw_panel(md3);
	renderscene();
	draws = draws + 1;
	if (draws == 40)
	{
		print(strcat("VSASSEMBLY RENDER CAPTURE ", phase_name(), "\n"));
		localcmd(strcat("screenshot renders/", phase_name(), ".png\n"));
	}
	if (draws == 60)
	{
		phase = phase + 1;
		draws = 0;
		if (phase == 7)
		{
			print("VSASSEMBLY RENDER COMPLETE\n");
			localcmd("quit\n");
		}
	}
};
void() m_shutdown = {};
void(float mode) m_toggle = {};
