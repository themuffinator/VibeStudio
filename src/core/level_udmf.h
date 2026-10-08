#pragma once
#include "core/level_map.h"
#include <QJsonValue>

namespace vibestudio {
enum class LevelUdmfValueKind { Number, String, Boolean, Keyword };
struct LevelUdmfProperty {
	QString name, literal;
	QJsonValue value;
	LevelUdmfValueKind kind = LevelUdmfValueKind::Keyword;
	qsizetype begin = 0, valueBegin = 0, valueEnd = 0, end = 0;
	int line = 1;
};
struct LevelUdmfBlock {
	QString type;
	int index = -1;
	qsizetype begin = 0, close = 0, end = 0;
	QVector<LevelUdmfProperty> properties;
	[[nodiscard]] QString selector() const;
};
struct LevelUdmfDocument {
	QByteArray source;
	QString nameSpace;
	QVector<LevelUdmfProperty> globals;
	QVector<LevelUdmfBlock> blocks;
};
struct LevelUdmfPropertyEdit {
	QString object; // global, vertex:0, linedef:0, sidedef:0, sector:0, thing:0, or an extension block.
	QString key;
	QString literal; // One UDMF scalar, including quotes for strings.
	bool remove = false;
};
// Original lossless UDMF parser: bounded source/spans, no recursive grammar,
// unknown blocks/fields and every untouched byte remain in the owned source.
// Replaces native projection/UDMF issues only on success; never changes history.
bool readLevelUdmfText(const QByteArray& bytes, LevelMapDocument* document, QString* error = nullptr,
					   const std::function<bool()>& isCancelled = {});
QByteArray prepareLevelUdmfProperties(const LevelUdmfDocument& source, const QVector<LevelUdmfPropertyEdit>& edits,
									  QString* error = nullptr, const std::function<bool()>& isCancelled = {});
// Encode a validated native transform using only changed coordinate, heading and
// linedef endpoint values. Large selections use the document's property budget.
QByteArray prepareLevelUdmfTransform(const LevelUdmfDocument& source, const LevelMapUndoCommand& transform, QString* error = nullptr,
									 const std::function<bool()>& isCancelled = {});
// Append lossless thing-block copies in the supplied order. Each record's id
// names its source thing; only x/y/height may differ. Other fields, comments,
// lexical values and unknown extension properties are copied verbatim.
QByteArray prepareLevelUdmfThingCopies(const LevelUdmfDocument& source, const QVector<LevelMapDoomThing>& copies,
	QString* error = nullptr, const std::function<bool()>& isCancelled = {});
QJsonObject levelUdmfDocumentJson(const LevelUdmfDocument& document);
// Shared atomic authoring/history entry point, implemented with map commands.
bool editLevelMapUdmfProperties(LevelMapDocument* document, const QVector<LevelUdmfPropertyEdit>& edits, QString* error = nullptr,
								const std::function<bool()>& isCancelled = {});
} // namespace vibestudio
