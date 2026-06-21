/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "storage/index/ivfflat_index.h"
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/db/db.h"
#include "storage/common/meta_util.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <random>
#include <limits>

using namespace std;

IvfflatIndex::~IvfflatIndex() noexcept
{
  close();
}

vector<float> IvfflatIndex::extract_vector(const char *record)
{
  vector<float> result(dim_);
  memcpy(result.data(), record + field_meta_.offset(), dim_ * sizeof(float));
  return result;
}

float IvfflatIndex::l2_distance(const vector<float> &a, const vector<float> &b)
{
  float sum = 0;
  for (size_t i = 0; i < a.size(); i++) {
    float diff = a[i] - b[i];
    sum += diff * diff;
  }
  return sqrt(sum);
}

RC IvfflatIndex::create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  if (inited_) {
    LOG_WARN("IvfflatIndex already inited");
    return RC::INTERNAL;
  }

  RC rc = Index::init(index_meta, field_meta);
  if (rc != RC::SUCCESS) {
    LOG_WARN("Failed to init index base");
    return rc;
  }

  table_  = table;
  lists_  = index_meta.lists();
  probes_ = index_meta.probes();
  dim_    = field_meta.len() / sizeof(float);
  file_name_ = file_name;

  if (lists_ < 1) {
    LOG_WARN("Invalid lists parameter: %d", lists_);
    return RC::INVALID_ARGUMENT;
  }

  inited_ = true;
  dirty_  = false;
  LOG_INFO("IvfflatIndex created: lists=%d, probes=%d, dim=%d", lists_, probes_, dim_);
  return RC::SUCCESS;
}

RC IvfflatIndex::open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  if (inited_) {
    LOG_WARN("IvfflatIndex already inited");
    return RC::INTERNAL;
  }

  RC rc = Index::init(index_meta, field_meta);
  if (rc != RC::SUCCESS) {
    LOG_WARN("Failed to init index base");
    return rc;
  }

  table_  = table;
  lists_  = index_meta.lists();
  probes_ = index_meta.probes();
  dim_    = field_meta.len() / sizeof(float);
  file_name_ = file_name;

  rc = load_from_file(file_name);
  if (rc != RC::SUCCESS) {
    LOG_WARN("Failed to load IVF index from file: %s", file_name);
    return rc;
  }

  inited_ = true;
  dirty_  = false;
  LOG_INFO("IvfflatIndex opened: lists=%d, probes=%d, dim=%d", lists_, probes_, dim_);
  return RC::SUCCESS;
}

RC IvfflatIndex::close()
{
  if (!inited_) {
    return RC::SUCCESS;
  }

  centroids_.clear();
  inverted_lists_.clear();
  pending_inserts_.clear();
  inited_ = false;
  return RC::SUCCESS;
}

RC IvfflatIndex::insert_entry(const char *record, const RID *rid)
{
  if (!inited_) {
    LOG_WARN("IvfflatIndex not inited");
    return RC::INTERNAL;
  }

  vector<float> vec = extract_vector(record);
  pending_inserts_.emplace_back(*rid, std::move(vec));
  dirty_ = true;
  return RC::SUCCESS;
}

RC IvfflatIndex::delete_entry(const char *record, const RID *rid)
{
  // 对于 IVF 索引，删除操作较复杂
  // 简化处理：将待删除的 RID 从 inverted_lists 中移除
  if (!inited_) {
    return RC::INTERNAL;
  }

  for (auto &cell : inverted_lists_) {
    auto it = remove_if(cell.begin(), cell.end(),
        [rid](const CellEntry &entry) { return entry.rid == *rid; });
    if (it != cell.end()) {
      cell.erase(it, cell.end());
      dirty_ = true;
    }
  }

  // 也从 pending 中移除
  auto pend_it = remove_if(pending_inserts_.begin(), pending_inserts_.end(),
      [rid](const pair<RID, vector<float>> &p) { return p.first == *rid; });
  if (pend_it != pending_inserts_.end()) {
    pending_inserts_.erase(pend_it, pending_inserts_.end());
  }

  return RC::SUCCESS;
}

IndexScanner *IvfflatIndex::create_scanner(const char *left_key, int left_len, bool left_inclusive,
    const char *right_key, int right_len, bool right_inclusive)
{
  // IVF 索引不支持范围扫描
  return nullptr;
}

