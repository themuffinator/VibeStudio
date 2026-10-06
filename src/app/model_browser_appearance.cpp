#include "app/application_shell.h"
#include "app/asset_views.h"
#include "app/model_preview_worker.h"
#include "app/model_skin_source_dialog.h"
#include "core/model_material_slots.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
QWidget* ApplicationShell::createModelAppearancePanel()
{
	auto* contents = new QWidget;
	contents->setObjectName("modelAppearanceContents");
	contents->setAccessibleName(tr("Model preview appearance"));
	auto* layout = new QVBoxLayout(contents);
	m_modelAppearanceForm = new QFormLayout;
	m_modelAppearanceForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
	m_modelAppearanceForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	layout->addLayout(m_modelAppearanceForm);
	const auto combo = [&](const QString& title, const char* name) {
		auto* row = new QWidget;
		auto* rowLayout = new QVBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		auto* control = new QComboBox;
		control->setObjectName(QString::fromLatin1(name));
		control->setAccessibleName(title);
		control->setLayoutDirection(Qt::LeftToRight);
		control->setMinimumContentsLength(8);
		control->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		auto* label = new QLabel(title);
		label->setWordWrap(true); label->setBuddy(control);
		rowLayout->addWidget(label); rowLayout->addWidget(control);
		m_modelAppearanceForm->addRow(row);
		return control;
	};
	m_modelAppearanceSurface = combo(tr("Preview surface"), "modelAppearanceSurface");
	m_modelAppearanceSlot = combo(tr("Material slot"), "modelAppearanceSlot");
	m_modelAppearanceMdlSkin = combo(tr("MDL skin"), "modelAppearanceMdlSkin");
	m_modelAppearanceMdlMember = combo(tr("Skin member"), "modelAppearanceMdlMember");
	const auto purpose = tr("Changes this preview only. Author material bindings and skins in the Mesh Editor.");
	for (auto* control : {m_modelAppearanceSurface, m_modelAppearanceSlot, m_modelAppearanceMdlSkin, m_modelAppearanceMdlMember}) {
		control->setAccessibleDescription(purpose); control->setToolTip(purpose);
	}
	m_modelAppearanceChoose = new QPushButton(tr("Skin File…"));
	m_modelAppearanceChoose->setObjectName("modelAppearanceChooseSkin");
	m_modelAppearanceChoose->setAccessibleName(tr("Preview package skin"));
	m_modelAppearanceChoose->setAccessibleDescription(tr("Choose an exact package entry with Quake III surface-to-shader bindings for this preview."));
	m_modelAppearanceChoose->setToolTip(m_modelAppearanceChoose->accessibleDescription());
	m_modelAppearanceForm->addRow(m_modelAppearanceChoose);
	m_modelAppearanceReset = new QPushButton(tr("Reset"));
	m_modelAppearanceReset->setObjectName("modelAppearanceReset");
	m_modelAppearanceReset->setAccessibleName(tr("Reset preview appearance"));
	m_modelAppearanceReset->setAccessibleDescription(tr("Return to the model's primary material slots and first embedded skin member."));
	m_modelAppearanceForm->addRow(m_modelAppearanceReset);
	m_modelAppearanceDetails = new QPlainTextEdit;
	m_modelAppearanceDetails->setObjectName("modelAppearanceDetails");
	m_modelAppearanceDetails->setAccessibleName(tr("Preview appearance details"));
	m_modelAppearanceDetails->setAccessibleDescription(tr("Selected material bindings or verified package skin input. Preview choices do not change exported or edited source bindings."));
	m_modelAppearanceDetails->setReadOnly(true);
	m_modelAppearanceDetails->setMinimumHeight(110);
	m_modelAppearanceDetails->setMaximumHeight(220);
	m_modelAppearanceDetails->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	m_modelAppearanceDetails->setLayoutDirection(Qt::LeftToRight);
	layout->addWidget(m_modelAppearanceDetails);
	layout->addWidget(m_modelSkinPreview, 1);
	auto* scroll = new QScrollArea;
	scroll->setObjectName("modelAppearancePanel");
	scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(contents);
	scroll->setAccessibleName(tr("Model preview appearance"));
	// Reparenting into the scroll area inherits its RTL direction. Restore the
	// logical order of technical paths and indices after ownership is established.
	for (auto* control : {m_modelAppearanceSurface, m_modelAppearanceSlot, m_modelAppearanceMdlSkin, m_modelAppearanceMdlMember}) {
		control->setLayoutDirection(Qt::LeftToRight);
	}
	m_modelAppearanceDetails->setLayoutDirection(Qt::LeftToRight);
	connect(m_modelAppearanceSurface, &QComboBox::currentIndexChanged, this, [this] {
		refreshModelAppearanceControls(); refreshModelAppearanceImage();
	});
	connect(m_modelAppearanceSlot, &QComboBox::currentIndexChanged, this, [this](int slot) {
		const int surface = m_modelAppearanceSurface->currentData().toInt();
		if (slot < 0 || surface < 0 || surface >= m_modelMesh.surfaces.size()) { return; }
		if (slot == 0) { m_modelAppearance.materialSlots.remove(surface); }
		else { m_modelAppearance.materialSlots.insert(surface, slot); }
		showSelectedModel();
	});
	connect(m_modelAppearanceMdlSkin, &QComboBox::currentIndexChanged, this, [this](int skin) {
		if (skin < 0) { return; }
		m_modelAppearance.mdlSkin = skin; m_modelAppearance.mdlMember = 0;
		showSelectedModel();
	});
	connect(m_modelAppearanceMdlMember, &QComboBox::currentIndexChanged, this, [this](int member) {
		if (member < 0) { return; }
		m_modelAppearance.mdlMember = member; showSelectedModel();
	});
	connect(m_modelAppearanceReset, &QPushButton::clicked, this, [this] {
		m_modelAppearance = {}; showSelectedModel();
	});
	connect(m_modelAppearanceChoose, &QPushButton::clicked, this, [this] {
		const auto revision = packageViewKey();
		const auto context = m_modelAppearanceContext;
		ModelSkinSourceDialog picker(packageViewArchive().entries(), 0, 0, 0.1, this, ModelSkinSourcePurpose::PreviewBindings);
		if (picker.exec() != QDialog::Accepted || revision != packageViewKey() || context != m_modelAppearanceContext) { return; }
		m_modelAppearance = {};
		m_modelAppearance.skin = picker.reference();
		showSelectedModel();
	});
	refreshModelAppearanceControls();
	return scroll;
}

