#include "app/code_editor.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QStandardItemModel>
#include <QTextCursor>
#include <QTimer>

#include <algorithm>

namespace vibestudio {
namespace {
constexpr int filterRole = Qt::UserRole + 1;
constexpr int choiceRole = Qt::UserRole + 2;
}

void StudioCodeEditor::initializeCompletion()
{
	m_completionTimer = new QTimer(this); m_completionTimer->setSingleShot(true); m_completionTimer->setInterval(120);
	const auto changed = [this]() {
		if (m_completionTimer->isActive() || (m_completionDocument && m_completionDocument != document())
			|| (m_languageCompletion && !languageCompletionCurrent(m_completionToken))) { dismissCompletions(); }
	};
	connect(this, &QPlainTextEdit::textChanged, this, changed);
	connect(this, &QPlainTextEdit::cursorPositionChanged, this, changed);
	installEventFilter(this);
}
QCompleter* StudioCodeEditor::completer() const { return m_completer; }
void StudioCodeEditor::ensureCompleter()
{
	if (m_completer) { return; }
	m_completionModel = new QStandardItemModel(this);
	m_completer = new QCompleter(m_completionModel, this);
	m_completer->setWidget(this); m_completer->setCompletionMode(QCompleter::PopupCompletion);
	m_completer->setCaseSensitivity(Qt::CaseInsensitive); m_completer->setModelSorting(QCompleter::UnsortedModel);
	m_completer->setCompletionRole(filterRole); m_completer->setWrapAround(false); m_completer->setMaxVisibleItems(12);
	m_completer->popup()->setObjectName(QStringLiteral("codeCompletions"));
	m_completer->popup()->setAccessibleName(QCoreApplication::translate("VibeStudioCodeEditor", "Completions"));
	m_completer->popup()->installEventFilter(this);
	connect(m_completer, qOverload<const QModelIndex&>(&QCompleter::activated), this, [this](const QModelIndex& index) {
		if (!index.isValid() || !index.flags().testFlag(Qt::ItemIsSelectable) || isReadOnly()) { return; }
		if (m_languageCompletion) {
			const int choice = index.data(choiceRole).toInt();
			if (!languageCompletionCurrent(m_completionToken) || choice < 0 || choice >= m_languageCompletionItems.size() || !m_acceptLanguageCompletion) { dismissCompletions(); return; }
			acceptCompletionChoice(choice);
		} else { insertCompletion(index.data(Qt::EditRole).toString()); dismissCompletions(); }
	});
	connect(m_completer, qOverload<const QModelIndex&>(&QCompleter::highlighted), this, [this](const QModelIndex& index) {
		if (m_populatingCompletions || !index.isValid()) { return; }
		const auto token = m_completionToken; const int choice = index.data(choiceRole).toInt();
		// Updating a row rebuilds QCompleter's proxy. Defer until its current
		// selection signal has returned, and only resolve the latest highlight.
		QTimer::singleShot(0, this, [this, token, choice]() {
			const auto selected = m_completer->popup()->currentIndex();
			if (languageCompletionCurrent(token) && m_completer->popup()->isVisible()
				&& selected.isValid() && selected.data(choiceRole).toInt() == choice) { resolveCompletionChoice(choice); }
		});
	});
}
void StudioCodeEditor::showCompletionPopup()
{
	m_completer->popup()->setFont(font()); m_completer->popup()->setLayoutDirection(layoutDirection());
	QRect rect = cursorRect(); rect.translate(viewportMargins().left(), viewportMargins().top());
	const int wanted = m_completer->popup()->sizeHintForColumn(0) + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 24;
	rect.setWidth(std::clamp(wanted, 200, std::max(200, width())));
	m_completer->complete(rect);
	m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
}
bool StudioCodeEditor::showCompletions()
{
	if (isReadOnly() || textCursor().hasSelection()) { return false; }
	if (languageCompletionsRequested && languageCompletionsRequested(1, {})) { return true; }
	return showLocalCompletions();
}
bool StudioCodeEditor::showLocalCompletions()
{
	dismissCompletions();
	if (isReadOnly() || !completionsFor || textCursor().hasSelection()) { return false; }
	ensureCompleter(); QScopedValueRollback guard(m_populatingCompletions, true);
	const QString prefix = wordBeforeCaret(); QStringList names = completionsFor(prefix); names.sort(Qt::CaseInsensitive);
	m_completionModel->clear();
	for (const auto& name : names) { auto* row = new QStandardItem(name); row->setData(name, filterRole); row->setData(name, Qt::EditRole); m_completionModel->appendRow(row); }
	m_completionDocument = document(); m_completer->setCompletionPrefix(prefix);
	m_completer->popup()->setAccessibleDescription(QCoreApplication::translate("VibeStudioCodeEditor", "Names from this document, the project index and language keywords."));
	if (m_completer->completionCount() == 0) { return false; }
	showCompletionPopup(); return true;
}
quint64 StudioCodeEditor::beginLanguageCompletions()
{
	dismissCompletions(); ensureCompleter();
	m_languageCompletion = true; m_completionDocument = document(); m_completionRevision = document()->revision(); m_completionPosition = textCursor().position();
	return m_completionToken;
}
bool StudioCodeEditor::languageCompletionCurrent(quint64 token) const
{
	return token == m_completionToken && m_languageCompletion && !isReadOnly() && !textCursor().hasSelection()
		&& m_completionDocument == document() && m_completionRevision == document()->revision() && m_completionPosition == textCursor().position();
}
bool StudioCodeEditor::finishLanguageCompletions(quint64 token, const LanguageCompletions& result, std::function<bool(const LanguageCompletionItem&)> accept,
	std::function<void(int, const LanguageCompletionItem&)> resolve)
{
	if (!languageCompletionCurrent(token)) { return false; }
	if (!result.error.isEmpty() || result.items.isEmpty()) { dismissCompletions(); return showLocalCompletions(); }
	{
		QScopedValueRollback guard(m_populatingCompletions, true);
		m_languageCompletionItems = result.items; m_acceptLanguageCompletion = std::move(accept); m_resolveLanguageCompletion = std::move(resolve);
		m_completionResolveErrors = QVector<QString>(result.items.size()); m_completionIncomplete = result.incomplete || result.limited;
		m_completionModel->clear(); const QString prefix = wordBeforeCaret();
		for (int i = 0; i < result.items.size(); ++i) {
			const auto& item = result.items[i]; auto* row = new QStandardItem; row->setData(i, choiceRole);
			row->setData(item.filterable ? item.filterText : prefix, filterRole); m_completionModel->appendRow(row); updateCompletionRow(i);
		}
		m_completer->setCompletionPrefix(prefix);
		m_completer->popup()->setAccessibleDescription(QCoreApplication::translate("VibeStudioCodeEditor", "Language server suggestions. Selection changes this unsaved document in one undo step. Deferred suggestions resolve before applying."));
		if (m_completer->completionCount() == 0) { dismissCompletions(); return showLocalCompletions(); }
		showCompletionPopup();
	}
	const auto selected = m_completer->popup()->currentIndex();
	if (selected.isValid()) { resolveCompletionChoice(selected.data(choiceRole).toInt()); }
	return true;
}
void StudioCodeEditor::updateCompletionRow(int choice)
{
	if (choice < 0 || choice >= m_languageCompletionItems.size()) { return; }
	auto* row = m_completionModel->item(choice); if (!row) { return; }
	QScopedValueRollback guard(m_populatingCompletions, true); const auto& item = m_languageCompletionItems[choice];
	const auto selected = m_completer->popup()->currentIndex();
	const int highlighted = m_completer->popup()->isVisible() && selected.isValid() ? selected.data(choiceRole).toInt() : -1;
	QString display = item.label;
	if (!item.detail.trimmed().isEmpty()) { display += QStringLiteral("  ·  ") + QString(item.detail).replace(QLatin1Char('\n'), QLatin1Char(' ')); }
	if (item.edits.size() > 1) { display += QCoreApplication::translate("VibeStudioCodeEditor", "  ·  includes related edits"); }
	if (std::any_of(item.tabStops.begin(), item.tabStops.end(), [](const auto& stop) { return stop.number > 0; })) { display += QCoreApplication::translate("VibeStudioCodeEditor", "  ·  editable fields"); }
	if (item.deprecated) { display += QCoreApplication::translate("VibeStudioCodeEditor", "  ·  deprecated"); }
	if (!m_completionResolveErrors.value(choice).isEmpty()) { display += QCoreApplication::translate("VibeStudioCodeEditor", "  ·  unavailable"); }
	else if (m_resolvingCompletion == choice) { display += QCoreApplication::translate("VibeStudioCodeEditor", "  ·  resolving…"); }
	else if (item.needsResolve) { display += QCoreApplication::translate("VibeStudioCodeEditor", "  ·  details on selection"); }
	row->setText(display);
	const QString details = item.label + QLatin1Char('\n') + item.detail + QLatin1Char('\n') + item.documentation + QLatin1Char('\n') + m_completionResolveErrors.value(choice);
	row->setToolTip(QStringLiteral("<pre>") + details.toHtmlEscaped() + QStringLiteral("</pre>"));
	row->setData(display + QLatin1Char('\n') + item.documentation + QLatin1Char('\n') + m_completionResolveErrors.value(choice), Qt::AccessibleDescriptionRole);
	QFont labelFont = font(); labelFont.setStrikeOut(item.deprecated); row->setFont(labelFont);
	// Metadata changes reset the filtered proxy even though filterText is
	// unchanged. Restore by item identity, not a now-invalid proxy index.
	if (highlighted >= 0) {
		const auto* model = m_completer->completionModel();
		for (int at = 0; at < model->rowCount(); ++at) {
			const auto index = model->index(at, 0);
			if (index.data(choiceRole).toInt() == highlighted) { m_completer->popup()->setCurrentIndex(index); break; }
		}
	}
}
void StudioCodeEditor::resolveCompletionChoice(int choice)
{
	if (!languageCompletionCurrent(m_completionToken) || choice < 0 || choice >= m_languageCompletionItems.size() || m_resolvingCompletion == choice) { return; }
	const int previous = m_resolvingCompletion;
	if (previous >= 0 && languageCompletionCancelled) { languageCompletionCancelled(); }
	m_resolvingCompletion = -1; m_completionAcceptPending = false;
	if (previous >= 0) { updateCompletionRow(previous); }
	if (!m_languageCompletionItems[choice].needsResolve || !m_resolveLanguageCompletion || !m_completionResolveErrors[choice].isEmpty()) { return; }
	m_resolvingCompletion = choice; updateCompletionRow(choice);
	m_resolveLanguageCompletion(choice, m_languageCompletionItems[choice]);
}
void StudioCodeEditor::acceptCompletionChoice(int choice)
{
	if (!languageCompletionCurrent(m_completionToken) || choice < 0 || choice >= m_languageCompletionItems.size() || !m_acceptLanguageCompletion || !m_completionResolveErrors[choice].isEmpty()) { return; }
	if (m_languageCompletionItems[choice].needsResolve) {
		resolveCompletionChoice(choice); m_completionAcceptPending = m_resolvingCompletion == choice;
		if (!m_completionAcceptPending && !m_languageCompletionItems[choice].needsResolve) { acceptCompletionChoice(choice); } return;
	}
	const auto item = m_languageCompletionItems[choice]; const auto accept = m_acceptLanguageCompletion;
	dismissCompletions(); accept(item);
}
bool StudioCodeEditor::finishLanguageCompletionResolve(quint64 token, int choice, const LanguageCompletionItem& item, const QString& error)
{
	if (!languageCompletionCurrent(token) || choice < 0 || choice >= m_languageCompletionItems.size() || m_resolvingCompletion != choice) { return false; }
	const bool apply = m_completionAcceptPending; m_completionAcceptPending = false; m_resolvingCompletion = -1;
	m_completionResolveErrors[choice] = error;
	if (error.isEmpty()) { m_languageCompletionItems[choice] = item; }
	updateCompletionRow(choice);
	if (apply && error.isEmpty()) { acceptCompletionChoice(choice); }
	else if (apply) { dismissCompletions(); }
	return true;
}

void StudioCodeEditor::dismissCompletions()
{
	const bool active = m_languageCompletion;
	m_languageCompletion = false; ++m_completionToken; m_completionIncomplete = false;
	m_completionDocument.clear();
	m_languageCompletionItems.clear(); m_acceptLanguageCompletion = {}; m_resolveLanguageCompletion = {};
	m_completionResolveErrors.clear(); m_resolvingCompletion = -1; m_completionAcceptPending = false;
	if (m_completionTimer) { m_completionTimer->stop(); }
	if (m_completer) { m_completer->popup()->hide(); }
	if (active && languageCompletionCancelled) { languageCompletionCancelled(); }
}
void StudioCodeEditor::queueLanguageCompletions(int kind, const QString& trigger)
{
	if (!languageCompletionsRequested) { return; }
	dismissCompletions();
	m_completionIncomplete = kind == 3;
	disconnect(m_completionTimer, &QTimer::timeout, this, nullptr);
	connect(m_completionTimer, &QTimer::timeout, this, [this, kind, trigger]() { if (!isReadOnly() && languageCompletionsRequested) { languageCompletionsRequested(kind, trigger); } });
	m_completionTimer->start();
}
void StudioCodeEditor::dismissLanguageCompletions()
{
	if (m_languageCompletion || m_completionTimer->isActive()) { dismissCompletions(); }
}
void StudioCodeEditor::narrowCompletions()
{
	if (!m_completer || m_languageCompletion || !m_completer->popup()->isVisible()) { return; }
	const QString prefix = wordBeforeCaret(); m_completer->setCompletionPrefix(prefix);
	if (prefix.isEmpty() || m_completer->completionCount() == 0) { dismissCompletions(); return; }
	m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
}
bool StudioCodeEditor::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == this && event->type() == QEvent::Hide) { finishSnippet(); }
	if (watched == this && event->type() == QEvent::Hide && signatureHelpDismissed) { signatureHelpDismissed(); }
	if (watched == this && (event->type() == QEvent::Hide || event->type() == QEvent::FocusOut || event->type() == QEvent::KeyPress)) { dismissQuickInfoHint(); }
	if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape
		&& (m_languageCompletion || m_completionTimer->isActive() || (m_completer && m_completer->popup()->isVisible()))) { dismissCompletions(); return true; }
	if (watched == this && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape
		&& finishSnippet()) { return true; }
	if (watched == this && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape
		&& signatureHelpDismissed && signatureHelpDismissed()) { return true; }
	if (watched == this && (event->type() == QEvent::Hide || (event->type() == QEvent::FocusOut && static_cast<QFocusEvent*>(event)->reason() != Qt::PopupFocusReason))) { dismissCompletions(); }
	if (m_completer && watched == m_completer->popup() && event->type() == QEvent::Hide && m_languageCompletion && !m_populatingCompletions) {
		const auto token = m_completionToken;
		QTimer::singleShot(0, this, [this, token]() { if (token == m_completionToken && !m_completionAcceptPending && !m_completer->popup()->isVisible()) { dismissCompletions(); } });
	}
	return QPlainTextEdit::eventFilter(watched, event);
}

} // namespace vibestudio
