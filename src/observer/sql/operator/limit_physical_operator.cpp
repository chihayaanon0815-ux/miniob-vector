/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/limit_physical_operator.h"

RC LimitPhysicalOperator::open(Trx *trx)
{
  emitted_ = 0;
  if (children_.empty()) {
    return RC::SUCCESS;
  }
  return children_[0]->open(trx);
}

RC LimitPhysicalOperator::next()
{
  if (emitted_ >= limit_num_) {
    return RC::RECORD_EOF;
  }
  if (children_.empty()) {
    return RC::RECORD_EOF;
  }
  RC rc = children_[0]->next();
  if (rc == RC::SUCCESS) {
    emitted_++;
  }
  return rc;
}

RC LimitPhysicalOperator::close()
{
  emitted_ = 0;
  if (!children_.empty()) {
    children_[0]->close();
  }
  return RC::SUCCESS;
}

Tuple *LimitPhysicalOperator::current_tuple()
{
  if (children_.empty()) {
    return nullptr;
  }
  return children_[0]->current_tuple();
}
