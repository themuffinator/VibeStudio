#include "app/model_skin_variants_dialog.h"

#include "app/model_editor_dialog.h"
#include "core/model_material_slots.h"
#include "core/model_skin_bindings.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelSkinVariants)
};
void describe(QWidget *widget, const char *id, const QString &name, const QString &description)
{
	widget->setObjectName(QString::fromLatin1(id));
	widget->setAccessibleName(name);
	widget->setAccessibleDescription(description);
	widget->setToolTip(description);
}
QPushButton *button(const QString &label, const char *id, const QString &description)
{
	auto *control = new QPushButton(label);
	control->setAutoDefault(false);
	describe(control, id, QString(label).remove(QLatin1Char('&')), description);
	return control;
}
} // namespace

ModelSkinVariantsDialog::ModelSkinVariantsDialog(ModelEditorDialog *editor)
	: QDialog(editor), m_editor(editor), m_revision(editor->m_document.revisionFingerprint()),
	  m_original(editor->m_document.mesh().skinVariants), m_variants(m_original)
{
	setObjectName(QStringLiteral("meshSkinVariantsDialog"));
	setWindowTitle(Text::tr("Skin Variants"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(Text::tr("Name skins such as red and blue, give some surfaces another material in each, preview them and write "
									  "their skin files. Apply keeps them in the source as one undo step."));
	auto *layout = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("meshSkinVariantScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *body = new QWidget;
	auto *content = new QVBoxLayout(body);
	scroll->setWidget(body);
	layout->addWidget(scroll, 1);
	m_summary = new QLabel;
	m_summary->setObjectName(QStringLiteral("meshSkinVariantSummary"));
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setAccessibleName(Text::tr("Skin variant summary"));
	content->addWidget(m_summary);
	m_list = new QListWidget;
	m_list->setLayoutDirection(Qt::LeftToRight);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setMinimumHeight(fontMetrics().lineSpacing() * 5);
	describe(m_list, "meshSkinVariantList", Text::tr("Skin variants"),
			 Text::tr("The skins the source owns beside its primary materials. Choose one to edit its surfaces."));
	auto *listLabel = new QLabel(Text::tr("&Variants:"));
	listLabel->setBuddy(m_list);
	content->addWidget(listLabel);
	content->addWidget(m_list);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_name = new QLineEdit;
	m_name->setMaxLength(24);
	m_name->setLayoutDirection(Qt::LeftToRight);
	describe(m_name, "meshSkinVariantName", Text::tr("Variant name"),
			 Text::tr("1 to 24 ASCII letters, digits, underscores or hyphens, starting with a letter, such as red or blue. default names the "
					  "primary materials."));
	form->addRow(Text::tr("&Name:"), m_name);
	content->addLayout(form);
	auto *variantButtons = new QHBoxLayout;
	m_add = button(Text::tr("&Add"), "meshSkinVariantAdd", Text::tr("Add a variant with the entered name; every surface starts with its primary material."));
	m_rename = button(Text::tr("Re&name"), "meshSkinVariantRename", Text::tr("Give the selected variant the entered name."));
	m_remove = button(Text::tr("&Remove"), "meshSkinVariantRemove", Text::tr("Remove the selected variant from the working copy."));
	variantButtons->addWidget(m_add);
	variantButtons->addWidget(m_rename);
	variantButtons->addWidget(m_remove);
	variantButtons->addStretch();
	content->addLayout(variantButtons);
	m_surfaces = new QTableWidget;
	m_surfaces->setColumnCount(3);
	m_surfaces->setHorizontalHeaderLabels({Text::tr("Surface"), Text::tr("Primary material"), Text::tr("Variant material")});
	m_surfaces->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_surfaces->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_surfaces->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_surfaces->setTabKeyNavigation(false);
	m_surfaces->setTextElideMode(Qt::ElideMiddle);
	m_surfaces->verticalHeader()->setVisible(false);
	m_surfaces->horizontalHeader()->setStretchLastSection(true);
	m_surfaces->setLayoutDirection(Qt::LeftToRight);
	m_surfaces->setMinimumHeight(fontMetrics().lineSpacing() * 7);
	describe(m_surfaces, "meshSkinVariantSurfaces", Text::tr("Surfaces of the selected variant"),
			 Text::tr("Each surface's primary material and the material the selected variant gives it; an empty variant material keeps the "
					  "primary one. Select rows to change them."));
	auto *surfaceLabel = new QLabel(Text::tr("&Surfaces:"));
	surfaceLabel->setBuddy(m_surfaces);
	content->addWidget(surfaceLabel);
	content->addWidget(m_surfaces, 1);
	auto *materialForm = new QFormLayout;
	materialForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	materialForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_material = new QLineEdit;
	m_material->setMaxLength(255);
	m_material->setLayoutDirection(Qt::LeftToRight);
	describe(m_material, "meshSkinVariantMaterial", Text::tr("Variant material"),
			 Text::tr("A package-relative texture or shader path for the selected surfaces in this variant."));
	materialForm->addRow(Text::tr("&Material:"), m_material);
	content->addLayout(materialForm);
	auto *materialButtons = new QHBoxLayout;
	m_set = button(Text::tr("Set &Material"), "meshSkinVariantSet", Text::tr("Give the selected surfaces the entered material in this variant."));
	m_clear = button(Text::tr("Use &Primary"), "meshSkinVariantClear",
					 Text::tr("Return the selected surfaces to their primary material in this variant."));
	materialButtons->addWidget(m_set);
	materialButtons->addWidget(m_clear);
	materialButtons->addStretch();
	content->addLayout(materialButtons);
	auto *viewForm = new QFormLayout;
	viewForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	viewForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_preview = new QComboBox;
	m_preview->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_preview->setMinimumContentsLength(12);
	describe(m_preview, "meshSkinVariantPreview", Text::tr("Skin shown in the viewport"),
			 Text::tr("Shows the primary materials or a variant in the viewport; the model does not change."));
	viewForm->addRow(Text::tr("Sho&w:"), m_preview);
	m_modelPath = new QLineEdit;
	m_modelPath->setLayoutDirection(Qt::LeftToRight);
	m_modelPath->setPlaceholderText(QStringLiteral("models/md5/…/name.md5mesh"));
	describe(m_modelPath, "meshSkinVariantModelPath", Text::tr("Doom 3 model path"),
			 Text::tr("The model a Doom 3 skin names in its model line and declaration name, such as models/md5/monsters/imp/imp.md5mesh. "
					  "Empty uses the source's file name."));
	viewForm->addRow(Text::tr("Doom 3 mo&del:"), m_modelPath);
	content->addLayout(viewForm);
	m_review = new QPlainTextEdit;
	m_review->setReadOnly(true);
	m_review->setTabChangesFocus(true);
	m_review->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	m_review->setMinimumHeight(fontMetrics().lineSpacing() * 5);
	describe(m_review, "meshSkinVariantReview", Text::tr("Skin variant review"),
			 Text::tr("What each skin file of the selected variant would contain, and anything that stops it being written."));
	content->addWidget(m_review);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	m_apply = buttons->addButton(Text::tr("A&pply"), QDialogButtonBox::ActionRole);
	describe(m_apply, "meshSkinVariantApply", Text::tr("Apply skin variants"),
			 Text::tr("Keeps the working copy in the editable source as one undo step. Save the source to keep it."));
	m_exportQ3 = buttons->addButton(Text::tr("Export .&skin…"), QDialogButtonBox::ActionRole);
	describe(m_exportQ3, "meshSkinVariantExportQ3", Text::tr("Export a Quake III skin"),
			 Text::tr("Writes the selected variant as a Quake III .skin naming every surface."));
	m_exportDoom3 = buttons->addButton(Text::tr("Export &Doom 3 Skin…"), QDialogButtonBox::ActionRole);
	describe(m_exportDoom3, "meshSkinVariantExportDoom3", Text::tr("Export a Doom 3 skin"),
			 Text::tr("Writes the selected variant as a Doom 3 skin declaration that maps materials to materials."));
	for (auto *control : {m_apply, m_exportQ3, m_exportDoom3})
		control->setAutoDefault(false);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	connect(m_list, &QListWidget::currentRowChanged, this, [this] {
		if (const int index = current(); index >= 0)
			m_name->setText(m_variants[index].name);
		showVariant();
	});
	connect(m_name, &QLineEdit::textChanged, this, [this] { refreshControls(); });
	connect(m_material, &QLineEdit::textChanged, this, [this] { refreshControls(); });
	connect(m_modelPath, &QLineEdit::textChanged, this, [this] { refreshControls(); });
	connect(m_surfaces, &QTableWidget::itemSelectionChanged, this, [this] {
		const int index = current();
		const auto *row = m_surfaces->currentItem() ? m_surfaces->item(m_surfaces->currentRow(), 2) : nullptr;
		if (index >= 0 && row)
		{
			const QSignalBlocker blocker(m_material);
			m_material->setText(row->text());
		}
		refreshControls();
	});
	connect(m_add, &QPushButton::clicked, this, [this] {
		ModelSkinVariantEdit edit;
		edit.action = ModelSkinVariantAction::Add;
		edit.name = m_name->text().trimmed();
		change(edit, edit.name);
	});
	connect(m_rename, &QPushButton::clicked, this, [this] {
		if (current() < 0)
			return;
		ModelSkinVariantEdit edit;
		edit.action = ModelSkinVariantAction::Rename;
		edit.name = m_variants[current()].name;
		edit.newName = m_name->text().trimmed();
		change(edit, edit.newName);
	});
	connect(m_remove, &QPushButton::clicked, this, [this] {
		if (current() < 0)
			return;
		ModelSkinVariantEdit edit;
		edit.action = ModelSkinVariantAction::Remove;
		edit.name = m_variants[current()].name;
		change(edit, {});
	});
	const auto setSelected = [this](bool primary) {
		if (current() < 0)
			return;
		ModelSkinVariantEdit edit;
		edit.action = ModelSkinVariantAction::Set;
		edit.name = m_variants[current()].name;
		QVector<int> rows;
		for (const auto &range : m_surfaces->selectedRanges())
			for (int row = range.topRow(); row <= range.bottomRow(); ++row)
				rows << row;
		std::sort(rows.begin(), rows.end());
		rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
		for (int row : std::as_const(rows))
			edit.bindings.append({m_surfaces->item(row, 0)->data(Qt::UserRole).toString(), primary ? QString() : m_material->text().trimmed()});
		change(edit, edit.name);
	};
	connect(m_set, &QPushButton::clicked, this, [setSelected] { setSelected(false); });
	connect(m_clear, &QPushButton::clicked, this, [setSelected] { setSelected(true); });
	connect(m_preview, &QComboBox::currentIndexChanged, this, [this](int index) { previewVariant(index - 1); });
	connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
	const auto exportTo = [this](bool doom3) {
		const auto name = current() >= 0 ? m_variants[current()].name : QStringLiteral("default");
		const auto base = m_editor->m_document.path().isEmpty() ? QStringLiteral("model") : QFileInfo(m_editor->m_document.path()).baseName();
		const auto path = QFileDialog::getSaveFileName(this, doom3 ? Text::tr("Export Doom 3 Skin") : Text::tr("Export Quake III Skin"),
													   QStringLiteral("%1_%2.skin").arg(base, name), Text::tr("Skin (*.skin)"));
		if (!path.isEmpty())
			exportSkin(path, doom3);
	};
	connect(m_exportQ3, &QPushButton::clicked, this, [exportTo] { exportTo(false); });
	connect(m_exportDoom3, &QPushButton::clicked, this, [exportTo] { exportTo(true); });
	populate(m_variants.isEmpty() ? QString() : m_variants.first().name);
	resize(fontMetrics().averageCharWidth() * 90, fontMetrics().lineSpacing() * 40);
	m_list->setFocus(Qt::OtherFocusReason);
}

ModelSkinVariantsDialog::~ModelSkinVariantsDialog() = default;

int ModelSkinVariantsDialog::current() const
{
	const int row = m_list->currentRow();
	return row >= 0 && row < m_variants.size() ? row : -1;
}

bool ModelSkinVariantsDialog::change(const ModelSkinVariantEdit &edit, const QString &select)
{
	QString error;
	if (!editModelSkinVariants(m_editor->m_document.mesh(), m_variants, edit, &m_variants, &error))
	{
		m_review->setPlainText(error);
		m_summary->setText(error);
		return false;
	}
	populate(select);
	return true;
}

void ModelSkinVariantsDialog::populate(const QString &select)
{
	{
		const QSignalBlocker blocker(m_list);
		m_list->clear();
		for (const auto &variant : std::as_const(m_variants))
		{
			auto *item = new QListWidgetItem(variant.name, m_list);
			const auto description = Text::tr("%1: %n surface(s) with their own material", nullptr, int(variant.bindings.size())).arg(variant.name);
			item->setToolTip(description);
			item->setData(Qt::AccessibleDescriptionRole, description);
		}
		const int row = findModelSkinVariant(m_variants, select);
		m_list->setCurrentRow(row >= 0 ? row : (m_variants.isEmpty() ? -1 : 0));
	}
	{
		const QSignalBlocker blocker(m_preview);
		const auto shown = m_preview->currentText();
		m_preview->clear();
		m_preview->addItem(Text::tr("Primary materials"));
		for (const auto &variant : std::as_const(m_variants))
			m_preview->addItem(variant.name);
		const int index = m_preview->findText(shown);
		m_preview->setCurrentIndex(std::max(0, index));
	}
	if (const int index = current(); index >= 0)
		m_name->setText(m_variants[index].name);
	showVariant();
	previewVariant(m_preview->currentIndex() - 1);
}

void ModelSkinVariantsDialog::showVariant()
{
	const auto &mesh = m_editor->m_document.mesh();
	const int index = current();
	QVector<ModelSkinVariantBinding> bindings = index >= 0 ? m_variants[index].bindings : QVector<ModelSkinVariantBinding>{};
	const QSignalBlocker blocker(m_surfaces);
	m_surfaces->clearContents();
	m_surfaces->setRowCount(0);
	const auto addRow = [this](const QString &surface, const QString &primary, const QString &material, const QString &description) {
		const int row = m_surfaces->rowCount();
		m_surfaces->insertRow(row);
		const QStringList cells{surface, primary, material};
		for (int column = 0; column < 3; ++column)
		{
			auto *item = new QTableWidgetItem(cells[column]);
			item->setToolTip(description);
			item->setData(Qt::AccessibleDescriptionRole, description);
			item->setData(Qt::UserRole, surface);
			m_surfaces->setItem(row, column, item);
		}
	};
	for (const auto &surface : mesh.surfaces)
	{
		const auto found = std::find_if(bindings.cbegin(), bindings.cend(), [&](const auto &b) { return b.surface == surface.name; });
		const auto primary = surface.skinPaths.value(0);
		const auto material = found == bindings.cend() ? QString() : found->material;
		addRow(surface.name, primary.isEmpty() ? Text::tr("Unassigned") : primary, material,
			   material.isEmpty() ? Text::tr("%1 keeps its primary material %2.").arg(surface.name, primary.isEmpty() ? Text::tr("(unassigned)") : primary)
								  : Text::tr("%1 shows %2 in this variant instead of %3.").arg(surface.name, material, primary));
	}
	// Bindings to surfaces the model no longer has stay visible so they can be
	// returned to the primary material (removed).
	for (const auto &binding : std::as_const(bindings))
		if (std::none_of(mesh.surfaces.cbegin(), mesh.surfaces.cend(), [&](const auto &s) { return s.name == binding.surface; }))
			addRow(binding.surface, Text::tr("Not in the model"), binding.material,
				   Text::tr("The model has no surface %1. Use Primary removes this binding.").arg(binding.surface));
	m_surfaces->resizeColumnsToContents();
	if (m_surfaces->rowCount() > 0)
		m_surfaces->selectRow(0);
	const auto *row = m_surfaces->item(0, 2);
	{
		const QSignalBlocker materialBlocker(m_material);
		m_material->setText(row ? row->text() : QString());
	}
	refreshControls();
}

void ModelSkinVariantsDialog::refreshControls()
{
	if (!m_apply)
		return;
	const auto &document = m_editor->m_document.mesh();
	const int index = current();
	const auto name = m_name->text().trimmed();
	const bool supported = supportsModelMaterialSlots(document);
	const bool rows = !m_surfaces->selectedRanges().isEmpty();
	m_add->setEnabled(supported && validModelSkinVariantName(name) && findModelSkinVariant(m_variants, name) < 0 &&
					  m_variants.size() < modelSkinVariantMaxCount);
	m_rename->setEnabled(index >= 0 && validModelSkinVariantName(name) && name != m_variants[index].name &&
						 (findModelSkinVariant(m_variants, name) < 0 || findModelSkinVariant(m_variants, name) == index));
	m_remove->setEnabled(index >= 0);
	m_set->setEnabled(index >= 0 && rows && !m_material->text().trimmed().isEmpty());
	m_clear->setEnabled(index >= 0 && rows);
	m_apply->setEnabled(modified());
	m_exportQ3->setEnabled(supported);
	m_exportDoom3->setEnabled(supported && index >= 0);
	QStringList lines;
	auto mesh = document;
	mesh.skinVariants = m_variants;
	if (!supported)
		lines << Text::tr("This model's skins are embedded; skin variants name external materials. Use its skin groups instead.");
	else if (index < 0)
	{
		QByteArray bytes;
		QString error;
		lines << (writeModelQ3Skin(mesh, &bytes, &error) ? Text::tr("Quake III .skin of the primary materials:") + QLatin1Char('\n') + QString::fromLatin1(bytes)
														 : Text::tr("Quake III .skin: %1").arg(error));
	}
	else
	{
		const auto &variant = m_variants[index];
		ModelMesh applied;
		QStringList inherited;
		QString error;
		QByteArray bytes;
		if (!applyModelSkinVariant(mesh, variant, &applied, &error, &inherited))
			lines << error;
		else
		{
			lines << (writeModelQ3Skin(applied, &bytes, &error) ? Text::tr("Quake III .skin:") + QLatin1Char('\n') + QString::fromLatin1(bytes)
																: Text::tr("Quake III .skin: %1").arg(error));
			const auto modelPath = m_modelPath->text().trimmed();
			const auto base = modelPath.isEmpty() ? (m_editor->m_document.path().isEmpty() ? QStringLiteral("model") : QFileInfo(m_editor->m_document.path()).baseName())
												  : modelPath;
			lines << (writeModelDoom3Skin(mesh, variant, modelDoom3SkinDeclName(base, variant.name), modelPath, &bytes, &error)
						  ? Text::tr("Doom 3 skin declaration:") + QLatin1Char('\n') + QString::fromLatin1(bytes)
						  : Text::tr("Doom 3 skin: %1").arg(error));
		}
	}
	m_review->setPlainText(lines.join(QStringLiteral("\n\n")));
	m_summary->setText(modified() ? Text::tr("%n skin variant(s) in the working copy. Apply keeps the list in the source as one undo step.", nullptr,
											 int(m_variants.size()))
								  : Text::tr("%n skin variant(s). A variant gives some surfaces another material; the others keep their primary "
											 "material.",
											 nullptr, int(m_variants.size())));
	m_summary->setAccessibleDescription(m_summary->text());
}

void ModelSkinVariantsDialog::previewVariant(int index)
{
	auto mesh = m_editor->m_document.mesh();
	mesh.skinVariants = m_variants;
	if (index < 0 || index >= m_variants.size() || !applyModelSkinVariant(mesh, m_variants[index], &m_previewMesh))
	{
		m_editor->previewTransientMesh(nullptr);
		return;
	}
	m_editor->previewTransientMesh(&m_previewMesh);
}

bool ModelSkinVariantsDialog::apply(QString *error)
{
	QString local;
	if (!error)
		error = &local;
	error->clear();
	if (!modified())
		return true;
	auto mesh = m_editor->m_document.mesh();
	mesh.skinVariants = m_variants;
	const auto selection = m_editor->m_document.selection();
	const auto base = m_revision;
	if (!m_editor->performWork(
			Text::tr("Apply Skin Variants"),
			[mesh, selection, base](ModelDocument &candidate, QString *failure, const ModelWorkControl &control) {
				return candidate.adopt(mesh, selection, base, failure, control);
			},
			error))
	{
		m_review->setPlainText(*error);
		m_summary->setText(*error);
		return false;
	}
	m_editor->refresh();
	m_original = m_variants;
	m_revision = m_editor->m_document.revisionFingerprint();
	m_editor->m_status->setText(Text::tr("Stored %n skin variant(s) in the source as one undo step. Save the source to keep the list.", nullptr, int(m_variants.size())));
	refreshControls();
	previewVariant(m_preview->currentIndex() - 1);
	return true;
}

bool ModelSkinVariantsDialog::exportSkin(const QString &path, bool doom3, QString *error)
{
	QString local;
	if (!error)
		error = &local;
	error->clear();
	auto mesh = m_editor->m_document.mesh();
	mesh.skinVariants = m_variants;
	const int index = current();
	QByteArray bytes;
	bool ready = false;
	if (doom3)
	{
		const auto modelPath = m_modelPath->text().trimmed();
		const auto base = modelPath.isEmpty() ? (m_editor->m_document.path().isEmpty() ? QStringLiteral("model") : QFileInfo(m_editor->m_document.path()).baseName())
											  : modelPath;
		ready = index >= 0 ? writeModelDoom3Skin(mesh, m_variants[index], modelDoom3SkinDeclName(base, m_variants[index].name), modelPath, &bytes, error)
						   : (*error = Text::tr("Choose a variant: a Doom 3 skin maps the primary materials to a variant's."), false);
	}
	else
	{
		ModelMesh applied = mesh;
		ready = (index < 0 || applyModelSkinVariant(mesh, m_variants[index], &applied, error)) && writeModelQ3Skin(applied, &bytes, error);
	}
	const auto source = m_editor->m_document.path();
	if (ready && !source.isEmpty() && modelPathsReferToSameFile(source, path))
		ready = (*error = Text::tr("Choose a skin file separate from the model source."), false);
	if (ready)
	{
		const auto target = inspectModelWriteTarget(path);
		ready = target.isValid() ? writeModelFile(target, bytes, error) : (*error = target.error, false);
	}
	const auto message = ready ? Text::tr("Wrote %1.").arg(QFileInfo(path).absoluteFilePath()) : *error;
	m_summary->setText(message);
	m_editor->m_status->setText(message);
	return ready;
}

void ModelSkinVariantsDialog::done(int result)
{
	// The viewport shows the document again whichever way the dialog closes.
	m_editor->previewTransientMesh(nullptr);
	QDialog::done(result);
}

bool ModelEditorDialog::editSkinVariants(QString *error)
{
	if (m_working || !supportsModelMaterialSlots(m_document.mesh()))
	{
		if (error)
			*error = m_working ? QCoreApplication::translate("VibeStudioModelSkinVariants", "Wait for the current model operation to finish.")
							   : QCoreApplication::translate("VibeStudioModelSkinVariants",
															 "This model's skins are embedded; skin variants name external materials.");
		return false;
	}
	ModelSkinVariantsDialog dialog(this);
	dialog.exec();
	if (dialog.modified())
		m_status->setText(QCoreApplication::translate("VibeStudioModelSkinVariants", "Skin variant changes were not applied."));
	return true;
}
} // namespace vibestudio
