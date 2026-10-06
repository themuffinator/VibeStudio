#pragma once
#include "core/audio_automation.h"
#include "core/audio_lookahead.h"
#include "core/audio_loop.h"
#include "core/audio_reverb.h"
#include <QJsonArray>
#include <QMap>
#include <QStringList>
#include <QVector>
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace vibestudio
{
inline constexpr int AudioEffectChainLimit = 8;
inline constexpr quint64 AudioEffectMemoryLimit = 128ULL * 1024 * 1024;
struct AudioEffectParameter {
	QString key, label;
	double minimum = 0, maximum = 1, initial = 0;
	int decimals = 2;
	bool automatable = true;
};
struct AudioEffect {
	QString id;
	QString type;
	bool enabled = true;
	QMap<QString, double> parameters;
	bool operator==(const AudioEffect &) const = default;
};
using AudioEffectChain = QVector<AudioEffect>;
inline constexpr int AudioEffectAutomationPointLimit = 65536;
struct AudioEffectAutomationLane {
	QString effectId, parameter;
	bool enabled = true;
	QVector<AudioAutomationPoint> points;
	bool operator==(const AudioEffectAutomationLane &) const = default;
};
using AudioEffectAutomation = QVector<AudioEffectAutomationLane>;
QString validateAudioEffectAutomation(const AudioEffectChain &chain, int sampleRate,
                                      const AudioEffectAutomation &lanes);
QJsonArray audioEffectAutomationToJson(const AudioEffectAutomation &lanes);
bool audioEffectAutomationFromJson(const QJsonValue &value, AudioEffectAutomation *lanes, bool legacyShape = false);
void retainAudioEffectAutomation(const AudioEffectChain &chain, AudioEffectAutomation *lanes);
QStringList audioEffectTypes();
QString audioEffectName(const QString &type);
QVector<AudioEffectParameter> audioEffectParameters(const QString &type, int sampleRate);
AudioEffect makeAudioEffect(const QString &type, int sampleRate);
QString validateAudioEffects(const AudioEffectChain &chain, int sampleRate);
quint64 audioEffectMemoryBytes(const AudioEffectChain &chain, int sampleRate, const AudioEffectAutomation &lanes = {});
// Returns -1 for an invalid chain. Creative delays do not add processing latency.
int audioEffectLatencyFrames(const AudioEffectChain &chain, int sampleRate);
bool audioEffectsHaveTail(const AudioEffectChain &chain, const AudioEffectAutomation &lanes = {});
bool audioEffectsEnabled(const AudioEffectChain &chain);
QJsonArray audioEffectsToJson(const AudioEffectChain &chain);
bool audioEffectsFromJson(const QJsonValue &value, AudioEffectChain *chain);

// One processing stream owns this state. Prepare allocates delay storage;
// process/reset use no allocation, locks, file access or device access.
// Processing latency is reported separately from intentional creative delays.
class AudioEffectsProcessor {
  public:
	bool prepare(const AudioEffectChain &chain, int sampleRate, QString *error, const AudioEffectAutomation &lanes = {},
	             int inputLatency = 0, AudioTimelineLoop loop = {});
	void reset(qint64 firstFrame = 0);
	bool process(std::span<double> stereo, qint64 firstFrame = -1);
	[[nodiscard]] bool empty() const { return m_units.empty(); }
	[[nodiscard]] int latencyFrames() const { return m_latencyFrames; }

  private:
	friend class AudioSessionRenderer;
	// The owning renderer validates the prepared bounds before toggling them.
	void setLoopEnabled(bool enabled) { m_loop.enabled = enabled; }
	enum class Kind {
		Gain,
		Filter,
		Compressor,
		Gate,
		Limiter,
		LookaheadLimiter,
		Delay,
		Saturation,
		Reverb,
		ModulatedDelay,
		Tremolo,
		Phaser
	};
	struct Unit {
		struct Lane {
			size_t parameter = 0;
			QVector<AudioAutomationPoint> points;
			qsizetype segment = 0;
		};
		Kind kind = Kind::Gain;
		int filter = 0;
		int inputLatency = 0, latency = 0;
		std::array<double, 5> coefficients{};
		std::array<double, 2> z1{}, z2{};
		std::array<double, 12> p{};
		std::array<double, 12> physical{};
		std::vector<Lane> lanes;
		std::vector<double> delay;
		std::unique_ptr<AudioReverb> reverb;
		std::unique_ptr<AudioLookaheadLimiter> lookahead;
		std::array<double, 8> allpass{};
		double phase = 0;
		size_t cursor = 0, validFrames = 0;
		double envelope = 0;
		int hold = 0;
		bool open = false;
	};
	void configure(Unit &unit);
	void automate(Unit &unit, qint64 frame);
	std::vector<Unit> m_units;
	int m_sampleRate = 48000;
	int m_latencyFrames = 0;
	qint64 m_nextFrame = 0, m_resetFrame = 0;
	AudioTimelineLoop m_loop;
};
} // namespace vibestudio
