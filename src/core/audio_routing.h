#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace vibestudio
{
inline constexpr int AudioSessionBusLimit = 32;
inline constexpr int AudioSessionSendLimit = 8;
struct AudioSend {
	QString targetId; // Empty is the stereo master; otherwise a bus ID.
	double gainDb = 0;
	double pan = 0; // Stereo balance, independent of the strip pan.
	bool preFader = false;
	bool enabled = true;
	bool operator==(const AudioSend &) const = default;
};
struct AudioTrackRouting {
	bool bus = false;
	QString outputId; // Empty is the stereo master.
	bool outputEnabled = true;
	bool invertLeft = false, invertRight = false, swapChannels = false;
	QVector<AudioSend> sends;
	bool operator==(const AudioTrackRouting &) const = default;
};
struct AudioSession;
struct AudioRoutingNode {
	int busIndex = -1;
	int output = -1; // Track index, or -1 for master.
	QVector<int> sends;
	bool reachesSolo = false;
};
struct AudioRoutingPlan {
	QVector<AudioRoutingNode> nodes;
	QVector<int> order;
	int buses = 0;
	bool solo = false, advanced = false;
};
// Validates every edge, including disabled sends/outputs, and rejects cycles.
// Stable topological order follows session order among independent strips.
QString prepareAudioRouting(const AudioSession &session, AudioRoutingPlan *plan = nullptr);
QJsonObject audioRoutingToJson(const AudioTrackRouting &routing);
bool audioRoutingFromJson(const QJsonObject &object, AudioTrackRouting *routing);
} // namespace vibestudio
