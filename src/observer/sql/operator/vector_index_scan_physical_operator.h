/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "sql/operator/physical_operator.h"
#include "sql/expr/tuple.h"
#include "storage/record/record_manager.h"

class Table;
class IvfflatIndex;

/**
 * @brief 向量索引扫描物理算子
 * @ingroup PhysicalOperator
 * @details 使用 IVF_Flat 索引进行近似最近邻搜索，
 * 通过 ann_search 获取候选 RID 列表，
 * 然后从表中读取对应的记录。
 */
class VectorIndexScanPhysicalOperator : public PhysicalOperator
{
public:
  VectorIndexScanPhysicalOperator(Table *table, IvfflatIndex *index,
      const vector<float> &query_vector, int limit);
  virtual ~VectorIndexScanPhysicalOperator() = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::VECTOR_INDEX_SCAN; }
  OpType               get_op_type() const override { return OpType::INDEXSCAN; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override;

  void set_predicates(vector<unique_ptr<Expression>> &&exprs);

private:
  Table                         *table_ = nullptr;
  IvfflatIndex                  *index_ = nullptr;
  vector<float>                  query_vector_;
  int                            limit_;
  vector<RID>                    rids_;
  size_t                         current_pos_ = 0;
  Record                         current_record_;  // 成员变量防止悬空指针
  RowTuple                       tuple_;
  vector<unique_ptr<Expression>> predicates_;
};
