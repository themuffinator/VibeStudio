#pragma once

#include <QLocale>
#include <QString>
#include <QStringList>

#include <initializer_list>

namespace vibestudio {

// A number written the way the user's region writes it ("0,5" in German),
// for text people read: status lines, readouts, accessible text and table
// cells. Group separators are left out, as the spin boxes beside these values
// leave them out, so a cell and its field read the same. Edit values, the
// clipboard, saved files and command-line output keep the C locale so they
// read back exactly. Counts in sentences use %Ln instead.
[[nodiscard]] inline QString localeNumber(double value, int precision = 6, char format = 'g')
{
	QLocale locale;
	locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
	return locale.toString(value, format, precision);
}

// What sets numbers in a row apart: a comma, or a semicolon where the
// region writes a decimal comma ("0,5; 1; 2"), so the decimal comma is not
// read as the separator.
[[nodiscard]] inline QString localeNumberSeparator()
{
	return QLocale().decimalPoint() == QStringLiteral(",") ? QStringLiteral("; ") : QStringLiteral(", ");
}

// Numbers in a row, such as a position ("0.5, 1, 2").
[[nodiscard]] inline QString localeNumberList(std::initializer_list<double> values, int precision = 6, char format = 'g')
{
	QStringList parts;
	for (const double value : values) {
		parts << localeNumber(value, precision, format);
	}
	return parts.join(localeNumberSeparator());
}

} // namespace vibestudio
