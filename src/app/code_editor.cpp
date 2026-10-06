#include "app/code_editor.h"

#include "app/studio_layout.h"
#include "app/studio_theme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QAbstractItemView>
#include <QCompleter>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QToolTip>
#include <QScrollBar>
#include <QTimer>
#include <QLineEdit>
#include <QMetaObject>
#include <QPainter>
#include <QRegularExpression>
#include <QPaintEvent>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>

namespace vibestudio {

namespace {

// Matches are counted up to this many; past it the bar says "10000+".
constexpr int kFindCountLimit = 10000;

// Zoom levels, as a browser steps them.
constexpr std::array<int, 13> kZoomSteps = {50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300};

QColor blend(const QColor& from, const QColor& to, double amount)
{
	return QColor::fromRgbF(
		static_cast<float>(from.redF() + (to.redF() - from.redF()) * amount),
		static_cast<float>(from.greenF() + (to.greenF() - from.greenF()) * amount),
		static_cast<float>(from.blueF() + (to.blueF() - from.blueF()) * amount));
}

// Paints the gutter on behalf of the editor, which owns the text layout, and
// folds or unfolds a pair when its chevron is clicked.
class LineNumberArea final : public QWidget {
public:
	explicit LineNumberArea(StudioCodeEditor* editor)
		: QWidget(editor)
		, m_editor(editor)
	{
		setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Line numbers"));
		setMouseTracking(true);
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

	void mousePressEvent(QMouseEvent* event) override
	{
		// Beside the sticky strip, a click goes to the pinned line, not to the
		// line scrolled under it.
		if (event->button() == Qt::LeftButton && event->position().y() < m_editor->stickyHeaderHeight()) {
			m_editor->stickyHeaderClicked(qRound(event->position().y()));
			event->accept();
			return;
		}
		const int line = m_editor->foldMarkerLineAt(event->position().toPoint());
		if (event->button() == Qt::LeftButton && line > 0) {
			m_editor->toggleFold(line);
			event->accept();
			return;
		}
		QWidget::mousePressEvent(event);
	}

	void mouseMoveEvent(QMouseEvent* event) override
	{
		const bool overMarker = m_editor->foldMarkerLineAt(event->position().toPoint()) > 0;
		if (overMarker != (cursor().shape() == Qt::PointingHandCursor)) {
			setCursor(overMarker ? Qt::PointingHandCursor : Qt::ArrowCursor);
		}
		QWidget::mouseMoveEvent(event);
	}

	bool event(QEvent* event) override
	{
		if (event->type() == QEvent::ToolTip) {
			const auto* help = static_cast<QHelpEvent*>(event);
			const int line = m_editor->foldMarkerLineAt(help->pos());
			if (line > 0) {
				QToolTip::showText(help->globalPos(), m_editor->foldMarkerToolTip(line), this);
			} else {
				QToolTip::hideText();
			}
			return true;
		}
		return QWidget::event(event);
	}

private:
	StudioCodeEditor* m_editor = nullptr;
};

// The strip above the text holding the opening lines of the blocks the top of
// the view is inside. The editor paints it and answers its clicks.
class StickyHeader final : public QWidget {
public:
	explicit StickyHeader(StudioCodeEditor* editor)
		: QWidget(editor->viewport())
		, m_editor(editor)
	{
		setObjectName(QStringLiteral("codeStickyHeader"));
		setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Enclosing blocks"));
		setCursor(Qt::PointingHandCursor);
		hide();
	}

protected:
	void paintEvent(QPaintEvent* event) override
	{
		m_editor->paintStickyHeaders(event, this);
	}

	// Every press, release, and double click stops here, so none reaches the
	// lines scrolled under the strip (a Ctrl+click's release would otherwise
	// go to the definition of what lies beneath).
	void mousePressEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::LeftButton) {
			m_editor->stickyHeaderClicked(qRound(event->position().y()));
		}
		event->accept();
	}

	void mouseReleaseEvent(QMouseEvent* event) override
	{
		event->accept();
	}

	void mouseDoubleClickEvent(QMouseEvent* event) override
	{
		event->accept();
	}

	void mouseMoveEvent(QMouseEvent* event) override
	{
		event->accept();
	}

	void wheelEvent(QWheelEvent* event) override
	{
		// The wheel still scrolls the text.
		event->ignore();
	}

private:
	StudioCodeEditor* m_editor = nullptr;
};

// Marks a line whose brace pair is folded. The document owns it, so the mark
// moves with the line through edits above it.
class FoldMark final : public QTextBlockUserData {};

bool hasFoldMark(const QTextBlock& block)
{
	return dynamic_cast<const FoldMark*>(block.userData()) != nullptr;
}

// For each line that opens a brace pair closing two or more lines further
// on, the line that closes it (0-based). Braces in `//` and `/* */` comments
// and in quoted strings do not count; a line opening more than one pair folds
// to the one closing last.
// Past this many characters a file does not fold: the brace scan reads the
// whole file after each edit, which a very large .map would feel in typing.
constexpr int kFoldCharacterLimit = 512 * 1024;

QHash<int, int> braceRegions(const QTextDocument* document)
{
	QHash<int, int> regions;
	if (document->characterCount() > kFoldCharacterLimit) {
		return regions;
	}
	QVector<int> open;
	bool blockComment = false;
	for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
		const QString text = block.text();
		const int line = block.blockNumber();
		QChar quote;
		for (qsizetype i = 0; i < text.size(); ++i) {
			const QChar ch = text.at(i);
			const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();
			if (blockComment) {
				if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
					blockComment = false;
					++i;
				}
				continue;
			}
			if (!quote.isNull()) {
				if (ch == QLatin1Char('\\')) {
					++i;
				} else if (ch == quote) {
					quote = QChar();
				}
				continue;
			}
			if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
				break;
			}
			if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
				blockComment = true;
				++i;
			} else if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
				quote = ch;
			} else if (ch == QLatin1Char('{')) {
				open << line;
			} else if (ch == QLatin1Char('}') && !open.isEmpty()) {
				const int start = open.takeLast();
				if (line - start >= 2) {
					regions[start] = std::max(regions.value(start, 0), line);
				}
			}
		}
	}
	return regions;
}

// A chevron pointing down (open) or toward the text's end (folded).
void drawFoldChevron(QPainter& painter, const QRectF& cell, bool folded, const QColor& color)
{
	const qreal size = std::max<qreal>(3.0, cell.height() * 0.2);
	const QPointF c = cell.center();
	QPolygonF chevron;
	if (folded) {
		chevron << QPointF(c.x() - size * 0.5, c.y() - size) << QPointF(c.x() + size * 0.5, c.y()) << QPointF(c.x() - size * 0.5, c.y() + size);
	} else {
		chevron << QPointF(c.x() - size, c.y() - size * 0.5) << QPointF(c.x(), c.y() + size * 0.5) << QPointF(c.x() + size, c.y() - size * 0.5);
	}
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	QPen pen(color, std::max<qreal>(1.2, size * 0.4));
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	painter.setPen(pen);
	painter.drawPolyline(chevron);
	painter.restore();
}

} // namespace

