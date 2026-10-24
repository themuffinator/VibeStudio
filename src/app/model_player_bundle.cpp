#include "app/model_assembly_dialog.h"
#include "app/dialog_screen_fit.h"
#include "app/model_skin_source_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelPlayerBundleDialog)
};
} // namespace
bool ModelAssemblyDialog::preparePlayerBundle(const ModelPlayerBundleOptions &options, ModelPlayerBundle *result, QString *error)
{
	if (!result)
		return false;
	play(false);
	const auto assembly = m_document.assembly();
	const auto resolved = m_resolved;
	const ModelAssemblyContext context{m_document.directory(), m_materialSource.archive, m_materialSource.paletteId};
	const auto revision = m_materialSource.revision;
	const auto source = m_document.recoverySource().isEmpty() ? m_document.path() : m_document.recoverySource();
	ModelPlayerBundle candidate;
	if (!performWork(
			Text::tr("Review Player Package"),
			[&](QString *failure, const ModelWorkControl &control) {
				return prepareModelPlayerBundle(assembly, resolved, context, options, source, &candidate, failure, control);
			},
			error))
		return false;
	m_playerBundleOptions = options;
	candidate.contextRevision = revision;
	*result = std::move(candidate);
	report(Text::tr("Player package ready for review: %Ln file(s).", nullptr, int(result->files.size())), LiveStatus::Result);
	return true;
}
bool ModelAssemblyDialog::exportPlayerBundle(const ModelPlayerBundle &bundle, const QString &path, bool overwrite,
											 PackageWriteReport *result, QString *error)
{
	if (result)
		*result = {};
	if (error)
		error->clear();
	if (bundle.recipeSha256 != modelAssemblyFingerprint(m_document.assembly()) || bundle.sourceArchive != m_materialSource.archive ||
		bundle.contextRevision != m_materialSource.revision)
	{
		const auto message = Text::tr("The assembly or package context changed. Review the player package again before exporting.");
		if (error)
			*error = message;
		if (result)
		{
			result->outputPath = QFileInfo(path).absoluteFilePath();
			result->format = PackageArchiveFormat::Pk3;
			result->blockedMessages << message;
		}
		return false;
	}
	play(false);
	PackageWriteReport written;
	const bool completed = performWork(
		Text::tr("Export Player Package"),
		[&](QString *failure, const ModelWorkControl &control) {
			written = writeModelPlayerBundle(bundle, path, overwrite, false, control);
			if (!written.succeeded())
				*failure = packageWriteReportText(written);
			return written.outputCommitted || written.succeeded();
		},
		error, true);
	if (result)
		*result = written;
	if (completed)
		report(packageWriteReportText(written));
	return completed;
}
void ModelAssemblyDialog::choosePlayerBundle()
{
	if (m_working)
		return;
	play(false);
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("playerBundleDialog"));
	dialog.setWindowTitle(Text::tr("Quake III Player Package"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto *layout = new QVBoxLayout(&dialog);
	auto *scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("playerBundleScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *body = new QWidget;
	auto *content = new QVBoxLayout(body);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	content->addLayout(form);
	const auto field = [&](QWidget *control, const char *id, const QString &label, const QString &description) {
		control->setObjectName(QString::fromLatin1(id));
		control->setAccessibleName(label);
		control->setAccessibleDescription(description);
		control->setToolTip(description);
		control->setMinimumWidth(0);
		control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		auto *text = new QLabel(label);
		text->setTextFormat(Qt::PlainText);
		text->setWordWrap(true);
		text->setBuddy(control);
		form->addRow(text, control);
	};
	auto *name = new QLineEdit(m_playerBundleOptions.modelName);
	name->setMaxLength(24);
	name->setLayoutDirection(Qt::LeftToRight);
	field(name, "playerBundleName", Text::tr("Player ID"),
		  Text::tr("Game filesystem ID under models/players. Starts with a letter; ASCII letters, digits, underscores and hyphens only."));
	auto *skin = new QLineEdit(m_playerBundleOptions.skinName);
	skin->setMaxLength(24);
	skin->setLayoutDirection(Qt::LeftToRight);
	field(skin, "playerBundleSkin", Text::tr("Skin ID"),
		  Text::tr("Name used by lower, upper and head skin files. Default is the ordinary non-team skin."));
	auto *head = new QComboBox;
	head->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	head->setMinimumContentsLength(8);
	for (const auto &part : m_document.assembly().parts)
	{
		head->addItem(part.id, part.id);
		if (part.id == m_playerBundleOptions.headPart ||
			(m_playerBundleOptions.headPart.isEmpty() && m_document.assembly().q3Animation &&
			 part.id != m_document.assembly().q3Animation->lowerPart && part.id != m_document.assembly().q3Animation->upperPart))
			head->setCurrentIndex(head->count() - 1);
	}
	field(head, "playerBundleHead", Text::tr("Head part"),
		  Text::tr("A single-pose head attached to the native upper part through tag_head. Lower and upper come from Native Animation."));
	auto *kind = new QComboBox;
	kind->addItems({Text::tr("Icon file"), Text::tr("Package icon")});
	kind->setCurrentIndex(m_playerBundleOptions.iconKind == ModelAssemblySource::File ? 0 : 1);
	field(kind, "playerBundleIconKind", Text::tr("Icon source"),
		  Text::tr("The chosen image is exported through the texture service as a TGA. Source pixels and files remain unchanged."));
	auto *icon = new QLineEdit(m_playerBundleOptions.iconSource);
	icon->setMaxLength(4096);
	icon->setLayoutDirection(Qt::LeftToRight);
	field(icon, "playerBundleIcon", Text::tr("Icon path"),
		  Text::tr("A single image up to 1024 × 1024 pixels. File paths may be relative to the assembly directory."));
	auto *index = new QSpinBox;
	index->setRange(-1, std::numeric_limits<int>::max());
	index->setSpecialValueText(Text::tr("Unique path"));
	index->setValue(m_playerBundleOptions.iconEntryIndex);
	index->setLayoutDirection(Qt::LeftToRight);
	index->setKeyboardTracking(false);
	field(
		index, "playerBundleIconIndex", Text::tr("Icon entry"),
		Text::tr("Zero-based package index. Unique path requires exactly one matching entry; an explicit index must also match the path."));
	index->setEnabled(kind->currentIndex() == 1);
	auto *browse = new QPushButton(Text::tr("Choose Icon…"));
	browse->setAutoDefault(false);
	field(browse, "playerBundleBrowseIcon", Text::tr("Choose image"),
		  Text::tr("Select a local image or an exact package entry without editing it."));
	// Skins beside the package's own: each cell is a .skin file (relative to
	// the assembly folder) or package:PATH; an empty skin cell uses the part's
	// stored variant of that name, else its own materials, and an empty icon
	// reuses the package icon.
	const auto table = [&](QTableWidget *control, const char *id, const QString &label, const QString &description, const QStringList &headers) {
		control->setColumnCount(int(headers.size()));
		control->setHorizontalHeaderLabels(headers);
		control->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
		control->setSelectionBehavior(QAbstractItemView::SelectItems);
		control->setSelectionMode(QAbstractItemView::SingleSelection);
		// Arrows move between cells; Tab leaves the table for the next control.
		control->setTabKeyNavigation(false);
		control->setTextElideMode(Qt::ElideMiddle);
		control->verticalHeader()->setVisible(false);
		// Paths share the width and elide in the middle, keeping both ends.
		control->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
		control->setLayoutDirection(Qt::LeftToRight);
		control->setMinimumHeight(control->fontMetrics().lineSpacing() * 6);
		control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		control->setObjectName(QString::fromLatin1(id));
		control->setAccessibleName(label);
		control->setAccessibleDescription(description);
		control->setToolTip(description);
		auto *text = new QLabel(label);
		text->setTextFormat(Qt::PlainText);
		text->setWordWrap(true);
		text->setBuddy(control);
		form->addRow(text);
		form->addRow(control);
	};
	auto *team = new QCheckBox(Text::tr("Team skins (red and blue)"));
	team->setChecked(m_playerBundleOptions.teamVariants);
	team->setObjectName(QStringLiteral("playerBundleTeam"));
	team->setAccessibleName(team->text());
	team->setAccessibleDescription(Text::tr("Team games look for lower_red.skin and lower_blue.skin whichever skin a player chose. Adds red and blue "
											"skins, which need red or blue materials on at least one part."));
	team->setToolTip(team->accessibleDescription());
	form->addRow(team);
	auto *skins = new QTableWidget;
	table(skins, "playerBundleSkins", Text::tr("More skins"),
		  Text::tr("One row per skin. Lower, upper and head take a .skin file or package:PATH; empty uses the part's stored skin variant of the "
				   "same name, else its own materials. Icon takes an image; empty reuses the package icon. Press F2 to edit a cell."),
		  {Text::tr("Skin"), Text::tr("Lower"), Text::tr("Upper"), Text::tr("Head"), Text::tr("Icon")});
	for (const auto &variant : m_playerBundleOptions.variants)
	{
		const int row = skins->rowCount();
		skins->insertRow(row);
		skins->setItem(row, 0, new QTableWidgetItem(variant.name));
		for (int role = 0; role < 3; ++role)
			skins->setItem(row, role + 1, new QTableWidgetItem(variant.skins[role] ? modelPlayerReferenceText(*variant.skins[role]) : QString()));
		skins->setItem(row, 4, new QTableWidgetItem(variant.icon ? modelPlayerReferenceText(*variant.icon) : QString()));
	}
	auto *skinButtons = new QHBoxLayout;
	auto *addSkin = new QPushButton(Text::tr("Add Skin"));
	auto *removeSkin = new QPushButton(Text::tr("Remove Skin"));
	addSkin->setObjectName(QStringLiteral("playerBundleAddSkin"));
	removeSkin->setObjectName(QStringLiteral("playerBundleRemoveSkin"));
	addSkin->setAccessibleDescription(Text::tr("Add a row for another skin; type its name in the Skin column."));
	removeSkin->setAccessibleDescription(Text::tr("Remove the selected skin row."));
	for (auto *control : {addSkin, removeSkin})
	{
		control->setAutoDefault(false);
		control->setAccessibleName(control->text());
		control->setToolTip(control->accessibleDescription());
		skinButtons->addWidget(control);
	}
	skinButtons->addStretch();
	form->addRow(skinButtons);
	auto *detail = new QComboBox;
	detail->addItems({Text::tr("None"), Text::tr("Generate"), Text::tr("Each part's stored detail levels")});
	detail->setCurrentIndex(int(m_playerBundleOptions.detailLevels));
	field(detail, "playerBundleDetail", Text::tr("Detail levels"),
		  Text::tr("Quake III loads lower_1.md3 and lower_2.md3 (and the same for upper and head) as lower-detail versions by distance. Generate "
				   "reduces each part; stored detail levels use each part's own recipe."));
	auto *levels = new QSpinBox;
	levels->setRange(1, 2);
	levels->setValue(std::clamp(m_playerBundleOptions.lodLevels, 1, 2));
	levels->setLayoutDirection(Qt::LeftToRight);
	field(levels, "playerBundleLodLevels", Text::tr("Generated levels"), Text::tr("How many detail levels to generate per part, up to the 2 Quake III loads."));
	auto *ratio = new QDoubleSpinBox;
	ratio->setRange(0.05, 0.95);
	ratio->setSingleStep(0.05);
	ratio->setDecimals(2);
	ratio->setValue(m_playerBundleOptions.lodRatio);
	ratio->setLayoutDirection(Qt::LeftToRight);
	field(ratio, "playerBundleLodRatio", Text::tr("Share kept per level"),
		  Text::tr("The share of the level above's triangles each generated level keeps."));
	auto *sounds = new QTableWidget;
	table(sounds, "playerBundleSounds", Text::tr("Custom sounds"),
		  Text::tr("A sound file or package:PATH per custom sound, published as sound/player/<player>/<name>.wav. Empty uses the default "
				   "model's sound. Mono PCM WAV is kept; other audio is converted to 22,050 Hz mono 16-bit PCM. Press F2 to edit a cell."),
		  {Text::tr("Sound"), Text::tr("Source")});
	for (const auto &name : modelPlayerSoundNames())
	{
		const int row = sounds->rowCount();
		sounds->insertRow(row);
		auto *label = new QTableWidgetItem(name);
		label->setFlags(label->flags() & ~Qt::ItemIsEditable);
		sounds->setItem(row, 0, label);
		const auto chosen = std::find_if(m_playerBundleOptions.sounds.cbegin(), m_playerBundleOptions.sounds.cend(),
										 [&](const auto &sound) { return sound.name == name; });
		sounds->setItem(row, 1, new QTableWidgetItem(chosen == m_playerBundleOptions.sounds.cend() ? QString() : modelPlayerReferenceText(chosen->source)));
	}
	sounds->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	auto *files = new QTreeWidget;
	files->setObjectName(QStringLiteral("playerBundleFiles"));
	files->setAccessibleName(Text::tr("Reviewed player package files"));
	files->setAccessibleDescription(Text::tr("Output path, role and byte count. Each row's details contain its content fingerprint."));
	files->setHeaderLabels({Text::tr("Package path"), Text::tr("Role"), Text::tr("Bytes")});
	files->setRootIsDecorated(false);
	files->setTextElideMode(Qt::ElideMiddle);
	files->setUniformRowHeights(true);
	files->setMinimumWidth(0);
	files->setMinimumHeight(files->fontMetrics().lineSpacing() * 8);
	files->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	files->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	files->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	content->addWidget(files);
	auto *details = new QPlainTextEdit;
	details->setObjectName(QStringLiteral("playerBundleDetails"));
	details->setAccessibleName(Text::tr("Player package review details"));
	details->setReadOnly(true);
	details->setTabChangesFocus(true);
	details->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	details->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	details->setMinimumWidth(0);
	details->setMinimumHeight(details->fontMetrics().lineSpacing() * 7);
	content->addWidget(details);
	scroll->setWidget(body);
	layout->addWidget(scroll, 1);
	auto *status = new QLabel(Text::tr("Choose the player, skin and icon, then review the package."));
	status->setObjectName(QStringLiteral("playerBundleStatus"));
	status->setWordWrap(true);
	status->setTextFormat(Qt::PlainText);
	status->setAccessibleName(Text::tr("Player package status"));
	layout->addWidget(status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	auto *review = buttons->addButton(Text::tr("Review Package"), QDialogButtonBox::ActionRole);
	auto *publish = buttons->addButton(Text::tr("Export PK3…"), QDialogButtonBox::ActionRole);
	review->setObjectName(QStringLiteral("playerBundleReview"));
	publish->setObjectName(QStringLiteral("playerBundleExport"));
	for (auto *button : {review, publish})
	{
		button->setAutoDefault(false);
		button->setAccessibleName(button->text());
	}
	review->setAccessibleDescription(
		Text::tr("Capture the native models, skins, icon and dependencies on a cancellable worker. No output is written."));
	publish->setAccessibleDescription(
		Text::tr("Publish the reviewed bytes through the atomic package writer, with protected inputs and determinism verification."));
	publish->setEnabled(false);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	std::optional<ModelPlayerBundle> prepared;
	connect(files, &QTreeWidget::itemSelectionChanged, &dialog, [&] {
		if (!prepared)
			return;
		QString text;
		if (const auto *row = files->currentItem())
			text = row->data(0, Qt::AccessibleDescriptionRole).toString() + QStringLiteral("\n\n");
		details->setPlainText(text + prepared->notes.join(QStringLiteral("\n\n")));
	});
	const auto invalidate = [&] {
		prepared.reset();
		files->clear();
		details->clear();
		publish->setEnabled(false);
		status->setText(Text::tr("Settings changed. Review the package before exporting."));
	};
	for (auto *edit : {name, skin, icon})
		connect(edit, &QLineEdit::textChanged, &dialog, invalidate);
	connect(head, &QComboBox::currentIndexChanged, &dialog, invalidate);
	connect(index, &QSpinBox::valueChanged, &dialog, invalidate);
	connect(team, &QCheckBox::toggled, &dialog, invalidate);
	connect(skins, &QTableWidget::cellChanged, &dialog, invalidate);
	connect(sounds, &QTableWidget::cellChanged, &dialog, invalidate);
	connect(levels, &QSpinBox::valueChanged, &dialog, invalidate);
	connect(ratio, &QDoubleSpinBox::valueChanged, &dialog, invalidate);
	const auto detailControls = [&] {
		levels->setEnabled(detail->currentIndex() == int(ModelPlayerDetailLevels::Generated));
		ratio->setEnabled(detail->currentIndex() == int(ModelPlayerDetailLevels::Generated));
	};
	detailControls();
	connect(detail, &QComboBox::currentIndexChanged, &dialog, [&] {
		detailControls();
		invalidate();
	});
	connect(addSkin, &QPushButton::clicked, &dialog, [&] {
		const int row = skins->rowCount();
		skins->insertRow(row);
		for (int column = 0; column < 5; ++column)
			skins->setItem(row, column, new QTableWidgetItem);
		skins->setCurrentCell(row, 0);
		skins->setFocus(Qt::OtherFocusReason);
		invalidate();
	});
	connect(removeSkin, &QPushButton::clicked, &dialog, [&] {
		if (skins->currentRow() >= 0)
		{
			skins->removeRow(skins->currentRow());
			invalidate();
		}
	});
	connect(kind, &QComboBox::currentIndexChanged, &dialog, [&] {
		index->setEnabled(kind->currentIndex() == 1);
		invalidate();
	});
	connect(browse, &QPushButton::clicked, &dialog, [&] {
		if (kind->currentIndex() == 0)
		{
			const auto chosen = QFileDialog::getOpenFileName(&dialog, Text::tr("Choose Player Icon"), icon->text(),
															 Text::tr("Images (*.png *.tga *.jpg *.jpeg *.bmp);;All files (*)"));
			if (!chosen.isEmpty())
				icon->setText(chosen);
			return;
		}
		const auto source = m_materialSource;
		if (!source.archive || !source.archive->isOpen())
		{
			status->setText(Text::tr("Open the project asset package before choosing an icon."));
			return;
		}
		ModelSkinSourceDialog picker(source.archive->entries(), -1, -1, 0, &dialog, ModelSkinSourcePurpose::Image);
		if (picker.exec() != QDialog::Accepted)
			return;
		if (source.archive != m_materialSource.archive || source.revision != m_materialSource.revision)
		{
			status->setText(Text::tr("The package changed. Choose the icon again."));
			return;
		}
		const auto reference = picker.reference();
		if (reference.entryIndex > std::numeric_limits<int>::max())
			return;
		icon->setText(reference.path);
		index->setValue(int(reference.entryIndex));
	});
	connect(review, &QPushButton::clicked, &dialog, [&] {
		invalidate();
		ModelPlayerBundleOptions options{name->text(),
										 skin->text(),
										 head->currentData().toString(),
										 icon->text(),
										 kind->currentIndex() == 0 ? ModelAssemblySource::File : ModelAssemblySource::Package,
										 kind->currentIndex() == 0 ? -1 : index->value()};
		const auto cell = [](QTableWidget *control, int row, int column) {
			const auto *item = control->item(row, column);
			return item ? item->text().trimmed() : QString();
		};
		for (int row = 0; row < skins->rowCount(); ++row)
		{
			ModelPlayerSkinVariant variant{cell(skins, row, 0), {}, {}};
			for (int role = 0; role < 3; ++role)
				if (!cell(skins, row, role + 1).isEmpty())
					variant.skins[role] = modelPlayerReference(cell(skins, row, role + 1));
			if (!cell(skins, row, 4).isEmpty())
				variant.icon = modelPlayerReference(cell(skins, row, 4));
			options.variants.append(variant);
		}
		options.teamVariants = team->isChecked();
		options.detailLevels = ModelPlayerDetailLevels(detail->currentIndex());
		options.lodLevels = levels->value();
		options.lodRatio = ratio->value();
		for (int row = 0; row < sounds->rowCount(); ++row)
			if (!cell(sounds, row, 1).isEmpty())
				options.sounds.append({cell(sounds, row, 0), modelPlayerReference(cell(sounds, row, 1))});
		body->setEnabled(false);
		buttons->setEnabled(false);
		status->setText(Text::tr("Preparing native models and dependencies…"));
		ModelPlayerBundle bundle;
		QString error;
		const bool ready = preparePlayerBundle(options, &bundle, &error);
		body->setEnabled(true);
		buttons->setEnabled(true);
		if (!ready)
		{
			status->setText(error);
			details->setPlainText(error);
			return;
		}
		for (const auto &item : bundle.files)
		{
			auto *row = new QTreeWidgetItem(files, {item.path, item.role, QString::number(item.bytes.size())});
			const auto identity = QStringLiteral("%1\nSHA-256: %2").arg(item.path, QString::fromLatin1(item.sha256.toHex()));
			row->setToolTip(0, identity);
			row->setData(0, Qt::AccessibleDescriptionRole, identity);
		}
		details->setPlainText(bundle.notes.join(QStringLiteral("\n\n")));
		status->setText(Text::tr("Ready: %Ln file(s), %1. Review the files and notes before exporting.", nullptr, int(bundle.files.size()))
							.arg(QLocale().formattedDataSize(bundle.totalBytes)));
		prepared = std::move(bundle);
		files->clearSelection();
		files->setCurrentItem(files->topLevelItem(0), 0, QItemSelectionModel::ClearAndSelect);
		publish->setEnabled(true);
	});
	connect(publish, &QPushButton::clicked, &dialog, [&] {
		if (!prepared)
			return;
		const auto path = QFileDialog::getSaveFileName(&dialog, Text::tr("Export Player Package"), prepared->options.modelName + ".pk3",
													   Text::tr("Quake III package (*.pk3)"));
		if (path.isEmpty())
			return;
		body->setEnabled(false);
		buttons->setEnabled(false);
		PackageWriteReport result;
		QString error;
		const bool completed = exportPlayerBundle(*prepared, path, QFileInfo::exists(path), &result, &error);
		body->setEnabled(true);
		buttons->setEnabled(true);
		status->setText(completed ? Text::tr("Published player package: %1").arg(result.outputPath) : error);
		details->setPlainText(packageWriteReportText(result));
		if (!completed)
			publish->setEnabled(false);
	});
	name->setFocus();
	fitDialogToScreen(&dialog, QSize(740, 800));
	dialog.exec();
}
} // namespace vibestudio
