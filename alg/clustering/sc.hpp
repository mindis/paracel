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
#include <tuple>
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

typedef Eigen::Triplet<double> eigen_triple;

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
    paracel_load_as_matrix(blk_A,
                           row_map, 
                           col_map, 
                           input, 
                           f_parser, 
                           "fmap", 
                           true);
    // init exchange matrix P 
    std::vector<size_t> P;
    std::unordered_map<std::string, size_t> reverse_col_map;
    for(auto & kv : col_map) {
      reverse_col_map[kv.second] = kv.first;
    }
    for(size_t i = 0; i < row_map.size(); ++i) {
      std::string rid = row_map[i];
      for(auto & kv : col_map) {
        std::string cid = kv.second;
        if(cid == rid) {
          P.push_back(reverse_col_map[cid]);
        }
      }
    }
    reverse_col_map.clear();
    paracel_write("exchange_P_" + std::to_string(get_worker_id()), P);
    P.resize(0);
    sync();
    for(int k = 0; k < get_worker_size(); ++k) {
      auto tmp_P = paracel_read<std::vector<size_t> >("exchange_P_" + std::to_string(k));
      P.insert(P.end(), tmp_P.begin(), tmp_P.end());
    }
    std::vector<Eigen::Triplet<double> > nonzero_tpls;
    for(size_t i = 0; i < P.size(); ++i) {
      nonzero_tpls.push_back(Eigen::Triplet<double>(i, P[i], 1.));
    }
    exchange_P.resize(P.size(), P.size());
    exchange_P.setFromTriplets(nonzero_tpls.begin(), nonzero_tpls.end());
    nonzero_tpls.resize(0);

    // construct similarity graph: (mutual) k-nearest neighbor graph
    if(mutual_sim) {
      // TODO
    } else {
      // by default, blk_A is a k-nearest neighbor graph
      // TODO: paracel_write_multi
      auto sim_constructer = [&] (int r, int c, double v) {
        std::string rid = row_map[r];
        std::string cid = col_map[c];
        std::string key = r < c ? "W_" + rid + "_" + cid : "W_" + cid + "_" + rid;
        paracel_write(key, v);
      };
      paracel::traverse_matrix(blk_A, sim_constructer);
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
          blk_A.coeffRef(row_reverse_map[lst[1]], col_reverse_map[lst[2]]) = kv.second;
        } else if(row_reverse_map.count(lst[2])) {
          blk_A.coeffRef(row_reverse_map[lst[2]], col_reverse_map[lst[1]]) = kv.second;
        }
      }
      row_reverse_map.clear();
      col_reverse_map.clear();
    }
    
    // construct degree-matrix D, and generate W' = D ^ (-1/2) * W
    std::unordered_map<std::string, double> D;
    for(size_t k = 0; k < (size_t)blk_A.rows(); ++k) {
      std::string name = row_map[k];
      double tmp = 1. / sqrt(blk_A.row(k).sum());
      blk_A.row(k) *= tmp; // W' = D ^ (-1/2) * W
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
    paracel::traverse_matrix(blk_A, rmul_lambda);

    init_blk_A_T();
    
    if(get_worker_id() == 0) {
      std::cout << blk_A << std::endl;
      std::cout << "---" << std::endl;
      std::cout << blk_A_T << std::endl;
    }

    C = blk_A.rows();
    N = blk_A.cols(); 
    K = kclusters + over_sampling;
    paracel_write("global_C_indx_" + std::to_string(get_worker_id()), C);
    global_indx.resize(0);
    sync();
    size_t accum = 0;
    for(int k = 0; k < get_worker_size(); ++k) {
      size_t indx = paracel_read<size_t>("global_C_indx_" + std::to_string(k));
      accum += indx;
      global_indx.push_back(accum); 
    }

    if(get_worker_id() == 0) {
      std::cout << "C " << C << std::endl;
      std::cout << "N " << N << std::endl;
      std::cout << "K " << K << std::endl;
    }

  } // init

  // fmap case blk_A
  // little tricky here
  void init_blk_A_T() {
    std::vector<std::string> row_id_seq, col_id_seq;
    auto sort_lambda = [] (std::pair<size_t, std::string> a, std::pair<size_t, std::string> b) {
      return a.first < b.first;
    };
    // init col_id_seq
    std::vector<std::pair<size_t, std::string> > row_id_seq_pairs, col_id_seq_pairs;
    for(auto & kv: col_map) {
      col_id_seq_pairs.push_back(std::make_pair(kv.first, kv.second));
    }
    std::sort(col_id_seq_pairs.begin(), col_id_seq_pairs.end(), sort_lambda);
    for(auto & data : col_id_seq_pairs) {
      col_id_seq.push_back(data.second);
    }
    col_id_seq_pairs.resize(0);

    // init row_id_seq
    for(auto & kv : row_map) {
      row_id_seq_pairs.push_back(std::make_pair(kv.first, kv.second));
    }
    std::sort(row_id_seq_pairs.begin(), row_id_seq_pairs.end(), sort_lambda);
    for(auto & data : row_id_seq_pairs) {
      row_id_seq.push_back(data.second);
    }
    row_id_seq_pairs.resize(0);
    paracel_write("blk_A_T_usage_" + std::to_string(get_worker_id()), row_id_seq);
    row_id_seq.resize(0);
    sync();
    
    for(int k = 0; k < get_worker_size(); ++k) {
      auto tmp_data = paracel_read<std::vector<std::string> >("blk_A_T_usage_" + std::to_string(k));
      row_id_seq.insert(row_id_seq.end(), tmp_data.begin(), tmp_data.end());
    }
    assert(row_id_seq.size() == col_id_seq.size());
    // init row_col_map, col_row_map
    std::unordered_map<std::string, std::string> row_col_map, col_row_map;
    for(size_t i = 0; i < row_id_seq.size(); ++i) {
      row_col_map[row_id_seq[i]] = col_id_seq[i];
      col_row_map[col_id_seq[i]] = row_id_seq[i];
    }
    row_id_seq.resize(0); col_id_seq.resize(0);
    std::unordered_map<std::string, std::vector<std::pair<std::string, double> > > tmp_container;
    auto index_exchange_lambda = [&] (int r, int c, double & v) {
      std::string rid = row_map[r];
      std::string cid = col_map[c];
      tmp_container[row_col_map[rid]].push_back(std::make_pair(col_row_map[cid], v));
    };
    paracel::traverse_matrix(blk_A, index_exchange_lambda);
    paracel_dump_dict(tmp_container, "blk_A_T_", false);
    auto f_parser = paracel::gen_parser(paracel::parser_b, '\t', '|');
    paracel_load_as_matrix(blk_A_T, output + "blk_A_T_*", f_parser, "smap", true);
  }

  void random_projection() {
    blk_H = Eigen::MatrixXd::Random(C, K);
    blk_W.resize(C, K);
    int rank = get_worker_id();
    if(rank == 0) {
      std::cout << blk_H << std::endl;
    }
    for(auto & d : global_indx) {
      std::cout << d << "!!";
    }
    std::cout << std::endl;
    for(int iter = 0; iter < 10; ++iter) {
      // blk_A * blk_H
      Eigen::MatrixXd tmp_W = blk_A_T * blk_H;
      // reduce
      std::vector<double> tmp_W_vec = paracel::mat2vec(tmp_W.transpose());
      paracel_bupdate("iter_W", 
                       tmp_W_vec, 
                       "/mfs/user/wuhong/paracel/local/lib/libclustering_update.so",
                       "local_update_sc");
      sync();
      // load and select to create blk_W
      tmp_W_vec = paracel_read<std::vector<double> >("iter_W");
      size_t it_begin, it_end;
      if(get_worker_id() == 0) {
        it_begin = 0;
      } else {
        it_begin = global_indx[rank - 1] * K;
      }
      it_end = global_indx[rank] * K;
      std::vector<double> blk_W_vec(tmp_W_vec.begin() + it_begin, tmp_W_vec.begin() + it_end);
      blk_W = paracel::vec2mat(blk_W_vec, C);
      // blk_A_T * blk_W
      Eigen::MatrixXd tmp_H = blk_A.transpose() * blk_W;
      break;
    }
  }

  void qr_iteration() {}
 
  /*
  void cal_eigen_value() {
    size_t local_n = blk_A.rows();
    size_t n = blk_A.cols();
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

  void learning() {
    random_projection();
  }

  virtual void solve() {
    init();
    sync();
    learning();
  }

  void dump() {}

 private:
  std::string input, output;
  int kclusters;
  int over_sampling = 10;
  bool mutual_sim;
  int rounds;

  Eigen::SparseMatrix<double, Eigen::RowMajor> blk_A; // C * N
  Eigen::SparseMatrix<double, Eigen::RowMajor> blk_A_T; // N * C
  Eigen::SparseMatrix<double, Eigen::RowMajor> exchange_P;
  std::unordered_map<size_t, std::string> row_map, col_map;
  size_t N;
  size_t C;
  size_t K;
  Eigen::MatrixXd blk_H; // C * K
  Eigen::MatrixXd blk_W; // C * K
  std::vector<size_t> global_indx;

}; // class spectral_clustering

} // namespace paracel