StudioCodeEditor::StudioCodeEditor(QWidget* parent)
	: QPlainTextEdit(parent)
{
	m_lineNumberArea = new LineNumberArea(this);
	initializeCompletion();
	initializeSnippet();
	connect(this, &QPlainTextEdit::textChanged, this, &StudioCodeEditor::dismissQuickInfoHint);
	connect(this, &QPlainTextEdit::cursorPositionChanged, this, &StudioCodeEditor::dismissQuickInfoHint);
	connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &StudioCodeEditor::dismissQuickInfoHint);
	connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &StudioCodeEditor::dismissQuickInfoHint);
	connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) {
		updateLineNumberAreaWidth();
	});
	connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
		updateLineNumberArea(rect, dy);
	});
	// A keystroke moves the caret and changes the text; the band, bracket
	// pair, and uses are worked out once for both. setPlainText() moves the
	// cursor back to the start without always announcing it, which the same
	// deferred pass covers. Scrolling brings other uses into view.
	connect(this, &QPlainTextEdit::cursorPositionChanged, this, &StudioCodeEditor::scheduleHighlight);
	connect(this, &QPlainTextEdit::textChanged, this, &StudioCodeEditor::scheduleHighlight);
	connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &StudioCodeEditor::scheduleHighlight);
	m_stickyHeader = new StickyHeader(this);
	// The pinned lines scroll sideways with the text.
	// A sideways scroll moves the viewport's children, the strip with them;
	// it is put back in place, and repainted scrolled as the text is.
	connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &StudioCodeEditor::updateStickyHeaders);
	connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
		m_caretMovedSinceHighlight = true;
	});
	// A fold stays with its opening line; an edit that unpairs its braces,
	// or a caret moved into it, opens it.
	connect(this, &QPlainTextEdit::textChanged, this, [this]() {
		m_foldRegionsDirty = true;
		applyFolds();
	});
	connect(this, &QPlainTextEdit::cursorPositionChanged, this, &StudioCodeEditor::revealCaret);
	viewport()->setMouseTracking(true);
	updateLineNumberAreaWidth();
	highlightCurrentLine();
}

void StudioCodeEditor::scheduleHighlight()
{
	if (m_highlightScheduled) {
		return;
	}
	m_highlightScheduled = true;
	QMetaObject::invokeMethod(this, [this]() {
		m_highlightScheduled = false;
		highlightCurrentLine();
		updateStickyHeaders();
		// A caret moved under the strip, by Up at the top of the view or a
		// find, is brought out below it. One above the view, where a tab or a
		// reload that restores its scroll position leaves it, stays there:
		// only from the first block shown on is its position known.
		if (m_caretMovedSinceHighlight && stickyHeaderHeight() > 0
			&& textCursor().block().blockNumber() >= firstVisibleBlock().blockNumber()) {
			const int covered = stickyHeaderHeight() - cursorRect().top();
			if (covered > 0) {
				const int lineHeight = std::max(1, fontMetrics().lineSpacing());
				verticalScrollBar()->setValue(std::max(0, verticalScrollBar()->value() - (covered + lineHeight - 1) / lineHeight));
				updateStickyHeaders();
			}
		}
		m_caretMovedSinceHighlight = false;
		m_lineNumberArea->update();
	}, Qt::QueuedConnection);
}

void StudioCodeEditor::setStickyHeadersEnabled(bool enabled)
{
	m_stickyHeadersEnabled = enabled;
	updateStickyHeaders();
}

bool StudioCodeEditor::stickyHeadersEnabled() const
{
	return m_stickyHeadersEnabled;
}

int StudioCodeEditor::stickyHeaderHeight() const
{
	return m_stickyHeader && m_stickyHeader->isVisible() ? m_stickyHeader->height() : 0;
}

QVector<int> StudioCodeEditor::stickyHeaderLines() const
{
	QVector<int> lines;
	for (const int line : m_stickyLines) {
		lines << line + 1;
	}
	return lines;
}

void StudioCodeEditor::updateStickyHeaders()
{
	if (!m_stickyHeader) {
		return;
	}
	constexpr int kMostPinned = 3;
	QVector<int> lines;
	const QTextBlock top = firstVisibleBlock();
	if (m_stickyHeadersEnabled && top.isValid()) {
		const QHash<int, int>& regions = foldRegions();
		// The blocks holding `line` whose opening lines are above the view,
		// innermost last.
		const auto holding = [&regions, &top](int line) {
			QVector<int> starts;
			for (auto it = regions.cbegin(); it != regions.cend(); ++it) {
				if (it.key() < top.blockNumber() && it.key() < line && line <= it.value()) {
					starts << it.key();
				}
			}
			std::sort(starts.begin(), starts.end());
			return starts;
		};
		// The strip covers the view's top lines, so what it pins is what holds
		// the first line below it.
		QVector<int> starts = holding(top.blockNumber());
		starts = holding(top.blockNumber() + static_cast<int>(std::min<qsizetype>(starts.size(), kMostPinned)));
		if (starts.size() > kMostPinned) {
			starts = starts.mid(starts.size() - kMostPinned);
		}
		for (const int start : std::as_const(starts)) {
			QTextBlock header = document()->findBlockByNumber(start);
			// A brace alone on its line: its block is named by the line before.
			if (header.text().trimmed() == QStringLiteral("{")) {
				QTextBlock previous = header.previous();
				while (previous.isValid() && previous.text().trimmed().isEmpty()) {
					previous = previous.previous();
				}
				header = previous.isValid() ? previous : header;
			}
			lines << header.blockNumber();
		}
	}
	m_stickyLines = lines;
	// What the strip shows, for screen readers: the pinned lines' text.
	QStringList spoken;
	for (const int line : std::as_const(lines)) {
		spoken << document()->findBlockByNumber(line).text().trimmed();
	}
	m_stickyHeader->setAccessibleDescription(spoken.join(QStringLiteral("; ")));
	const int lineHeight = fontMetrics().lineSpacing();
	m_stickyHeader->setGeometry(0, 0, viewport()->width(), static_cast<int>(lines.size()) * lineHeight + (lines.isEmpty() ? 0 : 1));
	m_stickyHeader->setVisible(!lines.isEmpty());
	m_stickyHeader->update();
	m_lineNumberArea->update();
}

void StudioCodeEditor::paintStickyHeaders(QPaintEvent* event, QWidget* surface)
{
	Q_UNUSED(event);
	const StudioThemeTokens& theme = currentStudioTheme();
	const StudioThemeColors& c = theme.colors;
	QPainter painter(surface);
	painter.fillRect(surface->rect(), theme.highContrast ? c.input : blend(c.input, c.panel, 0.5));
	const int lineHeight = fontMetrics().lineSpacing();
	for (int row = 0; row < m_stickyLines.size(); ++row) {
		const QTextBlock block = document()->findBlockByNumber(m_stickyLines.at(row));
		if (!block.isValid() || !block.layout()) {
			continue;
		}
		// Laid out before it is drawn, with its highlighting, as the editor
		// draws it and scrolled sideways as the text is.
		blockBoundingRect(block);
		block.layout()->draw(&painter, QPointF(contentOffset().x(), row * lineHeight));
	}
	painter.setPen(theme.highContrast ? c.text : c.border);
	painter.drawLine(0, surface->height() - 1, surface->width(), surface->height() - 1);
}

void StudioCodeEditor::stickyHeaderClicked(int y)
{
	const int row = y / std::max(1, fontMetrics().lineSpacing());
	if (row < 0 || row >= m_stickyLines.size()) {
		return;
	}
	setTextCursor(QTextCursor(document()->findBlockByNumber(m_stickyLines.at(row))));
	centerCursor();
	setFocus(Qt::MouseFocusReason);
}

int StudioCodeEditor::foldColumnWidth() const
{
	return fontMetrics().height();
}

int StudioCodeEditor::lineNumberAreaWidth() const
{
	int digits = 1;
	int maximum = std::max(1, blockCount());
	while (maximum >= 10) {
		maximum /= 10;
		++digits;
	}
	// Room for at least three digits so the text does not shift at line 100,
	// then the fold chevrons' column.
	digits = std::max(digits, 3);
	return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + foldColumnWidth();
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
	const int foldColumn = foldColumnWidth();
	const int foldLeft = m_lineNumberArea->width() - foldColumn - 2;
	const QHash<int, int>& regions = foldRegions();
	while (block.isValid() && top <= event->rect().bottom()) {
		if (block.isVisible() && bottom >= event->rect().top()) {
			painter.setPen(blockNumber == currentBlock ? c.text : c.textFaint);
			painter.drawText(0, top, foldLeft - 2, lineHeight, Qt::AlignRight | Qt::AlignVCenter,
				QString::number(blockNumber + 1));
			// A folded pair's chevron is drawn at full strength, so it is
			// found among the open ones.
			if (regions.contains(blockNumber)) {
				const bool folded = hasFoldMark(block);
				drawFoldChevron(painter, QRectF(foldLeft, top, foldColumn, lineHeight), folded, folded ? c.text : c.textFaint);
			}
		}
		block = block.next();
		top = bottom;
		bottom = top + qRound(blockBoundingRect(block).height());
		++blockNumber;
	}
	// Beside the pinned strip, the pinned lines' own numbers, not those of the
	// lines scrolled under it.
	if (!m_stickyLines.isEmpty() && m_stickyHeader && m_stickyHeader->isVisible()) {
		const int stripHeight = m_stickyHeader->height();
		const int stickyLineHeight = fontMetrics().lineSpacing();
		painter.fillRect(QRect(0, 0, m_lineNumberArea->width() - 1, stripHeight), theme.highContrast ? c.input : blend(c.input, c.panel, 0.5));
		painter.setPen(c.textFaint);
		for (int row = 0; row < m_stickyLines.size(); ++row) {
			painter.drawText(0, row * stickyLineHeight, foldLeft - 2, stickyLineHeight, Qt::AlignRight | Qt::AlignVCenter,
				QString::number(m_stickyLines.at(row) + 1));
		}
		painter.setPen(theme.highContrast ? c.text : c.border);
		painter.drawLine(0, stripHeight - 1, m_lineNumberArea->width(), stripHeight - 1);
	}
}

