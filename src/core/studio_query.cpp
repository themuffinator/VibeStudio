#include "core/studio_query.h"

#include <QStringList>

#include <algorithm>

namespace vibestudio {

bool StudioQuery::isEmpty() const
{
	return terms.isEmpty();
}

bool StudioQuery::testsProperties() const
{
	return std::any_of(terms.cbegin(), terms.cend(), [](const StudioQueryTerm& term) {
		return !term.key.isEmpty();
	});
}

StudioQuery parseStudioQuery(const QString& text)
{
	// Words split on spaces outside double quotes; the quotes themselves go.
	QStringList words;
	QString word;
	bool quoted = false;
	bool started = false;
	for (const QChar ch : text) {
		if (ch == QLatin1Char('"')) {
			quoted = !quoted;
			started = true;
		} else if (ch.isSpace() && !quoted) {
			if (started) {
				words << word;
			}
			word.clear();
			started = false;
		} else {
			word += ch;
			started = true;
		}
	}
	if (started) {
		words << word;
	}

	StudioQuery query;
	static const QStringList operators = {
		QStringLiteral("!="), QStringLiteral("<="), QStringLiteral(">="), QStringLiteral("="), QStringLiteral(":"), QStringLiteral("<"), QStringLiteral(">"),
	};
	for (const QString& candidate : std::as_const(words)) {
		// A key is a run of letters, digits, and underscores that an operator
		// follows at once; anything else is a word.
		qsizetype keyEnd = 0;
		while (keyEnd < candidate.size() && (candidate.at(keyEnd).isLetterOrNumber() || candidate.at(keyEnd) == QLatin1Char('_'))) {
			++keyEnd;
		}
		StudioQueryTerm term;
		if (keyEnd > 0) {
			for (const QString& op : operators) {
				if (candidate.mid(keyEnd).startsWith(op)) {
					term.key = candidate.left(keyEnd).toLower();
					term.op = op;
					term.value = candidate.mid(keyEnd + op.size());
					break;
				}
			}
		}
		if (term.key.isEmpty()) {
			term.value = candidate;
		}
		query.terms.push_back(term);
	}
	return query;
}

bool parseStudioQueryNumber(const QString& text, double* value)
{
	QString number = text.trimmed().toLower();
	double scale = 1.0;
	// The longer suffixes first, so "kb" is not read as a "b" number.
	static const QVector<std::pair<QString, double>> suffixes = {
		{QStringLiteral("gb"), 1024.0 * 1024.0 * 1024.0},
		{QStringLiteral("mb"), 1024.0 * 1024.0},
		{QStringLiteral("kb"), 1024.0},
		{QStringLiteral("g"), 1024.0 * 1024.0 * 1024.0},
		{QStringLiteral("m"), 1024.0 * 1024.0},
		{QStringLiteral("k"), 1024.0},
	};
	for (const auto& [suffix, factor] : suffixes) {
		if (number.endsWith(suffix) && number.size() > suffix.size()) {
			number.chop(suffix.size());
			scale = factor;
			break;
		}
	}
	bool ok = false;
	const double parsed = number.toDouble(&ok);
	if (ok && value) {
		*value = parsed * scale;
	}
	return ok;
}

QStringList studioQueryUnknownKeys(const StudioQuery& query, const QSet<QString>& known)
{
	QStringList unknown;
	for (const StudioQueryTerm& term : query.terms) {
		if (!term.key.isEmpty() && !known.contains(term.key) && !unknown.contains(term.key)) {
			unknown << term.key;
		}
	}
	return unknown;
}

bool studioQueryMatches(const StudioQuery& query, const StudioQueryProperties& properties, const QString& description)
{
	for (const StudioQueryTerm& term : query.terms) {
		if (term.key.isEmpty()) {
			if (!description.contains(term.value, Qt::CaseInsensitive)) {
				return false;
			}
			continue;
		}
		const QList<QString> values = properties.values(term.key);
		if (term.op == QStringLiteral("!=")) {
			if (std::any_of(values.cbegin(), values.cend(), [&term](const QString& value) { return value.compare(term.value, Qt::CaseInsensitive) == 0; })) {
				return false;
			}
			continue;
		}
		const bool numeric = term.op == QStringLiteral("<") || term.op == QStringLiteral(">") || term.op == QStringLiteral("<=") || term.op == QStringLiteral(">=");
		double limit = 0.0;
		if (numeric && !parseStudioQueryNumber(term.value, &limit)) {
			return false;
		}
		const bool holds = std::any_of(values.cbegin(), values.cend(), [&term, numeric, limit](const QString& value) {
			if (!numeric) {
				return term.op == QStringLiteral("=") ? value.compare(term.value, Qt::CaseInsensitive) == 0 : value.contains(term.value, Qt::CaseInsensitive);
			}
			bool ok = false;
			const double number = value.toDouble(&ok);
			if (!ok) {
				return false;
			}
			return term.op == QStringLiteral("<") ? number < limit
				: term.op == QStringLiteral(">") ? number > limit
				: term.op == QStringLiteral("<=") ? number <= limit
								 : number >= limit;
		});
		if (!holds) {
			return false;
		}
	}
	return true;
}

} // namespace vibestudio
