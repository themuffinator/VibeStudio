#include "core/sound_generation.h"

#include "core/ai_audio_transport.h"
#include "core/ai_image_transport.h"
#include "core/audio_markers.h"
#include "core/package_archive.h"

#include "tests/fake_ai_provider.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>

using namespace vibestudio;

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		++failures;
	}
}

bool waitUntil(const std::function<bool()>& done, int timeoutMsecs = 5000)
{
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < timeoutMsecs) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
	}
	return done();
}

SoundGenerationSpec specFor(const QString& prompt, const QString& game = QStringLiteral("quake"))
{
	SoundGenerationSpec spec;
	spec.prompt = prompt;
	spec.game = game;
	return spec;
}

double peakOf(const AudioClip& clip)
{
	double peak = 0.0;
	for (const float sample : clip.samples) {
		peak = std::max(peak, double(std::abs(sample)));
	}
	return peak;
}

QString headerValue(const AiHttpRequest& request, const QByteArray& name)
{
	for (const auto& header : request.headers) {
		if (header.first.compare(name, Qt::CaseInsensitive) == 0) {
			return QString::fromUtf8(header.second);
		}
	}
	return {};
}

void checkProfilesAndNames()
{
	SoundGameProfile profile;
	expect(soundGameProfileForId(QStringLiteral("q3"), &profile) && profile.id == QStringLiteral("quake3") && profile.preset == AudioDeliveryPreset::Quake3,
		"q3 is Quake III.");
	expect(soundGameProfileForId(QStringLiteral("doom2"), &profile) && profile.lumps, "Doom II sounds are lumps.");
	expect(!soundGameProfileForId(QStringLiteral("halflife"), &profile), "Unknown games are refused.");
	expect(soundGenerationKindIds().size() == 15, "There are fifteen kinds of sound.");
	expect(soundKindFromPrompt(QStringLiteral("rocket explosion in a cave")) == QStringLiteral("explosion"), "An explosion reads as one.");
	expect(soundKindFromPrompt(QStringLiteral("heavy metal door slam")) == QStringLiteral("door"), "A door slam is a door.");
	expect(soundKindFromPrompt(QStringLiteral("plasma rifle")) == QStringLiteral("laser"), "A plasma rifle is an energy weapon.");
	expect(soundKindFromPrompt(QStringLiteral("wind howling")) == QStringLiteral("ambience"), "Wind is ambience.");
	expect(soundKindFromPrompt(QString()) == QStringLiteral("impact"), "Nothing said is an impact.");

	const SoundGenerationSpec door = normalizedSoundGenerationSpec(specFor(QStringLiteral("heavy metal door slam")));
	expect(soundGenerationName(door) == QStringLiteral("door_slam"), "Names come from the nouns.");
	expect(soundGenerationVirtualPath(door) == QStringLiteral("sound/vibestudio/door_slam.wav"), "Quake sounds go under sound/.");
	expect(soundGenerationReference(door) == QStringLiteral("vibestudio/door_slam.wav"), "Quake names sounds from inside sound/.");
	const SoundGenerationSpec q3 = normalizedSoundGenerationSpec(specFor(QStringLiteral("heavy metal door slam"), QStringLiteral("quake3")));
	expect(soundGenerationReference(q3) == QStringLiteral("sound/vibestudio/door_slam.wav"), "Quake III names them with sound/.");
	const SoundGenerationSpec doom = normalizedSoundGenerationSpec(specFor(QStringLiteral("heavy metal door slam"), QStringLiteral("doom")));
	expect(soundGenerationName(doom) == QStringLiteral("DSDOORSL"), "Doom lumps are DS and six letters.");
	expect(soundGenerationVariantSpec(doom, 1, 3).name == QStringLiteral("DSDOORS2") && soundGenerationVariantSpec(door, 2, 3).name == QStringLiteral("door_slam_3"),
		"Variants are numbered within each game's limits.");
	expect(soundGenerationVariantSpec(door, 1, 3).seed != door.seed, "Variants differ by seed.");
	expect(door.seed == normalizedSoundGenerationSpec(specFor(QStringLiteral("heavy metal door slam"))).seed, "The same description makes the same seed.");

	SoundGenerationSpec named = specFor(QStringLiteral("x"), QStringLiteral("doom"));
	named.name = QStringLiteral("pistol");
	expect(soundGenerationName(named) == QStringLiteral("DSPISTOL"), "A Doom name gets its DS prefix.");

	SoundGenerationSpec looping = door;
	looping.loop = true;
	expect(soundGenerationPrompt(door).startsWith(QStringLiteral("heavy metal door slam. A sound effect in the style of 1996's Quake"))
			&& soundGenerationPrompt(door).endsWith(QStringLiteral("ends cleanly.")) && soundGenerationPrompt(looping).endsWith(QStringLiteral("loops seamlessly.")),
		"The model is told the description, the game's sound, and whether it loops.");
}

