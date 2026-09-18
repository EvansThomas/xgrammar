/*!
 *  Copyright (c) 2024 by Contributors
 * \file xgrammar/converter_ext/gemma.cc
 * \brief Implementation of the Gemma tool calling converter.
 */
#include <picojson.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "../json_schema_converter_ext.h"
#include "../support/logging.h"

namespace xgrammar {

const std::string GemmaToolCallingConverter::kGemmaStringDelim = "<|\"|>";
const std::string GemmaToolCallingConverter::kGemmaStringContent = "gemma_string_content";
const std::string GemmaToolCallingConverter::kGemmaVariableName = "gemma_variable_name";

GemmaToolCallingConverter::GemmaToolCallingConverter(
    std::optional<int> indent,
    std::optional<std::pair<std::string, std::string>> separators,
    bool any_whitespace,
    std::optional<int> max_whitespace_cnt,
    RefResolver ref_resolver,
    bool any_order
)
    : JSONSchemaConverter(
          indent, separators, any_whitespace, max_whitespace_cnt, ref_resolver, any_order
      ) {}

void GemmaToolCallingConverter::AddBasicRules() {
  JSONSchemaConverter::AddBasicRules({kGemmaStringContent, kGemmaVariableName});

  builder_.UpdateRuleBody(
      kGemmaStringContent, TagDispatch(/*loop_after_dispatch=*/false, {kGemmaStringDelim})
  );
  builder_.UpdateRuleBody(
      kGemmaVariableName,
      Sequence(
          {builder_.AddCharacterClass({{'a', 'z'}, {'A', 'Z'}, {'_', '_'}}),
           builder_.AddCharacterClassStar({{'a', 'z'}, {'A', 'Z'}, {'0', '9'}, {'_', '_'}})}
      )
  );
  // The base class writes basic_string as a JSON-quoted string directly rather than through
  // GenerateString, so the delimited form has to replace it after the base call. That leaves
  // basic_escape and basic_string_sub unreachable from root.
  builder_.UpdateRuleBody(kBasicString, GenerateString(StringSpec{}, kBasicString));
}

int32_t GemmaToolCallingConverter::GenerateString(
    const StringSpec& spec, const std::string& rule_name
) {
  int32_t delimiter = ByteString(kGemmaStringDelim);
  if (spec.format.has_value()) {
    auto regex = JSONFormatToRegexPattern(*spec.format);
    if (regex.has_value()) {
      return Sequence({delimiter, RegexExpression(*regex, false, true), delimiter});
    }
  }
  if (spec.pattern.has_value()) {
    return Sequence({delimiter, RegexExpression(*spec.pattern, /*json_string=*/false), delimiter});
  }
  if (spec.min_length != 0 || spec.max_length != -1) {
    int32_t character = builder_.AddCharacterClass({{0, 0x10FFFF}});
    int32_t body = Repeat(rule_name + "_characters", character, spec.min_length, spec.max_length);
    return Sequence({delimiter, body, delimiter});
  }
  return Sequence({delimiter, RuleRef(kGemmaStringContent), delimiter});
}

int32_t GemmaToolCallingConverter::FormatPropertyKey(
    const std::string& key, const SchemaSpecPtr& schema
) {
  return ByteString(key);
}

std::string GemmaToolCallingConverter::GetKeyPattern() const { return kGemmaVariableName; }

int32_t GemmaToolCallingConverter::GenerateLiteral(const picojson::value& value) {
  if (value.is<std::string>()) {
    const std::string& text = value.get<std::string>();
    XGRAMMAR_CHECK(text.find(kGemmaStringDelim) == std::string::npos)
        << "A gemma string literal cannot contain the string delimiter";
    return ByteString(kGemmaStringDelim + text + kGemmaStringDelim);
  }
  if (value.is<picojson::object>()) {
    const auto& object = value.get<picojson::object>();
    // The chat template renders mappings with dictsort, so literal keys are sorted.
    std::vector<std::string> keys = object.ordered_keys();
    std::sort(keys.begin(), keys.end());
    std::vector<int32_t> elements;
    elements.push_back(ByteString("{"));
    for (size_t index = 0; index < keys.size(); ++index) {
      if (index != 0) {
        elements.push_back(ByteString(","));
      }
      elements.push_back(ByteString(keys[index] + ":"));
      elements.push_back(GenerateLiteral(object.at(keys[index])));
    }
    elements.push_back(ByteString("}"));
    return Sequence(elements);
  }
  if (value.is<picojson::array>()) {
    const auto& array = value.get<picojson::array>();
    std::vector<int32_t> elements;
    elements.push_back(ByteString("["));
    for (size_t index = 0; index < array.size(); ++index) {
      if (index != 0) {
        elements.push_back(ByteString(","));
      }
      elements.push_back(GenerateLiteral(array[index]));
    }
    elements.push_back(ByteString("]"));
    return Sequence(elements);
  }
  return ByteString(value.serialize());
}

int32_t GemmaToolCallingConverter::GenerateConst(
    const ConstSpec& spec, const std::string& rule_name
) {
  picojson::value value;
  std::string error = picojson::parse(value, spec.json_value);
  XGRAMMAR_CHECK(error.empty()) << "Invalid const JSON value: " << error;
  return GenerateLiteral(value);
}

int32_t GemmaToolCallingConverter::GenerateEnum(
    const EnumSpec& spec, const std::string& rule_name
) {
  XGRAMMAR_DCHECK(!spec.json_values.empty())
      << "GenerateEnum called with empty enum spec for rule: " << rule_name;
  std::vector<int32_t> values;
  values.reserve(spec.json_values.size());
  for (const auto& json_value : spec.json_values) {
    picojson::value value;
    std::string error = picojson::parse(value, json_value);
    XGRAMMAR_CHECK(error.empty()) << "Invalid enum JSON value: " << error;
    values.push_back(GenerateLiteral(value));
  }
  return Choice(values);
}

}  // namespace xgrammar
