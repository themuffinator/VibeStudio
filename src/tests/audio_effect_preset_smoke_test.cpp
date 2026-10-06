#include "core/audio_effect_preset.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <filesystem>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	for (int rate : {1, 8000, 48000, 384000})
		for (const auto &factory : audioEffectFactoryPresets()) {
			const auto preset = makeAudioEffectPreset(factory.id, rate, &error);
			AudioEffectPreset decoded;
			AudioEffectChain applied;
			ok &= expect(error.isEmpty() && validateAudioEffectPreset(preset).isEmpty(),
			             "all factory recipes validate at supported rates");
			ok &= expect(decodeAudioEffectPreset(encodeAudioEffectPreset(preset, &error), &decoded, &error) &&
			                 decoded.effects == preset.effects && decoded.tailSeconds == preset.tailSeconds &&
			                 decoded.name == preset.name,
			             "preset JSON round trip retains complete ordered recipe");
			ok &= expect(instantiateAudioEffectPreset(decoded, rate, &applied, &error) &&
			                 applied.size() == decoded.effects.size(),
			             "preset can instantiate a chain");
			for (qsizetype i = 0; i < applied.size(); ++i)
				ok &= expect(applied[i].id != decoded.effects[i].id &&
				                 applied[i].parameters == decoded.effects[i].parameters,
				             "application gives fresh IDs without changing values");
		}
	const auto preset = makeAudioEffectPreset("large-hall", 48000, &error);
	const auto valid = encodeAudioEffectPreset(preset, &error);
	for (const auto &bytes : {QByteArray("{}"), QByteArray(65537, ' '), valid + "!"}) {
		auto untouched = preset;
		ok &= expect(!decodeAudioEffectPreset(bytes, &untouched, &error) && untouched.effects == preset.effects,
		             "malformed preset preserves destination");
	}
	for (const auto &key : {"version", "tailSeconds", "sampleRate", "extra"}) {
		auto object = QJsonDocument::fromJson(valid).object();
		object[key] = key == QString("sampleRate") ? 2.5 : -1;
		AudioEffectPreset next;
		ok &= expect(!decodeAudioEffectPreset(QJsonDocument(object).toJson(), &next, &error),
		             "strict schema rejects unknown/version/numeric violations");
	}
	AudioEffectChain untouched{makeAudioEffect("gain", 8000)};
	const auto original = untouched;
	ok &= expect(!instantiateAudioEffectPreset(preset, 8000, &untouched, &error) && untouched == original,
	             "rate-incompatible preset is rejected without implicit clamping");
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir folder(QDir(root).filePath("audio-effect-presets-XXXXXX"));
	if (!expect(folder.isValid(), "create private preset test directory"))
		return 1;
	AudioProjectSaveRequest request;
	request.path = folder.filePath("room.vsfx");
	request.dryRun = true;
	auto report = writeAudioEffectPreset(preset, request);
	ok &= expect(report.succeeded && !report.written && !QFile::exists(request.path),
	             "dry run validates but writes no preset");
	request.dryRun = false;
	report = writeAudioEffectPreset(preset, request);
	ok &= expect(report.succeeded && report.written && read(request.path) == valid,
	             "atomic preset save emits exact validated bytes");
	AudioEffectPreset loaded;
	AudioProjectIdentity identity;
	ok &= expect(readAudioEffectPreset(request.path, &loaded, &identity, &error) &&
	                 identity.sha256 == report.identity.sha256,
	             "read records file identity for later guarded save");
	ok &= expect(!writeAudioEffectPreset(preset, request).succeeded && read(request.path) == valid,
	             "existing preset requires explicit overwrite");
	request.expected = identity;
	ok &= expect(writeAudioEffectPreset(preset, request).succeeded, "unchanged fingerprint authorizes guarded save");
	QFile file(request.path);
	ok &= file.open(QIODevice::Append);
	file.write(" ");
	file.close();
	const auto changed = read(request.path);
	report = writeAudioEffectPreset(preset, request);
	ok &= expect(report.conflict && !report.written && read(request.path) == changed,
	             "external edit is preserved by digest conflict guard");
	request.expected = {};
	request.overwrite = true;
	ok &= expect(!writeAudioEffectPreset(preset, request, {request.path}).succeeded && read(request.path) == changed,
	             "protected source cannot be overwritten");
	const auto alias = folder.filePath("alias.vsfx");
	std::error_code linkError;
#ifdef Q_OS_WIN
	const auto native = [](const QString &path) { return std::filesystem::path(path.toStdWString()); };
#else
	const auto native = [](const QString &path) { return std::filesystem::path(QFile::encodeName(path).constData()); };
#endif
	std::filesystem::create_hard_link(native(request.path), native(alias), linkError);
	if (!linkError) {
		request.path = alias;
		ok &= expect(!writeAudioEffectPreset(preset, request, {file.fileName()}).succeeded && read(alias) == changed,
		             "hard-linked source alias is protected");
	} else
		ok &= expect(false, "test filesystem must support independent hard-link identity check");
	request.path = folder.filePath("cancelled.vsfx");
	ok &= expect(!writeAudioEffectPreset(preset, request, {}, {[] { return true; }}).succeeded &&
	                 !QFile::exists(request.path),
	             "cancelled save does not publish output");
	auto invalid = preset;
	invalid.name.clear();
	ok &= expect(!writeAudioEffectPreset(invalid, request).succeeded && !QFile::exists(request.path),
	             "invalid preset cannot publish output");
	ok &= expect(!readAudioEffectPreset(file.fileName(), &loaded, nullptr, &error, {[] { return true; }}),
	             "cancelled read rejects result");
	return ok ? 0 : 1;
}
