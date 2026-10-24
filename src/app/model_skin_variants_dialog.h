#pragma once

// Skin Variants…: the named skins an editable source owns
// (core/model_skin_variants.h). The dialog edits a working copy: add, rename
// and remove variants, and give the selected surfaces a variant material or
// return them to their primary material. Any variant can be shown in the
// viewport without changing the document. Apply keeps the list in the source
// as one undo step; Export .skin… and Export Doom 3 Skin… write the selected
// variant's skin file through the same core writers as model skin-export.

#include "core/model_skin_variants.h"

#include <QByteArray>
#include <QDialog>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace vibestudio
{
class ModelEditorDialog;

class ModelSkinVariantsDialog final : public QDialog
{
  public:
	explicit ModelSkinVariantsDialog(ModelEditorDialog *editor);
	~ModelSkinVariantsDialog() override;
	// The working copy.
	[[nodiscard]] const QVector<ModelSkinVariant> &variants() const { return m_variants; }
	[[nodiscard]] bool modified() const { return m_variants != m_original; }
	// Commits the working copy to the source as one undo step.
	bool apply(QString *error = nullptr);
	// Writes the selected variant (or the primary materials when none is
	// selected) as a Quake III .skin, or as a Doom 3 skin declaration.
	bool exportSkin(const QString &path, bool doom3, QString *error = nullptr);
	// Shows variant `index` (-1: the primary materials) in the viewport.
	void previewVariant(int index);

  protected:
	void done(int result) override;

  private:
	ModelEditorDialog *m_editor;
	QByteArray m_revision;
	QVector<ModelSkinVariant> m_original, m_variants;
	ModelMesh m_previewMesh;
	QListWidget *m_list = nullptr;
	QLineEdit *m_name = nullptr, *m_material = nullptr, *m_modelPath = nullptr;
	QTableWidget *m_surfaces = nullptr;
	QComboBox *m_preview = nullptr;
	QPlainTextEdit *m_review = nullptr;
	QLabel *m_summary = nullptr;
	QPushButton *m_add = nullptr, *m_rename = nullptr, *m_remove = nullptr, *m_set = nullptr, *m_clear = nullptr, *m_apply = nullptr,
				*m_exportQ3 = nullptr, *m_exportDoom3 = nullptr;
	[[nodiscard]] int current() const;
	bool change(const ModelSkinVariantEdit &edit, const QString &select);
	void populate(const QString &select);
	void showVariant();
	void refreshControls();
};
} // namespace vibestudio
