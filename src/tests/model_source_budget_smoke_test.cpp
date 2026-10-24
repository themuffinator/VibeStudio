// Realistic large documents within the 64 MiB source and recovery limit
// (core/model_source_encoding.h): shortest round-trip float numbers, packed
// arrays (source version 12) chosen automatically, the exact projected source
// size, saving, reloading and recovering the largest realistic documents
// bit-exactly, refusing an edit that would make a source unsaveable while
// allowing edits that shrink one, and the CLI writing the same bytes.
//
// VIBESTUDIO_EXHAUSTIVE_FLOAT_ROUND_TRIP=1 also checks all 2^32 float bit
// patterns through the JSON number writer and reader (minutes on 8 threads).
#include "core/model_document.h"
#include "core/model_fingerprint.h"
#include "core/model_recovery.h"
#include "core/model_topology.h"
#include "tests/model_scale_test_helpers.h"
#include "tests/model_skeletal_budget_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

#include <atomic>
#include <bit>
#include <cfloat>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int failures = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty())
			std::cerr << ": " << qPrintable(detail);
		std::cerr << std::endl;
	}
	return value;
}

// Floats compare by value: distinct floats differ, and negative zero equals
// zero, as in JSON numbers (core/model_source_encoding.h).
bool same(int a, int b) { return a == b; }
bool same(const ModelVec3 &a, const ModelVec3 &b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool same(const ModelTexCoord &a, const ModelTexCoord &b) { return a.u == b.u && a.v == b.v; }
bool same(const ModelTriangle &a, const ModelTriangle &b) { return a.a == b.a && a.b == b.b && a.c == b.c; }
bool same(const ModelJointMatrix &a, const ModelJointMatrix &b) { return std::equal(std::begin(a.m), std::end(a.m), std::begin(b.m)); }
bool same(const ModelJointInfluence &a, const ModelJointInfluence &b)
{
	return a.joint == b.joint && a.weight == b.weight && same(a.offset, b.offset) && same(a.normalOffset, b.normalOffset);
}
bool same(const ModelCollisionPose &a, const ModelCollisionPose &b)
{
	return same(a.centre, b.centre) && same(a.size, b.size) && same(a.rotation, b.rotation);
}
template <typename T> bool sameArray(const QList<T> &a, const QList<T> &b)
{
	return a.size() == b.size() && std::equal(a.cbegin(), a.cend(), b.cbegin(), [](const T &x, const T &y) { return same(x, y); });
}
// Every number of the geometry, skinning and skeleton, exactly.
bool sameBits(const ModelMesh &a, const ModelMesh &b)
{
	if (a.surfaces.size() != b.surfaces.size() || a.frames.size() != b.frames.size() || a.skeleton.clips.size() != b.skeleton.clips.size() ||
		a.skeleton.joints.size() != b.skeleton.joints.size() || a.tags.size() != b.tags.size() ||
		a.collisionBoxes.size() != b.collisionBoxes.size() || a.embeddedSkins.size() != b.embeddedSkins.size())
		return false;
	for (int s = 0; s < a.surfaces.size(); ++s)
	{
		const auto &x = a.surfaces[s], &y = b.surfaces[s];
		if (!sameArray(x.triangles, y.triangles) || !sameArray(x.texCoords, y.texCoords) || x.frames.size() != y.frames.size() ||
			x.topologyVertices != y.topologyVertices || x.uvSeams != y.uvSeams || !sameArray(x.skinning.first, y.skinning.first) ||
			!sameArray(x.skinning.count, y.skinning.count) || !sameArray(x.skinning.influences, y.skinning.influences) ||
			x.skinPaths != y.skinPaths)
			return false;
		for (int f = 0; f < x.frames.size(); ++f)
			if (!sameArray(x.frames[f].positions, y.frames[f].positions) || !sameArray(x.frames[f].normals, y.frames[f].normals))
				return false;
	}
	for (int f = 0; f < a.frames.size(); ++f)
		if (a.frames[f].name != b.frames[f].name || !same(a.frames[f].origin, b.frames[f].origin))
			return false;
	for (int t = 0; t < a.tags.size(); ++t)
		if (a.tags[t].name != b.tags[t].name || !same(a.tags[t].origin, b.tags[t].origin) ||
			!std::equal(std::begin(a.tags[t].axis), std::end(a.tags[t].axis), std::begin(b.tags[t].axis)))
			return false;
	for (int c = 0; c < a.collisionBoxes.size(); ++c)
		if (!sameArray(a.collisionBoxes[c].framePoses, b.collisionBoxes[c].framePoses))
			return false;
	for (int i = 0; i < a.embeddedSkins.size(); ++i)
		if (a.embeddedSkins[i].image.convertToFormat(QImage::Format_ARGB32) != b.embeddedSkins[i].image.convertToFormat(QImage::Format_ARGB32))
			return false;
	for (int j = 0; j < a.skeleton.joints.size(); ++j)
		if (!same(a.skeleton.joints[j].bind, b.skeleton.joints[j].bind))
			return false;
	for (int c = 0; c < a.skeleton.clips.size(); ++c)
	{
		const auto &x = a.skeleton.clips[c], &y = b.skeleton.clips[c];
		if (x.frames.size() != y.frames.size() || !sameArray(x.frameMins, y.frameMins) || !sameArray(x.frameMaxs, y.frameMaxs) ||
			!(x.curves == y.curves))
			return false;
		for (int f = 0; f < x.frames.size(); ++f)
			if (!sameArray(x.frames[f], y.frames[f]))
				return false;
	}
	return a.skeleton.bakedClipForFrame == b.skeleton.bakedClipForFrame;
}
qint64 frameVertices(const ModelMesh &mesh)
{
	qint64 result = 0;
	for (const auto &surface : mesh.surfaces)
		result += qint64(surface.vertexCount) * surface.frames.size();
	return result;
}
QByteArray encode(const ModelMesh &mesh, ModelSourceEncoding encoding, ModelSourceEncoding *written = nullptr)
{
	QString error;
	const auto bytes = serializeEditableModel(mesh, &error, {}, encoding, written);
	if (bytes.isEmpty())
		std::cerr << "serialise: " << qPrintable(error) << std::endl;
	return bytes;
}

// A small model with every kind of source data the packed form touches:
// fractional geometry over three frames, UV seams, topology identities,
// attachment tags, an animated collision box, an embedded skin and a timed clip.
ModelMesh variedMesh()
{
	ScaleTestRandom random(0x1234567u);
	ModelMesh mesh;
	mesh.geometryAvailable = true;
	mesh.sourcePath = QStringLiteral("tests/varied.md3");
	ModelSurface surface;
	surface.name = QStringLiteral("body");
	surface.skinPaths = {QStringLiteral("models/body.tga")};
	constexpr int side = 4, frames = 3;
	for (int y = 0; y < side; ++y)
		for (int x = 0; x < side; ++x)
		{
			surface.texCoords.append({random.unit(), random.unit()});
			if (x + 1 < side && y + 1 < side)
			{
				const int a = y * side + x, b = a + 1, c = a + side, d = c + 1;
				surface.triangles.append({a, b, c});
				surface.triangles.append({b, d, c});
			}
		}
	surface.vertexCount = side * side;
	for (int f = 0; f < frames; ++f)
	{
		ModelFrameGeometry pose;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			pose.positions.append({float(v % side) * 8.3f + random.unit(), float(v / side) * 8.7f + random.unit(), random.unit() * -3.0f});
			pose.normals.append({0.6f, 0.0f, 0.8f});
		}
		surface.frames.append(pose);
		ModelFrameInfo info;
		info.name = QStringLiteral("wave%1").arg(f);
		info.origin = {random.unit(), 0, 0};
		mesh.frames.append(info);
		ModelTag tag;
		tag.name = QStringLiteral("tag_weapon");
		tag.frameIndex = f;
		tag.origin = {random.unit() * 10.0f, -8.864461f, 1.0f / 3.0f};
		mesh.tags.append(tag);
	}
	surface.uvSeams.insert({0, 1});
	surface.uvSeams.insert({1, 5});
	for (int v = 0; v < surface.vertexCount; ++v)
		surface.topologyVertices.append(v);
	mesh.surfaces.append(surface);
	mesh.animations.append({QStringLiteral("wave"), 0, frames, 12.5});
	ModelCollisionBox box;
	box.name = QStringLiteral("hit");
	for (int f = 0; f < frames; ++f)
		box.framePoses.append(ModelCollisionPose{{random.unit(), random.unit(), random.unit()}, {16.25f, 8.5f, 4.125f}, {0, 0, random.unit() * 30.0f}});
	box.centre = box.framePoses[0].centre;
	box.size = box.framePoses[0].size;
	box.rotation = box.framePoses[0].rotation;
	mesh.collisionBoxes.append(box);
	ModelEmbeddedSkin skin;
	skin.index = 0;
	skin.name = QStringLiteral("skin0");
	skin.groupFrameCount = 1;
	skin.image = QImage(8, 8, QImage::Format_ARGB32);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
			skin.image.setPixel(x, y, qRgba(x * 30, y * 30, 90, 255));
	mesh.embeddedSkins.append(skin);
	updateEditableModelMetadata(&mesh);
	return mesh;
}

