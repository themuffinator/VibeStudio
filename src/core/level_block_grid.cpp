// Workflow reference: NRC XYWnd::XY_DrawBlockGrid and the ShowBlocks toggle in
// radiant/xywindow.cpp at 449778b3 (GPL-2.0-or-later); the worldspawn key is
// read as q3map2's ProcessWorldModel reads it. Original Qt implementation; see
// docs/CREDITS.md.

#include "core/level_block_grid.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

bool cSpace(QChar c)
{
	// C isspace() in the "C" locale, as sscanf skips before a number.
	return c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\v')
		|| c == QLatin1Char('\f') || c == QLatin1Char('\r');
}

const LevelMapEntity* worldspawn(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			return &entity;
		}
	}
	return nullptr;
}

} // namespace

int LevelMapBlockSize::along(int axis) const
{
	return axis >= 0 && axis < 3 ? std::max(0, axes[static_cast<std::size_t>(axis)]) : 0;
}

bool LevelMapBlockSize::anyAxis() const
{
	return along(0) > 0 || along(1) > 0 || along(2) > 0;
}

LevelMapBlockSize levelBlockSizeFromValue(const QString& value)
{
	LevelMapBlockSize size;
	size.value = value;
	std::array<qint64, 3> read {};
	qsizetype at = 0;
	const qsizetype length = value.size();
	for (int index = 0; index < 3; ++index) {
		while (at < length && cSpace(value.at(at))) {
			++at;
		}
		bool negative = false;
		if (at < length && (value.at(at) == QLatin1Char('+') || value.at(at) == QLatin1Char('-'))) {
			negative = value.at(at) == QLatin1Char('-');
			++at;
		}
		const qsizetype digits = at;
		qint64 number = 0;
		while (at < length && value.at(at) >= QLatin1Char('0') && value.at(at) <= QLatin1Char('9')) {
			// Saturate rather than overflow; such a size splits nothing anyway.
			number = std::min<qint64>(number * 10 + (value.at(at).unicode() - '0'), std::numeric_limits<int>::max());
			++at;
		}
		if (at == digits) {
			break;
		}
		read[static_cast<std::size_t>(index)] = negative ? -number : number;
		++size.numbers;
	}
	const auto axis = [](qint64 number) { return number > 0 ? static_cast<int>(number) : 0; };
	if (size.numbers == 1 || size.numbers == 2) {
		size.axes = {axis(read[0]), axis(read[0]), axis(read[0])};
	} else if (size.numbers == 3) {
		size.axes = {axis(read[0]), axis(read[1]), axis(read[2])};
	}
	return size;
}

bool levelMapHasBlockGrid(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

LevelMapBlockSize levelMapBlockSize(const LevelMapDocument& document)
{
	const LevelMapEntity* world = levelMapHasBlockGrid(document) ? worldspawn(document) : nullptr;
	if (!world) {
		return {};
	}
	for (const QString& key : {QStringLiteral("_blocksize"), QStringLiteral("blocksize"), QStringLiteral("chopsize")}) {
		for (const LevelMapProperty& property : world->properties) {
			if (property.key.compare(key, Qt::CaseInsensitive) != 0) {
				continue;
			}
			// q3map2 takes the first pair with the key and moves on to the
			// next name only when its value is empty.
			if (property.value.isEmpty()) {
				break;
			}
			LevelMapBlockSize size = levelBlockSizeFromValue(property.value);
			size.key = property.key;
			return size;
		}
	}
	return {};
}

QVector<double> levelBlockBoundaries(double low, double high, int size, int limit, bool* truncated)
{
	if (truncated) {
		*truncated = false;
	}
	QVector<double> boundaries;
	if (size <= 0 || !std::isfinite(low) || !std::isfinite(high) || low > high) {
		return boundaries;
	}
	const double first = std::ceil(low / size);
	const double last = std::floor(high / size);
	if (!std::isfinite(first) || !std::isfinite(last) || last < first) {
		return boundaries;
	}
	if (last - first + 1.0 > static_cast<double>(std::max(0, limit))) {
		if (truncated) {
			*truncated = true;
		}
		return boundaries;
	}
	const int count = static_cast<int>(last - first + 1.0);
	boundaries.reserve(count);
	// A double above 2^53 no longer advances when incremented by one.
	// Keep the loop bounded even for an extreme, but finite, world range.
	for (int offset = 0; offset < count; ++offset) {
		const double index = first + offset;
		// Avoid "-0" in pictures and reports.
		boundaries.append(index == 0.0 ? 0.0 : index * size);
	}
	return boundaries;
}

QString levelBlockSizeText(const LevelMapBlockSize& size)
{
	const auto part = [&size](int axis) {
		return size.along(axis) > 0 ? QString::number(size.along(axis))
									: QCoreApplication::translate("VibeStudioLevelBlockGrid", "off");
	};
	return QCoreApplication::translate("VibeStudioLevelBlockGrid", "%1 %4 %2 %4 %3")
		.arg(part(0), part(1), part(2), QString(QChar(0x00d7)));
}

QString levelBlockSizeSourceText(const LevelMapBlockSize& size)
{
	if (size.key.isEmpty()) {
		return QCoreApplication::translate("VibeStudioLevelBlockGrid", "the default");
	}
	if (size.numbers == 0) {
		return QCoreApplication::translate("VibeStudioLevelBlockGrid", "the default, as worldspawn %1 \"%2\" holds no number the compiler reads")
			.arg(size.key, size.value);
	}
	if (size.numbers == 2) {
		return QCoreApplication::translate("VibeStudioLevelBlockGrid", "worldspawn %1 \"%2\"; the compiler ignores a second number without a third")
			.arg(size.key, size.value);
	}
	return QCoreApplication::translate("VibeStudioLevelBlockGrid", "worldspawn %1 \"%2\"").arg(size.key, size.value);
}

} // namespace vibestudio
