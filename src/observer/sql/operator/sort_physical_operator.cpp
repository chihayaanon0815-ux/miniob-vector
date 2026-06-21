/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/sort_physical_operator.h"
#include "common/log/log.h"
#include "storage/record/record.h"
#include "storage/trx/trx.h"
#include <algorithm>

using namespace std;

SortPhysicalOperator::SortPhysicalOperator(
    vector<unique_ptr<Expression>> &&sort_expressions, vector<bool> &&is_asc)
    : sort_expressions_(std::move(sort_expressions)), is_asc_(std::move(is_asc))
{}

RC SortPhysicalOperator::open(Trx *trx)
{
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  PhysicalOperator *child = children_[0].get();
  RC                rc    = child->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  // Read all tuples from child and clone them
  while (true) {
    rc = child->next();
    if (rc == RC::RECORD_EOF) {
      break;
    }
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to get next tuple from child: %s", strrc(rc));
      return rc;
    }

    Tuple *child_tuple = child->current_tuple();
    if (child_tuple == nullptr) {
      break;
    }

    // Clone the child tuple using ValueListTuple::make
    auto value_list = make_unique<ValueListTuple>();
    rc = ValueListTuple::make(*child_tuple, *value_list);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to clone tuple: %s", strrc(rc));
      return rc;
    }
    tuples_.emplace_back(std::move(value_list));
    sorted_indices_.push_back(static_cast<int>(sorted_indices_.size()));
  }

  // Compute sort keys and sort
  if (!tuples_.empty() && !sort_expressions_.empty()) {
    // Pre-compute sort keys for each tuple and sort expression
    vector<vector<Value>> sort_keys;
    sort_keys.reserve(tuples_.size());

    for (auto &tuple : tuples_) {
      vector<Value> keys;
      keys.reserve(sort_expressions_.size());

      for (auto &expr : sort_expressions_) {
        Value key;
        rc = expr->get_value(*tuple, key);
        if (rc != RC::SUCCESS) {
          LOG_WARN("failed to get sort key value: %s", strrc(rc));
          return rc;
        }
        keys.emplace_back(std::move(key));
      }
      sort_keys.emplace_back(std::move(keys));
    }

    // Sort by sort keys lexicographically
    sort(sorted_indices_.begin(), sorted_indices_.end(),
        [&](int a, int b) {
          for (size_t i = 0; i < sort_expressions_.size(); i++) {
            int cmp = sort_keys[a][i].compare(sort_keys[b][i]);
            if (cmp != 0) {
              return is_asc_[i] ? (cmp < 0) : (cmp > 0);
            }
          }
          return false;  // Equal, keep original order
        });
  }

  child->close();
  current_index_ = 0;
  return RC::SUCCESS;
}

RC SortPhysicalOperator::next()
{
  if (current_index_ >= static_cast<int>(sorted_indices_.size())) {
    return RC::RECORD_EOF;
  }
  current_index_++;
  return RC::SUCCESS;
}

RC SortPhysicalOperator::close()
{
  tuples_.clear();
  sorted_indices_.clear();
  current_index_ = 0;
  return RC::SUCCESS;
}

Tuple *SortPhysicalOperator::current_tuple()
{
  if (current_index_ <= 0 || current_index_ > static_cast<int>(sorted_indices_.size())) {
    return nullptr;
  }
  int idx = sorted_indices_[current_index_ - 1];
  return tuples_[idx].get();
}