void checkSynthesizer()
{
	for (const QString& kind : soundGenerationKindIds()) {
		SoundGenerationSpec spec = specFor(QStringLiteral("test"));
		spec.kind = kind;
		spec.loop = soundGenerationKindLoops(kind);
		const AudioClip clip = synthesizeSound(spec);
		const double seconds = double(clip.frameCount()) / clip.sampleRate;
		const bool good = clip.channels == 1 && clip.sampleRate == 44100 && seconds > 0.05 && seconds < 8.0 && peakOf(clip) > 0.02;
		if (!good) {
			std::cerr << "  " << kind.toStdString() << ": " << seconds << " s, peak " << peakOf(clip) << "\n";
		}
		expect(good, "Every kind makes a sound of a sensible length.");
		const GeneratedSound sound = processGeneratedSound(clip, spec, QStringLiteral("synth"));
		expect(sound.ok && sound.delivery.succeeded(), "Every kind is delivered.");
		expect(std::abs(sound.peakDecibels + 1.0) < 1.5, "Sounds are normalized to about -1 dBFS.");
		if (std::abs(sound.peakDecibels + 1.0) >= 1.5) {
			std::cerr << "  " << kind.toStdString() << " peak " << sound.peakDecibels << " dB, delivery peak " << sound.delivery.peak << "\n";
		}
	}
	SoundGenerationSpec spec = specFor(QStringLiteral("big rocket explosion"));
	const AudioClip first = synthesizeSound(spec);
	expect(first.samples == synthesizeSound(spec).samples, "The same description and seed make the same sound.");
	spec.seed = 12345;
	expect(first.samples != synthesizeSound(spec).samples, "Another seed makes another sound.");
	SoundGenerationSpec longer = specFor(QStringLiteral("laser zap"));
	longer.durationSeconds = 2.0;
	const AudioClip stretched = synthesizeSound(longer);
	expect(std::abs(double(stretched.frameCount()) / stretched.sampleRate - 2.0) < 0.1, "A length asked for is the length made.");
}