// Writes every float as the old writer did: the exact widening to a double,
// which prints up to 17 significant digits.
QJsonValue widened(const QJsonValue &value)
{
	if (value.isArray())
	{
		QJsonArray result;
		for (const auto &item : value.toArray())
			result.append(widened(item));
		return result;
	}
	if (value.isObject())
	{
		QJsonObject result;
		const auto object = value.toObject();
		// Curve keys are doubles in the model too, so they stay as written.
		for (auto it = object.begin(); it != object.end(); ++it)
			result.insert(it.key(), it.key() == QStringLiteral("curves") ? it.value() : widened(it.value()));
		return result;
	}
	if (value.isDouble() && std::floor(value.toDouble()) != value.toDouble())
		return double(float(value.toDouble()));
	return value;
}

bool numbers()
{
	bool ok = true;
	QJsonArray values;
	QVector<float> floats{0.0f, -0.0f, 1.0f, -1.0f, 0.1f, 1.0f / 3.0f, -8.864461f, 4095.9998f, 1000000.0f, -1000000.0f, FLT_MIN,
						  std::numeric_limits<float>::denorm_min(), FLT_MAX, -FLT_MAX, std::nextafter(1.0f, 2.0f), 16777216.0f, 1e-5f, 123456.79f};
	ScaleTestRandom random(0xfeedbeefu);
	while (floats.size() < 1000000)
	{
		const quint32 bits = (quint32(random.unit() * 65536.0f) << 16) ^ quint32(random.unit() * 65536.0f);
		const float value = std::bit_cast<float>(bits);
		if (std::isfinite(value))
			floats.append(value);
	}
	for (float value : floats)
		values.append(modelSourceNumber(value));
	const auto text = QJsonDocument(values).toJson(QJsonDocument::Compact);
	const auto parsed = QJsonDocument::fromJson(text).array();
	// Equality is exact for every float; negative zero reads back as zero,
	// as JSON writes it.
	int mismatches = 0;
	for (int i = 0; i < floats.size(); ++i)
		if (float(parsed.at(i).toDouble()) != floats[i])
			++mismatches;
	ok &= expect(parsed.size() == floats.size() && mismatches == 0, "a million floats read back exactly from their shortest JSON numbers",
				 QString::number(mismatches));
	ok &= expect(QJsonDocument(QJsonArray{modelSourceNumber(-8.864461f), modelSourceNumber(0.1f), modelSourceNumber(1.0f / 3.0f)}).toJson(QJsonDocument::Compact) ==
					 "[-8.864461,0.1,0.33333334]",
				 "floats print as their shortest decimal, not the 17 digits of the widened double");
	// Realistic coordinates: the realistic grid's numbers print in about half
	// the characters the widened doubles took.
	const auto grid = realisticMaximumEditableGrid();
	qint64 shortest = 0, widenedLength = 0;
	for (int v = 0; v < 65536; ++v)
		for (float value : {grid.surfaces[0].frames[3].positions[v].x, grid.surfaces[0].frames[3].normals[v].y})
		{
			shortest += QByteArray::number(modelSourceNumber(value), 'g', QLocale::FloatingPointShortest).size();
			widenedLength += QByteArray::number(double(value), 'g', QLocale::FloatingPointShortest).size();
		}
	std::cout << "numbers: shortest " << shortest << " characters, widened doubles " << widenedLength << std::endl;
	ok &= expect(shortest * 10 < widenedLength * 6, "realistic coordinates and normals print in under 60% of the old length");
	if (qEnvironmentVariable("VIBESTUDIO_EXHAUSTIVE_FLOAT_ROUND_TRIP") == QStringLiteral("1"))
	{
		// The writer prints QByteArray::number(d, 'g', FloatingPointShortest)
		// and the reader parses with QByteArray::toDouble, as QJsonDocument does.
		QElapsedTimer timer;
		timer.start();
		std::atomic<quint64> wrong{0}, checked{0};
		const unsigned threads = std::max(2u, std::min(8u, std::thread::hardware_concurrency()));
		std::vector<std::thread> workers;
		for (unsigned t = 0; t < threads; ++t)
			workers.emplace_back(
				[&, t]
				{
					quint64 localWrong = 0, localChecked = 0;
					for (quint64 bits = t; bits <= 0xffffffffull; bits += threads)
					{
						const float value = std::bit_cast<float>(quint32(bits));
						if (!std::isfinite(value))
							continue;
						++localChecked;
						const auto text = QByteArray::number(modelSourceNumber(value), 'g', QLocale::FloatingPointShortest);
						if (float(text.toDouble()) != value)
							++localWrong;
					}
					wrong += localWrong;
					checked += localChecked;
				});
		for (auto &worker : workers)
			worker.join();
		std::cout << "exhaustive: " << checked.load() << " finite floats in " << timer.elapsed() / 1000 << " s, " << wrong.load()
				  << " mismatches" << std::endl;
		ok &= expect(wrong.load() == 0 && checked.load() == 4278190080ull, "every finite float reads back bit-exactly from its JSON number");
	}
	return ok;
}

