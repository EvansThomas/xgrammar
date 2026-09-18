/*!
 *  Copyright (c) 2024 by Contributors
 * \file xgrammar/converter_ext/gemma.cc
 * \brief Implementation of the Gemma tool calling converter.
 */
#include <string>
#include <vector>

#include "../json_schema_converter_ext.h"

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

}  // namespace xgrammar
