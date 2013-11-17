/**
 * Copyright (c) 2013, Douban Inc. 
 *   All rights reserved. 
 *
 * Distributed under the BSD License. Check out the LICENSE file for full text.
 *
 * Paracel - A distributed optimization framework with parameter server.
 *
 * Downloading
 *   git clone http://code.dapps.douban.com/wuhong/paracel.git
 *
 * Authors: Hong Wu <xunzhangthu@gmail.com>
 *
 */
#ifndef FILE_56636034_fc34_4f9e_4343_4111ca3b127d_HPP
#define FILE_56636034_fc34_4f9e_4343_4111ca3b127d_HPP

#include <sys/stat.h>
#include <glob.h>
#include <map>
#include "paracel_types.hpp"

namespace paracel {

typedef paracel::list_type< paracel::str_type > slst_type;

slst_type str_split(const paracel::str_type & str, char sep) {
  slst_type result;
  size_t st = 0, en = 0;
  while(1) {
    en = str.find(sep, st);
    auto s = str.substr(st, en - st);
    if(s != "") result.push_back(s);
    if(en == paracel::str_type::npos) break;
    st = en + 1;
  }
  return result;
}

slst_type str_split(const paracel::str_type & str, const paracel::str_type & seps) {
  slst_type result;
  size_t st = 0, en = 0;
  while(1) {
    en = str.find_first_of(seps, st);
    auto s = str.substr(st, en - st);
    if(s != "") result.push_back(s);
    if(en == paracel::str_type::npos) break;
    st = en + 1;
  }
  return result;
}

bool endswith(const paracel::str_type & str, const std::string & c) {
  return str.rfind(c) == (str.length() - c.length());
}

// ['a', 'c', 'b', 'a', 'a', 'b'] -> [3, 2, 1]
paracel::list_type<int> sort_and_cnt(const paracel::list_type<paracel::str_type> & in) {
  paracel::list_type<int> r;
  std::map<paracel::str_type, int> m;
  for(auto & str : in) {
    if(m.find(str) == m.end()) {
      m[str] = 1;
    } else {
      m[str] += 1;
    }
  }
  for(auto & mp : m) {
    r.push_back(mp.second);
  }
  return r;
}

bool isfile(paracel::str_type & f) {
  struct stat st;
  stat(f, &st);
  if(S_ISREG(st.st_mode)) {
    return true;
  }
  return false;
}

bool isdir(paracel::str_type & d) {
  struct stat st;
  stat(d, &st);
  if(S_ISDIR(st.st_mode)) {
    return true;
  }
  return false;
}

paracel::list_type<paracel::str_type> paracel_glob(const paracel::string & pattern) {
  glob_t glob_res;
  paracel::list_type<paracel::str_type> lst;
  glob(pattern.c_str(), GLOB_TILDE, NULL, &glob_res);
  for(size_t i = 0; i < glob_res.gl_pathc, ++i) {
    lst.push_back(std::string(glob_res.gl_pathv[i]));
  }
  globfree(&glob_result);
  return lst;
}

// expand a dictory name recursively
paracel::list_type<paracel::str_type> expand_dir_rec(const paracel::str_type & dname) {
 paracel::list_type<paracel::str_type> fl;
 if(!endswith(dname, "/")) {
   dname = dnmae + "/";
 }
 auto lst = paracel_glob(dname + "*");
 for(auto & name : lst) {
   if(isfile(name)) {
     fl.push_back(name);
   } else {
     auto tmp_lst = expand_dir_rec(name);
     fl.insert(fl.end(); tmp_lst.begin(), tmp_lst.end());
   }
 }
 return fl;
}

// fname can be 'demo.txt' or 'demo_dir'
paracel::list_type<paracel::str_type> expand(const paracel::str_type & fname) {
  paracel::list_type<paracel::str_type> fl;
  if(isfile(fname)) {
    fl.push_back(fname);
    return fl;
  } else if(isdir(fname)) {
    return expand_dir_rec(fname); 
  } else {
    // expect a reg exp: '/home/xunzhang/*.csv'
    return paracel_glob(fname);
  } 
}

// fname_lst can be ['demo.txt', 'demo_dir', ...]
paracel::list_type<paracel::str_type> expand(const paracel::list_type<paracel::str_type> & fname_lst) {
  paracel::list_type<paracel::str_type> fl;
  for(auto & name : lst) {
    if(isfile(name)) {
      fl.push_back(name);
    } else {
      auto tmp_lst = expand_dir_rec(name);
      fl.insert(fl.end(); tmp_lst.begin(), tmp_lst.end());
    }
  }
  return lst;
}

} // namespace paracel
#endif