void checkProcessing()
{
	// Silence either side of a tone is trimmed.
	AudioClip raw;
	raw.channels = 2;
	raw.sampleRate = 22050;
	for (int frame = 0; frame < 22050 * 2; ++frame) {
		const bool tone = frame >= 11025 && frame < 11025 + 11025;
		const float value = tone ? float(0.3 * std::sin(2.0 * 3.14159265 * 440.0 * frame / 22050.0)) : 0.0f;
		raw.samples << value << value;
	}
	const GeneratedSound trimmed = processGeneratedSound(raw, specFor(QStringLiteral("beep"), QStringLiteral("quake2")), QStringLiteral("test"));
	// Half a second of tone, a few milliseconds kept before it and 30 after.
	expect(trimmed.ok && trimmed.clip.channels == 1 && trimmed.durationMsecs > 500 && trimmed.durationMsecs < 560, "Silence is trimmed and the sound made mono.");
	expect(trimmed.delivery.plan.sampleRate == 22050 && audioWavBits(trimmed.delivery.plan.wav.format) == 16, "Quake II takes 22 kHz 16-bit WAV.");

	// Ambience loops seamlessly, with a loop marker Quake reads.
	SoundGenerationSpec ambience = specFor(QStringLiteral("machine hum"));
	ambience.loop = true;
	const GeneratedSound hum = processGeneratedSound(synthesizeSound(ambience), ambience, QStringLiteral("synth"));
	expect(hum.ok && hum.clip.markers.loop && hum.clip.markers.loop->end == hum.clip.frameCount(), "A loop is marked over the whole sound.");
	expect(std::abs(hum.durationMsecs - 4000) < 60, "The loop is as long as asked.");
	const float seam = std::abs(hum.clip.samples.last() - hum.clip.samples.first());
	expect(seam < 0.25f, "The loop's ends meet.");
	AudioMarkers markers;
	expect(decodeWavAudioMarkers(hum.delivery.bytes, hum.delivery.plan.frames, &markers) && markers.loop.has_value(), "The Quake WAV carries the loop.");
	expect(hum.delivery.plan.sampleRate == 11025 && audioWavBits(hum.delivery.plan.wav.format) == 8, "Quake takes 11 kHz 8-bit WAV.");

	const GeneratedSound doom = processGeneratedSound(synthesizeSound(specFor(QStringLiteral("shotgun blast"), QStringLiteral("doom"))),
		specFor(QStringLiteral("shotgun blast"), QStringLiteral("doom")), QStringLiteral("synth"));
	expect(doom.ok && doom.delivery.plan.dmx && isGeneratedAudioDmx(doom.delivery.bytes), "Doom takes a DMX lump.");

	// A model's encoded answer is decoded first.
	const QByteArray wav = encodeAudioWav(synthesizeSound(specFor(QStringLiteral("coin pickup"))));
	const GeneratedSound decoded = processGeneratedSoundBytes(wav, QStringLiteral("audio/wav"), specFor(QStringLiteral("coin pickup")), QStringLiteral("ai:test"));
	expect(decoded.ok && decoded.source == QStringLiteral("ai:test"), "A model's WAV answer becomes a sound.");
	const GeneratedSound garbage = processGeneratedSoundBytes(QByteArrayLiteral("not audio at all"), QStringLiteral("audio/mpeg"), specFor(QStringLiteral("x")), QStringLiteral("ai:test"));
	expect(!garbage.ok && garbage.error.contains(QStringLiteral("could not be read")), "An unreadable answer says so.");
	AudioClip silent;
	silent.channels = 1;
	silent.sampleRate = 44100;
	silent.samples.resize(4410);
	expect(!processGeneratedSound(silent, specFor(QStringLiteral("x")), QStringLiteral("test")).ok, "A silent sound is refused.");
}

