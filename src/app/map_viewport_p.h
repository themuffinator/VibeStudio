#pragma once

// Private to the MapViewport sources.

#include "app/map_viewport.h"

#include <QColor>

namespace vibestudio {

// Colour set for one paint pass. Every distinction the viewport draws is also
// carried by line weight, dash pattern or marker shape so nothing depends on
// colour alone.
struct MapViewport::Palette {
	QColor background;
	QColor gridMinor;
	QColor gridMajor;
	QColor axis;
	QColor oneSided;
	QColor twoSided;
	QColor vertex;
	QColor thing;
	QColor entity;
	QColor brush;
	QColor brushEntity;
	QColor patch;
	QColor warning;
	QColor selection;
	QColor hover;
	QColor text;
	QColor subtleText;
	QColor focus;
	QColor sectorDark;
	QColor sectorBright;
	QColor leak;
	QColor link;
	// The compiler block grid's dashed lines.
	QColor block;
};

} // namespace vibestudio
