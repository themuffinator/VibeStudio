#pragma once

#include "core/model_mesh.h"

#include <QJsonObject>

namespace vibestudio
{
struct ModelExportReport;
struct ModelEdit;
enum class ModelEditKind;

inline constexpr qint64 modelMdlMaxSkinPixels = 16 * 1024 * 1024;
inline constexpr int modelMdlMaxSkinMembers = 256;

// Native MDL groups, exact indexed pixels and header settings remain separate
// from inferred/user-authored animation clips. The latter are editor metadata.
QStringList validateModelMdl(const ModelMesh &mesh, const ModelWorkControl &control = {});
QByteArray modelMdlPaletteBytes(const IdTechPalette &palette);
QImage modelMdlSkinImage(const QByteArray &pixels, QSize size, const QByteArray &palette, const ModelWorkControl &control = {});
qint64 modelMdlStorageBytes(const ModelMesh &mesh);

enum class ModelMdlTiming
{
	Stored,
	GlQuake
};
struct ModelMdlPlayback
{
	int nativeFrame = 0, skin = 0;
	ModelMdlTiming timing = ModelMdlTiming::Stored;
	double seconds = 0, syncPhase = 0;
};
struct ModelMdlPlaybackSample
{
	int frame = 0, skinMember = 0;
	double frameCycle = 0, skinCycle = 0;
};
// Discrete native timing, independent of editor clips/FPS. syncPhase is a
// deterministic 0..1 second entity phase, applied only to random-sync software
// Quake models. GLQuake ignores it. Invalid requests leave output untouched.
bool sampleModelMdl(const ModelMesh &mesh, const ModelMdlPlayback &playback, ModelMdlPlaybackSample *output, QString *error = nullptr);
// Decode a selected skin once on the document worker, bounded to 16M opaque
// premultiplied pixels (64 MiB). Playback itself never decodes or copies pixels.
bool prepareModelMdlPlaybackSkins(const ModelMesh &mesh, int skin, QVector<QImage> *output, QString *error = nullptr,
								  const ModelWorkControl &control = {});

struct ModelMdlSkinInput
{
	QSize size;
	QByteArray pixels, palette;
	bool paletteGenerated = false;
	// Decoder provenance, independent of whether RGB entries equal the fallback.
	bool paletteEmbedded = false;
};
// PCX, indexed PNG, Quake LMP/miptextures, WAL and M8 use the shared bounded
// image decoder. Raw indices survive identical RGB colours in palette entries.
IdTechPalette modelMdlPreviewPalette(const ModelMesh &mesh);
bool decodeModelMdlSkin(const QString &path, const QByteArray &bytes, const IdTechPalette &fallbackPalette, ModelMdlSkinInput *skin,
						QString *error = nullptr, const ModelWorkControl &control = {});
bool isModelMdlEdit(ModelEditKind kind);
// Candidate-only mutation, called by applyModelEdit's atomic validation path.
bool applyModelMdlEdit(ModelMesh *mesh, const ModelEdit &edit, QString *error = nullptr, const ModelWorkControl &control = {});

QJsonObject modelMdlSettingsJson(const ModelMdlSettings &settings);
bool parseModelMdlSettings(const QJsonValue &value, ModelMdlSettings *settings, QString *error = nullptr,
						   const ModelWorkControl &control = {});
QJsonObject modelMdlSkinJson(const ModelEmbeddedSkin &skin);
bool parseModelMdlSkin(const QJsonValue &value, const ModelMdlSettings &settings, ModelEmbeddedSkin *skin, qint64 *totalPixels,
					   QString *error = nullptr, const ModelWorkControl &control = {});

enum class ModelMdlFrameChange
{
	Duplicate,
	Delete,
	InsertInbetweens,
};
// Called before changing pose arrays. Group timing changes atomically with the
// mesh edit: duplication copies duration, deletion removes it, and in-betweens
// inside a group divide the preceding pose's hold time without extending it.
bool changeModelMdlFrames(ModelMesh *mesh, ModelMdlFrameChange change, int frame, int count = 1, QString *error = nullptr);

QByteArray exportModelMdl(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {},
						  ModelExportReport *report = nullptr);
} // namespace vibestudio
