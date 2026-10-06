#pragma once

// Plain-text code editor with a line-number gutter and a current-line band.
//
// The gutter and band read the active studio theme when they paint, so they
// follow theme switches without being told. Everything else, including
// syntax highlighting and diagnostics, stays with the shell.

#include <QFont>
#include <QHash>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRect>
#include <QTextDocument>
#include <QVector>
#include <QWidget>
#include "core/language_completion.h"

#include <functional>

class QCompleter;
class QLabel;
class QLineEdit;
class QStandardItemModel;
class QTimer;
class QToolButton;

namespace vibestudio {

class StudioCodeEditor final : public QPlainTextEdit {
public:
	explicit StudioCodeEditor(QWidget* parent = nullptr);
	~StudioCodeEditor() override;
	void setDocument(QTextDocument* document);
	void setReadOnly(bool readOnly);
	// Shared validated insertion; tab stops track only this unsaved document.
	bool applyLanguageCompletion(const LanguageCompletionItem& item, QString* error = nullptr);
	bool snippetActive() const;
	bool navigateSnippet(int direction);
	bool finishSnippet(bool moveToFinal = false);
	bool chooseSnippetValue(int index);
	void replaceSnippetSelection(const QString& text);
	QString snippetStatus() const;
	QStringList snippetChoices() const;
	std::function<void()> snippetChanged;

	[[nodiscard]] int lineNumberAreaWidth() const;
	void paintLineNumbers(QPaintEvent* event);

	// Line operations over the selected lines, or the caret's line. Each is
	// one undo step and does nothing in a read-only editor.
	// Comments the lines with `token` at their shared indentation, or removes
	// it when every non-blank line already starts with it.
	bool toggleLineComment(const QString& token);
	// Copies the lines below themselves and moves the selection onto the copy,
	// so repeating it keeps duplicating downward.
	bool duplicateLines();
	// Swaps the lines with the line above (direction < 0) or below.
	bool moveLines(int direction);
	// Indents the selected lines by one unit, or outdents them (direction < 0)
	// by up to one unit, as one undo step, keeping the lines selected.
	bool indentLines(int direction);
	// The file's indentation unit: a tab, or as many spaces as its first
	// space-indented line; a tab when nothing is indented yet.
	[[nodiscard]] QString indentUnit() const;

	// Zoom scales the studio's monospace font for this editor alone, in the
	// steps a browser uses (50% to 300%); the gutter and tab stops follow.
	// Ctrl with the mouse wheel zooms too. The shell keeps the level.
	void setBaseFont(const QFont& font);
	[[nodiscard]] int zoomPercent() const;
	// Clamped to 50..300; returns the level now in effect.
	int setZoomPercent(int percent);
	// The next step up or down; false when already at that end.
	bool zoomInStep();
	bool zoomOutStep();
	// Called after each change of zoom with the new level.
	std::function<void(int)> zoomChanged;
	// Ctrl+click on a name asks for its definition; the shell answers.
	std::function<void()> definitionRequested;
	// A normal tooltip dwell requests information without moving the caret.
	// The shell also provides an explicit keyboard command and documentation pane.
	std::function<bool(int offset, const QPoint& globalPosition)> quickInfoRequested;
	std::function<void()> quickInfoDismissed;
	bool requestQuickInfoAt(const QPoint& viewportPosition, const QPoint& globalPosition);
	void dismissQuickInfoHint();

	// Requests semantic suggestions when a connected provider accepts the request,
	// otherwise uses `completionsFor` (file/project names and language keywords).
	// Typing updates the list, Enter or Tab accepts an item, and Escape cancels.
	// False when no request started and no local name matches the typed prefix.
	bool showCompletions();
	std::function<QStringList(const QString& prefix)> completionsFor;
	[[nodiscard]] QCompleter* completer() const;
	bool showLocalCompletions();
	quint64 beginLanguageCompletions();
	bool languageCompletionCurrent(quint64 token) const;
	bool finishLanguageCompletions(quint64 token, const LanguageCompletions& result, std::function<bool(const LanguageCompletionItem&)> accept,
		std::function<void(int, const LanguageCompletionItem&)> resolve = {});
	bool finishLanguageCompletionResolve(quint64 token, int choice, const LanguageCompletionItem& item, const QString& error);
	void dismissCompletions();
	void dismissLanguageCompletions();
	std::function<bool(int triggerKind, const QString& trigger)> languageCompletionsRequested;
	std::function<bool(const QString&)> languageCompletionTrigger;
	std::function<void()> languageCompletionCancelled;
	// Parameter hints use the normal editing path and never change source text.
	std::function<void(const QString&)> signatureHelpTyped;
	std::function<bool()> signatureHelpDismissed;

