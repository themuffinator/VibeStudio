#include "app/model_assembly_dialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
bool ModelAssemblyDialog::bakeAnimation(const ModelAssemblyAnimationOptions &options, ModelAssemblyAnimation *result, QString *error)
{
	play(false);
	const auto assembly = m_document.assembly();
	const auto resolved = m_resolved;
	ModelAssemblyAnimation candidate;
	if (!performWork(
			QCoreApplication::translate("ModelAssemblyDialog", "Bake Assembly Animation"),
			[&](QString *failure, const ModelWorkControl &control) {
				return bakeModelAssemblyAnimation(assembly, resolved, options, result ? &candidate : nullptr, failure, control);
			},
			error))
	{
		return false;
	}
	*result = std::move(candidate);
	return true;
}

bool ModelAssemblyDialog::exportAnimation(const ModelAssemblyAnimationOptions &options, const QString &path, bool overwrite, QString *error)
{
	play(false);
	const auto assembly = m_document.assembly();
	const auto resolved = m_resolved;
	const auto source = m_document.recoverySource().isEmpty() ? m_document.path() : m_document.recoverySource();
	const auto archive = m_materialSource.archive;
	ModelExportReport result;
	if (!performWork(
			QCoreApplication::translate("ModelAssemblyDialog", "Export Assembly Animation"),
			[&](QString *failure, const ModelWorkControl &control) {
				return exportModelAssemblyAnimation(assembly, resolved, options, path, source, overwrite, false, failure, control, &result,
													archive);
			},
			error, true))
	{
		return false;
	}
	report(QCoreApplication::translate("ModelAssemblyDialog", "Exported animation: %1. %2").arg(path, result.notes.join(QLatin1Char(' '))));
	return true;
}