void checkWriting()
{
	QTemporaryDir folder;
	expect(folder.isValid(), "A scratch folder is made.");
	SoundGenerationOutput output;
	output.folder = folder.path();
	output.provenance.insert(QStringLiteral("connector"), QStringLiteral("synth"));

	const SoundGenerationSpec quake = normalizedSoundGenerationSpec(specFor(QStringLiteral("heavy metal door slam")));
	const GeneratedSound door = processGeneratedSound(synthesizeSound(quake), quake, QStringLiteral("synth"));
	SoundGenerationWriteReport report = writeGeneratedSound(door, quake, output);
	const QString path = QDir(folder.path()).filePath(QStringLiteral("sound/vibestudio/door_slam.wav"));
	const QString record = QDir(folder.path()).filePath(QStringLiteral(".vibestudio/generated/sounds/door_slam.json"));
	expect(report.ok && QFileInfo::exists(path) && QFileInfo::exists(record) && report.reference == QStringLiteral("vibestudio/door_slam.wav"),
		"A Quake sound is written under sound/ with its record.");
	QJsonObject provenance;
	{
		QFile recordFile(record);
		if (recordFile.open(QIODevice::ReadOnly)) {
			provenance = QJsonDocument::fromJson(recordFile.readAll()).object();
		}
	}
	expect(provenance.value(QStringLiteral("schema")).toString() == QStringLiteral("vibestudio.generated-sound/1")
			&& provenance.value(QStringLiteral("connector")).toString() == QStringLiteral("synth") && provenance.value(QStringLiteral("sha256")).toString().size() == 64,
		"The record says what made it.");
	report = writeGeneratedSound(door, quake, output);
	expect(!report.ok && report.alreadyExists, "An existing sound is not replaced unasked.");
	output.replaceExisting = true;
	expect(writeGeneratedSound(door, quake, output).ok, "It is replaced when asked.");
	output.replaceExisting = false;

	// Doom lumps collect in one PWAD.
	const SoundGenerationSpec slam = normalizedSoundGenerationSpec(specFor(QStringLiteral("heavy metal door slam"), QStringLiteral("doom")));
	const SoundGenerationSpec blast = normalizedSoundGenerationSpec(specFor(QStringLiteral("shotgun blast"), QStringLiteral("doom")));
	const GeneratedSound first = processGeneratedSound(synthesizeSound(slam), slam, QStringLiteral("synth"));
	const GeneratedSound second = processGeneratedSound(synthesizeSound(blast), blast, QStringLiteral("synth"));
	expect(writeGeneratedSound(first, slam, output).ok && writeGeneratedSound(second, blast, output).ok, "Two Doom sounds are written.");
	const QString wadPath = QDir(folder.path()).filePath(QStringLiteral("wads/vibestudio_sounds.wad"));
	PackageArchive archive;
	QString error;
	QStringList lumps;
	if (archive.load(wadPath, &error)) {
		for (const PackageEntry& entry : archive.entries()) {
			lumps << entry.virtualPath;
		}
	}
	expect(lumps.contains(QStringLiteral("DSDOORSL")) && lumps.contains(soundGenerationName(blast)), "Both lumps are in the PWAD.");
	report = writeGeneratedSound(first, slam, output);
	expect(!report.ok && report.alreadyExists, "A lump already there is not replaced unasked.");
	output.dryRun = true;
	output.replaceExisting = true;
	const SoundGenerationSpec dry = normalizedSoundGenerationSpec(specFor(QStringLiteral("laser zap"), QStringLiteral("quake2")));
	report = writeGeneratedSound(processGeneratedSound(synthesizeSound(dry), dry, QStringLiteral("synth")), dry, output);
	expect(report.ok && !QFileInfo::exists(QDir(folder.path()).filePath(QStringLiteral("sound/vibestudio/laser_zap.wav"))), "A dry run writes nothing.");
}

void checkTransport()
{
	AiSoundRequest request;
	request.connectorId = QStringLiteral("elevenlabs");
	request.model = aiSuggestedSoundModel(request.connectorId);
	request.prompt = QStringLiteral("door slam");
	request.durationSeconds = 45.0;
	request.promptInfluence = 0.7;
	request.loop = true;
	AiHttpRequest http;
	QString error;
	expect(buildAiSoundHttpRequest(request, QStringLiteral("sk-test-123456789"), &http, &error), "The ElevenLabs request builds.");
	expect(http.url.toString() == QStringLiteral("https://api.elevenlabs.io/v1/sound-generation?output_format=mp3_44100_128"), "It goes to the sound-generation path.");
	expect(headerValue(http, "xi-api-key") == QStringLiteral("sk-test-123456789"), "ElevenLabs takes its key in xi-api-key.");
	const QJsonObject body = QJsonDocument::fromJson(http.body).object();
	expect(body.value(QStringLiteral("text")).toString() == QStringLiteral("door slam") && body.value(QStringLiteral("model_id")).toString() == QStringLiteral("eleven_text_to_sound_v2")
			&& body.value(QStringLiteral("duration_seconds")).toDouble() == 30.0 && body.value(QStringLiteral("prompt_influence")).toDouble() == 0.7
			&& body.value(QStringLiteral("loop")).toBool(),
		"The body carries the description, model, bounded length, influence, and loop.");
	expect(!describeAiHttpRequest(http).contains(QStringLiteral("sk-test-123456789")), "The review hides the key.");

	AiSoundRequest custom = request;
	custom.connectorId = QStringLiteral("custom-http");
	custom.endpoint = QStringLiteral("http://127.0.0.1:9000/v1/");
	custom.model.clear();
	custom.durationSeconds = 0.0;
	custom.promptInfluence = -1.0;
	custom.loop = false;
	expect(buildAiSoundHttpRequest(custom, QStringLiteral("token"), &http, &error) && http.url.path() == QStringLiteral("/v1/sound-generation")
			&& headerValue(http, "Authorization") == QStringLiteral("Bearer token"),
		"A custom endpoint takes the same request with a bearer token.");
	const QJsonObject lean = QJsonDocument::fromJson(http.body).object();
	expect(lean.keys() == QStringList {QStringLiteral("text")}, "Unset fields are left out.");
	custom.prompt.clear();
	expect(!buildAiSoundHttpRequest(custom, QString(), &http, &error), "A request without a description is refused.");
	request.connectorId = QStringLiteral("openai");
	expect(!buildAiSoundHttpRequest(request, QString(), &http, &error), "A connector without a sound API is refused.");

	const QByteArray wav = encodeAudioWav(synthesizeSound(specFor(QStringLiteral("coin pickup"))));
	AiSoundResponse response = parseAiSoundResponse(200, wav, "audio/wav");
	expect(response.ok && response.mimeType == QStringLiteral("audio/wav") && response.audio == wav, "A sound answer is taken as it is.");
	response = parseAiSoundResponse(200, QByteArrayLiteral("{\"oops\":true}"), "application/json");
	expect(!response.ok && response.failure == AiChatFailure::BadResponse, "JSON in place of a sound is a bad answer.");
	response = parseAiSoundResponse(401, QByteArrayLiteral("{\"detail\":{\"status\":\"invalid_api_key\",\"message\":\"Invalid API key\"}}"));
	expect(response.failure == AiChatFailure::Credential && response.errorMessage == QStringLiteral("Invalid API key"), "A refused key is a credential failure.");
	response = parseAiSoundResponse(400, QByteArrayLiteral("{\"detail\":{\"status\":\"quota_exceeded\",\"message\":\"Out of credits\"}}"));
	expect(response.failure == AiChatFailure::RateLimited, "An exhausted quota is a limit, not a fault.");
	response = parseAiSoundResponse(422, QByteArrayLiteral("{\"detail\":[{\"loc\":[\"body\",\"duration_seconds\"],\"msg\":\"too long\",\"type\":\"value_error\"}]}"));
	expect(response.errorMessage == QStringLiteral("body.duration_seconds: too long"), "Validation errors name the field.");
	expect(aiSoundMimeType(QByteArrayLiteral("ID3\x04")) == QStringLiteral("audio/mpeg") && aiSoundMimeType(QByteArrayLiteral("fLaC")) == QStringLiteral("audio/flac"),
		"Encodings are told from their first bytes.");
}

