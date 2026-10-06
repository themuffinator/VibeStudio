#pragma once

#include <QImage>
#include <QString>
#include <functional>

class QDialog;
class QWidget;

namespace vibestudio {

struct TexturePngExportResult {
	QString path;
	QString error;
	QSize size;
	bool succeeded = false;
	bool cancelled = false;
};

// Takes an implicitly shared pixel snapshot. Encoding/target inspection are
// cancellable; checked publication is a separate, non-cancellable worker phase.
QDialog* runTexturePngExportDialog(QWidget* parent, const QImage& image, const QString& path, bool overwrite,
	std::function<void(const TexturePngExportResult&)> completed = {});

} // namespace vibestudio