void StudioCodeEditor::setBaseFont(const QFont& font)
{
	m_baseFont = font;
	applyZoom();
}

int StudioCodeEditor::zoomPercent() const
{
	return m_zoomPercent;
}

int StudioCodeEditor::setZoomPercent(int percent)
{
	const int clamped = std::clamp(percent, kZoomSteps.front(), kZoomSteps.back());
	if (clamped != m_zoomPercent) {
		m_zoomPercent = clamped;
		applyZoom();
		if (zoomChanged) {
			zoomChanged(m_zoomPercent);
		}
	}
	return m_zoomPercent;
}

bool StudioCodeEditor::zoomInStep()
{
	const auto next = std::upper_bound(kZoomSteps.cbegin(), kZoomSteps.cend(), m_zoomPercent);
	if (next == kZoomSteps.cend()) {
		return false;
	}
	setZoomPercent(*next);
	return true;
}

bool StudioCodeEditor::zoomOutStep()
{
	const auto next = std::lower_bound(kZoomSteps.cbegin(), kZoomSteps.cend(), m_zoomPercent);
	if (next == kZoomSteps.cbegin()) {
		return false;
	}
	setZoomPercent(*std::prev(next));
	return true;
}

void StudioCodeEditor::applyZoom()
{
	QFont zoomed = m_baseFont;
	if (m_baseFont.pointSizeF() > 0) {
		zoomed.setPointSizeF(m_baseFont.pointSizeF() * m_zoomPercent / 100.0);
	} else if (m_baseFont.pixelSize() > 0) {
		zoomed.setPixelSize(std::max(4, qRound(m_baseFont.pixelSize() * m_zoomPercent / 100.0)));
	}
	setFont(zoomed);
	// Tab stops are measured in the font, so they grow with it.
	setTabStopDistance(QFontMetricsF(zoomed).horizontalAdvance(QLatin1Char(' ')) * 4.0);
}

void StudioCodeEditor::wheelEvent(QWheelEvent* event)
{
	// An editable text view only scrolls on Ctrl+wheel; the studio zooms, as a
	// browser or an IDE does. A notch is 120 units, however the wheel reports.
	if (event->modifiers() & Qt::ControlModifier) {
		m_wheelZoom += event->angleDelta().y();
		while (m_wheelZoom >= 120) {
			m_wheelZoom -= 120;
			zoomInStep();
		}
		while (m_wheelZoom <= -120) {
			m_wheelZoom += 120;
			zoomOutStep();
		}
		event->accept();
		return;
	}
	QPlainTextEdit::wheelEvent(event);
}

void StudioCodeEditor::mousePressEvent(QMouseEvent* event)
{
	// A click on a folded line's badge unfolds it.
	if (event->button() == Qt::LeftButton) {
		for (const auto& [badge, line] : foldBadges()) {
			if (badge.contains(event->position().toPoint())) {
				setFolded(line, false);
				event->accept();
				return;
			}
		}
	}
	QPlainTextEdit::mousePressEvent(event);
}

void StudioCodeEditor::mouseMoveEvent(QMouseEvent* event)
{
	dismissQuickInfoHint();
	QPlainTextEdit::mouseMoveEvent(event);
	bool overBadge = false;
	for (const auto& badge : foldBadges()) {
		overBadge = overBadge || badge.first.contains(event->position().toPoint());
	}
	if (overBadge != m_overFoldBadge) {
		m_overFoldBadge = overBadge;
		viewport()->setCursor(overBadge ? Qt::PointingHandCursor : Qt::IBeamCursor);
	}
}

bool StudioCodeEditor::requestQuickInfoAt(const QPoint& point, const QPoint& global)
{
	dismissQuickInfoHint();
	if (!quickInfoRequested || !viewport()->rect().contains(point) || point.y() < stickyHeaderHeight()) { return false; }
	for (const auto& badge : foldBadges()) { if (badge.first.contains(point)) { return false; } }
	auto at = cursorForPosition(point); const auto rect = cursorRect(at);
	if (point.y() < rect.top() || point.y() > rect.bottom()) { return false; }
	if (point.x() < rect.left() && at.positionInBlock() > 0) { at.movePosition(QTextCursor::PreviousCharacter); }
	const auto line = at.block().text();
	if (at.positionInBlock() >= line.size() || line[at.positionInBlock()].isSpace()) { return false; }
	m_quickInfoHintActive = quickInfoRequested(at.position(), global); return m_quickInfoHintActive;
}

void StudioCodeEditor::dismissQuickInfoHint()
{
	if (!m_quickInfoHintActive) { return; }
	m_quickInfoHintActive = false; QToolTip::hideText();
	if (quickInfoDismissed) { quickInfoDismissed(); }
}

bool StudioCodeEditor::viewportEvent(QEvent* event)
{
	if (event->type() == QEvent::ToolTip) {
		const auto* help = static_cast<QHelpEvent*>(event);
		requestQuickInfoAt(help->pos(), help->globalPos()); return true;
	}
	if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) { dismissQuickInfoHint(); }
	return QPlainTextEdit::viewportEvent(event);
}

namespace {

QString foldBadgeLabel(int hiddenLines)
{
	return QStringLiteral("\u2026 %1").arg(QCoreApplication::translate("VibeStudioCodeEditor", "%n line(s)", nullptr, hiddenLines));
}

} // namespace

QVector<QPair<QRect, int>> StudioCodeEditor::foldBadges() const
{
	// Worked out from the layout each time, not kept from the last paint: a
	// paint of only the caret's rectangle would forget the badges below it.
	QVector<QPair<QRect, int>> badges;
	const QHash<int, int>& regions = foldRegions();
	const QFontMetrics metrics(font());
	const int bottom = viewport()->height();
	for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
		if (!block.isVisible()) {
			continue;
		}
		const QRectF geometry = blockBoundingGeometry(block).translated(contentOffset());
		if (geometry.top() > bottom) {
			break;
		}
		const int end = hasFoldMark(block) ? regions.value(block.blockNumber(), -1) : -1;
		const QTextLayout* layout = block.layout();
		if (end < 0 || !layout || layout->lineCount() == 0) {
			continue;
		}
		const QTextLine last = layout->lineAt(layout->lineCount() - 1);
		const QString label = foldBadgeLabel(end - block.blockNumber() - 1);
		const qreal x = geometry.left() + layout->position().x() + last.x() + last.naturalTextWidth() + metrics.horizontalAdvance(QLatin1Char(' '));
		const qreal y = geometry.top() + layout->position().y() + last.y();
		badges.append({QRect(qRound(x), qRound(y) + 1, metrics.horizontalAdvance(label) + 12, qRound(last.height()) - 2), block.blockNumber()});
	}
	return badges;
}

