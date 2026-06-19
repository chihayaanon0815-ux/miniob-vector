#include "common/type/vector_type.h"
#include "common/value.h"

#include <cmath>
#include <cstring>
#include "common/log/log.h"
#include "common/lang/string.h"

#include <cstdlib>
#include <vector>

static RC check_vector_value(const Value &value, const float *&data, int &dimension)
{
  if (value.attr_type() != AttrType::VECTORS) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  if (value.length() <= 0 || value.length() % static_cast<int>(sizeof(float)) != 0 || value.data() == nullptr) {
    return RC::INVALID_ARGUMENT;
  }

  data      = reinterpret_cast<const float *>(value.data());
  dimension = value.length() / static_cast<int>(sizeof(float));
  return RC::SUCCESS;
}

static RC normalize_distance_method(string method, string &normalized)
{
  common::strip(method);
  common::str_to_upper(method);

  if (method == "EUCLIDEAN" || method == "L2" || method == "L2_DISTANCE") {
    normalized = "EUCLIDEAN";
  } else if (method == "COSINE" || method == "COSINE_DISTANCE") {
    normalized = "COSINE";
  } else if (method == "DOT" || method == "INNER_PRODUCT") {
    normalized = "DOT";
  } else {
    return RC::INVALID_ARGUMENT;
  }

  return RC::SUCCESS;
}

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

RC VectorType::vector_to_string(const Value &value, string &result)
{
  const float *data      = nullptr;
  int          dimension = 0;
  RC           rc        = check_vector_value(value, data, dimension);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  result = "[";
  for (int i = 0; i < dimension; i++) {
    if (i != 0) {
      result += ",";
    }
    result += common::double_to_str(data[i]);
  }
  result += "]";
  return RC::SUCCESS;
}

RC VectorType::distance(const Value &left, const Value &right, const string &method, Value &result)
{
  const float *left_data  = nullptr;
  const float *right_data = nullptr;
  int          dimension  = 0;
  int          right_dim  = 0;

  RC rc = check_vector_value(left, left_data, dimension);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  rc = check_vector_value(right, right_data, right_dim);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  if (dimension != right_dim) {
    return RC::INVALID_ARGUMENT;
  }

  string normalized;
  rc = normalize_distance_method(method, normalized);
  if (rc != RC::SUCCESS) {
    return rc;
  }

  double value = 0.0;
  if (normalized == "EUCLIDEAN") {
    double sum = 0.0;
    for (int i = 0; i < dimension; i++) {
      const double diff = static_cast<double>(left_data[i]) - static_cast<double>(right_data[i]);
      sum += diff * diff;
    }
    value = std::sqrt(sum);
  } else if (normalized == "DOT") {
    for (int i = 0; i < dimension; i++) {
      value += static_cast<double>(left_data[i]) * static_cast<double>(right_data[i]);
    }
  } else if (normalized == "COSINE") {
    double dot        = 0.0;
    double left_norm  = 0.0;
    double right_norm = 0.0;
    for (int i = 0; i < dimension; i++) {
      const double left_val  = static_cast<double>(left_data[i]);
      const double right_val = static_cast<double>(right_data[i]);
      dot += left_val * right_val;
      left_norm += left_val * left_val;
      right_norm += right_val * right_val;
    }

    if (left_norm == 0.0 || right_norm == 0.0) {
      return RC::INVALID_ARGUMENT;
    }

    value = 1.0 - dot / (std::sqrt(left_norm) * std::sqrt(right_norm));
  } else {
    return RC::INVALID_ARGUMENT;
  }

  result.set_float(static_cast<float>(value));
  return RC::SUCCESS;
}
