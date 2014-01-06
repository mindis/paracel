/**
 * Copyright (c) 2014, Douban Inc. 
 *   All rights reserved. 
 *
 * Paracel - A distributed optimization framework with parameter server.
 *
 * Downloading
 *   git clone http://code.dapps.douban.com/wuhong/paracel.git
 *
 * Authors: 
 *   Hong Wu <xunzhangthu@gmail.com>
 *   Changsheng Jiang <jiangzuoyan@gmail.com>
 *
 */

/**
 * a simple version, just for paracel usage
 * full version is implemented at Douban by Changsheng Jiang
 *
 */
#include "utils/comm.hpp"

namespace paracel {

Comm::Comm(MPI_Comm comm) {
  init(comm);
}

Comm::Comm(const Comm &r) {
  init(r.m_comm);
}

Comm::Comm(Comm &&r) {
  m_comm = r.m_comm;
  r.m_comm = MPI_COMM_NULL;
  m_rk = r.m_rk;
  m_sz = r.m_sz;
}

Comm::~Comm() {
  MPI_Comm_free(&m_comm);
}

Comm& Comm::operator=(const Comm &r) {
  MPI_Comm_free(&m_comm);
  init(r.m_comm);
  return *this;
}

Comm& Comm::operator=(Comm &&r) {
  MPI_Comm_free(&m_comm);
  m_comm = r.m_comm;
  r.m_comm = MPI_COMM_NULL;
  m_rk = r.m_rk;
  m_sz = r.m_sz;
  return *this;
}

void Comm::init(MPI_Comm comm) {
  MPI_Comm_dup(comm, &m_comm);
  MPI_Comm_rank(m_comm, &m_rk);
  MPI_Comm_size(m_comm, &m_sz);
}

Comm Comm::split(int color) {
  MPI_Comm new_comm;
  int key = m_rk;
  MPI_Comm_split(m_comm, color, key, &new_comm);
  Comm comm(new_comm);
  return comm;
}

} // namespace paracel
