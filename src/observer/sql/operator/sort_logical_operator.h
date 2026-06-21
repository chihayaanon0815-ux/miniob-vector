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

#include "sql/expr/expression.h"
#include "sql/operator/logical_operator.h"

/**
 * @brief 排序逻辑算子
 * @ingroup LogicalOperator
 * @details 对子算子的输出按指定的表达式排序
 */
class SortLogicalOperator : public LogicalOperator
{
public:
  SortLogicalOperator(vector<unique_ptr<Expression>> &&sort_expressions, vector<bool> &&is_asc);
  virtual ~SortLogicalOperator() = default;

  LogicalOperatorType type() const override { return LogicalOperatorType::SORT; }
  OpType              get_op_type() const override { return OpType::ORDERBY; }

  vector<unique_ptr<Expression>> &sort_expressions() { return sort_expressions_; }
  const vector<unique_ptr<Expression>> &sort_expressions() const { return sort_expressions_; }

  vector<bool> &is_asc() { return is_asc_; }
  const vector<bool> &is_asc() const { return is_asc_; }

private:
  vector<unique_ptr<Expression>> sort_expressions_;
  vector<bool>                   is_asc_;
};
