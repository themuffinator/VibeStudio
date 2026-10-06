#include "core/texture_project.h"
#include "core/texture_export.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>

#include <iostream>
#include <functional>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QByteArray revisedMetadata(const QByteArray& source, const std::function<void(QJsonObject&)>& change)
{
	const int previousLength = qFromLittleEndian<quint32>(source.constData() + 12);
	auto root = QJsonDocument::fromJson(source.mid(16, previousLength)).object(); change(root);
	const auto json = QJsonDocument(root).toJson(QJsonDocument::Compact);
	QByteArray bytes = source.first(16); qToLittleEndian<quint32>(quint32(json.size()), bytes.data() + 12);
	bytes += json; bytes += source.mid(16 + previousLength, source.size() - 48 - previousLength);
	bytes += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); return bytes;
}
bool samePixels(const QImage& a, const QImage& b) { return a.convertToFormat(QImage::Format_ARGB32) == b.convertToFormat(QImage::Format_ARGB32); }
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return EXIT_FAILURE; }
	const QDir root(temporary.path()); bool ok = true; QString error;
	TextureDocument document;
	if (!document.create({8, 8}, Qt::red, &error) || !document.addLayer(QStringLiteral("Ink"), {}, &error)) { std::cerr << error.toStdString(); return EXIT_FAILURE; }
	document.markSaved();
	ok &= expect(document.layers().size() == 2 && document.activeLayerIndex() == 1 && document.image().pixelColor(0, 0) == QColor(Qt::red), "transparent layer preserves underlying appearance");
	ok &= expect(document.paintStroke({{0, 0}}, QColor(0, 0, 255, 128), 1, &error), "paint partial alpha into active layer");
	const auto blended = document.image().pixelColor(0, 0);
	ok &= expect(std::abs(blended.red() - 127) <= 1 && std::abs(blended.blue() - 128) <= 1 && blended.alpha() == 255 && document.layers()[0].pixels.pixelColor(0, 0) == QColor(Qt::red), "normal compositing has the expected alpha and does not paint the base layer");
	ok &= expect(document.undo() && !document.isDirty() && document.redo() && document.isDirty(), "layer pixel history returns to the exact saved revision");
	ok &= expect(document.setLayerProperties(QStringLiteral("Ink"), false, true, 100, TextureBlendMode::Normal) && document.image().pixelColor(0, 0) == QColor(Qt::red), "hidden layers do not composite");
	const auto locked = encodeTextureProject(document);
	ok &= expect(!document.paintStroke({{1, 1}}, Qt::blue, 1, &error) && !document.floodFill({1, 1}, Qt::blue, &error) && !document.removeLayer(&error) && encodeTextureProject(document) == locked, "locked and hidden pixel actions cannot mutate content or structure");
	ok &= expect(document.undo(), "visibility and lock properties undo together");
	ok &= expect(document.setSelection({2, 2, 2, 2}) && document.paintStroke({{0, 3}, {7, 3}}, Qt::blue, 3), "wide strokes clip to selection bounds");
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			if (x == 0 && y == 0) { continue; }
			ok &= expect(document.activeLayer()->pixels.pixelColor(x, y).alpha() == (QRect(2, 2, 2, 2).contains(x, y) ? 255 : 0), "selection leaves every outside pixel untouched");
		}
	}
	ok &= expect(document.floodFill({2, 2}, Qt::green) && document.copySelection().size() == QSize(2, 2), "fill and copy use selection dimensions");
	ok &= expect(!document.floodFill({0, 1}, Qt::yellow) && !document.setSelection({-1, 0, 2, 2}), "outside fill seed and selection are rejected");
	ok &= expect(document.moveSelectedPixels({3, 1}) && document.selection() == QRect(5, 3, 2, 2) && document.activeLayer()->pixels.pixelColor(2, 2).alpha() == 0 && document.activeLayer()->pixels.pixelColor(5, 3) == QColor(Qt::green), "move cuts the source and places selected pixels");
	ok &= expect(document.undo() && document.selection() == QRect(2, 2, 2, 2) && document.activeLayer()->pixels.pixelColor(2, 2) == QColor(Qt::green), "moving pixels restores selection and content on undo");
	ok &= expect(!document.moveSelectedPixels({100, 0}), "move cannot silently clip pixels outside the canvas");
	ok &= expect(document.clearSelectedPixels() && document.activeLayer()->pixels.pixelColor(2, 2).alpha() == 0 && document.undo(), "clear is reversible and uses explicit alpha replacement");
	QImage paste(3, 3, QImage::Format_ARGB32); paste.fill(Qt::yellow);
	ok &= expect(document.pastePixels(paste, {3, 3}) && document.activeLayer()->pixels.pixelColor(3, 3) == QColor(Qt::yellow) && document.activeLayer()->pixels.pixelColor(4, 4).alpha() == 0, "paste respects selection clipping");
	document.clearSelection();
	const QImage visible = document.image();
	ok &= expect(document.mergeDown() && document.layers().size() == 1 && samePixels(document.image(), visible) && document.undo() && document.layers().size() == 2, "merge normal layers preserves the composite and undo restores both layers");
	ok &= expect(document.duplicateLayer() && document.layers().size() == 3 && document.activeLayer()->id != document.layers()[1].id, "duplicate retains pixels with a fresh stable layer id");
	const int copiedId = document.activeLayer()->id;
	ok &= expect(document.moveLayer(0) && document.activeLayer()->id == copiedId && document.activeLayerIndex() == 0 && document.undo() && document.activeLayerIndex() == 2, "layer ordering and active layer restore on undo");
	ok &= expect(document.removeLayer() && document.layers().size() == 2 && document.undo() && document.layers().size() == 3, "removed layers are fully recoverable");
	ok &= expect(document.setLayerProperties(QStringLiteral("Screen ink"), true, false, 37, TextureBlendMode::Screen), "blend and opacity properties accept supported values");
	const QImage screen = document.image();
	ok &= expect(!document.mergeDown(), "backdrop-dependent blend cannot merge incorrectly");
	ok &= expect(document.flatten() && document.layers().size() == 1 && samePixels(document.image(), screen) && document.undo(), "flatten composites all visible blend modes and is reversible");
	ok &= expect(document.crop({1, 1, 6, 4}) && document.layers()[0].pixels.size() == QSize(6, 4) && document.layers()[2].pixels.size() == QSize(6, 4) && document.undo(), "canvas crop changes every layer in one undo unit");
	ok &= expect(document.resize({4, 6}, false) && document.layers()[2].pixels.size() == QSize(4, 6) && document.rotateClockwise() && document.size() == QSize(6, 4) && document.undo() && document.undo(), "resize and canvas rotation preserve aligned layer dimensions");
	ok &= expect(document.historyBytes() > 0 && document.historyBytes() < 128 * 1024 * 1024, "history counts shared pixel buffers once and stays bounded");

	IdTechPaletteResolution resolution; resolution.palette = generatedIdTechPalette(QStringLiteral("quake"));
	resolution.palette.colors[17] = qRgba(13, 27, 42, 97); resolution.palette.generated = false;
	resolution.fromPackage = true; resolution.sourceVirtualPath = QStringLiteral("gfx/palette.lmp"); resolution.sourcePackagePath = QStringLiteral("project/pak0.pak");
	TextureExportOptions exportOptions; exportOptions.format = TextureExportFormat::Quake2Wal; exportOptions.surfaceFlags = 0xffffffffu; exportOptions.animationNext = QStringLiteral("author/wall2");
	const QJsonObject metadata{{QStringLiteral("export"), textureExportOptionsJson(exportOptions)}, {QStringLiteral("palette"), texturePaletteMetadata(resolution)}, {QStringLiteral("packageTexturePath"), QStringLiteral("textures/author/wall.png")},
		{QStringLiteral("futureExportSettings"), QJsonObject{{QStringLiteral("flags"), 47}}}};
	const QByteArray bytes = encodeTextureProject(document, metadata, &error);
	TextureDocument reopened; QJsonObject restoredMetadata;
	ok &= expect(!bytes.isEmpty() && bytes.startsWith(QByteArray::fromHex("56535445580d0a1a01000000")) && decodeTextureProject(bytes, &reopened, &restoredMetadata, &error), "versioned native project decodes");
	ok &= expect(encodeTextureProject(reopened, restoredMetadata) == bytes && restoredMetadata == metadata && !reopened.isDirty() && !reopened.canUndo(), "project round trip exactly preserves layer ids, ordering, properties, pixels, palette, and unknown export metadata");
	IdTechPaletteResolution restoredPalette;
	ok &= expect(texturePaletteFromMetadata(restoredMetadata.value(QStringLiteral("palette")).toObject(), &restoredPalette) && restoredPalette.palette.colors == resolution.palette.colors && restoredPalette.sourcePackagePath == resolution.sourcePackagePath && !restoredPalette.palette.generated, "saved actual game palette survives unavailable source packages");
	QImage indexed(2, 1, QImage::Format_Indexed8); indexed.setColorTable({qRgba(3, 5, 7, 0), qRgba(11, 13, 17, 128), qRgb(23, 29, 31)}); indexed.setPixel(0, 0, 0); indexed.setPixel(1, 0, 1);
	TextureDocument indexedDocument; indexedDocument.reset(indexed); TextureDocument indexedReopened;
	ok &= expect(decodeTextureProject(encodeTextureProject(indexedDocument), &indexedReopened) && indexedReopened.activeLayer()->pixels.format() == QImage::Format_Indexed8 && indexedReopened.activeLayer()->pixels.colorTable() == indexed.colorTable() && indexedReopened.activeLayer()->pixels.pixelIndex(1, 0) == 1, "native project preserves indexed palette entries, hidden RGB, alpha, and indices");
	QImage highDepth(8, 8, QImage::Format_RGBA64); highDepth.fill(QColor(17, 31, 73, 123));
	TextureDocument converted;
	ok &= expect(converted.reset(highDepth) && converted.activeLayer()->pixels.format() == QImage::Format_ARGB32 && converted.activeLayer()->pixels == highDepth.convertToFormat(QImage::Format_ARGB32), "raster import explicitly normalizes high-depth input to the editor's 8-bit RGBA surface");
	ok &= expect(converted.addLayer(QStringLiteral("Imported"), highDepth) && converted.activeLayer()->pixels == highDepth.convertToFormat(QImage::Format_ARGB32), "layer import uses the same documented pixel precision");
	const auto importedBytes = encodeTextureProject(converted);
	ok &= expect(!converted.addLayer(QStringLiteral("Wrong size"), QImage(7, 8, QImage::Format_ARGB32)) && encodeTextureProject(converted) == importedBytes, "mismatched imported layer dimensions cannot change the document");
	const auto checkMalformed = [&](const QByteArray& corrupt, const char* message) {
		QString problem; QJsonObject unchanged{{QStringLiteral("sentinel"), true}};
		return expect(!decodeTextureProject(corrupt, &reopened, &unchanged, &problem) && !problem.isEmpty() && encodeTextureProject(reopened, metadata) == bytes && unchanged.value(QStringLiteral("sentinel")).toBool(), message);
	};
	QByteArray corrupt = bytes; corrupt[corrupt.size() - 1] ^= 1;
	ok &= checkMalformed(corrupt, "checksum damage never replaces an open document or metadata");
	corrupt = bytes; corrupt[8] = 2;
	ok &= checkMalformed(corrupt, "unknown versions fail safely");
	ok &= checkMalformed(bytes.first(31), "truncated headers fail safely");
	ok &= checkMalformed(revisedMetadata(bytes, [](QJsonObject& json) { json.insert(QStringLiteral("metadata"), QJsonObject{{QStringLiteral("palette"), QJsonObject{}}}); }), "known palette metadata is validated by the core for GUI and CLI parity");
	ok &= checkMalformed(revisedMetadata(bytes, [](QJsonObject& json) { json.insert(QStringLiteral("metadata"), QJsonObject{{QStringLiteral("packageTexturePath"), 12}}); }), "wrong package path types cannot silently change during GUI restoration");
	ok &= checkMalformed(revisedMetadata(bytes, [](QJsonObject& json) { json.insert(QStringLiteral("metadata"), QJsonObject{{QStringLiteral("export"), false}}); }), "non-object export settings cannot reset silently");
	ok &= checkMalformed(revisedMetadata(bytes, [](QJsonObject& json) { json.insert(QStringLiteral("metadata"), QJsonObject{{QStringLiteral("export"), QJsonObject{{QStringLiteral("surfaceFlags"), -1}}}}); }), "invalid native output metadata cannot replace an open project");
	ok &= expect(encodeTextureProject(document, {{QStringLiteral("palette"), QJsonObject{}}}, &error).isEmpty() &&
		encodeTextureProject(document, {{QStringLiteral("packageTexturePath"), QString(4097, QLatin1Char('a'))}}, &error).isEmpty(), "project writers refuse invalid known metadata before producing an unreadable project or checkpoint");
	ok &= checkMalformed(revisedMetadata(bytes, [](QJsonObject& json) { json.insert(QStringLiteral("width"), 4096); json.insert(QStringLiteral("height"), 4096); }), "signed valid checksum cannot bypass document pixel limits");
	ok &= checkMalformed(revisedMetadata(bytes, [](QJsonObject& json) { auto layers = json.value(QStringLiteral("layers")).toArray(); auto layer = layers[1].toObject(); layer.insert(QStringLiteral("id"), layers[0].toObject().value(QStringLiteral("id"))); layers[1] = layer; json.insert(QStringLiteral("layers"), layers); }), "duplicate layer identities are invalid");
	ok &= checkMalformed(revisedMetadata(bytes, [](QJsonObject& json) { auto layers = json.value(QStringLiteral("layers")).toArray(); auto layer = layers[0].toObject(); layer.insert(QStringLiteral("opacity"), 50.5); layers[0] = layer; json.insert(QStringLiteral("layers"), layers); }), "fractional integer properties are rejected");
	ok &= checkMalformed(revisedMetadata(bytes, [](QJsonObject& json) { auto layers = json.value(QStringLiteral("layers")).toArray(); auto layer = layers[0].toObject(); layer.insert(QStringLiteral("bytes"), 192 * 1024 * 1024); layers[0] = layer; json.insert(QStringLiteral("layers"), layers); }), "payload lengths cannot cross the bounded file");
	ok &= checkMalformed(revisedMetadata(bytes, [](QJsonObject& json) { json.insert(QStringLiteral("width"), 7); }), "PNG dimensions must match the canvas before decoding");

	TextureProjectSaveRequest save; save.path = root.filePath(QStringLiteral("layers.vtexture")); save.dryRun = true;
	ok &= expect(writeTextureProject(document, save, metadata).succeeded && !QFileInfo::exists(save.path), "project dry run has no filesystem writes");
	save.dryRun = false; const auto firstSave = writeTextureProject(document, save, metadata);
	ok &= expect(firstSave.succeeded && firstSave.written && read(save.path) == bytes && document.isDirty(), "atomic save preserves authoring state until its caller acknowledges success");
	ok &= expect(!writeTextureProject(document, save, metadata).succeeded && read(save.path) == bytes, "project overwrite protection preserves bytes");
	TextureProjectSaveRequest preparedRequest; preparedRequest.path = root.filePath(QStringLiteral("prepared.vtexture"));
	const auto preparedNew = prepareTextureProjectSave(document, preparedRequest, metadata);
	ok &= expect(preparedNew.report.succeeded && !preparedNew.report.written && !QFileInfo::exists(preparedRequest.path), "preparing a project save writes no output or backup");
	ok &= expect(put(preparedRequest.path, "competing project") && publishTextureProjectSave(preparedNew).conflict && read(preparedRequest.path) == "competing project",
		"a new destination appearing during cancellable encoding cannot be replaced");
	preparedRequest.overwrite = true;
	const auto preparedExisting = prepareTextureProjectSave(document, preparedRequest, metadata);
	ok &= expect(preparedExisting.report.succeeded && put(preparedRequest.path, "changed while encoding") && publishTextureProjectSave(preparedExisting).conflict &&
		read(preparedRequest.path) == "changed while encoding" && !QFileInfo::exists(preparedExisting.report.backupPath), "publication rejects a changed destination before creating backups");
	const auto preparedDeleted = prepareTextureProjectSave(document, preparedRequest, metadata);
	ok &= expect(preparedDeleted.report.succeeded && QFile::remove(preparedRequest.path) && publishTextureProjectSave(preparedDeleted).conflict && !QFileInfo::exists(preparedRequest.path),
		"deleting an existing destination during encoding remains a conflict");
	int prepareChecks = 0;
	const auto cancelledPreparation = prepareTextureProjectSave(document, preparedRequest, metadata, [&](qint64, qint64) { return ++prepareChecks < 8; });
	ok &= expect(!cancelledPreparation.report.succeeded && cancelledPreparation.report.error.contains(QStringLiteral("cancelled")) && !QFileInfo::exists(preparedRequest.path),
		"cancellation within a layer's PNG output writes no project");
	int readChecks = 0; TextureProjectIdentity untouchedIdentity; untouchedIdentity.path = QStringLiteral("sentinel");
	QJsonObject untouchedMetadata{{QStringLiteral("sentinel"), true}}; TextureDocument untouchedDocument; untouchedDocument.create({3, 2}, Qt::red);
	ok &= expect(!readTextureProject(save.path, &untouchedDocument, &untouchedIdentity, &untouchedMetadata, &error, [&](qint64, qint64) { return ++readChecks < 3; }) &&
		error.contains(QStringLiteral("cancelled")) && untouchedDocument.size() == QSize(3, 2) && untouchedIdentity.path == QStringLiteral("sentinel") && untouchedMetadata.value(QStringLiteral("sentinel")).toBool(),
		"cancellation during project reading or hashing preserves all caller outputs");
	const auto originalBytes = read(save.path); auto edited = document; edited.selectLayer(0); edited.paintStroke({{7, 7}}, Qt::yellow, 1);
	TextureProjectSaveRequest backedUp = save; backedUp.overwrite = true; backedUp.dryRun = true;
	const auto preview = writeTextureProject(edited, backedUp, metadata);
	ok &= expect(preview.succeeded && !preview.backupPath.isEmpty() && !QFileInfo::exists(root.filePath(QStringLiteral(".vibestudio"))) && read(save.path) == originalBytes, "dry-run plans backups without creating folders or replacing the project");
	backedUp.dryRun = false;
	const auto savedWithBackup = writeTextureProject(edited, backedUp, metadata);
	ok &= expect(savedWithBackup.succeeded && read(savedWithBackup.backupPath) == originalBytes && read(save.path) == encodeTextureProject(edited, metadata), "native overwrite keeps the exact previous source bytes in a content-addressed backup");
	ok &= expect(put(save.path, originalBytes), "restore the baseline source fixture");
	const auto deduplicated = writeTextureProject(edited, backedUp, metadata);
	ok &= expect(deduplicated.succeeded && deduplicated.backupPath == savedWithBackup.backupPath, "identical previous source versions reuse a verified backup");
	put(save.path, originalBytes); put(savedWithBackup.backupPath, "damaged backup");
	ok &= expect(!writeTextureProject(edited, backedUp, metadata).succeeded && read(save.path) == originalBytes, "backup corruption prevents replacing the only original source copy");
	put(savedWithBackup.backupPath, originalBytes);
	TextureProjectIdentity identity;
	ok &= expect(readTextureProject(save.path, &reopened, &identity, &restoredMetadata, &error) && identity.sha256 == firstSave.identity.sha256, "read captures the exact observed file fingerprint");
	save.overwrite = true; save.expectedSha256 = identity.sha256; save.expectedCanonicalPath = identity.canonicalPath;
	ok &= expect(put(save.path, QByteArrayLiteral("external revision")), "simulate an external modification");
	const auto conflict = writeTextureProject(document, save, metadata);
	ok &= expect(conflict.conflict && !conflict.written && read(save.path) == "external revision", "stale opened projects cannot overwrite external changes");
	QFile::remove(save.path);
	ok &= expect(writeTextureProject(document, save, metadata).conflict && !QFileInfo::exists(save.path), "external deletion is a conflict, not an implicit recreate");
	save.path = root.filePath(QStringLiteral("missing/failed.vtexture")); save.expectedSha256.clear(); save.expectedCanonicalPath.clear();
	ok &= expect(!writeTextureProject(document, save, metadata).succeeded && document.isDirty() && encodeTextureProject(document, metadata) == bytes, "failed save leaves every layer and undoable edit intact");

	TextureDocument bounded; bounded.create({1, 1}, Qt::transparent);
	for (int i = 1; i < TextureDocument::MaximumLayers; ++i) { ok &= expect(bounded.addLayer(QString::number(i)), "layers within the count limit are accepted"); }
	ok &= expect(!bounded.addLayer(QStringLiteral("overflow")) && !bounded.duplicateLayer() && bounded.layers().size() == TextureDocument::MaximumLayers, "layer add and duplicate enforce the same count limit");
	TextureDocument recipe; recipe.create({4, 4}, Qt::black);
	const QJsonArray operations{QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-add")}, {QStringLiteral("name"), QStringLiteral("Recipe ink")}},
		QJsonObject{{QStringLiteral("op"), QStringLiteral("select")}, {QStringLiteral("x"), 1}, {QStringLiteral("y"), 1}, {QStringLiteral("width"), 2}, {QStringLiteral("height"), 2}},
		QJsonObject{{QStringLiteral("op"), QStringLiteral("fill")}, {QStringLiteral("x"), 1}, {QStringLiteral("y"), 1}, {QStringLiteral("color"), QStringLiteral("blue")}},
		QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-properties")}, {QStringLiteral("opacity"), 50}, {QStringLiteral("name"), QStringLiteral("Half ink")}}};
	ok &= expect(applyTextureOperations(&recipe, operations, resolution.palette, &error) && recipe.layers().size() == 2 && recipe.activeLayer()->opacity == 50 && recipe.activeLayer()->pixels.pixelColor(0, 0).alpha() == 0, "layer and selection recipes share authoring semantics");
	const auto beforeRecipe = encodeTextureProject(recipe);
	QJsonArray bad = operations; bad.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-properties")}, {QStringLiteral("locked"), QStringLiteral("false")}});
	ok &= expect(!applyTextureOperations(&recipe, bad, resolution.palette, &error) && encodeTextureProject(recipe) == beforeRecipe, "malformed structural recipes roll back all preceding pixel and layer changes");
	TextureDocument cancellable; cancellable.create({512, 512}, Qt::black); cancellable.markSaved();
	const auto unchanged = cancellable.image(); int checkpoints = 0;
	ok &= expect(!cancellable.floodFill({0, 0}, Qt::white, &error, [&](qint64 done, qint64) { ++checkpoints; return done < 4096; }) && checkpoints == 3 && !cancellable.isDirty() && cancellable.image() == unchanged && !cancellable.canUndo(), "fill cancellation interrupts its queue early without exposing partially filled pixels");
	checkpoints = 0;
	ok &= expect(!cancellable.paintStroke({{0, 0}, {511, 511}}, Qt::white, 5, &error, [&](qint64 done, qint64) { ++checkpoints; return done < 128; }) && checkpoints > 1 && !cancellable.isDirty() && cancellable.image() == unchanged, "stroke cancellation restores pixels and the saved revision mid-segment");
	QVector<QPoint> expensive;
	for (int i = 0; i < 512; ++i) { expensive.append(i % 2 ? QPoint(511, 511) : QPoint(0, 0)); }
	ok &= expect(!cancellable.paintStroke(expensive, Qt::white, 128, &error) && cancellable.image() == unchanged, "valid coordinates cannot bypass the stroke work bound");
	checkpoints = 0;
	ok &= expect(!cancellable.remapPalette(resolution.palette, true, &error, [&](qint64 row, qint64) { ++checkpoints; return row < 3; }) && checkpoints == 4 && !cancellable.isDirty() && cancellable.image() == unchanged, "palette cancellation stops between scanlines and retains source format and pixels");
	cancellable.addLayer(QStringLiteral("Upper")); cancellable.markSaved(); const auto beforeResize = encodeTextureProject(cancellable);
	ok &= expect(!cancellable.resize({128, 128}, true, &error, [](qint64 layer, qint64) { return layer < 1; }) && encodeTextureProject(cancellable) == beforeResize && !cancellable.isDirty(), "layered transform cancellation leaves no partially resized document");
	QImage art(512, 512, QImage::Format_ARGB32);
	for (int y = 0; y < art.height(); ++y) { for (int x = 0; x < art.width(); ++x) { art.setPixel(x, y, resolution.palette.colorAt((x / 8 + y / 8) % 16)); } }
	QElapsedTimer timer; timer.start(); const auto quantized = quantizeToIdTechPalette(art, resolution.palette, false); const auto elapsed = timer.elapsed();
	ok &= expect(!quantized.isNull() && samePixels(art, quantized), "bounded exact-color caching preserves the nearest-palette result");
	std::cout << "palette_512x512_16_colors_ms=" << elapsed << '\n';
	for (int count : {2, 16, 256}) {
		QVector<QRgb> grayscale; for (int i = 0; i < count; ++i) { grayscale.append(qRgb(i, i, i)); }
		QImage indices(count, 2, QImage::Format_Indexed8); indices.setColorTable(grayscale);
		for (int y = 0; y < 2; ++y) { for (int x = 0; x < count; ++x) { indices.setPixel(x, y, x); } }
		TextureDocument source, restored; source.reset(indices); const auto encoded = encodeTextureProject(source);
		ok &= expect(decodeTextureProject(encoded, &restored, nullptr, &error) && restored.activeLayer()->pixels == indices && encodeTextureProject(restored) == encoded, "identity grayscale palettes preserve exact indices and original palette length after Qt PNG optimization");
		if (count == 256) {
			const auto legacy = revisedMetadata(encoded, [](QJsonObject& json) { auto layers = json.value("layers").toArray(); auto layer = layers[0].toObject(); layer.remove("indexedPalette"); layers[0] = layer; json.insert("layers", layers); });
			ok &= expect(decodeTextureProject(legacy, &restored, nullptr, &error) && restored.activeLayer()->pixels == indices, "legacy grayscale PNG projects reopen without losing indices");
		}
		const auto invalid = revisedMetadata(encoded, [](QJsonObject& json) { auto layers = json.value("layers").toArray(); auto layer = layers[0].toObject(); layer.insert("indexedPalette", QJsonArray{"red"}); layers[0] = layer; json.insert("layers", layers); });
		ok &= expect(!decodeTextureProject(invalid, &restored, nullptr, &error) && restored.activeLayer()->pixels == indices, "palette metadata mismatches fail without replacing current pixels");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