bool packedArrays()
{
	bool ok = true;
	QString error;
	const auto varied = variedMesh();
	ok &= expect(validateEditableModel(varied).isEmpty(), "the varied fixture is a valid editable model", validateEditableModel(varied).join('\n'));
	const auto skeletal = realisticSkeletalDocument(3, 6, 1);
	ok &= expect(!skeletal.surfaces.isEmpty() && validateEditableModel(skeletal).isEmpty(), "the small skeletal fixture is valid");
	for (const auto *mesh : {&varied, &skeletal})
	{
		ModelSourceEncoding written = ModelSourceEncoding::Packed;
		const auto readable = encode(*mesh, ModelSourceEncoding::Automatic, &written);
		const auto packed = encode(*mesh, ModelSourceEncoding::Packed);
		ModelMesh fromReadable, fromPacked;
		ok &= expect(written == ModelSourceEncoding::Readable && QJsonDocument::fromJson(readable).object().value("version").toInt() < 12,
					 "a source that fits stays readable JSON at its own version");
		ok &= expect(QJsonDocument::fromJson(packed).object().value("version").toInt() == 12 &&
						 QJsonDocument::fromJson(packed).object().value("packedArrays") == QStringLiteral("base64-le32") &&
						 packed.size() < readable.size(),
					 "a packed source is version 12, declares its array encoding and is smaller");
		ok &= expect(parseEditableModel(readable, &fromReadable, &error) && sameBits(fromReadable, *mesh), "readable sources round-trip bit-exactly", error);
		ok &= expect(parseEditableModel(packed, &fromPacked, &error) && sameBits(fromPacked, *mesh), "packed sources round-trip bit-exactly", error);
		// Version 11 and earlier sources wrote widened doubles; they still load.
		const auto old = QJsonDocument(widened(QJsonDocument::fromJson(readable).object()).toObject()).toJson(QJsonDocument::Compact);
		ModelMesh fromOld;
		ok &= expect(old.size() > readable.size() && parseEditableModel(old, &fromOld, &error) && sameBits(fromOld, *mesh),
					 "sources with 17-digit doubles from earlier versions load the same floats", error);
	}
	ok &= expect(encode(varied, ModelSourceEncoding::Readable).contains("-8.864461,") &&
					 !encode(varied, ModelSourceEncoding::Readable).contains("-8.86446094"),
				 "readable sources hold the shortest decimal of each float");
	// Malformed packed sources are refused without touching the output.
	const auto packed = QJsonDocument::fromJson(encode(varied, ModelSourceEncoding::Packed)).object();
	const auto refused = [&](QJsonObject root, const char *message)
	{
		ModelMesh untouched;
		untouched.sourcePath = QStringLiteral("sentinel");
		const bool rejected = !parseEditableModel(QJsonDocument(root).toJson(QJsonDocument::Compact), &untouched, &error) &&
							  untouched.sourcePath == QStringLiteral("sentinel");
		return expect(rejected, message);
	};
	const auto withSurface = [&](const std::function<void(QJsonObject &)> &change)
	{
		auto root = packed;
		auto surfaces = root.value("surfaces").toArray();
		auto surface = surfaces[0].toObject();
		change(surface);
		surfaces[0] = surface;
		root.insert("surfaces", surfaces);
		return root;
	};
	auto root = packed;
	root.insert("version", 11);
	ok &= refused(root, "packed arrays in a version 11 source are refused");
	root = packed;
	root.remove("packedArrays");
	ok &= refused(root, "a version 12 source must declare its array encoding");
	root = packed;
	root.insert("packedArrays", QStringLiteral("base64-le16"));
	ok &= refused(root, "an unknown array encoding is refused");
	ok &= refused(withSurface([](QJsonObject &surface) { surface.insert("uvs", surface.value("uvs").toString().chopped(4)); }),
				  "a packed array of partial items is refused");
	ok &= refused(withSurface([](QJsonObject &surface) { surface.insert("triangles", QStringLiteral("AAAA*AAA")); }),
				  "packed arrays that are not base64 are refused");
	ok &= refused(withSurface(
					  [](QJsonObject &surface)
					  {
						  QVector<ModelTexCoord> coords(16, {0.5f, 0.5f});
						  coords[3].u = std::numeric_limits<float>::quiet_NaN();
						  surface.insert("uvs", packModelSourceTexCoords(coords));
					  }),
				  "packed floats that are not finite are refused");
	ok &= refused(withSurface(
					  [](QJsonObject &surface)
					  {
						  auto frames = surface.value("frames").toArray();
						  auto frame = frames[0].toObject();
						  frame.insert("positions", packModelSourceVectors(QVector<ModelVec3>(15)));
						  frames[0] = frame;
						  surface.insert("frames", frames);
					  }),
				  "a packed frame with the wrong vertex count is refused");
	return ok;
}

