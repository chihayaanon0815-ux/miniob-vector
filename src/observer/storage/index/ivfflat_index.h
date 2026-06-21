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

#include <vector>
#include <cmath>

#include "storage/index/index.h"

/**
 * @brief IVF_Flat 向量索引
 * @ingroup Index
 * @details 基于倒排文件(Inverted File)的向量索引。
 * 使用 K-Means 聚类将向量空间划分为多个单元(cell)，
 * 查询时先找到最近的 probes 个聚类中心，再在这些聚类中
 * 进行精确距离计算。
 */
class IvfflatIndex : public Index
{
public:
  IvfflatIndex()          = default;
  virtual ~IvfflatIndex() noexcept;

  RC create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta) override;
  RC open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta) override;
  RC close();

  bool is_vector_index() override { return true; }

  RC insert_entry(const char *record, const RID *rid) override;
  RC delete_entry(const char *record, const RID *rid) override;

  IndexScanner *create_scanner(const char *left_key, int left_len, bool left_inclusive,
      const char *right_key, int right_len, bool right_inclusive) override;

  RC sync() override;

  /**
   * @brief 近似最近邻搜索
   * @param base_vector 查询向量
   * @param limit 返回的最大结果数
   * @return 按距离排序的 RID 列表
   */
  vector<RID> ann_search(const vector<float> &base_vector, size_t limit) override;

private:
  /**
   * @brief 从记录中提取向量
   */
  vector<float> extract_vector(const char *record);

  /**
   * @brief 计算两个向量的 L2 距离
   */
  static float l2_distance(const vector<float> &a, const vector<float> &b);

  /**
   * @brief K-Means 聚类
   */
  void kmeans_cluster();

  /**
   * @brief 保存索引到文件
   */
  RC save_to_file(const char *file_name);

  /**
   * @brief 从文件加载索引
   */
  RC load_from_file(const char *file_name);

private:
  bool   inited_ = false;
  Table *table_  = nullptr;
  int    lists_  = 1;
  int    probes_ = 1;
  int    dim_    = 0;

  /// 聚类中心
  vector<vector<float>> centroids_;

  /// 倒排列表：每个聚类包含一组 (RID, vector) 对
  struct CellEntry
  {
    RID           rid;
    vector<float> vec;
  };
  vector<vector<CellEntry>> inverted_lists_;

  /// 未聚类的待插入向量
  vector<pair<RID, vector<float>>> pending_inserts_;

  /// 索引文件路径
  string file_name_;

  /// 写入标志
  bool dirty_ = false;
};
