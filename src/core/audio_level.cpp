#include "core/audio_level.h"

#include "core/level_document.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringView>
#include <QtEndian>
#include <cmath>

namespace vibestudio
{
namespace
{
bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}
} // namespace

LevelSoundTarget levelSoundTarget(const LevelMapDocument& document)
{
	LevelSoundTarget target{document.format, {}};
	if (document.format == LevelMapFormat::Quake3Map) {
		target.game = QStringLiteral("quake3");
	} else if (document.format == LevelMapFormat::QuakeMap) {
		// Context refreshes must not split or scan an entire large map. Recognize
		// the studio's short first-line marker; unusual headers require a choice.
		const QStringView text(document.originalText);
		const auto prefix = text.first(qMin<qsizetype>(text.size(), 128));
		const auto newline = prefix.indexOf(QLatin1Char('\n'));
		if ((newline >= 0 || text.size() <= 128) &&
		    prefix.first(newline < 0 ? prefix.size() : newline).trimmed() ==
		        QString::fromLatin1(kQuake2MapTargetHeader).trimmed()) {
			target.game = QStringLiteral("quake2");
		}
	}
	return target;
}

LevelSoundPlan planLevelSound(const LevelSoundTarget& target, const LevelSoundRequest& request)
{
	LevelSoundPlan plan;
	const auto invalid = [&](const QString& message) {
		plan.error = message;
		return plan;
	};
	if (target.format != LevelMapFormat::QuakeMap && target.format != LevelMapFormat::Quake3Map) {
		return invalid(QCoreApplication::translate(
		    "VibeStudioAudio", "Open a Quake II or Quake III map to place a sound. Other games use package "
		                       "delivery and their game-specific entity tools."));
	}
	plan.game = request.game.isEmpty() ? target.game : request.game;
	if (plan.game.isEmpty()) {
		return invalid(QCoreApplication::translate(
		    "VibeStudioAudio", "This map does not identify its game. Choose Quake II only if this is a Quake "
		                       "II map; stock Quake has no arbitrary sound speaker."));
	}
	if ((plan.game != QStringLiteral("quake2") && plan.game != QStringLiteral("quake3")) ||
	    (!target.game.isEmpty() && target.game != plan.game) ||
	    (target.format == LevelMapFormat::Quake3Map) != (plan.game == QStringLiteral("quake3"))) {
		return invalid(QCoreApplication::translate("VibeStudioAudio",
		                                           "The sound game profile does not match the open map."));
	}
	// Original target_speaker / S_LoadSound behaviour (GPL-2.0-or-later),
	// id-Software/Quake-2 game/g_target.c and client/snd_mem.c; and
	// id-Software/Quake-III-Arena code/game/g_target.c. Reviewed 2026-10-04.
	// Quake II prepends sound/; Quake III uses the full virtual path. Both have
	// MAX_QPATH = 64. No upstream code copied; source links in docs/CREDITS.md.
	const QString path = request.virtualPath;
	static const QRegularExpression soundPath(
	    QStringLiteral("\\Asound/[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*\\.wav\\z"));
	if (path.toUtf8().size() >= 64 || !soundPath.match(path).hasMatch() ||
	    path.split(QLatin1Char('/')).contains(QStringLiteral("..")) ||
	    path.split(QLatin1Char('/')).contains(QStringLiteral("."))) {
		return invalid(QCoreApplication::translate(
		    "VibeStudioAudio",
		    "Use a sound/ path shorter than 64 ASCII bytes, with letters, digits, underscores, hyphens or "
		    "dots and a lowercase .wav extension. Relative path segments are not allowed."));
	}
	if (!request.origin.valid || !std::isfinite(request.origin.x) || !std::isfinite(request.origin.y) ||
	    !std::isfinite(request.origin.z) || std::abs(request.origin.x) > 1048576 ||
	    std::abs(request.origin.y) > 1048576 || std::abs(request.origin.z) > 1048576) {
		return invalid(QCoreApplication::translate(
		    "VibeStudioAudio", "Sound coordinates must be finite and within ±1048576 map units."));
	}
	int flags = 0;
	if (request.mode == QStringLiteral("loop-on")) {
		flags = 1;
	} else if (request.mode == QStringLiteral("loop-off")) {
		flags = 2;
	} else if (request.mode != QStringLiteral("triggered")) {
		return invalid(QCoreApplication::translate("VibeStudioAudio",
		                                           "Choose loop-on, loop-off or triggered sound playback."));
	}
	static const QRegularExpression name(QStringLiteral("\\A[A-Za-z0-9_.:-]{1,63}\\z"));
	if ((!request.targetName.isEmpty() && !name.match(request.targetName).hasMatch()) ||
	    (request.mode != QStringLiteral("loop-on") && request.targetName.isEmpty())) {
		return invalid(QCoreApplication::translate(
		    "VibeStudioAudio", "Triggered and initially off sounds need a target name. Use at most 63 ASCII "
		                       "letters, digits, underscores, hyphens, dots or colons."));
	}
	plan.className = QStringLiteral("target_speaker");
	plan.soundReference = plan.game == QStringLiteral("quake2") ? path.mid(6) : path;
	plan.properties = {{QStringLiteral("noise"), plan.soundReference, 0},
	                   {QStringLiteral("spawnflags"), QString::number(flags), 0}};
	if (!request.targetName.isEmpty()) {
		plan.properties.append({QStringLiteral("targetname"), request.targetName, 0});
	}
	return plan;
}

