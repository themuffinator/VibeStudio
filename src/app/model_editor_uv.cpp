#include "app/model_editor_dialog.h"

#include "app/model_uv_view.h"
#include "app/model_viewport.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>

namespace vibestudio
{
QString ModelEditorDialog::nativeUvStatus() const
{
	const auto &surfaces = m_document.mesh().surfaces;
	return std::any_of(surfaces.cbegin(), surfaces.cend(), [](const auto &surface) { return !surface.uvSeams.isEmpty(); })
			   ? QLatin1Char(' ') +
					 QCoreApplication::translate("VibeStudioModelEditor",
												 "Export uses resolved UVs and split indices; seam marks remain in the editable source.")
			   : QString();
}
QWidget *ModelEditorDialog::createUvView()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *tools = new QToolBar;
	tools->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV view controls"));
	m_uv = new ModelUvView;
	m_uv->setObjectName(QStringLiteral("meshUvPreview"));
	const auto action = [&](const QString &label, const QString &name)
	{
		auto *item = tools->addAction(label);
		item->setObjectName(name);
		return item;
	};
	auto *all = action(QCoreApplication::translate("VibeStudioModelEditor", "Frame All"), QStringLiteral("frameMeshUvAll"));
	auto *selection =
		action(QCoreApplication::translate("VibeStudioModelEditor", "Frame Selection"), QStringLiteral("frameMeshUvSelection"));
	auto *islands = action(QCoreApplication::translate("VibeStudioModelEditor", "Select Islands"), QStringLiteral("selectMeshUvIslands"));
	islands->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Expand selected faces, vertices, or edges to their UV islands. Marked seams and split edges separate islands."));
	m_uvPickIslands = action(QCoreApplication::translate("VibeStudioModelEditor", "Pick Islands"), QStringLiteral("pickMeshUvIslands"));
	m_uvPickIslands->setCheckable(true);
	m_uvPickIslands->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "In face mode, clicking the UV view selects a whole island. Control-click toggles it."));
	m_uvMove = action(QCoreApplication::translate("VibeStudioModelEditor", "Move"), QStringLiteral("moveMeshUvs"));
	m_uvMove->setCheckable(true);
	m_uvMove->setChecked(true);
	m_uvMove->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
													 "Drag the centre square to move selected UVs. Release applies one undo step; "
													 "Escape cancels. Surface controls provide numeric movement and snapping."));
	layout->addWidget(tools);
	layout->addWidget(m_uv, 1);
	connect(all, &QAction::triggered, m_uv, &ModelUvView::frameAll);
	connect(selection, &QAction::triggered, m_uv, &ModelUvView::frameSelection);
	connect(islands, &QAction::triggered, this, &ModelEditorDialog::selectUvIslands);
	connect(m_uvPickIslands, &QAction::toggled, this, [this] { refreshUv(); });
	connect(m_uvMove, &QAction::toggled, this, [this] { refreshUv(); });
	connect(m_uv, &ModelUvView::componentPicked, this,
			[this](int kind, int a, int b, bool toggle)
			{
				if (m_working || m_uv->isRendering() || kind < 0 || kind > 3 || (kind == 3 ? 0 : kind) != m_selectionMode->currentIndex())
				{
					return;
				}
				auto selected = m_document.selection();
				if (!toggle)
				{
					selected.vertices.clear();
					selected.faces.clear();
					selected.edges.clear();
				}
				if (kind == 3)
				{
					const auto *topology = m_uv->topology();
					if (!topology || a < 0 || a >= topology->faceIsland.size())
					{
						return;
					}
					const auto &faces = topology->islands[topology->faceIsland[a]].faces;
					const bool remove =
						toggle && std::all_of(faces.cbegin(), faces.cend(), [&](int face) { return selected.faces.contains(face); });
					for (int face : faces)
					{
						if (remove)
						{
							selected.faces.remove(face);
						}
						else
						{
							selected.faces.insert(face);
						}
					}
				}
				else if (kind == 2)
				{
					const auto edge = modelEdge(a, b);
					if (toggle && selected.edges.contains(edge))
					{
						selected.edges.remove(edge);
					}
					else
					{
						selected.edges.insert(edge);
					}
				}
				else
				{
					auto &items = kind == 1 ? selected.vertices : selected.faces;
					if (toggle && items.contains(a))
					{
						items.remove(a);
					}
					else
					{
						items.insert(a);
					}
				}
				m_document.setSelection(selected);
				refresh();
			});
	connect(m_uv, &ModelUvView::movePreviewChanged, this,
			[this](double u, double v)
			{
				m_status->setText(
					QCoreApplication::translate("VibeStudioModelEditor", "UV move preview: U %1, V %2. Release to apply; Escape cancels.")
						.arg(u, 0, 'g', 7)
						.arg(v, 0, 'g', 7));
			});
	connect(m_uv, &ModelUvView::moveActiveChanged, this,
			[this](bool active)
			{
				if (active)
				{
					m_preview->finishEditMove(false);
				}
				else if (!m_refreshing && !m_working)
				{
					refreshSelection();
				}
			});
	connect(m_uv, &ModelUvView::moveRequested, this,
			[this](double u, double v)
			{
				if (m_working)
				{
					return;
				}
				ModelEdit edit;
				edit.kind = ModelEditKind::TransformUv;
				edit.selection = m_document.selection();
				edit.uvOffset = {float(u), float(v)};
				edit.uvTranslationGrid = m_uvSnap->isChecked() ? m_uvGrid->value() : 0;
				QString error;
				if (!applyEdit(edit, &error))
				{
					m_status->setText(error);
				}
			});
	return page;
}

