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

#include <time.h>
#include <cmath>
#include <tuple>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Sparse>
#include <eigen3/Eigen/QR>

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

/*
    if(get_worker_id() == 0) {
      std::cout << blk_A << std::endl;
      std::cout << "---" << std::endl;
      std::cout << blk_A_T << std::endl;
    }
*/

    C = blk_A.rows();
    N = blk_A.cols(); 
    K = kclusters + over_sampling;
    K = 3;
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
  
  void random_projection() {
    //blk_H = Eigen::MatrixXd::Random(C, K);
    blk_W.resize(C, K);
    int rank = get_worker_id();
    for(int iter = 0; iter < 10; ++iter) {
      // blk_A * blk_H
      Eigen::MatrixXd tmp_W = blk_A_T * blk_H;
      // reduce blk_W
      std::vector<double> tmp_W_vec = paracel::mat2vec(tmp_W.transpose());
      paracel_bupdate("iter_W", 
                       tmp_W_vec, 
                       "/mfs/user/wuhong/paracel/local/lib/libclustering_update.so",
                       "local_update_sc");
      sync();
      // load and select to create blk_W
      tmp_W_vec = paracel_read<std::vector<double> >("iter_W");
      size_t it_begin, it_end;
      if(rank == 0) {
        it_begin = 0;
      } else {
        it_begin = global_indx[rank - 1] * K;
      }
      it_end = global_indx[rank] * K;
      std::vector<double> blk_W_vec(tmp_W_vec.begin() + it_begin, tmp_W_vec.begin() + it_end);
      blk_W = paracel::vec2mat(blk_W_vec, C);

      // blk_A_T * blk_W
      Eigen::MatrixXd tmp_H = blk_A.transpose() * blk_W;
      // reduce blk_H
      std::vector<double> tmp_H_vec = paracel::mat2vec(tmp_H.transpose());
      paracel_bupdate("iter_H",
                      tmp_H_vec,
                      "/mfs/user/wuhong/paracel/local/lib/libclustering_update.so",
                      "local_update_sc");
      sync();
      tmp_H_vec = paracel_read<std::vector<double> >("iter_H");
      std::vector<double> blk_H_vec(tmp_H_vec.begin() + it_begin, tmp_H_vec.begin() + it_end);
      blk_H = paracel::vec2mat(blk_H_vec, C);
      
      Eigen::MatrixXd global_H = paracel::vec2mat(tmp_H_vec, N); // N * K
      Eigen::HouseholderQR<Eigen::MatrixXd> qr(global_H.transpose() * global_H);
      Eigen::MatrixXd R = qr.matrixQR().triangularView<Eigen::Upper>();
      
      global_H = global_H * R.inverse(); 
      size_t start_row_indx;
      if(rank == 0) {
        start_row_indx = 0;
      } else {
        start_row_indx = global_indx[rank - 1];
      }
      blk_H = global_H.block(start_row_indx, 0, C, K);
    }
    // last blk_A * blk_H
    Eigen::MatrixXd tmp_W = blk_A_T * blk_H;
    // reduce blk_W
    std::vector<double> tmp_W_vec = paracel::mat2vec(tmp_W.transpose());
    paracel_bupdate("iter_W", 
                    tmp_W_vec, 
                    "/mfs/user/wuhong/paracel/local/lib/libclustering_update.so",
                    "local_update_sc");
    sync();
    // load and select to create blk_W
    tmp_W_vec = paracel_read<std::vector<double> >("iter_W");
    size_t it_begin, it_end;
    if(rank == 0) {
      it_begin = 0;
    } else {
      it_begin = global_indx[rank - 1] * K;
    }
    it_end = global_indx[rank] * K;
    std::vector<double> blk_W_vec(tmp_W_vec.begin() + it_begin, tmp_W_vec.begin() + it_end);
    blk_W = paracel::vec2mat(blk_W_vec, C);

  }

  // compute then return H' * H, where H is a n by k dense matrix
  // H_blk: columns block of H'(rows block of H)
  Eigen::MatrixXd 
  parallel_mm_kxn_dense_by_nxk_dense(const Eigen::MatrixXd & H_blk,
                                     const std::string & keyname) {
    std::cout.precision(20);
    Eigen::MatrixXd local_result = H_blk.transpose() * H_blk;
    std::vector<double> local_result_vec = paracel::mat2vec(local_result.transpose()); 
    paracel_bupdate(keyname,
                    local_result_vec,
                    "/mfs/user/wuhong/paracel/local/lib/libclustering_update.so",
                    "local_update_sc");
    sync();
    std::vector<double> local_result_vec_new = paracel_read<std::vector<double> >(keyname);
    return paracel::vec2mat(local_result_vec_new, K);
  }

  // compute mA * mH, where mA is a n by n sparse matrix 
  // and mH is a n by k dense matrix
  // return rows block of mA * mH
  // A_blk: columns block of mA, H_blk: rows block of mH
  Eigen::MatrixXd 
  parallel_mm_nxn_sparse_by_nxk_dense(const Eigen::SparseMatrix<double, Eigen::RowMajor> & A_blk,
                                      const Eigen::MatrixXd & H_blk,
                                      const std::string & keyname) {
    int blk_size = A_blk.rows() / get_worker_size(); 
    int np = get_worker_size();
    int rk = get_worker_id();
    for(int k = 0; k < np; ++k) {
      int cols = A_blk.cols();
      int rows = blk_size;
      if(k == np - 1) {
        rows += A_blk.rows() % np;
      }
      Eigen::MatrixXd part_A_blk = Eigen::MatrixXd(A_blk).block(k * blk_size, 0, rows, cols);
      Eigen::MatrixXd local_result = part_A_blk * H_blk;
      std::vector<double> local_result_vec = paracel::mat2vec(local_result.transpose());
      paracel_bupdate(keyname + std::to_string(k),
                      local_result_vec,
                      "/mfs/user/wuhong/paracel/local/lib/libclustering_update.so",
                      "local_update_sc");
    } // end for
    sync();
    auto data = paracel_read<std::vector<double> >(keyname + std::to_string(rk));
    int rows = blk_size;
    if(rk == np - 1) {
      rows += A_blk.rows() % np;
    }
    return paracel::vec2mat(data, rows);
  }

  void matrix_factorization(const Eigen::SparseMatrix<double, Eigen::RowMajor> & A_blk,
                            const Eigen::SparseMatrix<double, Eigen::RowMajor> & At_blk) {
    //srand((unsigned)time(NULL));
    std::cout.precision(20);
    Eigen::MatrixXd H_blk = Eigen::MatrixXd::Random(C, K);
    sync();
    Eigen::MatrixXd W_blk(C, K);
    for(int iter = 0; iter < 10; ++iter) {
      // W = A * H * inv(H' * H)
      Eigen::MatrixXd HtH = parallel_mm_kxn_dense_by_nxk_dense(H_blk, "HtH");
      sync();
      Eigen::MatrixXd AH_blk = parallel_mm_nxn_sparse_by_nxk_dense(At_blk, H_blk, "AH_");
      W_blk = AH_blk * HtH.inverse();
      sync();
      // H = A' * W * inv(W' * W)
      Eigen::MatrixXd WtW = parallel_mm_kxn_dense_by_nxk_dense(W_blk, "WtW");
      sync();
      Eigen::MatrixXd AtW_blk = parallel_mm_nxn_sparse_by_nxk_dense(A_blk.transpose(), W_blk, "AtW_");
      H_blk = AtW_blk * WtW.inverse();
    }
    if(get_worker_id() == 1) {
      std::cout << W_blk * H_blk.transpose() << std::endl;
    }
    sync();
  }

  void qr_iteration() {}
 
  void learning() {
    //random_projection();
    if(get_worker_id() == 1) {
      std::cout << "star" << blk_A << std::endl;
      std::cout << "~~~~~" << std::endl;
    }
    sync();
    matrix_factorization(blk_A, blk_A_T);
    sync();
    qr_iteration();
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