void ApplicationShell::refreshModelAppearanceControls()
{
	if (!m_modelAppearanceForm) { return; }
	const QSignalBlocker surfaceBlock(m_modelAppearanceSurface), slotBlock(m_modelAppearanceSlot),
		skinBlock(m_modelAppearanceMdlSkin), memberBlock(m_modelAppearanceMdlMember);
	const int previous = m_modelAppearanceSurface->currentData().toInt();
	const bool ready = m_modelMesh.geometryAvailable;
	const bool busy = m_modelPreview && m_modelPreview->busy();
	const bool embedded = ready && m_modelMesh.mdl.enabled;
	const bool external = ready && supportsModelMaterialSlots(m_modelMesh);
	for (auto* control : {m_modelAppearanceSurface, m_modelAppearanceSlot, m_modelAppearanceMdlSkin, m_modelAppearanceMdlMember}) {
		control->setLayoutDirection(Qt::LeftToRight);
	}
	m_modelAppearanceDetails->setLayoutDirection(Qt::LeftToRight);
	m_modelAppearanceSurface->clear();
	for (int i = 0; i < m_modelMesh.surfaces.size(); ++i) {
		m_modelAppearanceSurface->addItem(QStringLiteral("\u2066%1: %2\u2069").arg(i).arg(m_modelMesh.surfaces[i].name), i);
	}
	m_modelAppearanceSurface->setCurrentIndex(std::clamp(previous, 0, std::max(0, m_modelAppearanceSurface->count() - 1)));
	m_modelAppearanceSurface->setEnabled(ready && !busy);
	m_modelAppearanceSlot->clear();
	const int surface = m_modelAppearanceSurface->currentData().toInt();
	if (external && surface >= 0 && surface < m_modelMesh.surfaces.size()) {
		const auto& skinPaths = m_modelMesh.surfaces[surface].skinPaths;
		for (int slot = 0; slot < skinPaths.size(); ++slot) {
			m_modelAppearanceSlot->addItem(QStringLiteral("\u2066%1: %2\u2069").arg(slot).arg(skinPaths[slot]), slot);
			m_modelAppearanceSlot->setItemData(slot, skinPaths[slot], Qt::ToolTipRole);
		}
		m_modelAppearanceSlot->setCurrentIndex(m_modelAppearance.materialSlots.value(surface, 0));
	}
	m_modelAppearanceSlot->setEnabled(external && !busy && !m_modelAppearance.skin && m_modelAppearanceSlot->count() > 0);
	m_modelAppearanceMdlSkin->clear();
	for (int skin = 0; skin < m_modelMesh.embeddedSkins.size(); ++skin) {
		m_modelAppearanceMdlSkin->addItem(QStringLiteral("\u2066%1: %2\u2069").arg(skin).arg(m_modelMesh.embeddedSkins[skin].name), skin);
	}
	m_modelAppearanceMdlSkin->setCurrentIndex(std::max(0, m_modelAppearance.mdlSkin));
	m_modelAppearanceMdlSkin->setEnabled(embedded && !busy);
	m_modelAppearanceMdlMember->clear();
	const int skin = m_modelAppearanceMdlSkin->currentIndex();
	if (skin >= 0 && skin < m_modelMesh.embeddedSkins.size()) {
		for (int member = 0; member < m_modelMesh.embeddedSkins[skin].indexedFrames.size(); ++member) {
			m_modelAppearanceMdlMember->addItem(QString::number(member), member);
		}
		m_modelAppearanceMdlMember->setCurrentIndex(std::max(0, m_modelAppearance.mdlMember));
	}
	m_modelAppearanceMdlMember->setEnabled(embedded && !busy);
	m_modelAppearanceChoose->setEnabled(external && !busy);
	m_modelAppearanceReset->setEnabled(ready && !busy && (!m_modelAppearance.materialSlots.isEmpty() || m_modelAppearance.skin ||
		m_modelAppearance.mdlSkin >= 0 || m_modelAppearance.mdlMember >= 0));
	m_modelAppearanceForm->setRowVisible(m_modelAppearanceSurface->parentWidget(), !embedded);
	m_modelAppearanceForm->setRowVisible(m_modelAppearanceSlot->parentWidget(), !embedded);
	m_modelAppearanceForm->setRowVisible(m_modelAppearanceChoose, !embedded);
	m_modelAppearanceForm->setRowVisible(m_modelAppearanceMdlSkin->parentWidget(), embedded);
	m_modelAppearanceForm->setRowVisible(m_modelAppearanceMdlMember->parentWidget(), embedded);
}

void ApplicationShell::refreshModelAppearanceImage()
{
	if (!m_modelSkinPreview || !m_modelAppearanceSurface) { return; }
	m_modelSkinPreview->clearImage();
	const int surface = m_modelMesh.mdl.enabled ? 0 : m_modelAppearanceSurface->currentData().toInt();
	for (const auto& material : m_modelAppearanceAssets.materials) {
		if (material.key == modelPreviewSurfaceMaterialKey(surface) && material.ready()) {
			m_modelSkinPreview->setImage(material.image, material.imagePath.isEmpty() ? m_modelMesh.sourcePath : material.imagePath);
			break;
		}
	}
}
} // namespace vibestudio