void ModelEditorDialog::addUvControls(QFormLayout *surface)
{
	m_uvAtlasResolution = new QSpinBox;
	m_uvAtlasResolution->setObjectName(QStringLiteral("meshUvAtlasResolution"));
	m_uvAtlasResolution->setRange(32, 4096);
	m_uvAtlasResolution->setValue(512);
	m_uvAtlasResolution->setLayoutDirection(Qt::LeftToRight);
	m_uvAtlasResolution->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV atlas width in pixels"));
	m_uvAtlasResolution->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Texture width used for chart packing and pixel padding. UVs use one texture tile; existing texture images are not repainted."));
	m_uvAtlasResolution->setAccessibleDescription(m_uvAtlasResolution->toolTip());
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Atlas Width"), m_uvAtlasResolution);
	m_uvAtlasHeight = new QSpinBox;
	m_uvAtlasHeight->setObjectName(QStringLiteral("meshUvAtlasHeight"));
	m_uvAtlasHeight->setRange(32, 4096);
	m_uvAtlasHeight->setValue(512);
	m_uvAtlasHeight->setLayoutDirection(Qt::LeftToRight);
	m_uvAtlasHeight->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV atlas height in pixels"));
	m_uvAtlasHeight->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
		"Use Same as width for a square atlas, or 32–4096 pixels for a rectangular texture. Chart shape is preserved in texture pixels. "
		"Match your texture dimensions; this does not resize images or change export skin dimensions."));
	m_uvAtlasHeight->setAccessibleDescription(m_uvAtlasHeight->toolTip());
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Atlas Height"), m_uvAtlasHeight);
	m_uvAtlasSquare = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Same as width"));
	m_uvAtlasSquare->setObjectName(QStringLiteral("meshUvAtlasSquare"));
	m_uvAtlasSquare->setChecked(true);
	m_uvAtlasSquare->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Keep atlas height equal to width"));
	m_uvAtlasSquare->setToolTip(QCoreApplication::translate("VibeStudioModelEditor", "Turn off to choose an independent atlas height for a rectangular texture."));
	m_uvAtlasSquare->setAccessibleDescription(m_uvAtlasSquare->toolTip());
	surface->addRow(m_uvAtlasSquare);
	m_uvAtlasHeight->setEnabled(false);
	m_uvAtlasPadding = new QSpinBox;
	m_uvAtlasPadding->setObjectName(QStringLiteral("meshUvAtlasPadding"));
	m_uvAtlasPadding->setRange(0, 63);
	m_uvAtlasPadding->setValue(4);
	m_uvAtlasPadding->setLayoutDirection(Qt::LeftToRight);
	m_uvAtlasPadding->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV atlas padding in pixels"));
	m_uvAtlasPadding->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Padding surrounds charts and the atlas border. Bilinear filtering space is included. Use more padding for mipmapped textures."));
	m_uvAtlasPadding->setAccessibleDescription(m_uvAtlasPadding->toolTip());
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "Atlas Padding"), m_uvAtlasPadding);
	const auto updatePadding = [this]()
	{
		if (m_uvAtlasSquare->isChecked())
		{
			m_uvAtlasHeight->setValue(m_uvAtlasResolution->value());
		}
		m_uvAtlasHeight->setEnabled(!m_uvAtlasSquare->isChecked());
		m_uvAtlasPadding->setMaximum(std::min(64, (std::min(m_uvAtlasResolution->value(), m_uvAtlasHeight->value()) - 1) / 8));
	};
	connect(m_uvAtlasResolution, &QSpinBox::valueChanged, this, updatePadding);
	connect(m_uvAtlasHeight, &QSpinBox::valueChanged, this, updatePadding);
	connect(m_uvAtlasSquare, &QCheckBox::toggled, this, updatePadding);
	m_uvUnwrap = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Unwrap and Pack Faces"));
	m_uvUnwrap->setObjectName(QStringLiteral("unwrapMeshUv"));
	m_uvUnwrap->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Create UV charts from selected faces in the displayed pose, honour marked seams, and pack one atlas. All "
								 "animation poses retain their geometry. Undo restores the previous mapping."));
	m_uvPack = new QPushButton(QCoreApplication::translate("VibeStudioModelEditor", "Pack Selected UVs"));
	m_uvPack->setObjectName(QStringLiteral("packMeshUv"));
	m_uvPack->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor",
		"Pack selected faces into one atlas while preserving island shapes, orientation, and relative UV scale. Use Select Islands first "
		"to include entire islands. Unselected UVs remain fixed and can overlap the new atlas."));
	for (auto *button : {m_uvUnwrap, m_uvPack})
	{
		button->setAccessibleName(button->text());
		button->setAccessibleDescription(button->toolTip());
		surface->addRow(button);
	}
	connect(m_uvUnwrap, &QPushButton::clicked, this, [this] { execute(ModelEditKind::UnwrapUv); });
	connect(m_uvPack, &QPushButton::clicked, this, [this] { execute(ModelEditKind::PackUv); });
	m_uvObstacleScale = new QComboBox;
	m_uvObstacleScale->setObjectName(QStringLiteral("meshUvObstacleScale"));
	m_uvObstacleScale->setMinimumContentsLength(10);
	m_uvObstacleScale->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_uvObstacleScale->addItems({QCoreApplication::translate("ModelUvObstacles", "Fit uniformly"),
		QCoreApplication::translate("ModelUvObstacles", "Keep current UV scale")});
	m_uvObstacleScale->setAccessibleName(QCoreApplication::translate("ModelUvObstacles", "Packing around fixed regions: scale"));
	m_uvObstacleScale->setToolTip(QCoreApplication::translate("ModelUvObstacles",
		"Pack Around Unselected can fit all moving islands with one shared scale, or keep their current texel scale and fail if they do not fit. Orientation and relative density are preserved."));
	m_uvObstacleScale->setAccessibleDescription(m_uvObstacleScale->toolTip());
	surface->addRow(QCoreApplication::translate("ModelUvObstacles", "Fixed-region packing"), m_uvObstacleScale);
	m_uvPackAround = new QPushButton(QCoreApplication::translate("ModelUvObstacles", "Pack Around Unselected"));
	m_uvPackAround->setObjectName(QStringLiteral("packMeshUvAround"));
	m_uvPackAround->setAccessibleName(m_uvPackAround->text());
	m_uvPackAround->setToolTip(QCoreApplication::translate("ModelUvObstacles",
		"Pack complete selected islands around fixed UV faces on this surface and other surfaces sharing a material slot. Fixed UVs must fit the 0–1 tile. Atlas dimensions and padding apply; images are not repainted. Use Select Islands first."));
	m_uvPackAround->setAccessibleDescription(m_uvPackAround->toolTip());
	surface->addRow(m_uvPackAround);
	connect(m_uvPackAround, &QPushButton::clicked, this, [this] { execute(ModelEditKind::PackUvAround); });
	m_uvPivotMode = new QComboBox;
	m_uvPivotMode->setObjectName(QStringLiteral("meshUvPivotMode"));
	m_uvPivotMode->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV transform pivot"));
	m_uvPivotMode->addItems({QCoreApplication::translate("VibeStudioModelEditor", "UV Origin"),
							 QCoreApplication::translate("VibeStudioModelEditor", "Selection Centre"),
							 QCoreApplication::translate("VibeStudioModelEditor", "Custom"),
							 QCoreApplication::translate("VibeStudioModelEditor", "Individual Islands")});
	m_uvPivotMode->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Scale and rotate around the origin, selection centre, custom coordinates or each complete island's centre. "
								 "For Individual Islands, use Select Islands first. Shared corners split across all poses. "
								 "Projection computes each pivot after projecting onto the chosen plane."));
	m_uvPivotMode->setAccessibleDescription(m_uvPivotMode->toolTip());
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "UV Pivot"), m_uvPivotMode);
	for (int axis = 0; axis < 2; ++axis)
	{
		auto *value = new QDoubleSpinBox;
		m_uvPivot[axis] = value;
		value->setObjectName(QStringLiteral("meshUvPivot%1").arg(axis));
		value->setDecimals(6);
		value->setRange(-1000000, 1000000);
		value->setEnabled(false);
		const auto label = axis == 0 ? QCoreApplication::translate("VibeStudioModelEditor", "Pivot U")
									 : QCoreApplication::translate("VibeStudioModelEditor", "Pivot V");
		value->setAccessibleName(label);
		surface->addRow(label, value);
	}
	connect(m_uvPivotMode, &QComboBox::currentIndexChanged, this,
			[this](int index)
			{
				for (auto *value : m_uvPivot)
				{
					value->setEnabled(index == int(ModelUvPivot::Custom));
				}
			});
	m_uvSnap = new QCheckBox(QCoreApplication::translate("VibeStudioModelEditor", "Snap UV Offset"));
	m_uvSnap->setObjectName(QStringLiteral("meshUvSnap"));
	m_uvSnap->setAccessibleName(m_uvSnap->text());
	m_uvSnap->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Snap numeric and dragged UV offsets to multiples of the grid step. One UV unit is one texture tile."));
	surface->addRow(m_uvSnap);
	m_uvGrid = new QDoubleSpinBox;
	m_uvGrid->setObjectName(QStringLiteral("meshUvGrid"));
	m_uvGrid->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "UV offset grid step"));
	m_uvGrid->setDecimals(6);
	m_uvGrid->setRange(0.000001, 1000000);
	m_uvGrid->setValue(0.125);
	m_uvGrid->setSingleStep(0.03125);
	m_uvGrid->setEnabled(false);
	surface->addRow(QCoreApplication::translate("VibeStudioModelEditor", "UV Grid"), m_uvGrid);
	connect(m_uvSnap, &QCheckBox::toggled, this,
			[this](bool enabled)
			{
				m_uvGrid->setEnabled(enabled);
				refreshUv();
			});
	connect(m_uvGrid, &QDoubleSpinBox::valueChanged, this, [this] { refreshUv(); });
	const auto button = [&](const QString &label, const QString &name, ModelEditKind kind)
	{
		auto *item = new QPushButton(label);
		item->setObjectName(name);
		item->setProperty("requiresMeshComponents", true);
		surface->addRow(item);
		connect(item, &QPushButton::clicked, this, [this, kind] { execute(kind); });
		return item;
	};
	auto *mark = button(QCoreApplication::translate("VibeStudioModelEditor", "Mark UV Seams"), QStringLiteral("markMeshUvSeams"),
						ModelEditKind::MarkUvSeams);
	mark->setToolTip(QCoreApplication::translate("VibeStudioModelEditor",
												 "Mark selected edges as UV island boundaries. Marks are saved in editable sources. Detach "
												 "or move selected faces to split their shared UV corners for native export."));
	button(QCoreApplication::translate("VibeStudioModelEditor", "Clear UV Seams"), QStringLiteral("clearMeshUvSeams"),
		   ModelEditKind::ClearUvSeams);
	auto *detach = button(QCoreApplication::translate("VibeStudioModelEditor", "Detach UV Faces"), QStringLiteral("detachMeshUvs"),
						  ModelEditKind::DetachUv);
	detach->setToolTip(QCoreApplication::translate(
		"VibeStudioModelEditor", "Split selected faces from unselected neighbours without moving UVs or changing poses and normals. Select "
								 "one island to detach it; clearing seam marks does not weld existing split vertices."));
}

