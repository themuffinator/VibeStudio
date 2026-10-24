#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QPushButton;

namespace vibestudio {

// Find and Replace Textures: a modeless window, so picks in the Textures tab
// or on the Textures surface can fill its fields while it stays open, as in
// NetRadiant Custom's FindReplaceTextures. The shell runs Find and Replace
// and supplies the preview; this widget holds the inputs and their state.
// Workflow reference: NRC findtexturedialog.cpp at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
class LevelFindReplaceDialog final : public QDialog {
	Q_OBJECT

public:
	enum class Scope { Map = 0, Selection = 1, Picks = 2 };

	explicit LevelFindReplaceDialog(QWidget* parent = nullptr);

	// The textures the map uses, offered in both fields. Typed text stays.
	void setTextureNames(const QStringList& names);
	[[nodiscard]] QString findText() const;
	[[nodiscard]] QString replaceText() const;
	void setFindText(const QString& text);
	void setReplaceText(const QString& text);
	[[nodiscard]] Scope scope() const;
	void setScope(Scope scope);
	// An unavailable scope stays listed, with the reason as its tooltip; a
	// chosen scope is never widened behind the user's back.
	void setScopeAvailable(Scope scope, bool available, const QString& reason = QString());
	// The live summary of what Find or Replace would do, and which can run.
	void setPreview(const QString& text, bool canFind, bool canReplace);
	[[nodiscard]] QString previewText() const;
	// Puts a texture-browser pick in the field that last had focus: Find
	// until Replace with is focused. Returns true when it filled Replace with.
	bool fillFocusedField(const QString& name);
	[[nodiscard]] bool replaceFieldActive() const { return m_replaceActive; }

signals:
	void inputsChanged();
	void findRequested();
	void replaceRequested();

private:
	void setReplaceActive(bool active);
	void refreshDefaultButton();

	QComboBox* m_find = nullptr;
	QComboBox* m_replace = nullptr;
	QComboBox* m_scope = nullptr;
	QLabel* m_pickTarget = nullptr;
	QLabel* m_preview = nullptr;
	QPushButton* m_findButton = nullptr;
	QPushButton* m_replaceButton = nullptr;
	bool m_replaceActive = false;
};

} // namespace vibestudio
