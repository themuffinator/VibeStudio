#pragma once

// Filter queries shared by the studio's lists and the CLI.
//
// A query is space-separated terms that must all hold. `key=value` matches a
// value exactly and `key:value` one containing it, both ignoring case;
// `key!=value` holds when no value is equal; `key<n`, `key>n`, `key<=n`, and
// `key>=n` compare numbers, and a number may carry a size suffix (k, kb, m,
// mb, g, gb, counted in 1024s), so `size>2mb` means more than 2 MiB. A value
// holding spaces is quoted. A term with no operator is a word the item's
// description must contain. What the keys are is up to each list: map objects
// answer to their keys and fields, package entries to their path and size.

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// One term: a property test, or a bare word when `key` is empty.
struct StudioQueryTerm {
	QString key;
	// "=", "!=", ":", "<", ">", "<=", or ">=".
	QString op;
	QString value;
};

struct StudioQuery {
	QVector<StudioQueryTerm> terms;
	[[nodiscard]] bool isEmpty() const;
	// True when some term tests a property rather than a word.
	[[nodiscard]] bool testsProperties() const;
};

// An item's properties, by lower-case key; a key can hold several values.
using StudioQueryProperties = QMultiHash<QString, QString>;

StudioQuery parseStudioQuery(const QString& text);
bool studioQueryMatches(const StudioQuery& query, const StudioQueryProperties& properties, const QString& description);
// A query number: plain, or with a size suffix counted in 1024s.
bool parseStudioQueryNumber(const QString& text, double* value);
// The keys `query` tests that no item has, in the order written, so a list a
// query empties can say it was a mistyped key rather than no match.
QStringList studioQueryUnknownKeys(const StudioQuery& query, const QSet<QString>& known);

} // namespace vibestudio
