#include "app/code_editor.h"

#include "app/studio_theme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QTextBlock>

#include <algorithm>

namespace vibestudio {

namespace {

QColor blend(const QColor& from, const QColor& to, double amount)
{
	return QColor::fromRgbF(
		static_cast<float>(from.redF() + (to.redF() - from.redF()) * amount),
		static_cast<float>(from.greenF() + (to.greenF() - from.greenF()) * amount),
		static_cast<float>(from.blueF() + (to.blueF() - from.blueF()) * amount));
}

// Paints the gutter on behalf of the editor, which owns the text layout.
class LineNumberArea final : public QWidget {
public:
	explicit LineNumberArea(StudioCodeEditor* editor)
		: QWidget(editor)
		, m_editor(editor)
	{
		setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Line numbers"));
	}

	QSize sizeHint() const override
	{
		return {m_editor->lineNumberAreaWidth(), 0};
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		m_editor->paintLineNumbers(event);
	}

private:
	StudioCodeEditor* m_editor = nullptr;
};

} // namespace

StudioCodeEditor::StudioCodeEditor(QWidget* parent)
	: QPlainTextEdit(parent)
{
	m_lineNumberArea = new LineNumberArea(this);
	connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) {
		updateLineNumberAreaWidth();
	});
	connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
		updateLineNumberArea(rect, dy);
	});
	connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
		highlightCurrentLine();
		m_lineNumberArea->update();
	});
	// setPlainText() moves the cursor back to the start after the text change
	// without always announcing it, so the band is recomputed once it settles.
	connect(this, &QPlainTextEdit::textChanged, this, [this]() {
		QMetaObject::invokeMethod(this, [this]() {
			highlightCurrentLine();
			m_lineNumberArea->update();
		}, Qt::QueuedConnection);
	});
	updateLineNumberAreaWidth();
	highlightCurrentLine();
}

int StudioCodeEditor::lineNumberAreaWidth() const
{
	int digits = 1;
	int maximum = std::max(1, blockCount());
	while (maximum >= 10) {
		maximum /= 10;
		++digits;
	}
	// Room for at least three digits so the text does not shift at line 100.
	digits = std::max(digits, 3);
	return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void StudioCodeEditor::paintLineNumbers(QPaintEvent* event)
{
	const StudioThemeTokens& theme = currentStudioTheme();
	const StudioThemeColors& c = theme.colors;
	QPainter painter(m_lineNumberArea);
	painter.fillRect(event->rect(), theme.highContrast ? c.input : blend(c.input, c.panel, 0.6));
	painter.setPen(c.borderSubtle);
	painter.drawLine(m_lineNumberArea->width() - 1, event->rect().top(), m_lineNumberArea->width() - 1, event->rect().bottom());
	painter.setFont(font());

	const int currentBlock = textCursor().blockNumber();
	QTextBlock block = firstVisibleBlock();
	int blockNumber = block.blockNumber();
	int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
	int bottom = top + qRound(blockBoundingRect(block).height());
	const int lineHeight = fontMetrics().height();
	while (block.isValid() && top <= event->rect().bottom()) {
		if (block.isVisible() && bottom >= event->rect().top()) {
			painter.setPen(blockNumber == currentBlock ? c.text : c.textFaint);
			painter.drawText(0, top, m_lineNumberArea->width() - 8, lineHeight, Qt::AlignRight | Qt::AlignVCenter,
				QString::number(blockNumber + 1));
		}
		block = block.next();
		top = bottom;
		bottom = top + qRound(blockBoundingRect(block).height());
		++blockNumber;
	}
}

void StudioCodeEditor::resizeEvent(QResizeEvent* event)
{
	QPlainTextEdit::resizeEvent(event);
	const QRect contents = contentsRect();
	m_lineNumberArea->setGeometry(QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
}

void StudioCodeEditor::changeEvent(QEvent* event)
{
	QPlainTextEdit::changeEvent(event);
	if (event->type() == QEvent::FontChange) {
		updateLineNumberAreaWidth();
	}
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
		highlightCurrentLine();
		m_lineNumberArea->update();
	}
}

void StudioCodeEditor::updateLineNumberAreaWidth()
{
	setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void StudioCodeEditor::updateLineNumberArea(const QRect& rect, int dy)
{
	if (dy != 0) {
		m_lineNumberArea->scroll(0, dy);
	} else {
		m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
	}
	if (rect.contains(viewport()->rect())) {
		updateLineNumberAreaWidth();
	}
}

void StudioCodeEditor::highlightCurrentLine()
{
	QList<QTextEdit::ExtraSelection> selections;
	if (!isReadOnly()) {
		const StudioThemeTokens& theme = currentStudioTheme();
		QTextEdit::ExtraSelection line;
		// A faint band, or an outline-strength one in the high-visibility
		// themes, so the caret's line is findable without hiding selection.
		line.format.setBackground(theme.highContrast ? theme.colors.panelRaised : blend(theme.colors.input, theme.colors.text, theme.light ? 0.04 : 0.05));
		line.format.setProperty(QTextFormat::FullWidthSelection, true);
		line.cursor = textCursor();
		line.cursor.clearSelection();
		selections.append(line);
	}
	setExtraSelections(selections);
}

} // namespace vibestudio
