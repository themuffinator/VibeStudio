#include "core/portal_file.h"

#include <QCoreApplication>
#include <QFile>
#include <QList>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

constexpr qint64 kMaximumPortalFileBytes = 256LL * 1024 * 1024;
constexpr int kMaximumPortals = 4'000'000;

PortalFile failed(const char* message)
{
	PortalFile file;
	file.error = QCoreApplication::translate("VibeStudioPortalFile", message);
	return file;
}

bool wholeNumber(const QByteArray& token, int* value)
{
	bool ok = false;
	const int number = token.toInt(&ok);
	if (ok && value) {
		*value = number;
	}
	return ok;
}

// One portal line: "<points> <leaf> <leaf> [flags] (x y z) (x y z) ...".
bool parsePortal(const QByteArray& line, QVector<LevelMapVec3>* polygon)
{
	const qsizetype open = line.indexOf('(');
	if (open < 0) {
		return false;
	}
	const QList<QByteArray> head = line.left(open).simplified().split(' ');
	int count = 0;
	if (head.size() < 2 || !wholeNumber(head.first(), &count) || count < 3 || count > 1024) {
		return false;
	}
	polygon->clear();
	qsizetype at = open;
	while (at >= 0 && at < line.size()) {
		const qsizetype close = line.indexOf(')', at);
		if (close < 0) {
			return false;
		}
		const QList<QByteArray> parts = line.mid(at + 1, close - at - 1).simplified().split(' ');
		if (parts.size() != 3) {
			return false;
		}
		bool okX = false;
		bool okY = false;
		bool okZ = false;
		const LevelMapVec3 point {parts.at(0).toDouble(&okX), parts.at(1).toDouble(&okY), parts.at(2).toDouble(&okZ), true};
		if (!okX || !okY || !okZ || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
			return false;
		}
		polygon->push_back(point);
		at = line.indexOf('(', close);
	}
	return polygon->size() == count;
}

} // namespace

PortalFile parsePortalFile(const QByteArray& bytes)
{
	QList<QByteArray> lines = bytes.split('\n');
	for (QByteArray& line : lines) {
		line = line.trimmed();
	}
	lines.removeAll(QByteArray());
	if (lines.isEmpty()) {
		return failed(QT_TRANSLATE_NOOP("VibeStudioPortalFile", "The portal file is empty."));
	}
	const QByteArray magic = lines.first();
	if (magic != "PRT1" && magic != "PRT1-AM" && magic != "PRT2") {
		return failed(QT_TRANSLATE_NOOP("VibeStudioPortalFile", "This is not a PRT1, PRT1-AM or PRT2 portal file."));
	}
	// The counts follow the magic, one to a line, until the first portal.
	QList<int> counts;
	int line = 1;
	for (; line < lines.size(); ++line) {
		int value = 0;
		if (lines.at(line).contains('(') || !wholeNumber(lines.at(line), &value)) {
			break;
		}
		counts.push_back(value);
	}
	PortalFile file;
	file.format = QString::fromLatin1(magic);
	int portalCount = 0;
	if (magic == "PRT2") {
		// Leaves, clusters, portals.
		if (counts.size() < 3) {
			return failed(QT_TRANSLATE_NOOP("VibeStudioPortalFile", "The PRT2 header needs leaf, cluster and portal counts."));
		}
		file.leafCount = counts.at(0);
		file.clusterCount = counts.at(1);
		portalCount = counts.at(2);
	} else if (magic == "PRT1-AM") {
		// Clusters, portals, leaves.
		if (counts.size() < 3) {
			return failed(QT_TRANSLATE_NOOP("VibeStudioPortalFile", "The PRT1-AM header needs cluster, portal and leaf counts."));
		}
		file.clusterCount = counts.at(0);
		portalCount = counts.at(1);
		file.leafCount = counts.at(2);
	} else {
		// Leaves (clusters in Quake II and III) and portals; q3map adds its
		// solid face count, whose faces follow the portals.
		if (counts.size() < 2) {
			return failed(QT_TRANSLATE_NOOP("VibeStudioPortalFile", "The portal file header needs leaf and portal counts."));
		}
		file.leafCount = counts.at(0);
		portalCount = counts.at(1);
	}
	if (portalCount < 0 || portalCount > kMaximumPortals) {
		return failed(QT_TRANSLATE_NOOP("VibeStudioPortalFile", "The portal count is out of range."));
	}
	file.portals.reserve(portalCount);
	QVector<LevelMapVec3> polygon;
	for (; line < lines.size() && file.portals.size() < portalCount; ++line) {
		if (!parsePortal(lines.at(line), &polygon)) {
			// Leaf-to-cluster lines and anything else are not portals.
			continue;
		}
		for (const LevelMapVec3& point : std::as_const(polygon)) {
			if (!file.mins.valid) {
				file.mins = point;
				file.maxs = point;
			} else {
				file.mins = {std::min(file.mins.x, point.x), std::min(file.mins.y, point.y), std::min(file.mins.z, point.z), true};
				file.maxs = {std::max(file.maxs.x, point.x), std::max(file.maxs.y, point.y), std::max(file.maxs.z, point.z), true};
			}
		}
		file.portals.push_back(polygon);
	}
	if (file.portals.size() != portalCount) {
		file.error = QCoreApplication::translate("VibeStudioPortalFile", "The header promises %n portal(s), but fewer could be read.", nullptr, portalCount);
		file.portals.clear();
		return file;
	}
	file.valid = true;
	return file;
}

PortalFile loadPortalFile(const QString& path)
{
	QFile input(path);
	if (!input.open(QIODevice::ReadOnly)) {
		PortalFile file;
		file.error = QCoreApplication::translate("VibeStudioPortalFile", "Could not read %1: %2").arg(path, input.errorString());
		return file;
	}
	if (input.size() > kMaximumPortalFileBytes) {
		return failed(QT_TRANSLATE_NOOP("VibeStudioPortalFile", "The portal file is larger than 256 MiB."));
	}
	return parsePortalFile(input.readAll());
}

} // namespace vibestudio