bool validateLevelSoundWav(const QByteArray& wav, QString* error)
{
	if (error) {
		error->clear();
	}
	const auto bad = [&]() {
		return fail(error,
		            QCoreApplication::translate(
		                "VibeStudioAudio", "Sound placement requires a complete mono 22050 Hz PCM16 WAV. "
		                                   "Export with the Quake II or Quake III delivery preset first."));
	};
	if (wav.size() < 46 || wav.size() > AudioInputByteLimit || wav.first(4) != "RIFF" ||
	    wav.mid(8, 4) != "WAVE" || qFromLittleEndian<quint32>(wav.constData() + 4) != wav.size() - 8) {
		return bad();
	}
	bool haveFormat = false, haveData = false;
	qint64 frames = 0, metadata = 0, offset = 12;
	int chunks = 0;
	while (offset < wav.size()) {
		if (++chunks > 4096 || wav.size() - offset < 8) {
			return bad();
		}
		const auto tag = QByteArrayView(wav).sliced(offset, 4);
		const qint64 size = qFromLittleEndian<quint32>(wav.constData() + offset + 4);
		const qint64 payload = offset + 8;
		if (size > wav.size() - payload || size + (size & 1) > wav.size() - payload) {
			return bad();
		}
		if (tag == "fmt ") {
			if (haveFormat || haveData || (size != 16 && size != 18)) {
				return bad();
			}
			const auto* p = wav.constData() + payload;
			if (qFromLittleEndian<quint16>(p) != 1 || qFromLittleEndian<quint16>(p + 2) != 1 ||
			    qFromLittleEndian<quint32>(p + 4) != 22050 || qFromLittleEndian<quint32>(p + 8) != 44100 ||
			    qFromLittleEndian<quint16>(p + 12) != 2 || qFromLittleEndian<quint16>(p + 14) != 16 ||
			    (size == 18 && qFromLittleEndian<quint16>(p + 16) != 0)) {
				return bad();
			}
			haveFormat = true;
		} else if (tag == "data") {
			if (!haveFormat || haveData || size < 2 || size % 2 || size / 2 > AudioSampleLimit) {
				return bad();
			}
			haveData = true;
			frames = size / 2;
		} else {
			metadata += size + 8;
			if (metadata > AudioMarkerByteLimit) {
				return bad();
			}
		}
		offset = payload + size + (size & 1);
	}
	if (!haveData) {
		return bad();
	}
	AudioMarkers markers;
	return decodeWavAudioMarkers(wav, frames, &markers, error);
}

bool placeLevelSound(LevelMapDocument* document, const LevelSoundRequest& request, const QByteArray& wav,
                     int* entityId, QString* error)
{
	if (entityId) {
		*entityId = -1;
	}
	if (!document) {
		return fail(error,
		            QCoreApplication::translate("VibeStudioAudio", "Open a map before placing a sound."));
	}
	const auto plan = planLevelSound(levelSoundTarget(*document), request);
	if (!plan.valid()) {
		return fail(error, plan.error);
	}
	if (!validateLevelSoundWav(wav, error)) {
		return false;
	}
	return addLevelMapEntity(document, plan.className, request.origin, plan.properties, entityId, error);
}

bool stageLevelSound(PackageStagingModel* staging, LevelMapDocument* document,
                     const LevelSoundRequest& request, const QByteArray& wav, bool replace, int* entityId,
                     QString* error)
{
	if (entityId) {
		*entityId = -1;
	}
	if (!staging || !document) {
		return fail(error,
		            QCoreApplication::translate(
		                "VibeStudioAudio", "Open a map and a package before staging and placing a sound."));
	}
	// Preflight before copying map history or adding a pending package operation.
	const auto plan = planLevelSound(levelSoundTarget(*document), request);
	if (!plan.valid()) {
		return fail(error, plan.error);
	}
	if (!validateLevelSoundWav(wav, error)) {
		return false;
	}
	PackageStagingModel nextStaging = *staging;
	if (!stageAudioDelivery(wav, request.virtualPath, &nextStaging, replace, error)) {
		return false;
	}
	LevelMapDocument nextMap = *document;
	int id = -1;
	if (!addLevelMapEntity(&nextMap, plan.className, request.origin, plan.properties, &id, error)) {
		return false;
	}
	*staging = std::move(nextStaging);
	*document = std::move(nextMap);
	if (entityId) {
		*entityId = id;
	}
	return true;
}

} // namespace vibestudio
