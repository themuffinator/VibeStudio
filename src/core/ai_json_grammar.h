#pragma once

// JSON Schema to a GBNF grammar, so a local GGUF model (core/ai_gguf_runtime.h) can only write
// answers of the shape a structured request asks for, as a provider's structured output would.
//
// Covers the subset the studio's structured requests use (aiJsonSchemaProblems): object with
// properties (all written, in "required" order, then the rest by name), array with items and
// minItems/maxItems, string (enum, const, maxLength), integer (a small minimum..maximum range is
// written out value by value), number, boolean, null, a "type" list, and anyOf/oneOf. Anything
// else becomes any JSON value. Bounds the grammar cannot hold are still checked afterwards by
// aiJsonSchemaProblems. The syntax is llama.cpp's GBNF (grammars/README.md at the pinned
// revision), with {m,n} repetition.

#include <QJsonObject>
#include <QString>

namespace vibestudio {

// Empty, with the reason, when the schema is not an object schema.
[[nodiscard]] QString aiJsonSchemaGrammar(const QJsonObject& schema, QString* error = nullptr);

} // namespace vibestudio
