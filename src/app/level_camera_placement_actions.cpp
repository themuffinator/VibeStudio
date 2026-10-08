#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/wrapping_action_button.h"
#include "core/level_placement.h"
#include <QAction>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPainter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace vibestudio
{

QWidget *ApplicationShell::buildLevelCameraPlacementTools()
{
	auto *panel = new QWidget;
	panel->setObjectName(QStringLiteral("levelCameraPlacementTools"));
	panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 4, 0, 0);
	auto *label = new QLabel(tr("Clearance"));
	label->setObjectName(QStringLiteral("levelCameraPlacementLabel"));
	label->setWordWrap(true);
	layout->addWidget(label);
	m_levelCameraClearance = new QDoubleSpinBox;
	m_levelCameraClearance->setObjectName(QStringLiteral("levelCameraPlacementClearance"));
	m_levelCameraClearance->setRange(0, 4096);
	m_levelCameraClearance->setDecimals(3);
	m_levelCameraClearance->setKeyboardTracking(false);
	m_levelCameraClearance->setAccessibleName(tr("Camera placement clearance"));
	m_levelCameraClearance->setToolTip(
		tr("Extra distance from the camera surface. Entity definition bounds stay outside the surface; classes without bounds use an "
		   "8-unit marker. Placement follows the map grid. Doom and Hexen things use the floor and ignore clearance."));
	label->setBuddy(m_levelCameraClearance);
	layout->addWidget(m_levelCameraClearance);
	m_levelCameraPlace = new WrappingActionButton(tr("Place at Camera Surface"));
	m_levelCameraPlace->setObjectName(QStringLiteral("levelCameraPlace"));
	m_levelCameraPlace->setFocusPolicy(Qt::TabFocus);
	m_levelCameraPlace->setAccessibleName(tr("Place selected class at camera surface"));
	m_levelCameraPlace->setToolTip(tr("Aim the camera at map geometry, select a class above, then place it on the surface at the camera "
									  "centre. Doom and Hexen things require a floor. Undo removes the placement."));
	layout->addWidget(m_levelCameraPlace);
	connect(m_levelCameraPlace, &QAbstractButton::clicked, this, &ApplicationShell::placeLevelMapPaletteAtCamera);
	connect(m_levelMapPalette, &QTreeWidget::currentItemChanged, this, [this] { refreshLevelCameraPlacementTools(); });
	refreshLevelCameraPlacementTools();
	return panel;
}

void ApplicationShell::connectLevelCameraPlacement()
{
	m_levelMap3D->setOverlayPainter(
		[this](QPainter &painter)
		{
			paintLevelCameraPortals(painter);
			if (!m_levelCameraPlace || !m_levelCameraPlace->isEnabled() || m_levelMap3D->isRendering() || !m_levelMap3D->isEnabled() ||
				m_levelMap3D->brushDrawTool() || m_levelMap3D->isLooking() || m_levelMap3D->isMovingSelection() ||
				m_levelMap3D->isResizingSelection() || m_levelMap3D->editingMove() || m_levelMap3D->surfaceStrokeActive() ||
				m_levelMap3D->materialStrokeActive())
			{
				return;
			}
			const QPointF centre(m_levelMap3D->width() * 0.5, m_levelMap3D->height() * 0.5);
			painter.save();
			// Two tones retain a visible shape on both bright and dark surfaces.
			for (bool outline : {true, false})
			{
				painter.setPen(QPen(outline ? Qt::black : Qt::white, outline ? 3.5 : 1.5));
				for (const QPointF &direction : {QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)})
				{
					painter.drawLine(centre + direction * 4, centre + direction * 10);
				}
			}
			painter.restore();
		});
}

void ApplicationShell::refreshLevelCameraPlacementTools()
{
	if (!m_levelCameraPlace)
	{
		return;
	}
	const auto *item = m_levelMapPalette->currentItem();
	const QString payload = item ? item->data(0, Qt::UserRole).toString() : QString();
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const bool doom = m_levelMapDocument.format == LevelMapFormat::DoomWad && m_levelMapDocument.doomFormat != LevelMapDoomFormat::Udmf;
	const bool ready = m_levelMap3D && m_levelMap3D->isEnabled() && levelMap3DShowing();
	const bool enabled =
		ready && ((quake && payload.startsWith(QStringLiteral("entity:"))) || (doom && payload.startsWith(QStringLiteral("thing:"))));
	m_levelCameraPlace->setEnabled(enabled);
	m_levelCameraClearance->setEnabled(quake);
	if (m_levelMap3D)
	{
		m_levelMap3D->update();
	}
	if (m_levelMapViewport)
	{
		m_levelCameraClearance->setSingleStep(m_levelMapViewport->snapToGrid() ? std::max(1, m_levelMapViewport->gridSize()) : 1);
	}
	if (m_commands)
	{
		if (auto *action = m_commands->action(QStringLiteral("map.placeAtCamera")))
		{
			action->setEnabled(enabled);
		}
	}
}

