#pragma once

// Internals shared by level_generation.cpp (spec and plan) and
// level_generation_build.cpp (layout, geometry, and writers). Not installed.

#include <QString>
#include <QStringList>
#include <QVector>

#include <QtGlobal>

#include <utility>

namespace vibestudio::levelgen {

// A deterministic generator (splitmix64), so a seed gives the same level with
// every compiler and standard library.
class LevelRandom {
public:
	explicit LevelRandom(quint64 seed)
		: m_state(seed)
	{
	}
	quint64 next();
	// Inclusive.
	int range(int low, int high);
	bool chance(double probability);
	template<typename T>
	const T& pick(const QVector<T>& values)
	{
		return values[range(0, int(values.size()) - 1)];
	}
	const QString& pick(const QStringList& values)
	{
		return values[range(0, int(values.size()) - 1)];
	}
	template<typename T>
	void shuffle(QVector<T>* values)
	{
		for (int index = int(values->size()) - 1; index > 0; --index) {
			std::swap((*values)[index], (*values)[range(0, index)]);
		}
	}

private:
	quint64 m_state;
};

// What a plan's placement becomes in one game.
struct ItemEntry {
	QString game;
	QString kind;
	QString id;
	// Quake-family class name.
	QString className;
	// Doom thing type.
	int doomType = 0;
	// Quake-family: extra keys as key=value; Doom: "2" for Doom II only.
	QString extra;
};

const QVector<ItemEntry>& itemEntries();
bool itemEntryFor(const QString& game, const QString& kind, const QString& item, ItemEntry* out = nullptr);
QString normalizedItemId(const QString& item);
// Ceiling height above the floor for a room height class.
int levelForHeight(const QString& height);

} // namespace vibestudio::levelgen
