#pragma once

#include "core/audio_effects.h"
#include "core/audio_latency.h"
#include "core/audio_meter.h"
#include "core/audio_project.h"
#include "core/audio_routing.h"
#include "core/audio_tempo.h"
#include <QHash>
#include <array>
#include <optional>
#include <span>

namespace vibestudio
{

// Session media is an immutable, implicitly shared sample snapshot. Timeline
// edits only replace small descriptors; they never rewrite imported samples.
inline constexpr int AudioSessionTrackLimit = 64;
inline constexpr int AudioSessionSourceLimit = 128;
inline constexpr int AudioSessionRegionLimit = 4096;
inline constexpr int AudioSessionGroupLimit = AudioSessionRegionLimit / 2;
inline constexpr int AudioSessionTakeLaneLimit = 128;
inline constexpr qint64 AudioSessionSampleLimit = 64 * 1024 * 1024;
inline constexpr qint64 AudioSessionFrameLimit = AudioAutomationFrameLimit;

struct AudioSessionSource {
	QString id;
	AudioProject audio;
};

struct AudioSessionRegion {
	QString id;
	QString name;
	QString sourceId;
	qint64 position = 0;
	qint64 sourceOffset = 0;
	qint64 length = 0;
	double gainDb = 0;
	qint64 fadeIn = 0;
	qint64 fadeOut = 0;
	bool muted = false;
	QString groupId = {};
	// A split retains a window into the original fade envelope. Zero span means
	// a normal envelope over length, with zero start. Source offsets are separate.
	qint64 fadeStart = 0;
	qint64 fadeSpan = 0;
};

struct AudioSessionGroup {
	QString id;
	QString name;
};

// Retained alternatives are inaudible until a range is promoted into the main
// arrangement. They share immutable source media but own their clip identities.
// Group links apply to the active arrangement, not to retained alternatives.
struct AudioSessionTakeLane {
	QString id;
	QString name;
	QVector<AudioSessionRegion> regions;
};

inline double audioSessionRegionFade(const AudioSessionRegion &region, qint64 local)
{
	const auto at = local + region.fadeStart;
	const auto span = region.fadeSpan ? region.fadeSpan : region.length;
	double fade = 1;
	if (region.fadeIn > 0 && at < region.fadeIn)
		fade *= region.fadeIn == 1 ? 0 : double(at) / double(region.fadeIn - 1);
	if (region.fadeOut > 0 && span - at <= region.fadeOut)
		fade *= region.fadeOut == 1 ? 0 : double(span - at - 1) / double(region.fadeOut - 1);
	return fade;
}

struct AudioSessionTrack {
	QString id;
	QString name;
	double gainDb = 0;
	double pan = 0;
	bool muted = false;
	bool solo = false;
	// Absolute timeline frames; gain is a dB offset to the track trim, pan
	// replaces static pan. Outgoing linear/step/smooth curves have held endpoints.
	QVector<AudioAutomationPoint> gainAutomation;
	QVector<AudioAutomationPoint> panAutomation;
	QVector<AudioSessionRegion> regions;
	AudioTrackRouting routing;
	AudioEffectChain effects;
	AudioEffectAutomation effectAutomation;
	QVector<AudioSessionTakeLane> takeLanes;
};

// Media accounting includes both audible clips and retained alternatives.
// Rendering and ordinary arrangement edits explicitly use track.regions only.
template <typename Track, typename Visitor> void visitAudioTrackRegions(Track &track, Visitor visitor)
{
	for (auto &region : track.regions)
		visitor(region, false);
	for (auto &lane : track.takeLanes)
		for (auto &region : lane.regions)
			visitor(region, true);
}

struct AudioSession {
	int sampleRate = 48000;
	AudioTempoMap musicalTime;
	double masterGainDb = 0;
	QString name;
	QVector<AudioSessionSource> sources;
	QVector<AudioSessionTrack> tracks;
	AudioEffectChain masterEffects;
	AudioEffectAutomation masterEffectAutomation;
	double effectTailSeconds = 2;
	QVector<AudioSessionGroup> groups;
};

struct AudioSessionResult {
	AudioSession session;
	QString error;
	QString addedTrackId;
	QString addedRegionId;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled; }
};

