#pragma once

#include "core/model_document.h"
#include "core/model_skin_source.h"

#include <QDialog>

class QComboBox;
class QTableView;

namespace vibestudio
{
enum class ModelSkinSourcePurpose { IndexedTexture, ShaderBindings, Image, PreviewBindings };
// Metadata-only picker. Payloads are read by the document worker after acceptance.
class ModelSkinSourceDialog final : public QDialog
{
  public:
	ModelSkinSourceDialog(QVector<PackageEntry> entries, int skin, int member, double duration, QWidget *parent = nullptr,
						  ModelSkinSourcePurpose purpose = ModelSkinSourcePurpose::IndexedTexture);
	[[nodiscard]] ModelSkinSourceReference reference() const;
	[[nodiscard]] ModelEditKind kind() const;

  private:
	QTableView *m_entries = nullptr;
	QComboBox *m_operation = nullptr;
};
} // namespace vibestudio
