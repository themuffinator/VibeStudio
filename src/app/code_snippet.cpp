#include "app/code_editor.h"
#include "app/studio_theme.h"
#include "core/text_document.h"

#include <QCoreApplication>
#include <QMimeData>
#include <QScopedValueRollback>
#include <algorithm>

namespace vibestudio {
namespace {
QString documentText(const QTextDocument* document) { return document->toRawText().replace(QChar(0x2029), QLatin1Char('\n')); }
}

StudioCodeEditor::~StudioCodeEditor() { snippetChanged = {}; finishSnippet(); }
void StudioCodeEditor::setDocument(QTextDocument* value) { finishSnippet(); QPlainTextEdit::setDocument(value); }
void StudioCodeEditor::setReadOnly(bool value) { if (value) { finishSnippet(); } QPlainTextEdit::setReadOnly(value); }
void StudioCodeEditor::initializeSnippet()
{
	connect(this, &QPlainTextEdit::cursorPositionChanged, this, &StudioCodeEditor::snippetContextChanged);
	connect(this, &QPlainTextEdit::textChanged, this, [this]() { if (m_snippetDocument && document() != m_snippetDocument) { finishSnippet(); } });
}
bool StudioCodeEditor::snippetActive() const { return m_snippetStop >= 0 && m_snippetDocument && m_snippetDocument == document() && !isReadOnly(); }
QStringList StudioCodeEditor::snippetChoices() const { return snippetActive() ? m_snippetStops[m_snippetStop].choices : QStringList {}; }
QString StudioCodeEditor::snippetStatus() const
{
	if (!snippetActive()) { return m_snippetNotice; }
	QVector<int> numbers;
	for (int i = 0; i < m_snippetStops.size(); ++i) { const int number = m_snippetStops[i].number; if (!m_snippetRetired[i] && number > 0 && !numbers.contains(number)) { numbers << number; } }
	std::sort(numbers.begin(), numbers.end());
	return QCoreApplication::translate("VibeStudioCodeEditor", "Snippet field %1 of %2")
		.arg(numbers.indexOf(m_snippetStops[m_snippetStop].number) + 1).arg(numbers.size());
}
void StudioCodeEditor::publishSnippetState()
{
	if (snippetActive()) { setAccessibleDescription(snippetStatus() + QStringLiteral(". ") + QCoreApplication::translate("VibeStudioCodeEditor", "Tab moves to the next field, Shift+Tab to the previous field. Escape finishes snippet editing.")); }
	scheduleHighlight(); if (snippetChanged) { snippetChanged(); }
}
bool StudioCodeEditor::finishSnippet(bool moveToFinal)
{
	if (m_snippetStop < 0) { return false; }
	const bool same = m_snippetDocument == document(); const auto final = m_snippetFinal;
	m_snippetStop = -1; disconnect(m_snippetConnection); m_snippetDocument.clear(); m_snippetStops.clear(); m_snippetRetired.clear(); m_snippetFinal = {}; m_snippetNotice.clear();
	setAccessibleDescription(m_snippetDescription); m_snippetDescription.clear();
	if (moveToFinal && same && !final.isNull()) { setTextCursor(final); ensureCursorVisible(); }
	publishSnippetState(); return true;
}
bool StudioCodeEditor::applyLanguageCompletion(const LanguageCompletionItem& item, QString* error)
{
	if (isReadOnly()) { return false; }
	QString preview; int caret = 0;
	if (!previewLanguageCompletion(documentText(document()), item, &preview, &caret, error)) { return false; }
	if (snippetActive() && !item.snippet && item.edits.size() == 1) {
		const auto& change = item.edits.front(); const auto& active = m_snippetStops[m_snippetStop];
		if (change.offset >= active.offset && change.offset + change.length <= active.offset + active.length) {
			runSnippetEdit([&]() { QTextCursor edit(document()); edit.setPosition(change.offset); edit.setPosition(change.offset + change.length, QTextCursor::KeepAnchor); edit.insertText(change.text); setTextCursor(edit); }); return true;
		}
	}
	finishSnippet(); m_snippetNotice.clear(); QTextCursor edit(document()); edit.beginEditBlock();
	for (const auto& change : item.edits) { edit.setPosition(change.offset); edit.setPosition(change.offset + change.length, QTextCursor::KeepAnchor); edit.insertText(change.text); }
	edit.endEditBlock(); edit.setPosition(caret); setTextCursor(edit); ensureCursorVisible();
	for (const auto& stop : item.tabStops) {
		if (stop.number == 0) { edit.setPosition(stop.offset); edit.setPosition(stop.offset + stop.length, QTextCursor::KeepAnchor); break; }
	}
	int first = -1;
	for (int i = 0; i < item.tabStops.size(); ++i) { if (item.tabStops[i].number > 0 && (first < 0 || item.tabStops[i].number < item.tabStops[first].number)) { first = i; } }
	if (first >= 0) {
		m_snippetDocument = document(); m_snippetStops = item.tabStops; m_snippetRetired.fill(false, item.tabStops.size());
		m_snippetFinal = edit; m_snippetDescription = accessibleDescription();
		m_snippetUtf8Bytes = preview.toUtf8().size();
		m_snippetConnection = connect(document(), &QTextDocument::contentsChange, this, &StudioCodeEditor::snippetDocumentChanged);
		selectSnippetStop(first);
	}
	else { setTextCursor(edit); }
	return true;
}
void StudioCodeEditor::selectSnippetStop(int index)
{
	QScopedValueRollback guard(m_snippetEditing, true); m_snippetStop = index;
	const auto& stop = m_snippetStops[index]; QTextCursor cursor(document()); cursor.setPosition(stop.offset); cursor.setPosition(stop.offset + stop.length, QTextCursor::KeepAnchor);
	setTextCursor(cursor); ensureCursorVisible(); publishSnippetState();
}
bool StudioCodeEditor::navigateSnippet(int direction)
{
	if (!snippetActive()) { finishSnippet(); return false; }
	const int number = m_snippetStops[m_snippetStop].number; int next = -1;
	for (int i = 0; i < m_snippetStops.size(); ++i) {
		const int candidate = m_snippetStops[i].number;
		if (m_snippetRetired[i] || candidate == 0 || (direction > 0 ? candidate <= number : candidate >= number)) { continue; }
		if (next < 0 || (direction > 0 ? candidate < m_snippetStops[next].number : candidate > m_snippetStops[next].number)) { next = i; }
	}
	if (next >= 0) { selectSnippetStop(next); }
	else if (direction > 0) { finishSnippet(true); }
	return true;
}
void StudioCodeEditor::snippetContextChanged()
{
	if (m_snippetStop < 0 || m_snippetEditing) { return; }
	if (!snippetActive()) { finishSnippet(); return; }
	const auto cursor = textCursor(); const auto& active = m_snippetStops[m_snippetStop];
	if (cursor.selectionStart() >= active.offset && cursor.selectionEnd() <= active.offset + active.length) { return; }
	// Moving deliberately to another retained field keeps navigation useful.
	for (int i = m_snippetStops.size() - 1; i >= 0; --i) {
		const auto& stop = m_snippetStops[i];
		if (!m_snippetRetired[i] && stop.number > 0 && cursor.selectionStart() >= stop.offset && cursor.selectionEnd() <= stop.offset + stop.length) { m_snippetStop = i; publishSnippetState(); return; }
	}
	finishSnippet();
}
bool StudioCodeEditor::adjustSnippetRanges(int target, int position, int removed, int added)
{
	if (target < 0 || target >= m_snippetStops.size()) { return false; }
	const auto active = m_snippetStops[target]; const int end = position + removed, delta = added - removed;
	if (position < active.offset || removed < 0 || end > active.offset + active.length) { return false; }
	for (int i = 0; i < m_snippetStops.size(); ++i) {
		if (m_snippetRetired[i]) { continue; }
		auto& stop = m_snippetStops[i];
		if (i == target) { stop.length += delta; continue; }
		bool child = false; for (int p = stop.parent; p >= 0; p = m_snippetStops[p].parent) { if (p == target) { child = true; break; } }
		if (child) { m_snippetRetired[i] = true; continue; }
		bool ancestor = false; for (int p = active.parent; p >= 0; p = m_snippetStops[p].parent) { if (p == i) { ancestor = true; break; } }
		if (ancestor) { stop.length += delta; continue; }
		if (stop.offset > end || (stop.offset == end && (removed > 0 || i > target))) { stop.offset += delta; }
		else if (stop.offset < end && position < stop.offset + stop.length) { return false; }
	}
	return true;
}
void StudioCodeEditor::snippetDocumentChanged(int position, int removed, int added)
{
	if (m_snippetStop < 0 || m_snippetMirroring || (removed == 0 && added == 0)) { return; }
	if (!m_snippetEditing || !adjustSnippetRanges(m_snippetStop, position, removed, added)) { finishSnippet(); return; }
	m_snippetMoved = true;
}
void StudioCodeEditor::runSnippetEdit(const std::function<void()>& operation)
{
	if (m_snippetEditing || !snippetActive()) { operation(); return; }
	snippetContextChanged(); if (!snippetActive()) { operation(); return; }
	const auto before = m_snippetStops[m_snippetStop]; QTextCursor oldValue(document()); oldValue.setPosition(before.offset); oldValue.setPosition(before.offset + before.length, QTextCursor::KeepAnchor);
	const qint64 oldBytes = oldValue.selectedText().replace(QChar(0x2029), QLatin1Char('\n')).toUtf8().size();
	QScopedValueRollback guard(m_snippetEditing, true); m_snippetMoved = false;
	QTextCursor block(document()); block.beginEditBlock(); operation(); block.endEditBlock();
	if (!snippetActive() || !m_snippetMoved) { m_snippetEditing = false; snippetContextChanged(); return; }
	const auto active = m_snippetStops[m_snippetStop]; QTextCursor value(document()); value.setPosition(active.offset); value.setPosition(active.offset + active.length, QTextCursor::KeepAnchor);
	const QString text = value.selectedText().replace(QChar(0x2029), QLatin1Char('\n')); auto caret = textCursor();
	QVector<int> mirrors;
	for (int i = 0; i < m_snippetStops.size(); ++i) { if (i != m_snippetStop && !m_snippetRetired[i] && m_snippetStops[i].number == active.number) { mirrors << i; } }
	const qint64 bytes = text.toUtf8().size(), projectedBytes = m_snippetUtf8Bytes + (bytes - oldBytes) * (mirrors.size() + 1);
	if (text.size() > 65536 || projectedBytes > textDocumentByteLimit) {
		finishSnippet(); m_snippetNotice = QCoreApplication::translate("VibeStudioCodeEditor", "Snippet linking ended because the edit exceeds its size limit. Your typed text was kept."); publishSnippetState(); return;
	}
	m_snippetUtf8Bytes = projectedBytes;
	std::sort(mirrors.begin(), mirrors.end(), [this](int a, int b) { return m_snippetStops[a].offset > m_snippetStops[b].offset; });
	if (!mirrors.isEmpty()) {
		QScopedValueRollback mirroring(m_snippetMirroring, true); block.joinPreviousEditBlock();
		for (const int index : mirrors) {
			const auto stop = m_snippetStops[index];
			if (!adjustSnippetRanges(index, stop.offset, stop.length, text.size())) { finishSnippet(); break; }
			QTextCursor edit(document()); edit.setPosition(stop.offset); edit.setPosition(stop.offset + stop.length, QTextCursor::KeepAnchor); edit.insertText(text);
		}
		block.endEditBlock();
	}
	setTextCursor(caret); publishSnippetState();
}
void StudioCodeEditor::replaceSnippetSelection(const QString& text) { if (!isReadOnly()) { runSnippetEdit([&]() { QPlainTextEdit::insertPlainText(text); }); } }
void StudioCodeEditor::insertFromMimeData(const QMimeData* source) { runSnippetEdit([&]() { QPlainTextEdit::insertFromMimeData(source); }); }
bool StudioCodeEditor::chooseSnippetValue(int index)
{
	if (!snippetActive()) { return false; }
	const auto choices = snippetChoices(); if (index < 0 || index >= choices.size()) { return false; }
	selectSnippetStop(m_snippetStop); replaceSnippetSelection(choices[index]); if (snippetActive()) { selectSnippetStop(m_snippetStop); } return true;
}
void StudioCodeEditor::appendSnippetSelections(QList<QTextEdit::ExtraSelection>* selections)
{
	if (!snippetActive()) { return; }
	for (int i = 0; i < m_snippetStops.size(); ++i) {
		if (m_snippetRetired[i] || m_snippetStops[i].number == 0) { continue; }
		const auto& stop = m_snippetStops[i]; QTextEdit::ExtraSelection mark;
		mark.cursor = QTextCursor(document()); mark.cursor.setPosition(stop.offset); mark.cursor.setPosition(stop.offset + stop.length, QTextCursor::KeepAnchor);
		mark.format.setUnderlineStyle(stop.number == m_snippetStops[m_snippetStop].number ? QTextCharFormat::SingleUnderline : QTextCharFormat::DashUnderline);
		mark.format.setUnderlineColor(currentStudioTheme().colors.focus); selections->append(mark);
	}
}

} // namespace vibestudio
