#pragma once

#include "core/model_document.h"

#include <functional>

class QWidget;

namespace vibestudio
{
using ModelDocumentJob = std::function<bool(ModelDocument &candidate, QString *error, const ModelWorkControl &control)>;
using ModelTask = std::function<bool(QString *error, const ModelWorkControl &control)>;

// Shared cancellable worker/progress surface for mesh and assembly services.
// The caller owns its candidate values and adopts them only after success.
bool runModelTask(QWidget *parent, const QString &title, ModelTask task, QString *error = nullptr, bool durableWrite = false,
				  std::function<void()> *cancelAction = nullptr);

// Runs a value-only worker while servicing the UI event loop. The caller keeps
// document-mutating controls disabled for this call. A delayed window-modal
// progress surface provides cancellation; only a successful candidate is adopted.
// A completed durable write remains successful if cancellation arrives late.
bool runModelDocumentWork(QWidget *parent, const QString &title, ModelDocument *document, ModelDocumentJob job, QString *error = nullptr,
						  bool durableWrite = false, std::function<void()> *cancelAction = nullptr);
} // namespace vibestudio