void ModelAssemblyDialog::chooseAnimationBake(bool exporting)
{
	if (!previewReady() || m_working || (!exporting && !editBakedPose))
	{
		return;
	}
	play(false);
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("assemblyAnimationOptions"));
	dialog.setWindowTitle(exporting ? QCoreApplication::translate("ModelAssemblyDialog", "Export Assembly Animation")
									: QCoreApplication::translate("ModelAssemblyDialog", "Bake Assembly Animation"));
	dialog.setAccessibleName(dialog.windowTitle());
	dialog.resize(620, 600);
	auto *layout = new QVBoxLayout(&dialog);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *body = new QWidget;
	auto *bodyLayout = new QVBoxLayout(body);
	auto *form = new QFormLayout;
	bodyLayout->addLayout(form);
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	const auto field = [&](QWidget *widget, const char *id, const QString &name, const QString &description) {
		widget->setObjectName(QString::fromLatin1(id));
		widget->setAccessibleName(name);
		widget->setAccessibleDescription(description);
		widget->setToolTip(description);
		widget->setMinimumWidth(0);
		widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		auto *label = new QLabel(name);
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		label->setBuddy(widget);
		form->addRow(label, widget);
	};
	auto *name = new QLineEdit(QStringLiteral("assembly"));
	name->setMaxLength(128);
	field(name, "assemblyAnimationName", QCoreApplication::translate("ModelAssemblyDialog", "Clip name"),
		  QCoreApplication::translate("ModelAssemblyDialog", "Name saved with the baked animation in the editable mesh source."));
	auto *start = new QDoubleSpinBox;
	start->setDecimals(6);
	start->setRange(0, 1000000);
	start->setValue(m_seconds);
	start->setKeyboardTracking(false);
	start->setLayoutDirection(Qt::LeftToRight);
	field(start, "assemblyAnimationStart", QCoreApplication::translate("ModelAssemblyDialog", "Start time (seconds)"),
		  QCoreApplication::translate(
			  "ModelAssemblyDialog",
			  "Assembly time of the first sample. Each part keeps its own rate, phase, loop and interpolation settings."));
	auto *count = new QSpinBox;
	const int capacity =
		int(std::min<qint64>(modelDocumentMaxFrames, modelDocumentMaxFrameVertices / std::max(1, m_pose.mesh.vertexCount)));
	count->setRange(1, capacity);
	count->setValue(std::min(30, capacity));
	count->setLayoutDirection(Qt::LeftToRight);
	field(count, "assemblyAnimationFrames", QCoreApplication::translate("ModelAssemblyDialog", "Frame count"),
		  QCoreApplication::translate(
			  "ModelAssemblyDialog",
			  "Number of uniformly spaced poses. The limit reflects the assembly vertex count and editable mesh storage budget."));
	auto *fps = new QDoubleSpinBox;
	fps->setDecimals(6);
	fps->setRange(.001, 1000);
	fps->setValue(30);
	fps->setKeyboardTracking(false);
	fps->setLayoutDirection(Qt::LeftToRight);
	field(fps, "assemblyAnimationFps", QCoreApplication::translate("ModelAssemblyDialog", "Sampling FPS"),
		  QCoreApplication::translate(
			  "ModelAssemblyDialog",
			  "Sampling and saved clip playback rate. Sample i is start time plus i divided by FPS; no duplicate endpoint is added."));
	auto *summary = new QPlainTextEdit;
	summary->setObjectName(QStringLiteral("assemblyAnimationSummary"));
	summary->setAccessibleName(QCoreApplication::translate("ModelAssemblyDialog", "Animation bake review"));
	summary->setReadOnly(true);
	summary->setTabChangesFocus(true);
	summary->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	summary->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	summary->setMinimumWidth(0);
	summary->setMinimumHeight(summary->fontMetrics().lineSpacing() * 8);
	summary->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	bodyLayout->addWidget(summary, 1);
	scroll->setWidget(body);
	layout->addWidget(scroll);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)
		->setText(exporting ? QCoreApplication::translate("ModelAssemblyDialog", "Export…")
							: QCoreApplication::translate("ModelAssemblyDialog", "Bake to Mesh"));
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	const auto options = [&] { return ModelAssemblyAnimationOptions{start->value(), count->value(), fps->value(), name->text()}; };
	const auto update = [&] {
		const auto value = options();
		summary->setPlainText(modelAssemblyAnimationNotes(m_resolved, value).join(QStringLiteral("\n\n")));
		summary->setAccessibleDescription(summary->toPlainText());
		buttons->button(QDialogButtonBox::Ok)
			->setEnabled(!value.clipName.trimmed().isEmpty() && value.clipName == value.clipName.trimmed() &&
						 value.startSeconds + (value.frameCount - 1) / value.framesPerSecond <= 1000000);
	};
	connect(start, &QDoubleSpinBox::valueChanged, &dialog, update);
	connect(count, &QSpinBox::valueChanged, &dialog, update);
	connect(fps, &QDoubleSpinBox::valueChanged, &dialog, update);
	connect(name, &QLineEdit::textChanged, &dialog, update);
	update();
	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}
	QString error;
	if (exporting)
	{
		const auto path = QFileDialog::getSaveFileName(
			this, QCoreApplication::translate("ModelAssemblyDialog", "Export Assembly Animation"), {},
			QCoreApplication::translate("ModelAssemblyDialog",
										"Editable mesh (*.mesh.json);;Quake II model (*.md2);;Quake III model (*.md3)"));
		if (!path.isEmpty())
		{
			exportAnimation(options(), path, true, &error);
		}
		return;
	}
	ModelAssemblyAnimation animation;
	if (!bakeAnimation(options(), &animation, &error))
	{
		return;
	}
	if (!editBakedPose(animation.mesh, &error))
	{
		report(error);
		return;
	}
	report(QCoreApplication::translate(
			   "ModelAssemblyDialog",
			   "Opened %1 baked poses in the Mesh Editor at %2 FPS. The assembly and its original inputs remain editable.")
			   .arg(options().frameCount)
			   .arg(options().framesPerSecond));
}
} // namespace vibestudio