void ApplicationShell::placeLevelMapPaletteAtCamera()
{
	const auto *item = m_levelMapPalette ? m_levelMapPalette->currentItem() : nullptr;
	const QString payload = item ? item->data(0, Qt::UserRole).toString() : QString();
	if (!m_levelMap3D || !m_levelMapViewport || !levelMap3DShowing() || !m_levelMap3D->isEnabled() || payload.isEmpty())
	{
		statusBar()->showMessage(tr("Show the camera and select a class in Create before placing it."));
		return;
	}
	CameraSurfacePoint surface;
	int triangle = -1;
	if (!m_levelMap3D->surfacePointAt({m_levelMap3D->width() * 0.5, m_levelMap3D->height() * 0.5}, &surface, &triangle) || triangle < 0 ||
		triangle >= m_levelMap3DOwners.size())
	{
		statusBar()->showMessage(m_levelMap3D->isRendering()
									 ? tr("Wait for the camera to finish rendering before placing an object.")
									 : tr("No current surface at the camera centre. Finish active tools and aim at map geometry."));
		return;
	}
	const auto owner = m_levelMap3DOwners.at(triangle);
	const double grid = m_levelMapViewport->snapToGrid() ? std::max(1, m_levelMapViewport->gridSize()) : 0;
	LevelPlacementRequest request;
	BoxResizePoint origin;
	QString what;
	if (payload.startsWith(QStringLiteral("entity:")) &&
		(m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map))
	{
		if (owner.kind != LevelMapSelectionKind::QuakeBrush && owner.kind != LevelMapSelectionKind::QuakePatch)
		{
			statusBar()->showMessage(tr("Aim at a brush or patch surface to place an entity."));
			return;
		}
		request.operation = LevelPlacementOperation::AddEntity;
		request.className = payload.mid(7).trimmed();
		what = request.className;
		ResizeBox bounds{{-8, -8, -8}, {8, 8, 8}};
		EntityClassDefinition definition;
		if (m_entityDefinitions.classForName(request.className, &definition))
		{
			if (definition.kind != EntityClassKind::Point)
			{
				statusBar()->showMessage(tr("Select a point entity class in Create."));
				return;
			}
			if (definition.hasSize)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					bounds.mins[axis] = definition.mins[axis];
					bounds.maxs[axis] = definition.maxs[axis];
				}
			}
		}
		if (!cameraSurfacePlacement(surface, bounds, m_levelCameraClearance ? m_levelCameraClearance->value() : 0, grid, &origin))
		{
			statusBar()->showMessage(tr("The entity bounds, clearance or grid would place it outside the editable map range."));
			return;
		}
	}
	else if (payload.startsWith(QStringLiteral("thing:")) && m_levelMapDocument.format == LevelMapFormat::DoomWad &&
			 m_levelMapDocument.doomFormat != LevelMapDoomFormat::Udmf)
	{
		if (owner.kind != LevelMapSelectionKind::DoomSector || surface.normal[2] < 0.999 ||
			triangle >= m_levelPreviewMaterialTargets.size() ||
			m_levelPreviewMaterialTargets[triangle].kind != LevelMaterialKind::SectorFloor)
		{
			statusBar()->showMessage(tr("Aim at a Doom or Hexen sector floor to place a thing."));
			return;
		}
		request.operation = LevelPlacementOperation::AddThing;
		bool validType = false;
		request.thingType = payload.mid(6).toInt(&validType);
		if (!validType || request.thingType <= 0)
		{
			statusBar()->showMessage(tr("Select a valid thing type in Create."));
			return;
		}
		what = tr("Thing %1").arg(request.thingType);
		origin = surface.position;
		if (grid > 0)
		{
			origin[0] = snapLevelMapCoordinate(origin[0], grid);
			origin[1] = snapLevelMapCoordinate(origin[1], grid);
		}
		// Binary Doom/Hexen stores integer coordinates even with snapping off.
		// Validate the final stored location, not its fractional camera sample.
		origin[0] = std::round(origin[0]);
		origin[1] = std::round(origin[1]);
		QPointF snappedScreen;
		CameraSurfacePoint snappedSurface;
		int snappedTriangle = -1;
		if (!m_levelMap3D->projectToView({float(origin[0]), float(origin[1]), float(origin[2])}, &snappedScreen) ||
			!m_levelMap3D->surfacePointAt(snappedScreen, &snappedSurface, &snappedTriangle) || snappedTriangle < 0 ||
			snappedTriangle >= m_levelPreviewMaterialTargets.size() ||
			m_levelPreviewMaterialTargets[snappedTriangle].kind != LevelMaterialKind::SectorFloor ||
			snappedTriangle >= m_levelMap3DOwners.size() || m_levelMap3DOwners[snappedTriangle] != owner)
		{
			statusBar()->showMessage(
				tr("The snapped position leaves the visible floor. Reduce the grid or aim further inside the sector."));
			return;
		}
	}
	else
	{
		statusBar()->showMessage(tr("Select a class supported by the open map."));
		return;
	}
	request.offset = {origin[0], origin[1], origin[2], true};
	QString error;
	if (!runLevelPlacementFromUi(request, &error))
	{
		statusBar()->showMessage(tr("Could not place at the camera surface: %1").arg(error));
		return;
	}
	if (request.operation == LevelPlacementOperation::AddEntity)
	{
		m_lastAddedEntityClass = request.className;
	}
	else
	{
		m_lastAddedThingType = request.thingType;
	}
	recordActivity(tr("Object placed at camera surface"), what, QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Placed %1 at %2 %3 %4. Undo removes it; save the map before building.")
								 .arg(what)
								 .arg(origin[0], 0, 'g', 8)
								 .arg(origin[1], 0, 'g', 8)
								 .arg(origin[2], 0, 'g', 8));
}

} // namespace vibestudio
