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
// Created by Wangyunlai.wyl on 2021/5/18.
//

#include "storage/index/index_meta.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "storage/field/field_meta.h"
#include "storage/table/table_meta.h"
#include "json/json.h"

const static Json::StaticString FIELD_NAME("name");
const static Json::StaticString FIELD_FIELD_NAME("field_name");
const static Json::StaticString FIELD_INDEX_TYPE("index_type");
const static Json::StaticString FIELD_LISTS("lists");
const static Json::StaticString FIELD_PROBES("probes");

RC IndexMeta::init(const char *name, const FieldMeta &field)
{
  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init index, name is empty.");
    return RC::INVALID_ARGUMENT;
  }

  name_  = name;
  field_ = field.name();
  return RC::SUCCESS;
}

RC IndexMeta::init(const char *name, const FieldMeta &field, IndexType type, int lists, int probes)
{
  RC rc = init(name, field);
  if (rc != RC::SUCCESS) {
    return rc;
  }
  index_type_ = type;
  lists_      = lists;
  probes_     = probes;
  return RC::SUCCESS;
}

void IndexMeta::to_json(Json::Value &json_value) const
{
  json_value[FIELD_NAME]       = name_;
  json_value[FIELD_FIELD_NAME] = field_;
  json_value[FIELD_INDEX_TYPE] = static_cast<int>(index_type_);
  if (index_type_ == IndexType::IVFFLAT) {
    json_value[FIELD_LISTS]  = lists_;
    json_value[FIELD_PROBES] = probes_;
  }
}

RC IndexMeta::from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index)
{
  const Json::Value &name_value  = json_value[FIELD_NAME];
  const Json::Value &field_value = json_value[FIELD_FIELD_NAME];
  if (!name_value.isString()) {
    LOG_ERROR("Index name is not a string. json value=%s", name_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  if (!field_value.isString()) {
    LOG_ERROR("Field name of index [%s] is not a string. json value=%s",
        name_value.asCString(), field_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  const FieldMeta *field = table.field(field_value.asCString());
  if (nullptr == field) {
    LOG_ERROR("Deserialize index [%s]: no such field: %s", name_value.asCString(), field_value.asCString());
    return RC::SCHEMA_FIELD_MISSING;
  }

  const Json::Value &type_value = json_value[FIELD_INDEX_TYPE];
  IndexType idx_type = IndexType::BTREE;
  if (type_value.isInt()) {
    idx_type = static_cast<IndexType>(type_value.asInt());
  }
  const Json::Value &lists_value = json_value[FIELD_LISTS];
  int lists = lists_value.isInt() ? lists_value.asInt() : 1;
  const Json::Value &probes_value = json_value[FIELD_PROBES];
  int probes = probes_value.isInt() ? probes_value.asInt() : 1;

  return index.init(name_value.asCString(), *field, idx_type, lists, probes);
}

const char *IndexMeta::name() const { return name_.c_str(); }

const char *IndexMeta::field() const { return field_.c_str(); }

void IndexMeta::desc(ostream &os) const
{
  os << "index name=" << name_ << ", field=" << field_
     << ", type=" << (index_type_ == IndexType::BTREE ? "btree" : "ivfflat");
  if (index_type_ == IndexType::IVFFLAT) {
    os << ", lists=" << lists_ << ", probes=" << probes_;
  }
}