#pragma once
#include <QJsonArray>
#include <QJsonObject>
namespace vibestudio::test
{
// Independently construct the pre-v7 point representation in legacy fixtures.
inline QJsonValue withoutAutomationSegments(const QJsonValue &value)
{
	if (value.isArray()) {
		QJsonArray result;
		for (const auto &item : value.toArray())
			result.append(withoutAutomationSegments(item));
		return result;
	}
	if (value.isObject()) {
		auto result = value.toObject();
		if (result.contains("frame") && result.contains("value") && result.contains("curve"))
			result.remove("segment");
		for (auto it = result.begin(); it != result.end(); ++it)
			it.value() = withoutAutomationSegments(it.value());
		return result;
	}
	return value;
}
} // namespace vibestudio::test