void StudioCodeEditor::paintEvent(QPaintEvent* event)
{
	QPlainTextEdit::paintEvent(event);
	// Each folded line ends in a badge counting the lines it hides.
	const QHash<int, int>& regions = foldRegions();
	const StudioThemeTokens& theme = currentStudioTheme();
	QPainter painter(viewport());
	for (const auto& [badge, line] : foldBadges()) {
		if (!badge.intersects(event->rect())) {
			continue;
		}
		const QString label = foldBadgeLabel(regions.value(line) - line - 1);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(theme.highContrast ? theme.colors.text : theme.colors.borderSubtle);
		painter.setBrush(blend(theme.colors.input, theme.colors.text, theme.light ? 0.06 : 0.10));
		painter.drawRoundedRect(QRectF(badge).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
		painter.setPen(theme.highContrast ? theme.colors.text : theme.colors.textMuted);
		painter.drawText(badge, Qt::AlignCenter, label);
	}
}

const QHash<int, int>& StudioCodeEditor::foldRegions() const
{
	// The revision moves as an edit begins, before the caret signal that can
	// reach here ahead of textChanged; a stale answer there would drop the
	// marks of every fold below the edit.
	const QTextDocument* text = document();
	if (m_foldRegionsDirty || m_foldRegionsDocument != text || m_foldRegionsRevision != text->revision()) {
		m_foldRegions = braceRegions(text);
		m_foldRegionsDocument = text;
		m_foldRegionsRevision = text->revision();
		m_foldRegionsDirty = false;
	}
	return m_foldRegions;
}

void StudioCodeEditor::applyFolds()
{
	if (m_applyingFolds) {
		return;
	}
	QTextDocument* text = document();
	// Most files have no folds, and nothing to do.
	bool any = false;
	for (QTextBlock block = text->begin(); block.isValid() && !any; block = block.next()) {
		any = hasFoldMark(block) || !block.isVisible();
	}
	if (!any) {
		return;
	}
	{
		const QScopedValueRollback<bool> guard(m_applyingFolds, true);
		const QHash<int, int>& regions = foldRegions();
		int hideUntil = -1;
		int from = -1;
		int to = -1;
		for (QTextBlock block = text->begin(); block.isValid(); block = block.next()) {
			const int line = block.blockNumber();
			const bool visible = line >= hideUntil;
			if (block.isVisible() != visible) {
				block.setVisible(visible);
				from = from < 0 ? block.position() : from;
				to = block.position() + block.length();
			}
			if (hasFoldMark(block)) {
				const int end = regions.value(line, -1);
				if (end < 0) {
					// Enter at the start of a folded line leaves the mark on
					// the new blank line above; it goes with the line it was on.
					const QTextBlock next = block.next();
					if (block.text().trimmed().isEmpty() && next.isValid() && regions.contains(next.blockNumber()) && !hasFoldMark(next)) {
						QTextBlock moved = next;
						moved.setUserData(new FoldMark);
					}
					block.setUserData(nullptr);
				} else {
					hideUntil = std::max(hideUntil, end);
				}
			}
		}
		if (from >= 0) {
			text->markContentsDirty(from, std::min(to, text->characterCount()) - from);
			viewport()->update();
			m_lineNumberArea->update();
		}
	}
	// An edit can leave the caret on a line a fold now hides, such as a new
	// line typed at the end of a folded one; that fold opens.
	if (!textCursor().block().isVisible()) {
		revealCaret();
	}
}

void StudioCodeEditor::revealCaret()
{
	const QTextBlock caret = textCursor().block();
	if (caret.isVisible() || m_applyingFolds) {
		return;
	}
	const int line = caret.blockNumber();
	const QHash<int, int>& regions = foldRegions();
	int first = 0;
	int last = 0;
	for (QTextBlock block = document()->begin(); block.isValid() && block.blockNumber() < line; block = block.next()) {
		const int end = regions.value(block.blockNumber(), -1);
		if (end > line && hasFoldMark(block)) {
			block.setUserData(nullptr);
			if (first == 0) {
				first = block.blockNumber() + 2;
				last = end;
			}
		}
	}
	applyFolds();
	ensureCursorVisible();
	if (foldChanged && first > 0) {
		foldChanged(first, last, false);
	}
}

void StudioCodeEditor::unfoldLines(int first, int last)
{
	const QHash<int, int>& regions = foldRegions();
	bool changed = false;
	for (QTextBlock block = document()->begin(); block.isValid() && block.blockNumber() <= last; block = block.next()) {
		if (hasFoldMark(block) && regions.value(block.blockNumber(), -1) >= first) {
			block.setUserData(nullptr);
			changed = true;
		}
	}
	if (changed) {
		applyFolds();
	}
}

bool StudioCodeEditor::setFolded(int line, bool folded)
{
	const int end = foldRegions().value(line, -1);
	QTextBlock block = document()->findBlockByNumber(line);
	if (end < 0 || !block.isValid() || hasFoldMark(block) == folded) {
		return false;
	}
	if (folded) {
		// The caret leaves the lines about to be hidden for the fold's own line.
		const int caret = textCursor().blockNumber();
		if (caret > line && caret < end) {
			QTextCursor cursor = textCursor();
			cursor.setPosition(block.position() + block.length() - 1);
			setTextCursor(cursor);
		}
		block.setUserData(new FoldMark);
	} else {
		block.setUserData(nullptr);
	}
	applyFolds();
	ensureCursorVisible();
	if (foldChanged) {
		foldChanged(line + 2, end, folded);
	}
	return true;
}

bool StudioCodeEditor::foldAtCaret()
{
	const int line = textCursor().blockNumber();
	const QHash<int, int>& regions = foldRegions();
	int innermost = -1;
	for (auto it = regions.cbegin(); it != regions.cend(); ++it) {
		if (it.key() <= line && line <= it.value() && it.key() > innermost && !hasFoldMark(document()->findBlockByNumber(it.key()))) {
			innermost = it.key();
		}
	}
	return innermost >= 0 && setFolded(innermost, true);
}

bool StudioCodeEditor::unfoldAtCaret()
{
	return setFolded(textCursor().blockNumber(), false);
}

bool StudioCodeEditor::toggleFold(int line)
{
	const QTextBlock block = document()->findBlockByNumber(line - 1);
	return block.isValid() && setFolded(line - 1, !hasFoldMark(block));
}

int StudioCodeEditor::foldAll()
{
	const QHash<int, int> regions = foldRegions();
	const int caret = textCursor().blockNumber();
	int outermost = -1;
	int count = 0;
	for (auto it = regions.cbegin(); it != regions.cend(); ++it) {
		QTextBlock block = document()->findBlockByNumber(it.key());
		if (!hasFoldMark(block)) {
			block.setUserData(new FoldMark);
			++count;
		}
		if (it.key() < caret && caret < it.value() && (outermost < 0 || it.key() < outermost)) {
			outermost = it.key();
		}
	}
	// The caret waits on the outermost fold's line rather than inside it.
	if (outermost >= 0) {
		const QTextBlock block = document()->findBlockByNumber(outermost);
		QTextCursor cursor = textCursor();
		cursor.setPosition(block.position() + block.length() - 1);
		setTextCursor(cursor);
	}
	applyFolds();
	ensureCursorVisible();
	return count;
}

int StudioCodeEditor::unfoldAll()
{
	int count = 0;
	for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
		if (hasFoldMark(block)) {
			block.setUserData(nullptr);
			++count;
		}
	}
	applyFolds();
	return count;
}

QVector<int> StudioCodeEditor::highlightedUses() const
{
	return m_highlightedUses;
}

QVector<int> StudioCodeEditor::foldedLines() const
{
	QVector<int> lines;
	for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
		if (hasFoldMark(block)) {
			lines << block.blockNumber() + 1;
		}
	}
	return lines;
}

int StudioCodeEditor::foldMarkerLineAt(const QPoint& position) const
{
	if (position.x() < m_lineNumberArea->width() - foldColumnWidth() - 4 || position.y() < stickyHeaderHeight()) {
		return 0;
	}
	const QHash<int, int>& regions = foldRegions();
	for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
		if (!block.isVisible()) {
			continue;
		}
		const QRectF geometry = blockBoundingGeometry(block).translated(contentOffset());
		if (geometry.top() > position.y()) {
			break;
		}
		if (position.y() < geometry.bottom()) {
			return regions.contains(block.blockNumber()) ? block.blockNumber() + 1 : 0;
		}
	}
	return 0;
}