	// Folding hides the lines inside a brace pair that spans three lines or
	// more, keeping its opening and closing lines in view. Braces in comments
	// and quoted strings do not count. A fold stays with its opening line
	// through edits elsewhere, opens again once its braces stop pairing, and
	// opens when the caret moves into it. Each line that can fold has a
	// chevron in the gutter, and a folded line ends in a badge counting the
	// lines it hides; a click on either unfolds it.
	// Folds the innermost pair holding the caret that is not folded yet;
	// false when there is none.
	bool foldAtCaret();
	// Unfolds the fold on the caret's line; false when there is none.
	bool unfoldAtCaret();
	// Folds or unfolds the pair opening on `line` (1-based), as a click on
	// its chevron does; false when no pair opens there.
	bool toggleFold(int line);
	// Folds or unfolds every pair in the file; each returns how many changed.
	int foldAll();
	int unfoldAll();
	// The lines (1-based) whose pair is folded, first to last.
	[[nodiscard]] QVector<int> foldedLines() const;
	// The line (1-based) whose chevron is at `position` in the gutter, or 0.
	// Beside the sticky strip there is none: the lines there are pinned ones.
	[[nodiscard]] int foldMarkerLineAt(const QPoint& position) const;
	// The height of the sticky strip at the top of the view, 0 when hidden.
	[[nodiscard]] int stickyHeaderHeight() const;
	// What a click on the chevron of `line` (1-based) would do.
	[[nodiscard]] QString foldMarkerToolTip(int line) const;
	// Called when a fold closes or opens, with the first and last lines
	// (1-based) it hides or shows again.
	std::function<void(int firstLine, int lastLine, bool folded)> foldChanged;

	// Sticky headers: while the top of the view is inside { } blocks whose
	// opening lines have scrolled away, those lines stay pinned above the
	// text, outermost first and at most three; a brace alone on its line is
	// stood for by the line before it, the function's or shader's name. A
	// click on one moves the caret to it.
	void setStickyHeadersEnabled(bool enabled);
	[[nodiscard]] bool stickyHeadersEnabled() const;
	// The lines (1-based) pinned now, outermost first.
	[[nodiscard]] QVector<int> stickyHeaderLines() const;
	// For the pinned strip, which the editor paints and answers for.
	void paintStickyHeaders(QPaintEvent* event, QWidget* surface);
	void stickyHeaderClicked(int y);

