#include "core/level_duplicate_p.h"
#include "core/level_placement_control_p.h"

#include <QHash>
#include <QSet>
#include <map>

// Independently implemented from NRC's namespace behaviour in radiant/map.cpp,
// libs/uniquenames.h and plugins/entity/namekeys.h at 449778b3840f8f2062daab15c87cb012f2ac945c
// (GPL-2.0-or-later). See docs/CREDITS.md. Decimal strings avoid upstream integer
// overflow, and dynamic strings impose no fixed name-length limit.
namespace vibestudio::detail {
namespace {
bool asciiDigit(QChar value)
{
	return value >= QLatin1Char('0') && value <= QLatin1Char('9');
}
bool namespaceKey(const QString& key)
{
	if (key == QLatin1String("target") || key == QLatin1String("targetname") || key == QLatin1String("killtarget")) { return true; }
	if (!key.startsWith(QLatin1String("target"))) { return false; }
	qsizetype i = 6;
	// NRC accepts a complete strtol-style integer suffix, including its leading
	// C whitespace and sign. Recognition does not need an integer conversion.
	while (i < key.size() && (key[i] == QLatin1Char(' ') || (key[i].unicode() >= 9 && key[i].unicode() <= 13))) { ++i; }
	if (i < key.size() && (key[i] == QLatin1Char('+') || key[i] == QLatin1Char('-'))) { ++i; }
	const auto first = i;
	while (i < key.size() && asciiDigit(key[i])) { ++i; }
	return i > first && i == key.size();
}
struct Name {
	QString prefix;
	QString slot;
};
Name splitName(const QString& value)
{
	qsizetype first = value.size();
	while (first > 0 && asciiDigit(value[first - 1])) { --first; }
	if (first == value.size()) { return {value, QStringLiteral("-")}; }
	auto digit = first;
	while (digit + 1 < value.size() && value[digit] == QLatin1Char('0')) { ++digit; }
	return {value.left(first), value.mid(digit)};
}
struct Slots {
	QSet<QString> used;
	quint64 next = 1;
};
} // namespace

void makeLevelMapCopiesUnique(const QVector<LevelMapEntity>& destination, QVector<LevelMapEntity>* copies)
{
	QHash<QString, Slots> reserved;
	for (const auto& entity : destination) {
		for (const auto& property : entity.properties) {
			placementCancellationCheckpoint();
			if (!namespaceKey(property.key) || property.value.isEmpty()) { continue; }
			const auto name = splitName(property.value);
			reserved[name.prefix].used.insert(name.slot);
		}
	}
	// NRC groups observers in UTF-8 lexical order. All copied references to the
	// same old string receive one new string, including outgoing references.
	std::map<QByteArray, QString> names;
	for (const auto& entity : *copies) {
		for (const auto& property : entity.properties) {
			placementCancellationCheckpoint();
			if (namespaceKey(property.key) && !property.value.isEmpty()) { names.try_emplace(property.value.toUtf8(), property.value); }
		}
	}
	QHash<QString, QString> mapping;
	for (const auto& [bytes, value] : names) {
		Q_UNUSED(bytes);
		placementCancellationCheckpoint();
		const auto name = splitName(value);
		auto& namespaceSlots = reserved[name.prefix];
		auto slot = name.slot;
		auto replacement = value;
		if (namespaceSlots.used.contains(slot)) {
			do {
				placementCancellationCheckpoint();
				slot = QString::number(namespaceSlots.next++);
			} while (namespaceSlots.used.contains(slot));
			replacement = name.prefix + slot;
		}
		namespaceSlots.used.insert(slot);
		mapping.insert(value, replacement);
	}
	for (auto& entity : *copies) {
		for (auto& property : entity.properties) {
			placementCancellationCheckpoint();
			if (namespaceKey(property.key) && !property.value.isEmpty()) { property.value = mapping.value(property.value); }
		}
	}
}
QString makeLevelMapNameUnique(const QVector<LevelMapEntity>& destination, const QString& proposed)
{
	LevelMapEntity name;
	name.properties << LevelMapProperty{QStringLiteral("targetname"),proposed,0};
	QVector<LevelMapEntity> probe{name};
	makeLevelMapCopiesUnique(destination,&probe);
	return probe.first().properties.first().value;
}
} // namespace vibestudio::detail