QString StudioCodeEditor::foldMarkerToolTip(int line) const
{
	const int end = foldRegions().value(line - 1, -1);
	if (end < 0) {
		return {};
	}
	return hasFoldMark(document()->findBlockByNumber(line - 1))
		? QCoreApplication::translate("VibeStudioCodeEditor", "Unfold lines %1 to %2").arg(line + 1).arg(end)
		: QCoreApplication::translate("VibeStudioCodeEditor", "Fold lines %1 to %2").arg(line + 1).arg(end);
}

void StudioCodeEditor::mouseReleaseEvent(QMouseEvent* event)
{
	// The press put the caret on the name; a click, not a drag, asks for it.
	QPlainTextEdit::mouseReleaseEvent(event);
	if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier) && !textCursor().hasSelection() && definitionRequested) {
		definitionRequested();
	}
}

QString StudioCodeEditor::wordBeforeCaret() const
{
	const QTextCursor cursor = textCursor();
	const QString line = cursor.block().text().left(cursor.positionInBlock());
	return line.mid(languageWordStart(line, int(line.size())));
}

void StudioCodeEditor::insertCompletion(const QString& completion)
{
	if (isReadOnly() || m_completionDocument != document()) { return; }
	// What was typed of the name is replaced, in one undo step, so the case
	// the completion has is the case that ends up in the file.
	runSnippetEdit([&]() {
		QTextCursor cursor = textCursor();
		cursor.beginEditBlock();
		cursor.setPosition(cursor.position() - int(wordBeforeCaret().size()), QTextCursor::KeepAnchor);
		cursor.insertText(completion);
		cursor.endEditBlock();
		setTextCursor(cursor);
	});
}

void StudioCodeEditor::resizeEvent(QResizeEvent* event)
{
	QPlainTextEdit::resizeEvent(event);
	const QRect contents = contentsRect();
	m_lineNumberArea->setGeometry(QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
	updateStickyHeaders();
}

namespace {

// The first and last line a selection covers. A selection that ends at the
// very start of a line does not take that line with it.
std::pair<QTextBlock, QTextBlock> selectedLineRange(const QPlainTextEdit* editor)
{
	const QTextCursor cursor = editor->textCursor();
	const int start = std::min(cursor.anchor(), cursor.position());
	const int end = std::max(cursor.anchor(), cursor.position());
	const QTextDocument* document = editor->document();
	const QTextBlock first = document->findBlock(start);
	QTextBlock last = document->findBlock(end);
	if (end > start && last.position() == end && last != first) {
		last = last.previous();
	}
	return {first, last};
}

QString linesText(const QTextBlock& first, const QTextBlock& last)
{
	QStringList lines;
	for (QTextBlock block = first; block.isValid(); block = block.next()) {
		lines << block.text();
		if (block == last) {
			break;
		}
	}
	return lines.join(QLatin1Char('\n'));
}

int leadingWhitespace(const QString& text)
{
	int count = 0;
	while (count < text.size() && text.at(count).isSpace()) {
		++count;
	}
	return count;
}

} // namespace

bool StudioCodeEditor::toggleLineComment(const QString& token)
{
	if (token.isEmpty() || isReadOnly()) {
		return false;
	}
	const auto [first, last] = selectedLineRange(this);
	bool any = false;
	bool allCommented = true;
	int column = -1;
	for (QTextBlock block = first; block.isValid(); block = block.next()) {
		const QString text = block.text();
		const int indent = leadingWhitespace(text);
		if (indent < text.size()) {
			any = true;
			allCommented = allCommented && text.mid(indent).startsWith(token);
			column = column < 0 ? indent : std::min(column, indent);
		}
		if (block == last) {
			break;
		}
	}
	if (!any) {
		return false;
	}
	QTextCursor edit(document());
	edit.beginEditBlock();
	for (QTextBlock block = first; block.isValid(); block = block.next()) {
		const QString text = block.text();
		const int indent = leadingWhitespace(text);
		if (indent < text.size()) {
			if (allCommented) {
				int length = static_cast<int>(token.size());
				if (indent + length < text.size() && text.at(indent + length) == QLatin1Char(' ')) {
					++length;
				}
				edit.setPosition(block.position() + indent);
				edit.setPosition(block.position() + indent + length, QTextCursor::KeepAnchor);
				edit.removeSelectedText();
			} else {
				edit.setPosition(block.position() + column);
				edit.insertText(token + QLatin1Char(' '));
			}
		}
		if (block == last) {
			break;
		}
	}
	edit.endEditBlock();
	return true;
}

bool StudioCodeEditor::duplicateLines()
{
	if (isReadOnly()) {
		return false;
	}
	const auto [first, last] = selectedLineRange(this);
	// Folds around the lines open first, so no hidden line is copied unseen.
	unfoldLines(first.blockNumber(), last.blockNumber());
	const QString copy = linesText(first, last);
	const int end = last.position() + last.length() - 1;
	// Plain positions: a QTextCursor copy would follow the edit below.
	const int anchor = textCursor().anchor();
	const int position = textCursor().position();
	QTextCursor edit(document());
	edit.beginEditBlock();
	edit.setPosition(end);
	edit.insertText(QLatin1Char('\n') + copy);
	edit.endEditBlock();
	const int shift = static_cast<int>(copy.size()) + 1;
	QTextCursor moved(document());
	moved.setPosition(anchor + shift);
	moved.setPosition(position + shift, QTextCursor::KeepAnchor);
	setTextCursor(moved);
	return true;
}

bool StudioCodeEditor::moveLines(int direction)
{
	if (isReadOnly() || direction == 0) {
		return false;
	}
	const auto [first, last] = selectedLineRange(this);
	const QTextBlock neighbour = direction < 0 ? first.previous() : last.next();
	if (!neighbour.isValid()) {
		return false;
	}
	// Folds around the lines and their neighbour open first, so no hidden
	// line moves unseen.
	unfoldLines(std::min(first.blockNumber(), neighbour.blockNumber()), std::max(last.blockNumber(), neighbour.blockNumber()));
	const QString moving = linesText(first, last);
	const QString other = neighbour.text();
	// Plain positions: a QTextCursor copy would follow the edit below.
	const int anchor = textCursor().anchor();
	const int position = textCursor().position();
	QTextCursor edit(document());
	edit.beginEditBlock();
	if (direction < 0) {
		edit.setPosition(neighbour.position());
		edit.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
		edit.insertText(moving + QLatin1Char('\n') + other);
	} else {
		edit.setPosition(first.position());
		edit.setPosition(neighbour.position() + neighbour.length() - 1, QTextCursor::KeepAnchor);
		edit.insertText(other + QLatin1Char('\n') + moving);
	}
	edit.endEditBlock();
	const int shift = (direction < 0 ? -1 : 1) * (static_cast<int>(other.size()) + 1);
	QTextCursor moved(document());
	moved.setPosition(anchor + shift);
	moved.setPosition(position + shift, QTextCursor::KeepAnchor);
	setTextCursor(moved);
	return true;
}

QString StudioCodeEditor::indentUnit() const
{
	for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
		const QString text = block.text();
		if (text.startsWith(QLatin1Char('\t'))) {
			return QStringLiteral("\t");
		}
		if (text.startsWith(QLatin1Char(' ')) && !text.trimmed().isEmpty()) {
			const int spaces = leadingWhitespace(text);
			return QString(std::clamp(spaces, 2, 8), QLatin1Char(' '));
		}
	}
	return QStringLiteral("\t");
}

