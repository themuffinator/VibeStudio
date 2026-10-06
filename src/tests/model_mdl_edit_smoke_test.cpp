#include "core/model_document.h"
#include "tests/model_mdl_test_helpers.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QJsonDocument>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
bool near(float value, double target) { return std::abs(value - target) < .000001; }
QByteArray source(const ModelDocument &document)
{
	return QJsonDocument(editableModelJson(document.mesh())).toJson(QJsonDocument::Compact);
}
QByteArray png(const QImage &image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}
QImage indexed(const ModelMesh &mesh)
{
	QImage result(mesh.mdl.skinSize, QImage::Format_Indexed8);
	result.setColorTable(modelMdlPreviewPalette(mesh).colors);
	const auto &bytes = mesh.embeddedSkins[0].indexedFrames[0];
	for (int y = 0; y < result.height(); ++y)
	{
		for (int x = 0; x < result.width(); ++x)
		{
			result.scanLine(y)[x] = quint8(bytes[y * result.width() + x]);
		}
	}
	return result;
}
QByteArray pcx(const ModelMesh &mesh)
{
	QByteArray bytes(128, '\0');
	bytes[0] = 10;
	bytes[1] = 5;
	bytes[2] = 1;
	bytes[3] = 8;
	bytes[65] = 1;
	qToLittleEndian<quint16>(mesh.mdl.skinSize.width() - 1, bytes.data() + 8);
	qToLittleEndian<quint16>(mesh.mdl.skinSize.height() - 1, bytes.data() + 10);
	qToLittleEndian<quint16>(mesh.mdl.skinSize.width(), bytes.data() + 66);
	for (const auto value : mesh.embeddedSkins[0].indexedFrames[0])
	{
		if (quint8(value) >= 192)
		{
			bytes.append(char(193));
		}
		bytes.append(value);
	}
	bytes.append(char(12));
	bytes += mesh.mdl.palette;
	return bytes;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	auto palette = generatedIdTechPalette("quake");
	palette.colors[1] = palette.colors[0];
	palette.generated = false;
	const auto mesh = decodeModelMesh("fixture.mdl", tests::groupedMdlFixture().bytes, &palette);
	ModelMdlSkinInput input;
	QString error;
	bool ok = true;
	QByteArray lump;
	tests::mdlInteger(lump, 8);
	tests::mdlInteger(lump, 4);
	lump += mesh.embeddedSkins[0].indexedFrames[0];
	for (const auto &file :
		 QVector<QPair<QString, QByteArray>>{{"skin.png", png(indexed(mesh))}, {"skin.pcx", pcx(mesh)}, {"skin.lmp", lump}})
	{
		const bool matches = decodeModelMdlSkin(file.first, file.second, palette, &input, &error) && input.size == mesh.mdl.skinSize &&
							 input.pixels == mesh.embeddedSkins[0].indexedFrames[0] && input.palette == mesh.mdl.palette &&
							 !input.paletteGenerated;
		ok &= expect(matches, "indexed PNG, PCX and LMP preserve exact indices including duplicate palette colours");
		if (!matches)
		{
			std::cerr << file.first.toStdString() << ": " << error.toStdString() << '\n';
		}
	}
	auto transparent = indexed(mesh);
	transparent.setColor(255, qRgba(1, 2, 3, 0));
	for (const auto &bad : {png(indexed(mesh).convertToFormat(QImage::Format_RGB32)), png(transparent), QByteArray("bad")})
	{
		ok &= expect(!decodeModelMdlSkin("bad.png", bad, palette, &input, &error) && !error.isEmpty() &&
						 input.pixels == mesh.embeddedSkins[0].indexedFrames[0],
					 "truecolour, transparent or corrupt imports leave the output intact");
	}
	auto fresh = mesh;
	fresh.mdl = {};
	fresh.embeddedSkins.clear();
	fresh.format = ModelMeshFormat::Unknown;
	ModelDocument document;
	ok &= expect(document.setMesh(fresh, &error), "ordinary mesh needs no MDL settings");
	const auto before = source(document);
	ModelEdit edit;
	edit.kind = ModelEditKind::AddMdlSkin;
	edit.mdlSkin = input;
	edit.text = "custom skin";
	ok &= expect(document.edit(edit, &error) && document.mesh().mdl.enabled && document.mesh().mdl.frameGroups.size() == 3 &&
					 document.mesh().embeddedSkins[0].name == "custom skin" && !exportModelMdl(document.mesh(), &error).isEmpty(),
				 "adding the first indexed skin prepares a fresh model for MDL export");
	ok &= expect(document.undo() && source(document) == before && document.redo(), "MDL preparation is a normal undoable edit");
	edit.kind = ModelEditKind::AppendMdlSkinMember;
	edit.mdlDuration = .2;
	edit.mdlSkin.pixels[0] = char(127);
	ok &= expect(document.edit(edit, &error) && document.mesh().embeddedSkins[0].intervals == QVector<float>{.2f, .4f},
				 "appending promotes a single skin to a group using the chosen hold duration for both members");
	edit.kind = ModelEditKind::SetMdlSkinDuration;
	edit.mdlSkinMember = 0;
	edit.mdlDuration = .1;
	ok &= expect(document.edit(edit, &error) && near(document.mesh().embeddedSkins[0].intervals[1], .3),
				 "skin hold edit moves later cumulative end times");
	edit.kind = ModelEditKind::RemoveMdlSkinMember;
	ok &= expect(document.edit(edit, &error) && document.mesh().embeddedSkins[0].indexedFrames.size() == 1 &&
					 near(document.mesh().embeddedSkins[0].intervals[0], .2) &&
					 document.mesh().embeddedSkins[0].image.pixel(0, 0) == palette.colors[127],
				 "removing first member retains group-of-one semantics and refreshes the preview");
	edit.kind = ModelEditKind::ReplaceMdlSkinMember;
	edit.mdlSkin = input;
	ok &= expect(document.edit(edit, &error) && document.mesh().embeddedSkins[0].indexedFrames[0] == input.pixels &&
					 near(document.mesh().embeddedSkins[0].intervals[0], .2),
				 "replacement changes pixels without discarding timing");
	edit = {};
	edit.kind = ModelEditKind::GroupMdlFrames;
	edit.rangeFirst = 0;
	edit.rangeLast = 2;
	edit.mdlDuration = .25;
	ok &= expect(document.edit(edit, &error) && document.mesh().mdl.frameGroups.size() == 1 &&
					 document.mesh().mdl.frameGroups[0].intervals == QVector<float>{.25f, .5f, .75f},
				 "native grouping changes grouping without changing poses");
	edit.kind = ModelEditKind::SetMdlFrameDuration;
	edit.frame = 1;
	edit.mdlDuration = .5;
	ok &= expect(document.edit(edit, &error) && document.mesh().mdl.frameGroups[0].intervals == QVector<float>{.25f, .75f, 1.f},
				 "native pose duration shifts only its own and following end times");
	edit.kind = ModelEditKind::UngroupMdlFrames;
	edit.rangeFirst = edit.rangeLast = 1;
	ok &= expect(document.edit(edit, &error) && document.mesh().mdl.frameGroups.size() == 3 &&
					 document.mesh().mdl.frameGroups[0].intervals == QVector<float>{.25f} &&
					 document.mesh().mdl.frameGroups[1].intervals.isEmpty() &&
					 document.mesh().mdl.frameGroups[2].intervals == QVector<float>{.25f},
				 "partial ungroup retains both untouched timed segments and their hold durations");
	edit.kind = ModelEditKind::GroupMdlFrames;
	edit.rangeFirst = 1;
	edit.rangeLast = 2;
	edit.mdlDuration = .125;
	ok &= expect(document.edit(edit, &error) && document.mesh().mdl.frameGroups.size() == 2 &&
					 document.mesh().mdl.frameGroups[1].intervals == QVector<float>{.125f, .25f},
				 "grouping may span existing single and grouped native frames");
	edit = {};
	edit.kind = ModelEditKind::SetMdlHeader;
	edit.mdlSettings = document.mesh().mdl;
	edit.mdlSettings.flags = 0xffffffffu;
	edit.mdlSettings.eyePosition = {1, 2, 3};
	edit.mdlSettings.size = 5;
	edit.mdlSettings.syncType = 1;
	ok &= expect(document.edit(edit, &error) && document.mesh().mdl.flags == 0xffffffffu && document.mesh().mdl.frameGroups.size() == 2,
				 "header edit retains native ranges and every unsigned flag bit");
	edit.kind = ModelEditKind::SetMdlPalette;
	edit.mdlSettings.palette[0] = char(42);
	ok &= expect(document.edit(edit, &error) && qRed(document.mesh().embeddedSkins[0].image.pixel(0, 0)) == 42 &&
					 document.mesh().embeddedSkins[0].indexedFrames[0] == input.pixels,
				 "palette edit changes preview colours without remapping any index");
	const auto stable = source(document), digest = document.revisionFingerprint();
	edit.kind = ModelEditKind::ReplaceMdlSkinMember;
	edit.mdlSkin = input;
	ok &= expect(!document.edit(edit, &error) && source(document) == stable && document.revisionFingerprint() == digest,
				 "mismatching palettes fail atomically instead of silently converting indices");
	edit.kind = ModelEditKind::SetMdlHeader;
	edit.mdlSettings.syncType = 2;
	ok &= expect(!document.edit(edit, &error) && source(document) == stable, "invalid native metadata cannot enter history");
	edit.kind = ModelEditKind::SetMdlFrameDuration;
	edit.frame = 1;
	edit.mdlDuration = 0;
	ok &= expect(!document.edit(edit, &error) && source(document) == stable, "zero duration fails atomically");
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	ok &= expect(!decodeModelMdlSkin("skin.pcx", pcx(mesh), palette, &input, &error, cancelled) &&
					 !document.edit(edit, &error, cancelled) && source(document) == stable,
				 "cancelled image and metadata edits preserve their outputs");
	edit = {};
	edit.kind = ModelEditKind::RemoveMdlSkin;
	ok &= expect(document.edit(edit, &error) && document.mesh().embeddedSkins.isEmpty() &&
					 exportModelMdl(document.mesh(), &error).isEmpty() && document.undo() && source(document) == stable,
				 "last-slot removal is reversible; export explains the missing required skin");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
