#pragma once

// Time evaluation for materials: everything that changes while a material
// plays, computed the way each engine computes it.
//
// - Quake III waveforms use the engine's 1024-entry function tables
//   (tr_init.c R_Init) and its lattice noise (tr_noise.c R_NoiseGet4f, seeded
//   with srand(1001) and the Microsoft C runtime's rand() like id's Windows
//   build), so a wave samples the same values as in game.
// - Doom 3 expressions follow idMaterial::EvaluateRegisters: comparisons and
//   logic give 1 or 0, `%` truncates both sides to integers, `/` is plain
//   float division, and table lookups follow idDeclTable::TableLookup,
//   including its single-value quirk.
// - Classic frames advance on each engine's clock: Doom tics (35 a second),
//   Quake's 0.2 s frames, Quake II's 2 Hz world animation.
// - Quake light styles step at 10 Hz, 'a' dark to 'z' double bright, 'm'
//   normal, as in Quake's R_AnimateLight; the preset strings are Quake's
//   world.qc light styles.
//
// Sources are credited in docs/CREDITS.md ("Materials").

#include "core/material_model.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <array>

namespace vibestudio {

// --- Quake III ---------------------------------------------------------------

inline constexpr int kQuake3FunctionTableSize = 1024;

// One entry of a function table, index wrapped to 0..1023. Noise has no
// table (TableForFunc returns none for it).
double quake3FunctionTable(MaterialWaveFunction function, int index);
// EvalWaveForm: base + table[(phase + time * frequency) * 1024 & 1023] * amp,
// truncating like ioquake3. Noise evaluates R_NoiseGet4f.
double evaluateQuake3Wave(const MaterialWave& wave, double time);
double evaluateQuake3WaveClamped(const MaterialWave& wave, double time);
// R_NoiseGet4f: 4-D value noise with linear interpolation, -1..1.
double quake3Noise(double x, double y, double z, double t);

// --- Doom 3 ------------------------------------------------------------------

struct MaterialEvalContext {
	// Seconds.
	double time = 0.0;
	// Entity shader parms: parm0-3 colour (default 1), parm4 time offset.
	std::array<double, 12> parms {1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	std::array<double, 8> globals {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	// The entity's sound amplitude, 0..1.
	double sound = 0.0;
	bool fragmentPrograms = true;
};

// Tables visible to expressions, by case-insensitive name.
class MaterialTableSet {
public:
	void add(const MaterialTable& table);
	void addAll(const QVector<MaterialTable>& tables);
	[[nodiscard]] const MaterialTable* find(const QString& name) const;
	[[nodiscard]] bool isEmpty() const { return m_tables.isEmpty(); }
	[[nodiscard]] QStringList names() const;
	// Adds procedural stand-ins for the tables Doom 3's tables.mtr ships
	// (sinTable, cosTable, squareTable, triangleTable, sawtoothTable,
	// inverseSawtoothTable) when no real one is loaded. They are marked
	// generated; their exact retail values are game data.
	void addStandIns();

private:
	QHash<QString, MaterialTable> m_tables;
};

// idDeclTable::TableLookup. A table with one value (or none) returns 1.
double lookupMaterialTable(const MaterialTable& table, double index);

// Every node of a material's expression pool, evaluated once. Unknown tables
// evaluate to 0 and are reported through `missingTables`.
QVector<double> evaluateMaterialExpressions(const MaterialDefinition& definition, const MaterialTableSet& tables,
	const MaterialEvalContext& context, QStringList* missingTables = nullptr);
// One expression from a pool, for the graph editor and tests.
double evaluateMaterialExpression(const QVector<MaterialExpressionNode>& nodes, int root, const MaterialTableSet& tables,
	const MaterialEvalContext& context);

// --- Classic frames ----------------------------------------------------------

// The frame showing at `time`. Frames with a maximum duration (ANIMDEFS
// rand) pick a duration per cycle from `seed`, deterministically.
int materialFrameIndexAt(const QVector<MaterialFrame>& frames, double time, quint32 seed = 0);
// Total length of one cycle using minimum durations.
double materialFramesCycleSeconds(const QVector<MaterialFrame>& frames);

// --- Quake light styles ------------------------------------------------------

struct QuakeLightStyle {
	int number = 0;
	QString id;
	QString name;
	QString pattern;
};

// Quake's world.qc styles 0-11, the ones mappers pick with a light's style.
QVector<QuakeLightStyle> quakeLightStyles();
// The style's brightness at `time`, where 'm' is 1.0 (264/256 in Quake,
// reported here relative to 'm' so the normal style is exactly 1).
double quakeLightStyleValue(const QString& pattern, double time);
// The raw Quake value: (c - 'a') * 22, 256 for an empty pattern.
int quakeLightStyleRaw(const QString& pattern, double time);

} // namespace vibestudio