bool StudioCodeEditor::indentLines(int direction)
{
	if (isReadOnly()) {
		return false;
	}
	const auto [first, last] = selectedLineRange(this);
	const QString unit = indentUnit();
	// With nothing selected the caret stays a caret, moved with its text, so
	// the next key types instead of replacing the line.
	const QTextCursor original = textCursor();
	const bool keepCaret = !original.hasSelection();
	const int caretBlock = original.blockNumber();
	int caretColumn = original.positionInBlock();
	QTextCursor cursor(document());
	cursor.beginEditBlock();
	for (QTextBlock block = first; block.isValid(); block = block.next()) {
		const QString text = block.text();
		if (direction > 0) {
			if (!text.trimmed().isEmpty()) {
				cursor.setPosition(block.position());
				cursor.insertText(unit);
				if (block.blockNumber() == caretBlock) {
					caretColumn += static_cast<int>(unit.size());
				}
			}
		} else {
			// Up to one unit: a tab, or up to the unit's width in spaces (four
			// when the unit is a tab).
			const int spaceWidth = unit == QStringLiteral("\t") ? 4 : static_cast<int>(unit.size());
			int remove = 0;
			if (text.startsWith(QLatin1Char('\t'))) {
				remove = 1;
			} else {
				while (remove < text.size() && remove < spaceWidth && text.at(remove) == QLatin1Char(' ')) {
					++remove;
				}
			}
			if (remove > 0) {
				cursor.setPosition(block.position());
				cursor.setPosition(block.position() + remove, QTextCursor::KeepAnchor);
				cursor.removeSelectedText();
				if (block.blockNumber() == caretBlock) {
					caretColumn = std::max(0, caretColumn - remove);
				}
			}
		}
		if (block == last) {
			break;
		}
	}
	cursor.endEditBlock();
	if (keepCaret) {
		const QTextBlock block = document()->findBlockByNumber(caretBlock);
		QTextCursor caret(document());
		caret.setPosition(block.position() + std::min(caretColumn, std::max(0, block.length() - 1)));
		setTextCursor(caret);
		return true;
	}
	// Keep the whole lines selected so Tab can be pressed again.
	QTextCursor selection(document());
	selection.setPosition(first.position());
	selection.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
	setTextCursor(selection);
	return true;
}

void StudioCodeEditor::keyPressEvent(QKeyEvent* event)
{
	const bool languageSession = m_languageCompletion || m_completionTimer->isActive();
	const bool incomplete = m_completionIncomplete;
	// While completions show, the list takes the keys that choose one or
	// close it; any other key types, and narrows the list as it goes.
	if (m_completer && m_completer->popup()->isVisible()) {
		switch (event->key()) {
		case Qt::Key_Return:
		case Qt::Key_Enter:
		case Qt::Key_Escape:
		case Qt::Key_Tab:
		case Qt::Key_Backtab:
			event->ignore();
			return;
		default:
			break;
		}
	}
	if (snippetActive() && (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)
		&& !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
		navigateSnippet(event->key() == Qt::Key_Backtab || event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1); event->accept(); return;
	}
	if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) { finishSnippet(); }
	runSnippetEdit([&]() { handleKey(event); });
	const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
	if (!isReadOnly() && plain && !event->text().isEmpty() && signatureHelpTyped) { signatureHelpTyped(event->text()); }
	if (!isReadOnly() && plain && !textCursor().hasSelection() && languageCompletionTrigger && languageCompletionTrigger(event->text())) {
		queueLanguageCompletions(2, event->text());
	} else if (!isReadOnly() && plain && languageSession && (!wordBeforeCaret().isEmpty())
		&& (!event->text().isEmpty() || event->key() == Qt::Key_Backspace)) {
		queueLanguageCompletions(incomplete ? 3 : 1, {});
	} else { narrowCompletions(); }
}

void StudioCodeEditor::handleKey(QKeyEvent* event)
{
	const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
	// Tab indents, so Ctrl+Tab and Ctrl+Shift+Tab are the keyboard's way out.
	// QPlainTextEdit refuses focus changes while Tab is text, so this goes to
	// QWidget's own focus chain.
	if (event->key() == Qt::Key_Tab && modifiers == Qt::ControlModifier) {
		QWidget::focusNextPrevChild(true);
		return;
	}
	if ((event->key() == Qt::Key_Backtab || event->key() == Qt::Key_Tab) && modifiers == (Qt::ControlModifier | Qt::ShiftModifier)) {
		QWidget::focusNextPrevChild(false);
		return;
	}
	if (isReadOnly()) {
		QPlainTextEdit::keyPressEvent(event);
		return;
	}
	QTextCursor cursor = textCursor();
	if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && modifiers == Qt::NoModifier) {
		const QString line = cursor.block().text();
		// The indent the new line needs is what lies before the caret: from
		// inside the leading whitespace, the rest of it moves down with the text.
		QString indent = line.left(std::min(leadingWhitespace(line), cursor.positionInBlock()));
		if (line.left(cursor.positionInBlock()).trimmed().endsWith(QLatin1Char('{'))) {
			indent += indentUnit();
		}
		cursor.beginEditBlock();
		cursor.insertText(QStringLiteral("\n") + indent);
		cursor.endEditBlock();
		setTextCursor(cursor);
		ensureCursorVisible();
		return;
	}
	const bool multiLine = cursor.hasSelection()
		&& document()->findBlock(cursor.anchor()) != document()->findBlock(cursor.position());
	if (event->key() == Qt::Key_Tab && modifiers == Qt::NoModifier && multiLine) {
		indentLines(1);
		return;
	}
	if (event->key() == Qt::Key_Backtab) {
		indentLines(-1);
		return;
	}
	if (event->text() == QStringLiteral("}") && !cursor.hasSelection()) {
		const QString before = cursor.block().text().left(cursor.positionInBlock());
		const QString unit = indentUnit();
		if (!before.isEmpty() && before.trimmed().isEmpty() && before.endsWith(unit)) {
			cursor.beginEditBlock();
			cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, static_cast<int>(unit.size()));
			cursor.insertText(QStringLiteral("}"));
			cursor.endEditBlock();
			setTextCursor(cursor);
			return;
		}
	}
	QPlainTextEdit::keyPressEvent(event);
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
	// The bracket beside the caret and its partner, found by counting nesting
	// within a few thousand characters either way.
	const QTextDocument* text = document();
	const int position = textCursor().position();
	const QString opens = QStringLiteral("([{");
	const QString closes = QStringLiteral(")]}");
	int at = -1;
	for (const int candidate : {position, position - 1}) {
		if (candidate >= 0 && candidate < text->characterCount()) {
			const QChar ch = text->characterAt(candidate);
			if (opens.contains(ch) || closes.contains(ch)) {
				at = candidate;
				break;
			}
		}
	}
	// Every use of the name at the caret, when it has more than one, so a
	// variable's uses show at a glance. Whole words in the name's own case;
	// capped, so a common word in a long file stays cheap.
	m_highlightedUses.clear();
	if (!textCursor().hasSelection()) {
		const QTextBlock block = textCursor().block();
		const QString line = block.text();
		const auto isNameChar = [](QChar ch) {
			return ch.isLetterOrNumber() || ch == QLatin1Char('_');
		};
		qsizetype start = textCursor().positionInBlock();
		qsizetype end = start;
		while (start > 0 && isNameChar(line.at(start - 1))) {
			--start;
		}
		while (end < line.size() && isNameChar(line.at(end))) {
			++end;
		}
		const QString name = line.mid(start, end - start);
		if (name.size() >= 2 && !name.at(0).isDigit()) {
			// The lines in view and a screenful either side: scrolling works
			// them out again, and typing in a large file never scans all of it.
			constexpr int kUseLimit = 1000;
			const QRegularExpression pattern(QStringLiteral("(?<![\\w])%1(?![\\w])").arg(QRegularExpression::escape(name)),
				QRegularExpression::UseUnicodePropertiesOption);
			const int lineHeight = std::max(1, fontMetrics().height());
			const int margin = std::max(40, viewport()->height() / lineHeight);
			QTextBlock from = firstVisibleBlock();
			for (int step = 0; step < margin && from.previous().isValid(); ++step) {
				from = from.previous();
			}
			const int lastLine = firstVisibleBlock().blockNumber() + 2 * margin + viewport()->height() / lineHeight;
			QVector<QTextCursor> uses;
			for (QTextBlock scan = from; scan.isValid() && scan.blockNumber() <= lastLine && uses.size() < kUseLimit; scan = scan.next()) {
				QRegularExpressionMatchIterator matches = pattern.globalMatch(scan.text());
				while (matches.hasNext() && uses.size() < kUseLimit) {
					const QRegularExpressionMatch match = matches.next();
					QTextCursor use(document());
					use.setPosition(scan.position() + static_cast<int>(match.capturedStart()));
					use.setPosition(scan.position() + static_cast<int>(match.capturedEnd()), QTextCursor::KeepAnchor);
					uses << use;
				}
			}
			if (uses.size() > 1) {
				const StudioThemeTokens& theme = currentStudioTheme();
				for (const QTextCursor& use : std::as_const(uses)) {
					QTextEdit::ExtraSelection shade;
					shade.format.setBackground(blend(theme.colors.input, theme.colors.text, theme.light ? 0.10 : 0.14));
					// The high-visibility themes underline them as well.
					if (theme.highContrast) {
						shade.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
						shade.format.setUnderlineColor(theme.colors.text);
					}
					shade.cursor = use;
					selections.append(shade);
					m_highlightedUses << use.selectionStart();
				}
			}
		}
	}
	if (at >= 0) {
		const QChar bracket = text->characterAt(at);
		const bool forward = opens.contains(bracket);
		const QChar partner = forward ? closes.at(opens.indexOf(bracket)) : opens.at(closes.indexOf(bracket));
		constexpr int kSearchLimit = 20000;
		int depth = 0;
		int match = -1;
		for (int step = 1; step <= kSearchLimit; ++step) {
			const int index = forward ? at + step : at - step;
			if (index < 0 || index >= text->characterCount()) {
				break;
			}
			const QChar ch = text->characterAt(index);
			if (ch == bracket) {
				++depth;
			} else if (ch == partner) {
				if (depth == 0) {
					match = index;
					break;
				}
				--depth;
			}
		}
		if (match >= 0) {
			const StudioThemeTokens& theme = currentStudioTheme();
			for (const int index : {at, match}) {
				QTextEdit::ExtraSelection pair;
				pair.format.setBackground(blend(theme.colors.input, theme.colors.accent, theme.highContrast ? 0.6 : 0.35));
				pair.format.setFontWeight(QFont::Bold);
				pair.cursor = QTextCursor(document());
				pair.cursor.setPosition(index);
				pair.cursor.setPosition(index + 1, QTextCursor::KeepAnchor);
				selections.append(pair);
			}
		}
	}
	appendSnippetSelections(&selections); setExtraSelections(selections);
}