struct AudioSessionEdit {
	// add/remove-track, track, region, remove-region, duplicate, split, automation,
	// master, add-bus, routing. Missing targets are errors; input is unchanged on failure.
	QString operation;
	AudioTempoMap musicalTime;
	QString trackId;
	QString regionId;
	QString name;
	qint64 position = 0;
	qint64 sourceOffset = 0;
	qint64 length = 0;
	qint64 fadeIn = 0;
	qint64 fadeOut = 0;
	double gainDb = 0;
	double pan = 0;
	bool muted = false;
	bool solo = false;
	QVector<AudioAutomationPoint> gainAutomation;
	QVector<AudioAutomationPoint> panAutomation;
	AudioTrackRouting routing;
	AudioEffectChain effects;
	// Omission preserves lanes for retained effect IDs; removal/replacement prunes them.
	std::optional<AudioEffectAutomation> effectAutomation;
	// -1 retains the saved tail. Empty track ID selects the master chain.
	double effectTailSeconds = -1;
	// Otherwise an unchanged fade retains its inherited segment after a split.
	bool resetFades = false;
};

QString validateAudioSessionStructure(const AudioSession &session);
QString validateAudioSession(const AudioSession &session, const AudioWorkControl &control = {});
qint64 audioSessionFrames(const AudioSession &session);
AudioSessionResult editAudioSession(const AudioSession &session, const AudioSessionEdit &edit);
// Explicit resampling only; mono/stereo snapshots share the session clock.
AudioSessionResult importAudioSessionSource(const AudioSession &session, const AudioProject &source,
                                            const QString &trackId = {}, qint64 position = 0, bool resample = false,
                                            const AudioWorkControl &control = {});

// Prepared once on a worker and owned by one rendering stream. Contiguous
// blocks retain effect history and are independent of block size. A discontinuity
// starts fresh; seek/stop explicitly reset. Loop boundaries retain processing
// state while authored positions repeat. No implicit preroll or device.
struct AudioSessionRenderTarget {
	// Empty selects the master mix. Strip taps precede sends, downstream buses
	// and the master; pre-fader also precedes the strip's gain/pan/inserts.
	enum class Tap { PostFader, PreFader };
	QString stripId;
	Tap tap = Tap::PostFader;
};
enum class AudioSessionRenderClock { Compensated, Live };
// A caller-owned input window within one live rendering block. The samples are
// dry mono/stereo audio, ordered by channel; no device or capture storage is
// retained. Monitoring joins the track before its polarity, pan, sends and
// inserts. Replacement suppresses existing regions only in this input window.
struct AudioSessionLiveInput {
	int trackIndex = -1;
	int channels = 1;
	std::span<const float> samples{};
	int offsetFrames = 0;
	double gain = 1;
	bool replacePlayback = false;
};
class AudioSessionRenderer {
  public:
	enum class BlockStatus { Ready, InvalidRange, Cancelled, Overflow };
	bool prepare(const AudioSession &session, QString *error, const AudioWorkControl &control = {},
	             const AudioSessionRenderTarget &target = {}, AudioMeterWindow metering = {},
	             AudioSessionRenderClock clock = AudioSessionRenderClock::Compensated, AudioTimelineLoop loop = {});
	void resetProcessing();
	// Compensated playback may toggle bounds supplied at preparation, even if
	// initially disabled. Changing policy resets processing/meters without allocation;
	// the caller must discard queued output and restart its physical clock.
	bool setLoopEnabled(bool enabled);
	void resetMetering() { m_meters.reset(); }
	[[nodiscard]] AudioMeterSnapshot meters() const { return m_meters.snapshot(); }
	// Caller-owned stereo output and double-precision accumulation storage. No
	// allocation, locks, file access or model mutation after preparation. Invalid
	// or cancelled blocks clear the supplied output; never expose a partial mix.
	// With a prepared loop, first is unwrapped and compensation primes only once.
	BlockStatus renderInto(qint64 first, std::span<float> output, std::span<double> scratch,
	                       const AudioWorkControl &control = {});
	// Live mode never consumes future samples to hide processing latency. The
	// caller advances the physical sample clock, including the initial latency
	// and final drain. Master output frame N represents source frame N-latency.
	// No input is required to drain a tail. Each audio track has at most one
	// input window per call. playbackEnd stops source regions while effects and
	// live input can drain. Source/timeline data and native files stay immutable.
	// A prepared loop wraps authored time only; physical time and DSP remain
	// continuous. playbackEnd is the exclusive unwrapped source cutoff.
	BlockStatus renderLiveInto(qint64 first, std::span<float> output, std::span<double> scratch,
	                           std::span<const AudioSessionLiveInput> inputs = {}, const AudioWorkControl &control = {},
	                           qint64 playbackEnd = AudioSessionFrameLimit);
	AudioClipResult renderBlock(qint64 first, int frames, const AudioWorkControl &control = {});
	[[nodiscard]] qint64 frameCount() const { return audioSessionFrames(m_session); }
	[[nodiscard]] int sampleRate() const { return m_session.sampleRate; }
	[[nodiscard]] int processingLatencyFrames() const { return m_latency.total; }
	[[nodiscard]] size_t scratchSamples(int frames) const;

