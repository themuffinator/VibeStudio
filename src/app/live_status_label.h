#pragma once

#include "app/studio_accessibility.h"

#include <QLabel>

namespace vibestudio {

// A status label whose accessible name stays fixed ("Mesh editor status")
// while what it says changes. Its text is mirrored into the accessible
// description, which Qt reads out once a name is set (see setLiveStatus), and
// a result or a problem is announced.
//
// setText() and clear() here stand in for QLabel's, which are not virtual,
// for every caller that holds a LiveStatusLabel pointer. Code holding the
// label as a plain QLabel* calls setLiveStatus() instead, or the description
// would go stale.
class LiveStatusLabel : public QLabel {
public:
	explicit LiveStatusLabel(QWidget* parent = nullptr) : QLabel(parent) {}

	void setText(const QString& text, LiveStatus kind = LiveStatus::Update) { setLiveStatus(this, text, kind); }
	void clear() { setLiveStatus(this, QString()); }
};

} // namespace vibestudio