bool projection()
{
	bool ok = true;
	QString error;
	const QVector<QPair<const char *, ModelMesh>> fixtures{{"varied", variedMesh()},
															 {"skeletal", realisticSkeletalDocument(3, 6, 1)},
															 {"dense skeletal", denseSkeletalFixture(64, {{"walk", 12}, {"run", 8}}, {0})},
															 {"integer grid", maximumEditableGrid()}};
	for (const auto &[name, mesh] : fixtures)
	{
		const auto packed = encode(mesh, ModelSourceEncoding::Packed);
		ModelDocument document;
		ok &= expect(!packed.isEmpty() && projectEditableModelBytes(mesh) == packed.size() && document.setMesh(mesh, &error) &&
						 document.sourceBytes() == packed.size(),
					 "the projected source size is the exact size of the packed source", QString::fromLatin1(name));
	}
	return ok;
}

// Numbers whose readable text the writer chooses differently from a plain
// shortest-digits rule: negative zero, whole numbers in exponent range, tiny
// and huge values, denormals and long fractions.
ModelMesh specialNumbersMesh()
{
	auto mesh = variedMesh();
	const float specials[] = {-0.0f, 0.0001f, 1e-5f, 1e20f, 100000.0f, 123456789.0f, 1e-7f, 3.4028235e38f, 1e-45f, -2.5e-3f, 0.1f, 16777216.0f};
	auto &surface = mesh.surfaces[0];
	int at = 0;
	for (auto &frame : surface.frames)
		for (auto &position : frame.positions)
		{
			position.x = specials[at++ % std::size(specials)];
			position.y = -specials[at++ % std::size(specials)];
		}
	for (auto &uv : surface.texCoords)
		uv.u = specials[at++ % std::size(specials)];
	return mesh;
}