void checkConnections()
{
	AiAutomationPreferences preferences;
	preferences.aiFreeMode = true;
	expect(resolveAiSoundConnection(preferences).block == AiSoundConnectionBlock::AiFreeMode, "AI-free mode stops sounds.");
	preferences.aiFreeMode = false;
	expect(resolveAiSoundConnection(preferences).block == AiSoundConnectionBlock::NoConnector, "No audio connector, no sound.");
	preferences.preferredAudioConnectorId = QStringLiteral("openai");
	expect(resolveAiSoundConnection(preferences).block == AiSoundConnectionBlock::NoSoundTransport, "OpenAI has no sound-effects API.");
	preferences.preferredAudioConnectorId = QStringLiteral("elevenlabs");
	expect(resolveAiSoundConnection(preferences).block == AiSoundConnectionBlock::CloudNotAllowed, "ElevenLabs needs cloud connectors allowed.");
	preferences.cloudConnectorsEnabled = true;
	preferences.elevenLabsCredentialEnvironmentVariable = QStringLiteral("VIBESTUDIO_TEST_SOUND_KEY");
	qunsetenv("VIBESTUDIO_TEST_SOUND_KEY");
	AiSoundConnection connection = resolveAiSoundConnection(preferences);
	expect(connection.block == AiSoundConnectionBlock::NoCredential && aiSoundConnectionBlockText(connection).contains(QStringLiteral("VIBESTUDIO_TEST_SOUND_KEY")),
		"A missing key is named.");
	qputenv("VIBESTUDIO_TEST_SOUND_KEY", "sk-sound");
	connection = resolveAiSoundConnection(preferences);
	expect(connection.ready() && connection.model == QStringLiteral("eleven_text_to_sound_v2") && aiSoundConnectionApiKey(connection) == QStringLiteral("sk-sound"),
		"With a key, ElevenLabs is ready with its default model.");
	qunsetenv("VIBESTUDIO_TEST_SOUND_KEY");
	preferences.cloudConnectorsEnabled = false;
	preferences.preferredAudioConnectorId = QStringLiteral("custom-http");
	preferences.connectorAudioEndpoints.insert(QStringLiteral("custom-http"), QStringLiteral("http://127.0.0.1:9000"));
	preferences.connectorAudioModels.insert(QStringLiteral("custom-http"), QStringLiteral("local-sfx"));
	connection = resolveAiSoundConnection(preferences);
	expect(connection.ready() && connection.local && connection.credentialVariable.isEmpty() && connection.model == QStringLiteral("local-sfx"),
		"A custom endpoint on this machine needs no cloud opt-in and no key.");

	// A project whose manifest turns AI off stops every kind of request, and says so.
	preferences.preferredLocalConnectorId = QStringLiteral("local-offline");
	preferences.preferredImageConnectorId = QStringLiteral("local-offline");
	preferences.connectorModels.insert(QStringLiteral("local-offline"), QStringLiteral("llama3.2"));
	preferences.projectAiFree = true;
	const AiSoundConnection sound = resolveAiSoundConnection(preferences);
	const AiImageConnection image = resolveAiImageConnection(preferences);
	const AiTextConnection text = resolveAiTextConnection(preferences);
	expect(sound.block == AiSoundConnectionBlock::ProjectAiFree && image.block == AiImageConnectionBlock::ProjectAiFree
			&& text.block == AiTextConnectionBlock::ProjectAiFree,
		"The project's AI-free override stops sounds, images, and text.");
	expect(aiSoundConnectionBlockText(sound).contains(QStringLiteral("--project-ai-free off")) && aiSoundConnectionBlockId(sound.block) == QStringLiteral("project-ai-free"),
		"It says how to allow AI for the project.");
	preferences.projectAiFree = false;
	expect(resolveAiSoundConnection(preferences).ready() && resolveAiTextConnection(preferences).ready(), "Without the override the same settings are ready.");
}