	// Every use of the name at the caret, as a whole word and in its own
	// case, is shaded while the caret rests on it and nothing is selected,
	// once the name is used more than once. These are those uses, first to
	// last, as document positions.
	[[nodiscard]] QVector<int> highlightedUses() const;

protected:
	void insertFromMimeData(const QMimeData* source) override;
	bool viewportEvent(QEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void changeEvent(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	// Enter keeps the line's indentation, one unit deeper after an opening
	// brace; Tab and Shift+Tab indent and outdent selected lines; a closing
	// brace typed on a blank line steps back one unit.
	void keyPressEvent(QKeyEvent* event) override;

private:
	void updateLineNumberAreaWidth();
	void initializeSnippet();
	void runSnippetEdit(const std::function<void()>& operation);
	void snippetContextChanged();
	void snippetDocumentChanged(int position, int removed, int added);
	bool adjustSnippetRanges(int target, int position, int removed, int added);
	void selectSnippetStop(int index);
	void publishSnippetState();
	void appendSnippetSelections(QList<QTextEdit::ExtraSelection>* selections);
	QPointer<QTextDocument> m_snippetDocument;
	QMetaObject::Connection m_snippetConnection;
	QVector<CompletionSnippetStop> m_snippetStops;
	QVector<bool> m_snippetRetired;
	QTextCursor m_snippetFinal;
	QString m_snippetDescription;
	QString m_snippetNotice;
	qint64 m_snippetUtf8Bytes = 0;
	int m_snippetStop = -1;
	bool m_snippetEditing = false, m_snippetMirroring = false, m_snippetMoved = false;
	void updateLineNumberArea(const QRect& rect, int dy);
	void highlightCurrentLine();
	void applyZoom();
	void handleKey(QKeyEvent* event);
	void narrowCompletions();
	void initializeCompletion();
	void ensureCompleter();
	void showCompletionPopup();
	void resolveCompletionChoice(int choice);
	void updateCompletionRow(int choice);
	void acceptCompletionChoice(int choice);
	void queueLanguageCompletions(int kind, const QString& trigger);
	void insertCompletion(const QString& completion);
	[[nodiscard]] QString wordBeforeCaret() const;
	// Each line (0-based) opening a foldable pair, with the line closing it.
	// Read afresh whenever the text has changed since, however the change
	// was announced; empty for a file too large to fold.
	[[nodiscard]] const QHash<int, int>& foldRegions() const;
	// The badges after folded lines in view, each with the folded line
	// (0-based), where they are painted and clicked.
	[[nodiscard]] QVector<QPair<QRect, int>> foldBadges() const;
	// The current-line band, bracket pair, and uses, worked out once however
	// many changes ask for them in one turn.
	void scheduleHighlight();
	[[nodiscard]] int foldColumnWidth() const;
	bool setFolded(int line, bool folded);
	// Hides exactly the lines inside folded pairs, dropping folds whose
	// braces no longer pair.
	void applyFolds();
	// Unfolds whatever hides the caret's line.
	void revealCaret();
	// Unfolds the folds that overlap lines `first` to `last` (0-based).
	void unfoldLines(int first, int last);
	void updateStickyHeaders();

	QWidget* m_lineNumberArea = nullptr;
	QFont m_baseFont;
	int m_zoomPercent = 100;
	// Wheel travel not yet worth a whole step, for high-resolution wheels.
	int m_wheelZoom = 0;
	QCompleter* m_completer = nullptr;
	QStandardItemModel* m_completionModel = nullptr;
	QTimer* m_completionTimer = nullptr;
	QPointer<QTextDocument> m_completionDocument;
	int m_completionRevision = -1, m_completionPosition = -1;
	quint64 m_completionToken = 0;
	bool m_languageCompletion = false, m_completionIncomplete = false, m_populatingCompletions = false;
	int m_resolvingCompletion = -1;
	bool m_completionAcceptPending = false;
	QVector<QString> m_completionResolveErrors;
	std::function<void(int, const LanguageCompletionItem&)> m_resolveLanguageCompletion;
	QVector<LanguageCompletionItem> m_languageCompletionItems;
	std::function<bool(const LanguageCompletionItem&)> m_acceptLanguageCompletion;
	// Brace pairs, worked out again after each edit or change of document.
	mutable QHash<int, int> m_foldRegions;
	mutable QPointer<const QTextDocument> m_foldRegionsDocument;
	mutable int m_foldRegionsRevision = -1;
	mutable bool m_foldRegionsDirty = true;
	bool m_applyingFolds = false;
	bool m_highlightScheduled = false;
	QWidget* m_stickyHeader = nullptr;
	// The lines (0-based) pinned, outermost first.
	QVector<int> m_stickyLines;
	bool m_stickyHeadersEnabled = true;
	// Set when the caret moves, so the next pass keeps it clear of the strip;
	// a scroll by wheel or bar alone leaves the view where the user put it.
	bool m_caretMovedSinceHighlight = false;
	bool m_overFoldBadge = false;
	bool m_quickInfoHintActive = false;
	QVector<int> m_highlightedUses;
};

// In-file find bar for the code editor. It opens with Ctrl+F on the Code
// surface and closes with Escape. Enter finds the next match and Shift+Enter
// the previous one; the search wraps at either end of the file, and the bar
// reports where the current match sits ("3 of 12"). Typing searches as you go,
// starting from the current match so the selection grows in place.
class CodeFindBar final : public QWidget {
public:
	explicit CodeFindBar(QPlainTextEdit* editor, QWidget* parent = nullptr);

	// Shows the bar, seeds it from a single-line editor selection, and focuses
	// the field with its text selected so typing replaces it.
	void activate();
	// The same with the replace row showing, as Ctrl+H opens it: the find
	// field takes focus while it is empty, the replace field once it is not.
	void activateReplace();
	// Select the next or previous match relative to the caret. False when the
	// field is empty or the text does not occur anywhere in the file.
	bool findNext();
	bool findPrevious();
	// Replaces the selection when it is a match, then selects the next one; a
	// selection that is not a match only moves to the next. True when
	// something was replaced.
	bool replaceCurrent();
	// Replaces every match in the file as one undo step; returns how many.
	int replaceAll();
	[[nodiscard]] QString text() const;
	[[nodiscard]] QLineEdit* field() const;
	[[nodiscard]] QLineEdit* replaceField() const;
	[[nodiscard]] bool replaceShown() const;
	[[nodiscard]] int matchCount() const;
	// Recounts after the document changes underneath the bar.
	void refreshCount();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	bool find(bool backward, bool fromSelectionStart = false);
	// Enables the replace row only while the editor can be changed, and says
	// in the row why when it cannot.
	void syncReadOnly();

	[[nodiscard]] QTextDocument::FindFlags findFlags() const;

	QPlainTextEdit* m_editor = nullptr;
	QLineEdit* m_field = nullptr;
	QWidget* m_replaceRow = nullptr;
	QLineEdit* m_replaceField = nullptr;
	QLabel* m_count = nullptr;
	QToolButton* m_matchCase = nullptr;
	int m_matchCount = 0;
};

} // namespace vibestudio