// ---------------------------------------------------------------------------
// CodeFindBar
// ---------------------------------------------------------------------------

CodeFindBar::CodeFindBar(QPlainTextEdit* editor, QWidget* parent)
	: QWidget(parent)
	, m_editor(editor)
{
	setObjectName(QStringLiteral("codeFindBar"));
	setAttribute(Qt::WA_StyledBackground, true);
	setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Find in file"));
	// Find on the first row, replace on a second one that only Ctrl+H shows.
	auto* rows = new QVBoxLayout(this);
	rows->setContentsMargins(8, 4, 6, 4);
	rows->setSpacing(4);
	auto* layout = new QHBoxLayout;
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	rows->addLayout(layout);

	m_field = new QLineEdit(this);
	m_field->setObjectName(QStringLiteral("codeFindField"));
	m_field->setPlaceholderText(QCoreApplication::translate("VibeStudioCodeEditor", "Find in file"));
	m_field->setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Find in file"));
	m_field->setAccessibleDescription(QCoreApplication::translate("VibeStudioCodeEditor", "Enter finds the next match, Shift+Enter the previous one, and Escape closes the find bar."));
	m_field->setClearButtonEnabled(true);
	m_field->installEventFilter(this);
	layout->addWidget(m_field, 1);

	m_count = new QLabel(this);
	m_count->setObjectName(QStringLiteral("fieldHint"));
	m_count->setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Find matches"));
	layout->addWidget(m_count);

	m_matchCase = new QToolButton(this);
	m_matchCase->setText(QStringLiteral("Aa"));
	m_matchCase->setCheckable(true);
	m_matchCase->setAutoRaise(true);
	m_matchCase->setFocusPolicy(Qt::TabFocus);
	m_matchCase->setToolTip(QCoreApplication::translate("VibeStudioCodeEditor", "Match case"));
	m_matchCase->setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Match case"));
	layout->addWidget(m_matchCase);

	QToolButton* previous = createToolButton(QStringLiteral("chevron-up"), QCoreApplication::translate("VibeStudioCodeEditor", "Previous Match"), QCoreApplication::translate("VibeStudioCodeEditor", "Select the previous match (Shift+Enter or Shift+F3)."), false);
	QToolButton* next = createToolButton(QStringLiteral("chevron-down"), QCoreApplication::translate("VibeStudioCodeEditor", "Next Match"), QCoreApplication::translate("VibeStudioCodeEditor", "Select the next match (Enter or F3)."), false);
	QToolButton* close = createToolButton(QStringLiteral("close"), QCoreApplication::translate("VibeStudioCodeEditor", "Close Find"), QCoreApplication::translate("VibeStudioCodeEditor", "Hide the find bar (Escape)."), false);
	for (QToolButton* button : {previous, next, close}) {
		setBaseIconSize(button, QSize(14, 14));
		layout->addWidget(button);
	}

	m_replaceRow = new QWidget(this);
	auto* replaceLayout = new QHBoxLayout(m_replaceRow);
	replaceLayout->setContentsMargins(0, 0, 0, 0);
	replaceLayout->setSpacing(4);
	m_replaceField = new QLineEdit(m_replaceRow);
	m_replaceField->setObjectName(QStringLiteral("codeReplaceField"));
	m_replaceField->setPlaceholderText(QCoreApplication::translate("VibeStudioCodeEditor", "Replace with"));
	m_replaceField->setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Replace with"));
	m_replaceField->setAccessibleDescription(QCoreApplication::translate("VibeStudioCodeEditor", "Enter replaces the selected match and moves to the next, Ctrl+Enter replaces every match, and Escape closes the bar."));
	m_replaceField->setClearButtonEnabled(true);
	m_replaceField->installEventFilter(this);
	replaceLayout->addWidget(m_replaceField, 1);
	auto* replaceOne = new QToolButton(m_replaceRow);
	replaceOne->setObjectName(QStringLiteral("codeReplaceOne"));
	replaceOne->setText(QCoreApplication::translate("VibeStudioCodeEditor", "Replace"));
	replaceOne->setToolTip(QCoreApplication::translate("VibeStudioCodeEditor", "Replace the selected match and select the next (Enter)."));
	replaceOne->setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Replace"));
	auto* replaceEvery = new QToolButton(m_replaceRow);
	replaceEvery->setObjectName(QStringLiteral("codeReplaceAll"));
	replaceEvery->setText(QCoreApplication::translate("VibeStudioCodeEditor", "Replace All"));
	replaceEvery->setToolTip(QCoreApplication::translate("VibeStudioCodeEditor", "Replace every match in the file as one undo step (Ctrl+Enter)."));
	replaceEvery->setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Replace All"));
	for (QToolButton* button : {replaceOne, replaceEvery}) {
		button->setToolButtonStyle(Qt::ToolButtonTextOnly);
		button->setAutoRaise(true);
		button->setFocusPolicy(Qt::TabFocus);
		replaceLayout->addWidget(button);
	}
	rows->addWidget(m_replaceRow);
	m_replaceRow->hide();
	connect(replaceOne, &QToolButton::clicked, this, [this]() {
		replaceCurrent();
	});
	connect(replaceEvery, &QToolButton::clicked, this, [this]() {
		replaceAll();
	});

	connect(m_field, &QLineEdit::textChanged, this, [this]() {
		// Searching as the user types starts from the current match, so adding
		// characters narrows the selection in place instead of jumping ahead.
		if (m_field->text().isEmpty()) {
			refreshCount();
			return;
		}
		find(false, true);
	});
	connect(m_matchCase, &QToolButton::toggled, this, [this]() {
		find(false, true);
	});
	connect(previous, &QToolButton::clicked, this, [this]() {
		findPrevious();
	});
	connect(next, &QToolButton::clicked, this, [this]() {
		findNext();
	});
	connect(close, &QToolButton::clicked, this, [this]() {
		hide();
		if (m_editor) {
			m_editor->setFocus(Qt::OtherFocusReason);
		}
	});
	if (m_editor) {
		// Read-only changes arrive as an event on the editor.
		m_editor->installEventFilter(this);
	}
	syncReadOnly();
	hide();
}

