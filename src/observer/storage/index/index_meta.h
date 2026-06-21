/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2021/5/12.
//

#pragma once

#include "common/sys/rc.h"
#include "common/lang/string.h"

class TableMeta;
class FieldMeta;

namespace Json {
class Value;
}  // namespace Json

/**
 * @brief 索引类型
 */
enum class IndexType
{
  BTREE,
  IVFFLAT,
};

/**
 * @brief 描述一个索引
 * @ingroup Index
 * @details 一个索引包含了表的哪些字段，索引的名称等。
 * 如果以后实现了多种类型的索引，还需要记录索引的类型，对应类型的一些元数据等
 */
class IndexMeta
{
public:
  IndexMeta() = default;

  RC init(const char *name, const FieldMeta &field);
  RC init(const char *name, const FieldMeta &field, IndexType type, int lists, int probes);

public:
  const char   *name() const;
  const char   *field() const;
  IndexType     index_type() const { return index_type_; }
  int           lists() const { return lists_; }
  int           probes() const { return probes_; }

  void desc(ostream &os) const;

public:
  void      to_json(Json::Value &json_value) const;
  static RC from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index);

protected:
  string    name_;
  string    field_;
  IndexType index_type_ = IndexType::BTREE;
  int       lists_      = 1;
  int       probes_     = 1;
};
