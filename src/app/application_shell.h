#pragma once

#include <QMainWindow>

namespace vibestudio {

class ApplicationShell final : public QMainWindow {
public:
	explicit ApplicationShell(QWidget* parent = nullptr);
};

} // namespace vibestudio
