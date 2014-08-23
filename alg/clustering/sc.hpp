/**
 * Copyright (c) 2014, Douban Inc. 
 *   All rights reserved. 
 *
 * Distributed under the BSD License. Check out the LICENSE file for full text.
 *
 * Paracel - A distributed optimization framework with parameter server.
 *
 * Downloading
 *   git clone http://code.dapps.douban.com/paracel.git
 *
 * Authors: Hong Wu <xunzhangthu@gmail.com>
 *
 */

#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Sparse>

#include "ps.hpp"
#include "utils.hpp"
#include "load.hpp"
#include "paracel_types.hpp"

namespace paracel {

class spectral_clustering : public paracel::paralg {
 
 public:
  spectral_clustering(paracel::Comm comm,
                      std::string hosts_dct_str,
                      std::string _input,
                      std::string _output,
                      int _kclusters,
                      bool _mutual_sim = false,
                      int _rounds = 1,
                      int limit_s = 0) : 
      paracel::paralg(hosts_dct_str, comm, _output, _rounds, limit_s, true),
      input(_input),
      output(_output),
      kclusters(_kclusters),
      mutual_sim(_mutual_sim),
      rounds(_rounds) {}

  void init() {
    // load data
    auto f_parser = paracel::gen_parser(paracel::parser_b, '\t', '|');
    paracel_load_as_matrix(blk_W,
                           row_map, 
                           col_map, 
                           input, 
                           f_parser, 
                           "fmap", 
                           true);
    //std::cout << "blk_W: " << blk_W << std::endl;
    // construct similarity graph: (mutual) k-nearest neighbor graph
    if(mutual_sim) {
      // TODO
    } else {
      // by default, blk_W is a k-nearest neighbor graph
      // TODO: paracel_write_multi
      auto sim_constructer = [&] (int r, int c, double v) {
        std::string rid = row_map[r];
        std::string cid = col_map[c];
        std::string key = r < c ? "W_" + rid + "_" + cid : "W_" + cid + "_" + rid;
        paracel_write(key, v);
      };
      paracel::traverse_matrix(blk_W, sim_constructer);
      sync();
      
      auto kvmap_tmp = paracel_read_special<double>(
          "/mfs/user/wuhong/paracel/local/lib/libclustering_filter.so",
          "W_filter");
      std::unordered_map<std::string, size_t> row_reverse_map, col_reverse_map;
      for(auto & kv : row_map) {
        row_reverse_map[kv.second] = kv.first;
      }
      for(auto & kv : col_map) {
        col_reverse_map[kv.second] = kv.first;
      }
      for(auto & kv : kvmap_tmp) {
        auto lst = paracel::str_split(kv.first, '_');
        if(row_reverse_map.count(lst[1])) {
          blk_W.coeffRef(row_reverse_map[lst[1]], col_reverse_map[lst[2]]) = kv.second;
        } else if(row_reverse_map.count(lst[2])) {
          blk_W.coeffRef(row_reverse_map[lst[2]], col_reverse_map[lst[1]]) = kv.second;
        }
      }
      row_reverse_map.clear();
      col_reverse_map.clear();
    }
    
    // construct degree-matrix D, and generate W' = D ^ (-1/2) * W
    std::unordered_map<std::string, double> D;
    for(size_t k = 0; k < (size_t)blk_W.rows(); ++k) {
      std::string name = row_map[k];
      double tmp = 1. / sqrt(blk_W.row(k).sum());
      blk_W.row(k) *= tmp; // W' = D ^ (-1/2) * W
      D[name] = tmp;
      paracel_write("D_" + name, tmp);
    }
    sync();

    // construct transformation matrix W'' = W' * D ^ (-1/2)
    auto rmul_lambda = [&] (int r, int c, double & v) {  
      if(D.count(col_map[c])) {
        v *= D[col_map[c]];
      } else {
        v *= paracel_read<double>("D_" + col_map[c]);
      }
    };
    paracel::traverse_matrix(blk_W, rmul_lambda);
    if(get_worker_id() == 1) {
      std::cout << blk_W << std::endl;
    }

    // load blk_W_T
    int worker_sz = get_worker_size();
    int rank = get_worker_id();
    std::unordered_map<std::string, std::vector<std::pair<std::string, double> > > tmp_container;
    auto pack_smtx_lambda = [&] (int r, int c, double & v) {
      int col_terma = c / worker_sz;
      int col_termb = c % worker_sz;
      tmp_container[std::to_string(r + rank * worker_sz)].push_back(std::make_pair(std::to_string(col_terma + worker_sz * col_termb), v));
    };
    paracel::traverse_matrix(blk_W, pack_smtx_lambda);
    paracel_dump_dict(tmp_container, "blk_W_T_", false);
    paracel_load_as_matrix(blk_W_T, output + "blk_W_T_*", f_parser, "smap", true);
    blk_W_T = blk_W_T.transpose();
    if(get_worker_id() == 1) {
      std::cout << blk_W_T << std::endl;
    }
  } // init

  /*
  void cal_eigen_value() {
    size_t local_n = blk_W.rows();
    size_t n = blk_W.cols();
    int over_sampling = 10;
    Eigen::MatrixXd W(local_n, n); // n/np * n
    std::unordered_map<std::string, Eigen::MatrixXd> H_dct;
    // random_matrix H
    int local_h_ydim = (kclusters + over_sampleing) / get_worker_size();
    for(auto & id : row_map) {
      H_dct[id.second] = Eigen::Random(n, local_h_ydim);
    }
  }
  */

  void learning() {}

  void dump() {}

 private:
  std::string input, output;
  int kclusters;
  bool mutual_sim;
  int k, p;
  int rounds;

  Eigen::SparseMatrix<double, Eigen::RowMajor> blk_W;
  Eigen::SparseMatrix<double, Eigen::RowMajor> blk_W_T;
  std::unordered_map<size_t, std::string> row_map, col_map;

}; // class spectral_clustering

} // namespace paracel
