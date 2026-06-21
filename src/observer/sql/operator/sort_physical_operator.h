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
#include "sql/expr/expression.h"
#include "sql/expr/tuple.h"
#include "common/lang/vector.h"
#include "common/lang/memory.h"

/**
 * @brief 排序物理算子
 * @ingroup PhysicalOperator
 * @details 从子算子读取所有记录，按指定表达式排序后逐条返回
 */
class SortPhysicalOperator : public PhysicalOperator
{
public:
  SortPhysicalOperator(vector<unique_ptr<Expression>> &&sort_expressions, vector<bool> &&is_asc);
  virtual ~SortPhysicalOperator() = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::SORT; }
  OpType               get_op_type() const override { return OpType::ORDERBY; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override;

private:
  vector<unique_ptr<Expression>> sort_expressions_;
  vector<bool>                   is_asc_;
  vector<unique_ptr<Tuple>>      tuples_;
  vector<int>                    sorted_indices_;
  int                            current_index_ = 0;
};
