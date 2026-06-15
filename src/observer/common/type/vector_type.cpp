#include "common/type/vector_type.h"
#include "common/value.h"

#include <cstring>
#include "common/log/log.h"

#include <cstdlib>
#include <vector>

int VectorType::compare(const Value &left, const Value &right) const
{
  if (left.attr_type() != AttrType::VECTORS || right.attr_type() != AttrType::VECTORS) {
    return INT32_MAX;
  }

  if (left.length() != right.length()) {
    return INT32_MAX;
  }

  if (left.length() == 0) {
    return 0;
  }

  return std::memcmp(left.data(), right.data(), left.length());
}

RC VectorType::parse_vector(const char *text, Value &value)
{
  if (text == nullptr) {
    return RC::INVALID_ARGUMENT;
  }

  string input(text);
  size_t begin = input.find_first_not_of(" \t\r\n");
  size_t end   = input.find_last_not_of(" \t\r\n");

  if (begin == string::npos || end == string::npos || input[begin] != '[' || input[end] != ']') {
    return RC::INVALID_ARGUMENT;
  }

  string content = input.substr(begin + 1, end - begin - 1);
  vector<float> values;

  size_t pos = 0;
  while (pos < content.size()) {
    size_t comma = content.find(',', pos);
    string token = comma == string::npos ? content.substr(pos) : content.substr(pos, comma - pos);

    size_t token_begin = token.find_first_not_of(" \t\r\n");
    size_t token_end   = token.find_last_not_of(" \t\r\n");
    if (token_begin == string::npos || token_end == string::npos) {
      return RC::INVALID_ARGUMENT;
    }

    token = token.substr(token_begin, token_end - token_begin + 1);

    char *parse_end = nullptr;
    float number = strtof(token.c_str(), &parse_end);
    if (parse_end == token.c_str() || *parse_end != '\0') {
      return RC::INVALID_ARGUMENT;
    }

    values.push_back(number);

    if (comma == string::npos) {
      break;
    }
    pos = comma + 1;
  }

  if (values.empty()) {
    return RC::INVALID_ARGUMENT;
  }

  value.set_vector(reinterpret_cast<const char *>(values.data()), values.size() * sizeof(float));
  return RC::SUCCESS;
}