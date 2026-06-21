/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/vector_index_scan_physical_operator.h"
#include "sql/expr/expression.h"
#include "storage/index/ivfflat_index.h"
#include "storage/table/table.h"

VectorIndexScanPhysicalOperator::VectorIndexScanPhysicalOperator(
    Table *table, IvfflatIndex *index, const vector<float> &query_vector, int limit)
    : table_(table), index_(index), query_vector_(query_vector), limit_(limit)
{}

RC VectorIndexScanPhysicalOperator::open(Trx *trx)
{
  if (index_ == nullptr) {
    return RC::INVALID_ARGUMENT;
  }
  tuple_.set_schema(table_, table_->table_meta().field_metas());
  rids_ = index_->ann_search(query_vector_, limit_);
  current_pos_ = 0;
  return RC::SUCCESS;
}

RC VectorIndexScanPhysicalOperator::next()
{
  while (current_pos_ < rids_.size()) {
    RC rc = table_->get_record(rids_[current_pos_], current_record_);
    current_pos_++;
    if (rc != RC::SUCCESS) {
      continue;  // Skip records that can't be read
    }
    tuple_.set_record(&current_record_);

    // Apply predicates if any
    if (!predicates_.empty()) {
      bool pass = true;
      for (auto &pred : predicates_) {
        Value val;
        RC eval_rc = pred->get_value(tuple_, val);
        if (eval_rc != RC::SUCCESS) {
          pass = false;
          break;
        }
        if (val.attr_type() == AttrType::BOOLEANS && !val.get_boolean()) {
          pass = false;
          break;
        }
      }
      if (!pass) continue;
    }
    return RC::SUCCESS;
  }
  return RC::RECORD_EOF;
}

RC VectorIndexScanPhysicalOperator::close()
{
  rids_.clear();
  current_pos_ = 0;
  return RC::SUCCESS;
}

Tuple *VectorIndexScanPhysicalOperator::current_tuple()
{
  if (current_pos_ == 0 || current_pos_ > rids_.size()) {
    return nullptr;
  }
  return &tuple_;
}

void VectorIndexScanPhysicalOperator::set_predicates(vector<unique_ptr<Expression>> &&exprs)
{
  predicates_ = std::move(exprs);
}
