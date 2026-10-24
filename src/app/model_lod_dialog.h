#pragma once

// Detail Levels…: reviews, stores and exports the detail levels an editable
// source owns (core/model_lod.h). Each level is generated from the level above
// it, by a share of its triangles or to a triangle target, or is a hand-made
// model. Review builds the levels on the worker and lists each surface's
// triangles per level, the set's consistency (the frames and surface names the
// game needs on every level) and where each level shows; a level can be shown
// in the viewport without changing the document. Store in Source keeps the
// recipe in the source as one undo step; Export writes the set beside an MD3.

#include "core/model_lod.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QWidget;

namespace vibestudio
{
class ModelEditorDialog;

class ModelLodDialog final : public QDialog
{
  public:
	explicit ModelLodDialog(ModelEditorDialog *editor);
	~ModelLodDialog() override;
	// The recipe the controls describe.
	[[nodiscard]] QVector<ModelLodRecipeLevel> recipe() const;
	void setRecipe(const QVector<ModelLodRecipeLevel> &recipe);
	// Builds and checks the levels on the worker; false with the reason in
	// the findings when the recipe cannot be built.
	bool review();
	// True after a review of the current controls that found no errors.
	[[nodiscard]] bool reviewed() const;
	[[nodiscard]] const ModelLodValidation &validation() const { return m_validation; }
	[[nodiscard]] const QVector<ModelLodLevel> &levels() const { return m_levels; }
	// Shows level `level` (0: the document) in the editor's viewport.
	void previewLevel(int level);
	// Stores the reviewed recipe in the source as one undo step.
	bool store(QString *error = nullptr);
	// Writes the reviewed set beside `basePath` (an MD3).
	bool exportTo(const QString &basePath, QString *error = nullptr);

  protected:
	void done(int result) override;

  private:
	struct LevelControls
	{
		QWidget *group = nullptr;
		QComboBox *mode = nullptr;
		QDoubleSpinBox *ratio = nullptr;
		QSpinBox *triangles = nullptr;
		QLineEdit *source = nullptr;
		QPushButton *browse = nullptr;
	};
	ModelEditorDialog *m_editor;
	ModelTargetProfile m_target;
	QString m_baseDirectory;
	QSpinBox *m_count = nullptr;
	QVector<LevelControls> m_rows;
	QTableWidget *m_surfaces = nullptr;
	QPlainTextEdit *m_findings = nullptr;
	QComboBox *m_preview = nullptr;
	QPushButton *m_review = nullptr, *m_store = nullptr, *m_export = nullptr;
	QLabel *m_summary = nullptr;
	QVector<ModelLodLevel> m_levels;
	ModelLodValidation m_validation;
	QVector<ModelLodRecipeLevel> m_reviewedRecipe;
	QByteArray m_reviewedRevision;
	void controlsChanged();
	void showReview();
};
} // namespace vibestudio
