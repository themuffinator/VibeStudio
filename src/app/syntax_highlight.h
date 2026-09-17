#pragma once

// Syntax highlighting for the text/script surfaces the studio edits.
//
// Rules are data-driven so a language can be added without new widget code, and
// colours come from the active studio theme so high-contrast themes stay
// readable. Highlighting never changes document text.

#include <QColor>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class QTextDocument;

namespace vibestudio {

enum class StudioLanguage {
	PlainText,
	Config,          // .cfg / .rc / autoexec
	ShaderScript,    // idTech3 .shader
	QuakeC,          // .qc
	MapSource,       // .map
	EntityDef,       // .def / .fgd / .ent
	Ini,             // .ini / .arena / .menu-ish key-value
	Json,
};

struct StudioLanguageDescriptor {
	StudioLanguage language = StudioLanguage::PlainText;
	QString id;
	QString displayName;
	QStringList extensions;
	QStringList lineCommentTokens;
	QString blockCommentStart;
	QString blockCommentEnd;
	QStringList keywords;
	QStringList secondaryKeywords;
	bool caseSensitiveKeywords = false;
};

struct StudioSyntaxTheme {
	QColor text;
	QColor comment;
	QColor keyword;
	QColor secondaryKeyword;
	QColor stringLiteral;
	QColor number;
	QColor directive;
	QColor identifier;
	QColor errorUnderline;
	QColor warningUnderline;
	QColor currentLine;
	bool highContrast = false;
};

struct StudioDiagnosticMarker {
	int line = 0;          // 1-based
	int column = 0;        // 1-based, 0 means whole line
	int length = 0;        // 0 means to end of line
	QString severity;      // "error" | "warning" | "info"
	QString message;
};

QVector<StudioLanguageDescriptor> studioLanguageDescriptors();
StudioLanguage studioLanguageForPath(const QString& path);
StudioLanguage studioLanguageFromId(const QString& id);
QString studioLanguageId(StudioLanguage language);
QString studioLanguageDisplayName(StudioLanguage language);
bool studioLanguageDescriptorFor(StudioLanguage language, StudioLanguageDescriptor* out = nullptr);

StudioSyntaxTheme studioSyntaxTheme(bool lightTheme, bool highContrast);

class StudioSyntaxHighlighter final : public QSyntaxHighlighter {
	Q_OBJECT

public:
	explicit StudioSyntaxHighlighter(QTextDocument* document = nullptr);

	void setLanguage(StudioLanguage language);
	[[nodiscard]] StudioLanguage language() const;
	void setTheme(const StudioSyntaxTheme& theme);
	void setDiagnostics(const QVector<StudioDiagnosticMarker>& diagnostics);
	void clearDiagnostics();
	[[nodiscard]] QVector<StudioDiagnosticMarker> diagnostics() const;

protected:
	void highlightBlock(const QString& text) override;

private:
	struct Rule {
		QRegularExpression pattern;
		QTextCharFormat format;
		int captureGroup = 0;
	};

	void rebuildRules();
	void applyDiagnostics(const QString& text);

	StudioLanguage m_language = StudioLanguage::PlainText;
	StudioLanguageDescriptor m_descriptor;
	StudioSyntaxTheme m_theme;
	QVector<Rule> m_rules;
	QRegularExpression m_blockCommentStart;
	QRegularExpression m_blockCommentEnd;
	QTextCharFormat m_commentFormat;
	QVector<StudioDiagnosticMarker> m_diagnostics;
	bool m_hasBlockComments = false;
};

} // namespace vibestudio