void checkClient()
{
	FakeAiProvider provider;
	expect(provider.listening(), "The fake provider listens.");
	const QByteArray wav = encodeAudioWav(synthesizeSound(specFor(QStringLiteral("teleport whoosh"))));
	provider.answerInTurn(200, wav, QByteArrayLiteral("audio/wav"));
	AiSoundRequest request;
	request.connectorId = QStringLiteral("custom-http");
	request.endpoint = provider.baseUrl();
	request.prompt = soundGenerationPrompt(specFor(QStringLiteral("teleport whoosh")));
	AiSoundClient client;
	std::optional<AiSoundResponse> response;
	expect(client.send(request, QString(), [&response](const AiSoundResponse& value) { response = value; }), "The sound request is sent.");
	expect(waitUntil([&response]() { return response.has_value(); }), "The fake provider answers.");
	expect(response && response->ok && response->audio == wav, "The sound comes back whole.");
	expect(!provider.exchanges().isEmpty() && provider.exchanges().last().path.startsWith("/v1/sound-generation?output_format=")
			&& QJsonDocument::fromJson(provider.exchanges().last().body).object().value(QStringLiteral("text")).toString().contains(QStringLiteral("teleport whoosh")),
		"The request reaches the sound-generation path with the prompt.");
	const GeneratedSound sound = processGeneratedSoundBytes(response ? response->audio : QByteArray(), response ? response->mimeType : QString(),
		specFor(QStringLiteral("teleport whoosh")), QStringLiteral("ai:custom-http/"));
	expect(sound.ok, "The answer becomes a game sound.");

	provider.hang();
	response.reset();
	expect(client.send(request, QString(), [&response](const AiSoundResponse& value) { response = value; }), "A second request is sent.");
	waitUntil([&provider]() { return provider.exchanges().size() >= 2; });
	client.cancel();
	expect(waitUntil([&response]() { return response.has_value(); }) && response->failure == AiChatFailure::Cancelled, "A request in flight cancels.");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	checkProfilesAndNames();
	checkSynthesizer();
	checkProcessing();
	checkWriting();
	checkTransport();
	checkConnections();
	checkClient();
	if (failures > 0) {
		std::cerr << failures << " sound generation check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "Sound generation checks passed.\n";
	return EXIT_SUCCESS;
}
