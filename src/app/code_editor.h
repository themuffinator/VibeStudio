#pragma once

// Plain-text code editor with a line-number gutter and a current-line band.
//
// The gutter and band read the active studio theme when they paint, so they
// follow theme switches without being told. Everything else, including
// syntax highlighting and diagnostics, stays with the shell.

#include <QPlainTextEdit>

namespace vibestudio {

class StudioCodeEditor final : public QPlainTextEdit {
public:
	explicit StudioCodeEditor(QWidget* parent = nullptr);

	[[nodiscard]] int lineNumberAreaWidth() const;
	void paintLineNumbers(QPaintEvent* event);

protected:
	void resizeEvent(QResizeEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void updateLineNumberAreaWidth();
	void updateLineNumberArea(const QRect& rect, int dy);
	void highlightCurrentLine();

	QWidget* m_lineNumberArea = nullptr;
};

} // namespace vibestudio
