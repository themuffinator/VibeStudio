#include "app/application_shell.h"

#include "app/code_editor.h"
#include "app/project_search_panel.h"
#include "core/language_completion.h"

#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QScrollBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace vibestudio {
namespace {

QString searchPathIdentity(const QString& path)
{
	if (path.isEmpty()) { return {}; }
	const QFileInfo info(path);
	QString identity = info.canonicalFilePath();
	if (identity.isEmpty()) { identity = info.absoluteFilePath(); }
	identity = QDir::cleanPath(identity);
#ifdef Q_OS_WIN
	identity = identity.toCaseFolded();
#endif
	return identity;
}

QString searchDocumentText(const QTextDocument* document)
{
	return document->toRawText().replace(QChar(0x2029), QLatin1Char('\n'));
}

} // namespace

void ApplicationShell::configureCodeProjectSearch()
{
	m_codeSearchResults = m_codeSearchPanel->resultsList();
	m_codeSearchPanel->captureBuffers = [this]() {
		QVector<AssetTextBuffer> buffers;
		const QString root = m_settings.currentProjectPath();
		if (root.isEmpty()) { return buffers; }
		qint64 snapshotBytes = 0;
		for (const auto& tab : m_codeTabs) {
			if (tab.path.isEmpty() || tab.packageCopy || !tab.document) { continue; }
			const QString relative = QDir(root).relativeFilePath(tab.path);
			if (QDir::isAbsolutePath(relative) || relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) { continue; }
			AssetTextBuffer buffer;
			buffer.filePath = tab.path;
			buffer.id = tab.searchId;
			buffer.revision = tab.document->revision();
			buffer.encoding = textEncodingName(tab.source.encoding);
			const qint64 bytes = qint64(tab.document->characterCount() - 1) * sizeof(QChar);
			if (!tab.source.editable()) {
				buffer.error = tr("This open document is read-only or incomplete.");
			} else if (bytes > textDocumentByteLimit * 2 || bytes > 64ll * 1024 * 1024 - snapshotBytes) {
				buffer.error = tr("The open document exceeds the search snapshot limit.");
			} else {
				snapshotBytes += bytes;
				buffer.text = searchDocumentText(tab.document);
			}
			// An unavailable snapshot suppresses disk fallback, so the preview
			// cannot silently replace text different from the open document.
			buffers << std::move(buffer);
		}
		return buffers;
	};

	m_codeSearchPanel->openMatch = [this](const AssetTextMatch& match) {
		const int index = codeTabIndexOf(match.filePath);
		if (!match.bufferId.isEmpty() && (index < 0 || m_codeTabs[index].searchId != match.bufferId
			|| m_codeTabs[index].document->revision() != match.bufferRevision)) {
			statusBar()->showMessage(tr("The searched document changed or closed. Search again to refresh this location."));
			return;
		}
		if (currentMode() == StudioMode::Code) { rememberPlace(); }
		if (!openCodeFile(match.filePath)) { return; }
		setMode(StudioMode::Code);
		if (assetTextSnapshotHash(searchDocumentText(m_codeEditor->document())) != match.textSha256) {
			statusBar()->showMessage(tr("The searched text changed. Search again to refresh this location."));
			return;
		}
		moveCodeCaretTo(match.line, match.column);
		if (match.endLine > 0 && match.endColumn > 0) {
			const auto text = searchDocumentText(m_codeEditor->document());
			const int start = languageSourceOffset(text, match.line - 1, match.column - 1), end = languageSourceOffset(text, match.endLine - 1, match.endColumn - 1);
			if (start >= 0 && end >= start) { QTextCursor cursor(m_codeEditor->document()); cursor.setPosition(end); cursor.setPosition(start, QTextCursor::KeepAnchor); m_codeEditor->setTextCursor(cursor); m_codeEditor->ensureCursorVisible(); }
		}
	};

	const auto validate = [this](const AssetTextSearchReport& report) -> QString {
		if (searchPathIdentity(m_settings.currentProjectPath()) != searchPathIdentity(report.rootPath)) {
			return tr("The active project changed. Search the current project before replacing.");
		}
		if (m_codeSaveRunning) { return tr("Wait for the document save to finish, then search again before replacing."); }
		for (const auto& change : report.changes) {
			const int index = codeTabIndexOf(change.filePath);
			if (!change.bufferId.isEmpty()) {
				if (index < 0 || m_codeTabs[index].searchId != change.bufferId || m_codeTabs[index].packageCopy
					|| !m_codeTabs[index].source.editable() || m_codeTabs[index].document->revision() != change.bufferRevision
					|| assetTextSnapshotHash(searchDocumentText(m_codeTabs[index].document)) != change.textSha256) {
					return tr("The open document changed or closed after preview: %1. Search again before replacing.").arg(QDir::toNativeSeparators(change.filePath));
				}
			} else if (index >= 0) {
				return tr("This file was opened after preview: %1. Search again to replace its open document.").arg(QDir::toNativeSeparators(change.filePath));
			}
			if (levelMapHasUnsavedEdits() && searchPathIdentity(m_levelMapDocument.sourcePath) == searchPathIdentity(change.filePath)) {
				return tr("Save or close the modified map in Levels before replacing: %1").arg(QDir::toNativeSeparators(change.filePath));
			}
		}
		return {};
	};
	m_codeSearchPanel->beforeApply = validate;
	m_codeSearchPanel->applyBuffers = [this, validate](AssetTextSearchReport& report) {
		const QString error = validate(report);
		report.bufferEditsPending = false;
		if (!error.isEmpty()) {
			report.saveState = QStringLiteral("failed");
			report.warnings << error;
			return;
		}
		// No event processing between validation and editing. The modal progress
		// window stays open until every reviewed snapshot has been applied.
		storeCodeTabView(m_codeTab);
		const QTextCursor activeCursor = m_codeEditor->textCursor();
		const int verticalScroll = m_codeEditor->verticalScrollBar()->value();
		const int horizontalScroll = m_codeEditor->horizontalScrollBar()->value();
		for (const auto& change : report.changes) {
			if (change.bufferId.isEmpty()) { continue; }
			CodeTab& tab = m_codeTabs[codeTabIndexOf(change.filePath)];
			QTextCursor view(tab.document);
			view.setPosition(std::clamp(tab.anchor, 0, tab.document->characterCount() - 1));
			view.setPosition(std::clamp(tab.position, 0, tab.document->characterCount() - 1), QTextCursor::KeepAnchor);
			QTextCursor edit(tab.document);
			edit.beginEditBlock();
			for (auto range = change.edits.crbegin(); range != change.edits.crend(); ++range) {
				edit.setPosition(int(range->offset));
				edit.setPosition(int(range->offset + range->length), QTextCursor::KeepAnchor);
				edit.insertText(range->replacement);
			}
			edit.endEditBlock();
			tab.anchor = view.anchor();
			tab.position = view.position();
			report.editedBuffers << change.filePath;
			report.replacementsApplied += change.replacementCount;
		}
		m_codeEditor->setTextCursor(activeCursor);
		m_codeEditor->verticalScrollBar()->setValue(verticalScroll);
		m_codeEditor->horizontalScrollBar()->setValue(horizontalScroll);
		report.saveState = report.writtenFiles.isEmpty() ? QStringLiteral("modified") : QStringLiteral("saved-and-modified");
		invalidateDefinitionIndex();
		refreshCodeTabLabels();
		refreshCodeFileStatus();
		checkpointCodeDocuments();
	};

	m_codeSearchPanel->operationStarted = [this](bool applying) {
		m_codeProjectReplaceRunning = applying;
		if (m_codeOutputTabs) { m_codeOutputTabs->setCurrentWidget(m_codeSearchPanel); }
		m_codeSearchActivityId = m_activity.createTask(!m_codeSearchPanel->report().codeActionTitle.isEmpty() ? tr("Code Action") : m_codeSearchPanel->report().semanticRename ? tr("Rename Symbol") : m_codeSearchPanel->referencesActive() ? tr("Find References") : applying ? tr("Project Replace") : tr("Project Find"),
			m_codeFind->text(), QStringLiteral("code"), OperationState::Running, true);
		refreshActivityCenter(m_codeSearchActivityId);
	};
	m_codeSearchPanel->operationFinished = [this](const AssetTextSearchReport& report) {
		for (const auto& path : report.writtenFiles) {
			invalidateDefinitionIndex();
			const int tab = codeTabIndexOf(path);
			if (tab >= 0 && !m_codeTabs.at(tab).document->isModified()) { reloadCodeTab(tab); }
			if (searchPathIdentity(m_levelMapDocument.sourcePath) == searchPathIdentity(path) && !levelMapHasUnsavedEdits()) { loadLevelMapPath(path); }
		}
		m_codeProjectReplaceRunning = false;
		if (!report.writtenFiles.isEmpty()) { refreshCodeWorkspaceTree(true); }
		// Full match text and provenance remain in Search Results.
		const QString summary = tr("Root: %1\nMatches: %2\nFiles with matches: %3\nReplacements applied: %4\nState: %5")
			.arg(report.rootPath).arg(report.matchCount).arg(report.filesWithMatches).arg(report.replacementsApplied).arg(report.saveState)
			+ (report.writtenFiles.isEmpty() ? QString() : tr("\nSaved files:\n%1").arg(report.writtenFiles.join(QLatin1Char('\n'))))
			+ (report.editedBuffers.isEmpty() ? QString() : tr("\nEdited, not saved:\n%1").arg(report.editedBuffers.join(QLatin1Char('\n'))));
		for (const auto& warning : report.warnings) { m_activity.appendWarning(m_codeSearchActivityId, warning); }
		if (report.cancelled) { m_activity.cancelTask(m_codeSearchActivityId, summary); }
		else if (!report.succeeded()) { m_activity.failTask(m_codeSearchActivityId, summary); }
		else { m_activity.completeTask(m_codeSearchActivityId, summary); }
		persistActivityTask(m_codeSearchActivityId);
		refreshActivityCenter(m_codeSearchActivityId);
		refreshCommandEnablement();
	};
}

} // namespace vibestudio