// The readable size follows from the packed size exactly, without writing the
// readable source; a source whose packed form is over the limit but whose
// readable form fits is measured by the form that fits, so it can be edited,
// saved and recovered.
bool readableProjection(const QDir &dir)
{
	bool ok = true;
	QString error;
	auto skinned = realisticSkeletalDocument(3, 6, 1);
	skinned.surfaces[0].skinning.influences[5].offset.x = -0.0f;
	skinned.surfaces[0].skinning.influences[6].weight = 1e-7f;
	const QVector<QPair<const char *, ModelMesh>> fixtures{{"varied", variedMesh()},
															 {"special numbers", specialNumbersMesh()},
															 {"skeletal", skinned},
															 {"dense skeletal", denseSkeletalFixture(64, {{"walk", 12}, {"run", 8}}, {0})}};
	for (const auto &[name, mesh] : fixtures)
	{
		const auto packed = encode(mesh, ModelSourceEncoding::Packed), readable = encode(mesh, ModelSourceEncoding::Readable);
		const int version = QJsonDocument::fromJson(readable).object().value(QStringLiteral("version")).toInt();
		qint64 delta = 0;
		const auto field = qint64(QByteArrayLiteral(",\"packedArrays\":\"base64-le32\"").size());
		ok &= expect(!packed.isEmpty() && !readable.isEmpty() && modelSourceReadableBulkDelta(mesh, &delta, &error) &&
						 readable.size() == packed.size() + delta - field - (2 - (version >= 10 ? 2 : 1)),
					 "the readable size follows exactly from the packed size and the bulk arrays", QString::fromLatin1(name));
	}
	// 360,000 poses of a three-joint strip: about 66 MiB of packed joint
	// matrices, which short decimals hold in under half of that.
	const auto mesh = denseSkeletalFixture(4, {{"long", 360000}}, {});
	if (!expect(!mesh.surfaces.isEmpty(), "prepare the low-entropy skeletal fixture"))
		return false;
	const auto packed = summarizeModelState(mesh).packedSourceBytes;
	const auto readable = encode(mesh, ModelSourceEncoding::Readable);
	ok &= expect(packed > modelDocumentMaxSourceBytes && !readable.isEmpty() && readable.size() < packed &&
					 projectEditableModelBytes(mesh) == readable.size(),
				 "a packed form over the limit projects the smaller readable size",
				 QStringLiteral("%1 packed, %2 readable").arg(packed).arg(readable.size()));
	ModelDocument document;
	ok &= expect(document.setMesh(mesh, &error) && document.sourceBytes() == readable.size(), "the document measures the readable form", error);
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameSkeletalClip;
	rename.skeletal.clip = 0;
	rename.skeletal.name = QStringLiteral("a_longer_name_for_the_long_clip");
	ok &= expect(document.edit(rename, &error) && document.canUndo(), "an edit that grows the source still applies", error);
	const auto path = dir.filePath(QStringLiteral("low-entropy.mesh.json"));
	ok &= expect(document.save(path, false, &error) && document.savedSourceEncoding() == ModelSourceEncoding::Readable &&
					 document.savedSourceBytes() == document.sourceBytes() && QFileInfo(path).size() == document.sourceBytes(),
				 "Automatic saves the readable form that fits", error);
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = document.mesh();
	snapshot.title = QStringLiteral("low entropy");
	const auto recovery =
		writeModelRecovery(snapshot, dir.filePath(QStringLiteral("recovery-low")), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).error.isEmpty() &&
					 restored.mesh.skeleton.clips[0].frames.size() == 360000,
				 "the recovery copy writes the form that fits", error);
	return ok;
}

