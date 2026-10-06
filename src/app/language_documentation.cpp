#include "app/language_documentation.h"
#include "app/ui_primitives.h"
#include <QCoreApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>

namespace vibestudio {
namespace {
// Documentation never reads files, fetches images or invokes a global resource provider.
class InfoDocument final : public QTextDocument {
public:
	using QTextDocument::QTextDocument;
protected:
	QVariant loadResource(int, const QUrl&) override { return QByteArray(); }
};

} // namespace

QTextDocument* createLanguageDocumentationDocument(QObject* parent) { return new InfoDocument(parent); }

void renderLanguageDocumentation(QTextDocument* document, const QVector<LanguageHoverPart>& parts)
{
	document->clear(); QTextCursor cursor(document);
	bool first = true;
	for (const auto& part : parts) {
		if (!first) { cursor.insertBlock(QTextBlockFormat {}, QTextCharFormat {}); cursor.insertBlock(); } first = false;
		if (part.kind == QStringLiteral("markdown")) {
			InfoDocument markdown; markdown.setDefaultFont(document->defaultFont());
			#if QT_CONFIG(textmarkdownreader)
			markdown.setMarkdown(part.text, QTextDocument::MarkdownFeatures{QTextDocument::MarkdownDialectGitHub} | QTextDocument::MarkdownNoHTML);
			#else
			markdown.setPlainText(part.text);
			#endif
			QVector<QTextCursor> images;
			QVector<QPair<QTextCursor, QTextCharFormat>> anchors;
			for (auto block = markdown.begin(); block.isValid(); block = block.next()) {
				for (auto item = block.begin(); !item.atEnd(); ++item) {
					const auto fragment = item.fragment(); if (!fragment.isValid()) { continue; }
					QTextCursor selection(&markdown); selection.setPosition(fragment.position()); selection.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
					auto format = fragment.charFormat();
					if (format.isImageFormat()) { images << selection; }
					else if (format.isAnchor()) { format.setAnchor(false); format.clearProperty(QTextFormat::AnchorHref); format.clearProperty(QTextFormat::AnchorName); format.clearForeground(); format.setFontUnderline(false); anchors << qMakePair(selection, format); }
				}
			}
			for (auto& anchor : anchors) { anchor.first.setCharFormat(anchor.second); }
			for (auto image = images.rbegin(); image != images.rend(); ++image) { image->insertText(QCoreApplication::translate("CodeQuickInfo", "[Image omitted]"), QTextCharFormat {}); }
			cursor.insertFragment(QTextDocumentFragment(&markdown));
		} else {
			QTextCharFormat format; QTextBlockFormat block;
			if (part.kind == QStringLiteral("code")) {
				auto font = studioMonospaceFont(); font.setPointSizeF(document->defaultFont().pointSizeF()); format.setFont(font);
				block.setLayoutDirection(Qt::LeftToRight);
			}
			cursor.setBlockFormat(block); cursor.insertText(part.text, format);
		}
	}
	// Match the editor/assistant typography for Markdown inline and fenced code too.
	QTextCharFormat codeFormat; codeFormat.setFontFamilies(studioMonospaceFont().families()); codeFormat.setFontPointSize(document->defaultFont().pointSizeF());
	QVector<QTextCursor> codeRanges;
	for (auto block = document->begin(); block.isValid(); block = block.next()) {
		const bool fenced = block.blockFormat().hasProperty(QTextFormat::BlockCodeFence) || block.blockFormat().nonBreakableLines();
		if (fenced) { QTextCursor line(block); auto format = block.blockFormat(); format.setLayoutDirection(Qt::LeftToRight); line.setBlockFormat(format); }
		for (auto item = block.begin(); !item.atEnd(); ++item) {
			const auto fragment = item.fragment();
			if (fragment.isValid() && (fenced || fragment.charFormat().fontFixedPitch())) {
				QTextCursor code(document); code.setPosition(fragment.position()); code.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor); codeRanges << code;
			}
		}
	}
	for (auto& code : codeRanges) { code.mergeCharFormat(codeFormat); }
}
} // namespace vibestudio
