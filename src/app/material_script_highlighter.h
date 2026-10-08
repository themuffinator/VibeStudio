#pragma once

// Highlighting for the Materials page's text editor: Quake III shaders and
// Doom 3 materials (global keywords, stage keywords, values, names, blocks
// and comments), Boom SWANTBLS, Hexen/ZDoom ANIMDEFS and ericw-tools
// .wal_json, with the parser's problems underlined where they are.

#include "app/syntax_highlight.h"
#include "core/material_graph.h"
#include "core/material_model.h"

#include <QHash>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace vibestudio {

class MaterialScriptHighlighter final : public QSyntaxHighlighter {
	Q_OBJECT

public:
	explicit MaterialScriptHighlighter(QTextDocument* document);

	void setTextKind(MaterialTextKind kind);
	[[nodiscard]] MaterialTextKind textKind() const { return m_kind; }
	void setTheme(const StudioSyntaxTheme& theme);
	// Problems by 1-based line; a column of 0 marks the whole line.
	void setDiagnostics(const QVector<MaterialDiagnostic>& diagnostics);

protected:
	void highlightBlock(const QString& text) override;

private:
	void rebuildFormats();
	void highlightScript(const QString& text);
	void highlightSwantbls(const QString& text);
	void highlightAnimdefs(const QString& text);
	void highlightJson(const QString& text);
	void underlineDiagnostics(const QString& text);

	MaterialTextKind m_kind = MaterialTextKind::None;
	StudioSyntaxTheme m_theme;
	QSet<QString> m_global;
	QSet<QString> m_stage;
	QSet<QString> m_values;
	QHash<int, QVector<MaterialDiagnostic>> m_diagnostics;
	QTextCharFormat m_keyword;
	QTextCharFormat m_stageKeyword;
	QTextCharFormat m_value;
	QTextCharFormat m_name;
	QTextCharFormat m_path;
	QTextCharFormat m_number;
	QTextCharFormat m_string;
	QTextCharFormat m_comment;
	QTextCharFormat m_brace;
};

} // namespace vibestudio