bool realistic(const QDir &dir)
{
	bool ok = true;
	QString error;
	const QVector<QPair<QString, ModelMesh>> documents{{QStringLiteral("realistic-grid"), realisticMaximumEditableGrid()},
													   {QStringLiteral("realistic-skeletal"), realisticSkeletalDocument()}};
	for (const auto &[name, mesh] : documents)
	{
		if (!expect(!mesh.surfaces.isEmpty() && validateEditableModel(mesh).isEmpty(), "the realistic fixture is valid", name))
			continue;
		QElapsedTimer timer;
		timer.start();
		const auto readable = QJsonDocument(editableModelJson(mesh, &error, {}, ModelSourceEncoding::Readable)).toJson(QJsonDocument::Compact).size();
		const auto readableMs = timer.restart();
		ModelSourceEncoding written = ModelSourceEncoding::Readable;
		const auto bytes = encode(mesh, ModelSourceEncoding::Automatic, &written);
		const auto serialiseMs = timer.restart();
		ModelMesh parsed;
		parseEditableModel(bytes, &parsed, &error);
		const auto parseMs = timer.elapsed();
		std::cout << QJsonDocument(QJsonObject{{"fixture", name},
											   {"frameVertices", frameVertices(mesh)},
											   {"skeletalMatrices", [&] { qint64 n = 0; for (const auto &clip : mesh.skeleton.clips) n += clip.frames.size() * mesh.skeleton.joints.size(); return n; }()},
											   {"readableBytes", readable},
											   {"packedBytes", bytes.size()},
											   {"bytesPerFrameVertex", double(bytes.size()) / double(frameVertices(mesh))},
											   {"readableMs", readableMs},
											   {"automaticMs", serialiseMs},
											   {"parseMs", parseMs}})
						 .toJson(QJsonDocument::Compact)
						 .constData()
				  << std::endl;
		ok &= expect(readable > modelDocumentMaxSourceBytes && written == ModelSourceEncoding::Packed && !bytes.isEmpty() &&
						 bytes.size() <= modelDocumentMaxSourceBytes && bytes.size() == projectEditableModelBytes(mesh),
					 "a realistic document over the limit as readable JSON is written packed within it", name);
		ModelDocument document;
		const auto path = dir.filePath(name + QStringLiteral(".mesh.json"));
		ok &= expect(document.setMesh(mesh, &error) && document.sourceBytes() == bytes.size() && document.save(path, false, &error) &&
						 document.savedSourceEncoding() == ModelSourceEncoding::Packed && QFileInfo(path).size() == bytes.size(),
					 "the realistic document saves within the source limit", error);
		ModelDocument loaded;
		ok &= expect(loaded.load(path, &error) && sameBits(loaded.mesh(), document.mesh()) &&
						 loaded.revisionFingerprint() == document.revisionFingerprint(),
					 "the saved realistic document reopens bit-exactly", error);
		ModelRecoverySnapshot snapshot;
		snapshot.mesh = document.mesh();
		snapshot.selection = document.selection();
		snapshot.title = name;
		const auto recovery = writeModelRecovery(snapshot, dir.filePath(QStringLiteral("recovery")), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
		ModelRecoverySnapshot restored;
		const auto record = recovery.isEmpty() ? ModelRecoveryRecord{} : inspectModelRecovery(recovery, &restored);
		ok &= expect(!recovery.isEmpty() && record.error.isEmpty() && sameBits(restored.mesh, document.mesh()),
					 "the realistic document writes and restores a recovery copy bit-exactly", error + record.error);
	}
	return ok;
}

// A noise skin, which compression cannot shrink, to bring a model close to
// the source limit with geometry inside the editable limits.
QImage noise(int side)
{
	QImage image(side, side, QImage::Format_ARGB32);
	ScaleTestRandom random(0xabcdefu);
	for (int y = 0; y < side; ++y)
	{
		auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
		for (int x = 0; x < side; ++x)
			line[x] = qRgba(int(random.unit() * 256), int(random.unit() * 256), int(random.unit() * 256), 255);
	}
	return image;
}
ModelMesh withFrames(const ModelMesh &grid, const QImage &skin, int frames)
{
	auto mesh = grid;
	mesh.surfaces[0].frames.resize(frames);
	mesh.frames.resize(frames);
	ModelEmbeddedSkin embedded;
	embedded.index = 0;
	embedded.name = QStringLiteral("noise");
	embedded.groupFrameCount = 1;
	embedded.image = skin;
	mesh.embeddedSkins = {embedded};
	updateEditableModelMetadata(&mesh);
	return mesh;
}

bool refusal(const QDir &dir, const QString &program)
{
	bool ok = true;
	QString error;
	const auto grid = realisticMaximumEditableGrid();
	const auto skin = noise(2900);
	const auto one = projectEditableModelBytes(withFrames(grid, skin, 1));
	const auto perFrame = projectEditableModelBytes(withFrames(grid, skin, 2)) - one;
	const int frames = int(1 + (modelDocumentMaxSourceBytes - one) / perFrame);
	if (!expect(one > 0 && perFrame > 0 && frames >= 2 && frames < 16, "the near-limit fixture fits the editable frame limits",
				QStringLiteral("%1 bytes with one frame, %2 per frame, %3 frames").arg(one).arg(perFrame).arg(frames)))
		return false;
	const auto near = withFrames(grid, skin, frames);
	ModelDocument document;
	ok &= expect(document.setMesh(near, &error) && document.sourceBytes() <= modelDocumentMaxSourceBytes &&
					 document.sourceBytes() + perFrame > modelDocumentMaxSourceBytes,
				 "the fixture is within one frame of the source limit", error);
	const auto before = document.revisionFingerprint();
	ModelEdit duplicate;
	duplicate.kind = ModelEditKind::DuplicateFrame;
	duplicate.frame = 0;
	ok &= expect(!document.edit(duplicate, &error) && error.contains(QStringLiteral("over the 64 MiB limit")) && !document.canUndo() &&
					 document.revisionFingerprint() == before && document.historyBytes() == 0,
				 "an edit that would take the source over the limit is refused and history is unchanged", error);
	const auto path = dir.filePath(QStringLiteral("near.mesh.json"));
	ok &= expect(document.save(path, false, &error) && QFileInfo(path).size() <= modelDocumentMaxSourceBytes,
				 "the document just under the limit saves", error);
	// Selecting every edge adds more to a recovery copy than the room left,
	// so the copy keeps the model and leaves the component selection out.
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = document.mesh();
	snapshot.selection.edges = document.surfaceTopology(0).allEdges;
	snapshot.title = QStringLiteral("near");
	const auto recovery = writeModelRecovery(snapshot, dir.filePath(QStringLiteral("recovery")), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	const auto record = recovery.isEmpty() ? ModelRecoveryRecord{} : inspectModelRecovery(recovery, &restored);
	ok &= expect(!recovery.isEmpty() && record.error.isEmpty() && restored.selection.edges.isEmpty() && sameBits(restored.mesh, document.mesh()),
				 "a source within the limit stays recoverable when its selection would not fit", error + record.error);
	// A model opened over the limit can be reduced, never grown.
	ModelDocument over;
	ok &= expect(over.setMesh(withFrames(grid, skin, frames + 1), &error) && over.sourceBytes() > modelDocumentMaxSourceBytes,
				 "a model over the source limit can still be opened", error);
	ok &= expect(!over.save(dir.filePath(QStringLiteral("over.mesh.json")), false, &error) && error.contains(QStringLiteral("over the 64 MiB limit")) &&
					 !QFile::exists(dir.filePath(QStringLiteral("over.mesh.json"))),
				 "saving a source over the limit is refused with its size", error);
	ok &= expect(!over.edit(duplicate, &error) && !over.canUndo(), "an edit that grows a source over the limit is refused", error);
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteFrame;
	remove.frame = 0;
	ok &= expect(over.edit(remove, &error) && over.sourceBytes() <= modelDocumentMaxSourceBytes && over.canUndo(),
				 "an edit that shrinks a source over the limit is allowed", error);
	// The CLI applies the same refusal.
	if (!program.isEmpty())
	{
		QProcess process;
		process.start(program, {"--cli", "model", "edit", path, "--operation", "duplicate-frame", "--frame", "0", "--output",
								dir.filePath(QStringLiteral("grown.mesh.json")), "--json", "--settings-file", dir.filePath("settings.ini")});
		const bool finished = process.waitForStarted(10000) && process.waitForFinished(300000);
		const auto output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
		ok &= expect(finished && process.exitCode() != 0 && output.contains(QStringLiteral("over the 64 MiB limit")) &&
						 !QFile::exists(dir.filePath(QStringLiteral("grown.mesh.json"))),
					 "CLI model edit refuses the same edit", output.left(400));
	}
	return ok;
}

bool cliParity(const QDir &dir, const QString &program)
{
	bool ok = true;
	QString error;
	ModelDocument document;
	const auto source = dir.filePath(QStringLiteral("realistic-grid.mesh.json"));
	if (!expect(document.load(source, &error), "reload the realistic grid for the CLI", error))
		return false;
	const auto output = dir.filePath(QStringLiteral("renamed.mesh.json"));
	QProcess process;
	process.start(program, {"--cli", "model", "edit", source, "--operation", "rename-frame", "--frame", "0", "--name", "start", "--output", output,
							"--json", "--settings-file", dir.filePath("settings.ini")});
	const bool finished = process.waitForStarted(10000) && process.waitForFinished(300000);
	const auto json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameFrame;
	rename.frame = 0;
	rename.text = QStringLiteral("start");
	QFile written(output);
	ok &= expect(finished && process.exitCode() == 0 && document.edit(rename, &error) && written.open(QIODevice::ReadOnly) &&
					 written.readAll() == encode(document.mesh(), ModelSourceEncoding::Automatic),
				 "the CLI writes the same packed source bytes as the editor's document service", error);
	const auto report = json.value("source").toObject();
	ok &= expect(report.value("encoding") == QStringLiteral("packed") && report.value("bytes").toInteger() == QFileInfo(output).size() &&
					 report.value("limitBytes").toInteger() == modelDocumentMaxSourceBytes,
				 "CLI JSON reports the source bytes, the packed encoding and the limit");
	return ok;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("source-budget-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const QDir dir(temporary.path());
	const auto program = app.arguments().value(1);
	numbers();
	packedArrays();
	projection();
	readableProjection(dir);
	realistic(dir);
	refusal(dir, program);
	if (!program.isEmpty())
		cliParity(dir, program);
	if (failures == 0)
		std::cout << "model source budget smoke passed" << std::endl;
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