void CodeFindBar::syncReadOnly()
{
	const bool writable = m_editor && !m_editor->isReadOnly();
	m_replaceRow->setEnabled(writable);
	m_replaceField->setPlaceholderText(writable ? QCoreApplication::translate("VibeStudioCodeEditor", "Replace with") : QCoreApplication::translate("VibeStudioCodeEditor", "Read-only file"));
	m_replaceField->setToolTip(writable ? QString() : QCoreApplication::translate("VibeStudioCodeEditor", "This file is open read-only, so nothing in it can be replaced."));
}

void CodeFindBar::activateReplace()
{
	activate();
	syncReadOnly();
	m_replaceRow->show();
	if (!m_field->text().isEmpty()) {
		// Select the match Enter would replace: the one at or after the caret.
		find(false, true);
		if (m_replaceRow->isEnabled()) {
			m_replaceField->setFocus(Qt::ShortcutFocusReason);
			m_replaceField->selectAll();
		}
	}
}

QTextDocument::FindFlags CodeFindBar::findFlags() const
{
	QTextDocument::FindFlags flags;
	if (m_matchCase->isChecked()) {
		flags |= QTextDocument::FindCaseSensitively;
	}
	return flags;
}

bool CodeFindBar::replaceCurrent()
{
	const QString needle = m_field->text();
	if (needle.isEmpty() || !m_editor || m_editor->isReadOnly()) {
		return false;
	}
	QTextCursor cursor = m_editor->textCursor();
	const Qt::CaseSensitivity sensitivity = m_matchCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
	bool replaced = false;
	if (cursor.hasSelection() && QString::compare(cursor.selectedText(), needle, sensitivity) == 0) {
		cursor.insertText(m_replaceField->text());
		m_editor->setTextCursor(cursor);
		replaced = true;
	}
	find(false);
	return replaced;
}

int CodeFindBar::replaceAll()
{
	const QString needle = m_field->text();
	if (needle.isEmpty() || !m_editor || m_editor->isReadOnly()) {
		return 0;
	}
	const QString replacement = m_replaceField->text();
	QTextDocument* document = m_editor->document();
	QTextCursor edit(document);
	int count = 0;
	edit.beginEditBlock();
	// Each search starts after the text just put in, so a replacement that
	// contains the find text is never replaced again.
	QTextCursor match(document);
	while (true) {
		match = document->find(needle, match, findFlags());
		if (match.isNull()) {
			break;
		}
		match.insertText(replacement);
		++count;
	}
	edit.endEditBlock();
	refreshCount();
	m_count->setText(QCoreApplication::translate("VibeStudioCodeEditor", "%n replaced", nullptr, count));
	m_field->setAccessibleDescription(m_count->text());
	return count;
}

QLineEdit* CodeFindBar::replaceField() const
{
	return m_replaceField;
}

bool CodeFindBar::replaceShown() const
{
	return m_replaceRow->isVisible();
}

void CodeFindBar::activate()
{
	if (m_editor) {
		const QString selected = m_editor->textCursor().selectedText();
		// QTextCursor reports a line break inside a selection as U+2029.
		if (!selected.isEmpty() && !selected.contains(QChar(0x2029))) {
			const QSignalBlocker blocker(m_field);
			m_field->setText(selected);
		}
	}
	show();
	m_replaceRow->hide();
	m_field->setFocus(Qt::ShortcutFocusReason);
	m_field->selectAll();
	refreshCount();
}

bool CodeFindBar::findNext()
{
	return find(false);
}

bool CodeFindBar::findPrevious()
{
	return find(true);
}

QString CodeFindBar::text() const
{
	return m_field->text();
}

QLineEdit* CodeFindBar::field() const
{
	return m_field;
}

int CodeFindBar::matchCount() const
{
	return m_matchCount;
}

bool CodeFindBar::find(bool backward, bool fromSelectionStart)
{
	const QString needle = m_field->text();
	if (needle.isEmpty() || !m_editor) {
		refreshCount();
		return false;
	}
	QTextDocument::FindFlags flags;
	if (backward) {
		flags |= QTextDocument::FindBackward;
	}
	if (m_matchCase->isChecked()) {
		flags |= QTextDocument::FindCaseSensitively;
	}
	if (fromSelectionStart) {
		QTextCursor cursor = m_editor->textCursor();
		cursor.setPosition(cursor.selectionStart());
		m_editor->setTextCursor(cursor);
	}
	bool found = m_editor->find(needle, flags);
	if (!found) {
		// Wrap around and try once more from the other end of the file.
		QTextCursor cursor = m_editor->textCursor();
		cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
		m_editor->setTextCursor(cursor);
		found = m_editor->find(needle, flags);
	}
	refreshCount();
	return found;
}

void CodeFindBar::refreshCount()
{
	m_matchCount = 0;
	int current = 0;
	const QString needle = m_field->text();
	if (!needle.isEmpty() && m_editor) {
		QTextDocument::FindFlags flags;
		if (m_matchCase->isChecked()) {
			flags |= QTextDocument::FindCaseSensitively;
		}
		const QTextCursor selection = m_editor->textCursor();
		QTextCursor cursor(m_editor->document());
		while (m_matchCount < kFindCountLimit) {
			cursor = m_editor->document()->find(needle, cursor, flags);
			if (cursor.isNull()) {
				break;
			}
			++m_matchCount;
			if (selection.hasSelection() && cursor.selectionStart() == selection.selectionStart()
				&& cursor.selectionEnd() == selection.selectionEnd()) {
				current = m_matchCount;
			}
		}
	}
	if (needle.isEmpty()) {
		m_count->clear();
	} else if (m_matchCount == 0) {
		m_count->setText(QCoreApplication::translate("VibeStudioCodeEditor", "No matches"));
	} else if (m_matchCount >= kFindCountLimit) {
		m_count->setText(QCoreApplication::translate("VibeStudioCodeEditor", "%1+ matches").arg(kFindCountLimit));
	} else if (current > 0) {
		m_count->setText(QCoreApplication::translate("VibeStudioCodeEditor", "%1 of %2").arg(current).arg(m_matchCount));
	} else {
		m_count->setText(QCoreApplication::translate("VibeStudioCodeEditor", "%n match(es)", nullptr, m_matchCount));
	}
	m_field->setAccessibleDescription(m_count->text());
}

bool CodeFindBar::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_editor && event->type() == QEvent::ReadOnlyChange) {
		syncReadOnly();
		return false;
	}
	if (watched == m_replaceField && event->type() == QEvent::KeyPress) {
		const auto* key = static_cast<QKeyEvent*>(event);
		if (key->key() == Qt::Key_Escape) {
			hide();
			if (m_editor) {
				m_editor->setFocus(Qt::OtherFocusReason);
			}
			return true;
		}
		if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
			if (key->modifiers().testFlag(Qt::ControlModifier)) {
				replaceAll();
			} else {
				replaceCurrent();
			}
			return true;
		}
	}
	if (watched == m_replaceField && event->type() == QEvent::ShortcutOverride) {
		const auto* key = static_cast<QKeyEvent*>(event);
		if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
			event->accept();
			return true;
		}
	}
	if (watched == m_field && event->type() == QEvent::KeyPress) {
		const auto* key = static_cast<QKeyEvent*>(event);
		if (key->key() == Qt::Key_Escape) {
			hide();
			if (m_editor) {
				m_editor->setFocus(Qt::OtherFocusReason);
			}
			return true;
		}
		if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
			if (key->modifiers().testFlag(Qt::ShiftModifier)) {
				findPrevious();
			} else {
				findNext();
			}
			return true;
		}
	}
	// Escape belongs to the bar while its field has focus, even where a
	// surface shortcut also uses the key.
	if (watched == m_field && event->type() == QEvent::ShortcutOverride) {
		if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
			event->accept();
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

} // namespace vibestudio
