#include "app/model_assembly_dialog.h"
#include "app/model_skin_source_dialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

#include <limits>
#include <array>

namespace vibestudio
{
namespace
{
struct SkinText
{
	Q_DECLARE_TR_FUNCTIONS(ModelAssemblyDialog)
};
void field(QFormLayout *form, const QString &name, QWidget *control, const char *id)
{
	control->setObjectName(QString::fromLatin1(id));
	control->setAccessibleName(name);
	auto *label = new QLabel(name);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	label->setBuddy(control);
	form->addRow(label, control);
}
} // namespace
void ModelAssemblyDialog::addSkinControls(QFormLayout *form)
{
	m_skinKind = new QComboBox;
	m_skinKind->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_skinKind->setMinimumContentsLength(10);
	m_skinKind->addItems({SkinText::tr("Model materials"), SkinText::tr("Skin file"), SkinText::tr("Package skin")});
	field(form, SkinText::tr("Materials"), m_skinKind, "assemblySkinKind");
	m_skinSource = new QLineEdit;
	m_skinSource->setMaxLength(4096);
	m_skinSource->setLayoutDirection(Qt::LeftToRight);
	field(form, SkinText::tr("Skin source"), m_skinSource, "assemblySkinSource");
	m_skinIndex = new QSpinBox;
	m_skinIndex->setRange(-1, std::numeric_limits<int>::max());
	m_skinIndex->setSpecialValueText(SkinText::tr("Unique path"));
	m_skinIndex->setKeyboardTracking(false);
	m_skinIndex->setLayoutDirection(Qt::LeftToRight);
	field(form, SkinText::tr("Skin entry"), m_skinIndex, "assemblySkinIndex");
	auto *browse = new QPushButton(SkinText::tr("Choose Skin…"));
	browse->setObjectName(QStringLiteral("assemblyBrowseSkin"));
	browse->setAutoDefault(false);
	browse->setAccessibleName(browse->text());
	form->addRow(browse);
	const auto help = SkinText::tr(
		"Link a Quake III .skin file to this part. Apply resolves all surface assignments on a worker. "
		"Reload Inputs reads external changes; Model materials removes the override. Original model and skin files stay unchanged.");
	for (QWidget *control : std::array<QWidget *, 4>{m_skinKind, m_skinSource, m_skinIndex, browse})
	{
		control->setToolTip(help);
		control->setAccessibleDescription(help);
	}
	m_skinIndex->setAccessibleDescription(
		SkinText::tr("Zero-based package entry index. Unique path requires exactly one matching skin. "
					 "An explicit index must also match the source path; changing package order can require choosing the skin again."));
	connect(m_skinKind, &QComboBox::currentIndexChanged, this, [this, browse] {
		m_skinSource->setEnabled(m_skinKind->currentIndex() != 0);
		m_skinIndex->setEnabled(m_skinKind->currentIndex() == 2);
		browse->setEnabled(m_skinKind->currentIndex() != 0);
	});
	connect(browse, &QPushButton::clicked, this, [this] {
		if (m_skinKind->currentIndex() == 2)
		{
			const auto source = m_materialSource;
			if (!source.archive || !source.archive->isOpen())
			{
				report(SkinText::tr("Open a package before choosing a linked skin."));
				return;
			}
			ModelSkinSourceDialog picker(source.archive->entries(), -1, -1, 0, this, ModelSkinSourcePurpose::ShaderBindings);
			if (picker.exec() != QDialog::Accepted)
				return;
			if (source.archive != m_materialSource.archive || source.revision != m_materialSource.revision)
			{
				report(SkinText::tr("The package changed while choosing a skin. Open the picker again."));
				return;
			}
			const auto reference = picker.reference();
			if (reference.entryIndex > std::numeric_limits<int>::max())
				return;
			m_skinSource->setText(reference.path);
			m_skinIndex->setValue(int(reference.entryIndex));
			return;
		}
		const auto path = QFileDialog::getOpenFileName(this, SkinText::tr("Link Assembly Skin"), m_skinSource->text(),
													   SkinText::tr("Quake III skin assignments (*.skin)"));
		if (!path.isEmpty())
			m_skinSource->setText(path);
	});
	m_skinSource->setEnabled(false);
	m_skinIndex->setEnabled(false);
	m_skinIndex->setValue(-1);
	browse->setEnabled(false);
}
void ModelAssemblyDialog::refreshSkinControls(const ModelAssemblyPart &part)
{
	m_skinKind->setCurrentIndex(part.skin ? (part.skin->sourceKind == ModelAssemblySource::File ? 1 : 2) : 0);
	m_skinSource->setText(part.skin ? part.skin->source : QString());
	m_skinIndex->setValue(part.skin ? part.skin->entryIndex : -1);
}
std::optional<ModelAssemblySkin> ModelAssemblyDialog::skinFromInspector() const
{
	if (m_skinKind->currentIndex() == 0)
		return std::nullopt;
	return ModelAssemblySkin{m_skinSource->text().trimmed(),
							 m_skinKind->currentIndex() == 1 ? ModelAssemblySource::File : ModelAssemblySource::Package,
							 m_skinKind->currentIndex() == 1 ? -1 : m_skinIndex->value()};
}
} // namespace vibestudio