void ModelEditorDialog::refreshUv()
{
	if (!m_uv || m_document.mesh().surfaces.isEmpty())
	{
		return;
	}
	const auto selected = m_document.selection();
	const auto &surface = m_document.mesh().surfaces[selected.surface];
	const auto material = m_preview->mdlPlaybackActive() ? m_preview->mdlPlaybackSkin()
						  : m_mdlPreview.isNull()		 ? m_surfaceImages.value(selected.surface)
														 : m_mdlPreview;
	const auto image = m_renderMode->currentIndex() == 3 && !material.isNull() ? material : m_checker;
	const bool meshMode = m_selectionMode->currentIndex() < 3 && selected.tag.isEmpty() && selected.collision.isEmpty() && selected.surfaces.isEmpty();
	findChild<QAction *>(QStringLiteral("selectMeshUvIslands"))->setEnabled(meshMode);
	const bool atlasSelection = meshMode && !selected.faces.isEmpty() && selected.vertices.isEmpty() && selected.edges.isEmpty();
	m_uvUnwrap->setEnabled(atlasSelection);
	m_uvPack->setEnabled(atlasSelection);
	m_uvPackAround->setEnabled(atlasSelection);
	m_uvObstacleScale->setEnabled(atlasSelection);
	m_uvMove->setEnabled(meshMode);
	m_uvPickIslands->setEnabled(m_selectionMode->currentIndex() == 0);
	m_uv->setPickMode(m_selectionMode->currentIndex() == 0 && m_uvPickIslands->isChecked() ? 3 : m_selectionMode->currentIndex());
	m_uv->setMoveEnabled(meshMode && m_uvMove->isChecked(), m_uvSnap->isChecked() ? m_uvGrid->value() : 0);
	m_uv->setSource(surface, selected, image);
}

void ModelEditorDialog::selectUvIslands()
{
	if (m_working || m_selectionMode->currentIndex() >= 3)
	{
		return;
	}
	QString error;
	if (!performWork(
			QCoreApplication::translate("VibeStudioModelEditor", "Select UV Islands"),
			[](ModelDocument &candidate, QString *failure, const ModelWorkControl &control)
			{
				auto selected = candidate.selection();
				const auto &surface = candidate.mesh().surfaces[selected.surface];
				ModelUvTopology topology;
				QSet<int> faces;
				if (!buildModelUvTopology(surface, &topology, failure, control) ||
					!expandModelUvIslands(surface, topology, selected.faces, selected.vertices, selected.edges, &faces, failure, control))
				{
					return false;
				}
				selected.faces = std::move(faces);
				selected.vertices.clear();
				selected.edges.clear();
				candidate.setSelection(selected);
				return true;
			},
			&error))
	{
		m_status->setText(error);
		return;
	}
	QSignalBlocker blocker(m_selectionMode);
	m_selectionMode->setCurrentIndex(0);
	refresh();
}
} // namespace vibestudio