void IvfflatIndex::kmeans_cluster()
{
  // 收集所有向量
  vector<pair<RID, vector<float>>> all_vectors;
  for (auto &cell : inverted_lists_) {
    for (auto &entry : cell) {
      all_vectors.emplace_back(entry.rid, entry.vec);
    }
  }
  for (auto &p : pending_inserts_) {
    all_vectors.push_back(p);
  }

  if (all_vectors.empty()) {
    LOG_INFO("No vectors to cluster");
    inverted_lists_.clear();
    centroids_.clear();
    return;
  }

  // 确定实际的聚类数
  int k = min(lists_, static_cast<int>(all_vectors.size()));
  if (k < 1) k = 1;

  // 初始化聚类中心（随机选择 K 个向量）
  centroids_.resize(k);
  vector<int> indices(all_vectors.size());
  for (size_t i = 0; i < indices.size(); i++) indices[i] = static_cast<int>(i);

  // 使用 std::shuffle 随机选择
  random_device rd;
  mt19937 g(rd());
  shuffle(indices.begin(), indices.end(), g);

  for (int i = 0; i < k; i++) {
    centroids_[i] = all_vectors[indices[i]].second;
  }

  // 迭代分配-更新
  vector<int> assignments(all_vectors.size(), 0);
  vector<vector<float>> new_centroids(k);
  const int max_iterations = 20;

  for (int iter = 0; iter < max_iterations; iter++) {
    // 分配步骤
    bool changed = false;
    for (size_t i = 0; i < all_vectors.size(); i++) {
      float min_dist = numeric_limits<float>::max();
      int best = 0;
      for (int j = 0; j < k; j++) {
        float dist = l2_distance(all_vectors[i].second, centroids_[j]);
        if (dist < min_dist) {
          min_dist = dist;
          best = j;
        }
      }
      if (assignments[i] != best) {
        assignments[i] = best;
        changed = true;
      }
    }

    if (!changed) break;

    // 更新步骤
    for (int j = 0; j < k; j++) {
      new_centroids[j].assign(dim_, 0.0f);
    }
    vector<int> counts(k, 0);

    for (size_t i = 0; i < all_vectors.size(); i++) {
      int c = assignments[i];
      counts[c]++;
      for (int d = 0; d < dim_; d++) {
        new_centroids[c][d] += all_vectors[i].second[d];
      }
    }

    for (int j = 0; j < k; j++) {
      if (counts[j] > 0) {
        for (int d = 0; d < dim_; d++) {
          new_centroids[j][d] /= counts[j];
        }
        centroids_[j] = new_centroids[j];
      }
    }
  }

  // 构建倒排列表
  inverted_lists_.clear();
  inverted_lists_.resize(k);
  for (size_t i = 0; i < all_vectors.size(); i++) {
    CellEntry entry;
    entry.rid = all_vectors[i].first;
    entry.vec = all_vectors[i].second;
    inverted_lists_[assignments[i]].push_back(std::move(entry));
  }

  pending_inserts_.clear();
  LOG_INFO("K-Means clustering done: k=%d, vectors=%zu", k, all_vectors.size());
}

RC IvfflatIndex::sync()
{
  if (!inited_) {
    return RC::INTERNAL;
  }

  if (!dirty_ && !pending_inserts_.empty()) {
    // 有待插入的向量，需要重新聚类
  }

  if (dirty_ || !pending_inserts_.empty()) {
    kmeans_cluster();
    RC rc = save_to_file(file_name_.c_str());
    if (rc != RC::SUCCESS) {
      LOG_WARN("Failed to save IVF index to file: %s", file_name_.c_str());
      return rc;
    }
    dirty_ = false;
  }

  return RC::SUCCESS;
}

vector<RID> IvfflatIndex::ann_search(const vector<float> &base_vector, size_t limit)
{
  vector<RID> result;

  if (!inited_ || centroids_.empty()) {
    // 如果还没有聚类，直接扫描 pending_inserts_
    vector<pair<float, RID>> distances;
    for (auto &p : pending_inserts_) {
      float dist = l2_distance(base_vector, p.second);
      distances.emplace_back(dist, p.first);
    }
    sort(distances.begin(), distances.end(),
         [](const pair<float, RID> &a, const pair<float, RID> &b) { return a.first < b.first; });
    for (size_t i = 0; i < min(limit, distances.size()); i++) {
      result.push_back(distances[i].second);
    }
    return result;
  }

  if (base_vector.size() != static_cast<size_t>(dim_)) {
    LOG_WARN("Query vector dimension mismatch: expected %d, got %zu", dim_, base_vector.size());
    return result;
  }

  // 计算查询向量到每个聚类中心的距离
  vector<pair<float, int>> centroid_dists;
  for (size_t i = 0; i < centroids_.size(); i++) {
    float dist = l2_distance(base_vector, centroids_[i]);
    centroid_dists.emplace_back(dist, static_cast<int>(i));
  }

  // 选取最近的 probes_ 个聚类
  sort(centroid_dists.begin(), centroid_dists.end());
  int probes_to_search = min(probes_, static_cast<int>(centroids_.size()));

  // 在被选中的聚类中进行精确搜索
  vector<pair<float, RID>> candidates;
  for (int p = 0; p < probes_to_search; p++) {
    int cell_id = centroid_dists[p].second;
    if (cell_id < 0 || static_cast<size_t>(cell_id) >= inverted_lists_.size()) {
      continue;
    }
    for (auto &entry : inverted_lists_[cell_id]) {
      float dist = l2_distance(base_vector, entry.vec);
      candidates.emplace_back(dist, entry.rid);
    }
  }

  // 也搜索未聚类的向量
  for (auto &p : pending_inserts_) {
    float dist = l2_distance(base_vector, p.second);
    candidates.emplace_back(dist, p.first);
  }

  // 按距离排序，返回 Top-K
  sort(candidates.begin(), candidates.end(),
       [](const pair<float, RID> &a, const pair<float, RID> &b) { return a.first < b.first; });
  // 去重
  vector<RID> unique_result;
  for (size_t i = 0; i < candidates.size() && unique_result.size() < limit; i++) {
    bool dup = false;
    for (auto &r : unique_result) {
      if (r == candidates[i].second) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      unique_result.push_back(candidates[i].second);
    }
  }

  return unique_result;
}

