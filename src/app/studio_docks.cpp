#include "app/studio_docks.h"

#include <QDataStream>
#include <QIODevice>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

#include <algorithm>
#include <utility>

namespace vibestudio {

namespace {

// QMainWindow::saveState() writes a marker and the caller's version, then one
// section per marker below: the dock areas, a floating group of tabbed panels
// each, and last the tool bars. The values and record layouts are Qt's own,
// read from qtbase v6.10.1 (src/widgets/widgets/): QMainWindow::saveState()
// in qmainwindow.cpp, QMainWindowLayoutState::saveState() in
// qmainwindowlayout.cpp, QDockAreaLayout::saveState() and
// QDockAreaLayoutInfo::saveState() in qdockarealayout.cpp, and the markers in
// qmainwindowlayout_p.h, qdockarealayout_p.h, and qtoolbararealayout_p.h.
constexpr qint32 kVersionMarker = 0xff;
constexpr quint8 kDockWidgetStateMarker = 0xfd;
constexpr quint8 kFloatingDockWidgetTabMarker = 0xf9;
constexpr quint8 kToolBarStateMarker = 0xfe;
constexpr quint8 kToolBarStateMarkerEx = 0xfc;
// Inside a dock area's record.
constexpr quint8 kSequenceMarker = 0xfc;
constexpr quint8 kTabMarker = 0xfa;
constexpr quint8 kWidgetMarker = 0xfb;
// QInternal::DockPosition: left, right, top, bottom.
constexpr qint32 kLeftDock = 0;
constexpr qint32 kRightDock = 1;
constexpr qint32 kDockCount = 4;
// Deeper nesting than any window builds means the bytes are not a state.
constexpr int kMaximumDepth = 32;

qint32 mirroredDockPosition(qint32 position)
{
	return position == kLeftDock ? kRightDock : position == kRightDock ? kLeftDock : position;
}

qint32 mirroredDockArea(qint32 area)
{
	if (area == Qt::LeftDockWidgetArea) {
		return Qt::RightDockWidgetArea;
	}
	if (area == Qt::RightDockWidgetArea) {
		return Qt::LeftDockWidgetArea;
	}
	return area;
}

// Copies one QDockAreaLayoutInfo record: a tab group (with the index of its
// current tab) or a sequence, its orientation, and its items, each a panel or
// a nested record. Mirrored, the items of a horizontal sequence, which sit
// side by side, swap order. A tab group keeps its order, since its tab bar
// lays its tabs out by the layout direction itself.
bool copyDockRecord(QDataStream& in, QDataStream& out, bool mirror, int depth)
{
	if (depth > kMaximumDepth) {
		return false;
	}
	quint8 marker = 0;
	in >> marker;
	if (marker != kTabMarker && marker != kSequenceMarker) {
		return false;
	}
	out << marker;
	if (marker == kTabMarker) {
		qint32 currentTab = -1;
		in >> currentTab;
		out << currentTab;
	}
	quint8 orientation = 0;
	qint32 count = 0;
	in >> orientation >> count;
	if (in.status() != QDataStream::Ok || count < 0) {
		return false;
	}
	out << orientation << count;
	QList<QByteArray> items;
	for (qint32 index = 0; index < count; ++index) {
		QByteArray item;
		QDataStream itemOut(&item, QIODevice::WriteOnly);
		itemOut.setVersion(in.version());
		quint8 itemMarker = 0;
		in >> itemMarker;
		itemOut << itemMarker;
		if (itemMarker == kWidgetMarker) {
			QString name;
			quint8 flags = 0;
			in >> name >> flags;
			itemOut << name << flags;
		} else if (itemMarker != kSequenceMarker) {
			return false;
		}
		// Position, size, minimum, and maximum along the record's orientation,
		// or a floating panel's geometry.
		qint32 numbers[4] = {};
		for (qint32& number : numbers) {
			in >> number;
		}
		for (const qint32 number : numbers) {
			itemOut << number;
		}
		if (itemMarker == kSequenceMarker && !copyDockRecord(in, itemOut, mirror, depth + 1)) {
			return false;
		}
		if (in.status() != QDataStream::Ok) {
			return false;
		}
		items.push_back(item);
	}
	if (mirror && marker == kSequenceMarker && orientation == Qt::Horizontal) {
		std::reverse(items.begin(), items.end());
	}
	for (const QByteArray& item : std::as_const(items)) {
		out.writeRawData(item.constData(), static_cast<int>(item.size()));
	}
	return true;
}

// The dock areas' section: how many areas hold panels, each one's position,
// size, and record, then the central widget's size and the area each corner
// belongs to. Left and right trade records; the corners mirror.
bool mirrorDockAreas(QDataStream& in, QDataStream& out)
{
	qint32 count = 0;
	in >> count;
	if (in.status() != QDataStream::Ok || count < 0 || count > kDockCount) {
		return false;
	}
	out << count;
	for (qint32 index = 0; index < count; ++index) {
		qint32 position = -1;
		QSize size;
		in >> position >> size;
		if (in.status() != QDataStream::Ok || position < 0 || position >= kDockCount) {
			return false;
		}
		out << mirroredDockPosition(position) << size;
		if (!copyDockRecord(in, out, true, 0)) {
			return false;
		}
	}
	QSize central;
	qint32 corners[4] = {};
	in >> central;
	for (qint32& corner : corners) {
		in >> corner;
	}
	if (in.status() != QDataStream::Ok) {
		return false;
	}
	// Top left, top right, bottom left, bottom right.
	out << central << mirroredDockArea(corners[1]) << mirroredDockArea(corners[0]) << mirroredDockArea(corners[3])
		<< mirroredDockArea(corners[2]);
	return true;
}

} // namespace

Qt::DockWidgetArea trailingDockArea(Qt::LayoutDirection direction)
{
	return direction == Qt::RightToLeft ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
}

QString trailingPanelGlyph(Qt::LayoutDirection direction)
{
	return direction == Qt::RightToLeft ? QStringLiteral("sidebar-left") : QStringLiteral("sidebar-right");
}

std::optional<QByteArray> mirroredWindowState(const QByteArray& state)
{
	QDataStream in(state);
	in.setVersion(QDataStream::Qt_5_0);
	QByteArray mirrored;
	QDataStream out(&mirrored, QIODevice::WriteOnly);
	out.setVersion(QDataStream::Qt_5_0);
	qint32 marker = 0;
	qint32 version = 0;
	in >> marker >> version;
	if (in.status() != QDataStream::Ok || marker != kVersionMarker) {
		return std::nullopt;
	}
	out << marker << version;
	bool docksMirrored = false;
	while (!in.atEnd()) {
		quint8 section = 0;
		in >> section;
		out << section;
		if (section == kDockWidgetStateMarker && !docksMirrored) {
			if (!mirrorDockAreas(in, out)) {
				return std::nullopt;
			}
			docksMirrored = true;
		} else if (section == kFloatingDockWidgetTabMarker) {
			// A floating group stays where it is on the screen, tabs and all.
			QRect geometry;
			in >> geometry;
			out << geometry;
			if (in.status() != QDataStream::Ok || !copyDockRecord(in, out, false, 0)) {
				return std::nullopt;
			}
		} else if (section == kToolBarStateMarker || section == kToolBarStateMarkerEx) {
			// The tool bars come last and keep their places.
			const qint64 offset = in.device()->pos();
			out.writeRawData(state.constData() + offset, static_cast<int>(state.size() - offset));
			break;
		} else {
			return std::nullopt;
		}
	}
	if (!docksMirrored || in.status() != QDataStream::Ok || out.status() != QDataStream::Ok) {
		return std::nullopt;
	}
	return mirrored;
}

QByteArray windowStateForDirection(const QByteArray& state, Qt::LayoutDirection direction)
{
	if (direction != Qt::RightToLeft || state.isEmpty()) {
		return state;
	}
	return mirroredWindowState(state).value_or(state);
}

} // namespace vibestudio