  private:
	friend AudioMeterReport measureAudioSessionMeters(const AudioSession &, qint64, qint64, int,
	                                                  const AudioWorkControl &,
	                                                  const std::function<void(qint64, qint64)> &);
	BlockStatus finishMetering(qint64 end, const AudioWorkControl &control);
	AudioMeterBank m_meters;
	qint64 m_meterEnd = 0;
	int m_meterTail = 0;
	AudioSession m_session;
	QHash<QString, int> m_sourceIndices;
	AudioRoutingPlan m_routing;
	AudioLatencyPlan m_latency;
	struct RouteDelays {
		AudioLatencyLine output;
		std::vector<AudioLatencyLine> sends;
	};
	std::vector<RouteDelays> m_routeDelays;
	std::vector<float> m_primeOutput;
	std::vector<double> m_primeScratch;
	BlockStatus renderRouted(qint64 first, std::span<float> output, std::span<double> scratch,
	                         const AudioWorkControl &control, std::span<const AudioSessionLiveInput> inputs = {},
	                         qint64 playbackEnd = AudioSessionFrameLimit);
	BlockStatus renderRoutedSpan(qint64 first, std::span<float> output, std::span<double> scratch,
	                             const AudioWorkControl &control, std::span<const AudioSessionLiveInput> inputs = {},
	                             qint64 playbackEnd = AudioSessionFrameLimit);
	std::vector<AudioEffectsProcessor> m_effectProcessors;
	AudioEffectsProcessor m_masterProcessor;
	qint64 m_nextFrame = -1;
	bool m_effectsActive = false;
	bool m_ready = false;
	AudioSessionRenderClock m_clock = AudioSessionRenderClock::Compensated;
	AudioTimelineLoop m_loop;
	int m_targetIndex = -1;
	bool m_preFaderTarget = false;
	std::array<bool, AudioSessionTrackLimit> m_included{};
};

// Bounded materialization for existing waveform editing, analysis and game
// delivery. Long sessions use the streaming session mixdown writer instead.
AudioClipResult renderAudioSession(const AudioSession &session, qint64 first = 0, qint64 end = -1,
                                   const AudioWorkControl &control = {});
QJsonObject audioSessionSummary(const AudioSession &session);

} // namespace vibestudio