RC IvfflatIndex::save_to_file(const char *file_name)
{
  if (file_name == nullptr || strlen(file_name) == 0) {
    return RC::INVALID_ARGUMENT;
  }

  ofstream ofs(file_name, ios::binary | ios::trunc);
  if (!ofs.is_open()) {
    LOG_WARN("Failed to open file for write: %s", file_name);
    return RC::IOERR_OPEN;
  }

  // 写入 header: lists, dim, probes
  ofs.write(reinterpret_cast<const char *>(&lists_), sizeof(lists_));
  ofs.write(reinterpret_cast<const char *>(&dim_), sizeof(dim_));
  ofs.write(reinterpret_cast<const char *>(&probes_), sizeof(probes_));

  // 写入聚类中心
  for (auto &centroid : centroids_) {
    ofs.write(reinterpret_cast<const char *>(centroid.data()), dim_ * sizeof(float));
  }

  // 写入每个倒排列表
  for (auto &cell : inverted_lists_) {
    int count = static_cast<int>(cell.size());
    ofs.write(reinterpret_cast<const char *>(&count), sizeof(count));
    for (auto &entry : cell) {
      // RID
      ofs.write(reinterpret_cast<const char *>(&entry.rid), sizeof(RID));
      // vector
      ofs.write(reinterpret_cast<const char *>(entry.vec.data()), dim_ * sizeof(float));
    }
  }

  ofs.close();
  LOG_INFO("IVF index saved to %s: lists=%d, dim=%d", file_name, lists_, dim_);
  return RC::SUCCESS;
}

RC IvfflatIndex::load_from_file(const char *file_name)
{
  if (file_name == nullptr || strlen(file_name) == 0) {
    return RC::INVALID_ARGUMENT;
  }

  ifstream ifs(file_name, ios::binary);
  if (!ifs.is_open()) {
    // 文件不存在可能是首次创建，不算错误
    LOG_INFO("Index file not found (may be first creation): %s", file_name);
    return RC::SUCCESS;
  }

  // 检查文件是否为空
  ifs.seekg(0, ios::end);
  if (ifs.tellg() == 0) {
    ifs.close();
    LOG_INFO("Index file is empty: %s", file_name);
    return RC::SUCCESS;
  }
  ifs.seekg(0, ios::beg);

  // 读取 header
  int file_lists, file_dim, file_probes;
  ifs.read(reinterpret_cast<char *>(&file_lists), sizeof(file_lists));
  ifs.read(reinterpret_cast<char *>(&file_dim), sizeof(file_dim));
  ifs.read(reinterpret_cast<char *>(&file_probes), sizeof(file_probes));

  if (ifs.eof() || ifs.fail()) {
    LOG_WARN("Failed to read index header from %s", file_name);
    return RC::IOERR_READ;
  }

  lists_  = file_lists;
  dim_    = file_dim;
  probes_ = file_probes;

  // 读取聚类中心
  centroids_.resize(lists_);
  for (int i = 0; i < lists_; i++) {
    centroids_[i].resize(dim_);
    ifs.read(reinterpret_cast<char *>(centroids_[i].data()), dim_ * sizeof(float));
    if (ifs.eof() || ifs.fail()) {
      // 可能 centroids 读完了
      centroids_.resize(i);
      break;
    }
  }

  // 读取倒排列表
  inverted_lists_.clear();
  while (!ifs.eof()) {
    int count = 0;
    ifs.read(reinterpret_cast<char *>(&count), sizeof(count));
    if (ifs.eof() || ifs.fail()) break;

    vector<CellEntry> cell;
    cell.reserve(count);
    for (int i = 0; i < count; i++) {
      CellEntry entry;
      ifs.read(reinterpret_cast<char *>(&entry.rid), sizeof(RID));
      if (ifs.eof() || ifs.fail()) break;
      entry.vec.resize(dim_);
      ifs.read(reinterpret_cast<char *>(entry.vec.data()), dim_ * sizeof(float));
      if (ifs.eof() || ifs.fail()) break;
      cell.push_back(std::move(entry));
    }
    inverted_lists_.push_back(std::move(cell));
  }

  ifs.close();
  LOG_INFO("IVF index loaded from %s: lists=%d, centroids=%zu, cells=%zu",
      file_name, lists_, centroids_.size(), inverted_lists_.size());
  return RC::SUCCESS;
}
