#include <iostream>
#include <string>
#include <vector>
#include <tr1/unordered_map>
#include "paracel_types.hpp"
#include "packer.hpp"
#include "utils/ext_utility.hpp"

// terminate function for recursive variadic template
template<class T>
paracel::str_type paste(const T & arg) {
  paracel::packer<T> pk(arg);
  paracel::str_type scrip;
  pk.pack(scrip);
  return scrip;
}

// T must be paracel::str_type
// use template T to do recursive variadic
template<class T, class ...Args>
T paste(const T & op_str, const Args & ...args) { 
  paracel::packer<T> pk(op_str);
  T scrip;
  pk.pack(scrip); // pack to scrip
  return scrip + paracel::seperator + paste(args...); 
}

void check_result(const paracel::str_type & s) {
  auto result = paracel::str_split(s, paracel::seperator);
  std::cout << "--------------------------------" << std::endl;
  paracel::packer<paracel::str_type> pk;
  for(auto & s : result) {
    std::cout << "#" << pk.unpack(s) << "#" << std::endl;
  }
  std::cout << "--------------------------------" << std::endl;
}

int main(int argc, char *argv[])
{
  {
    std::string key = "p[0,:]";
    std::string op_str = "pull";
    auto s1 = paste(op_str, key);
    check_result(s1);
    auto s2 = paste(std::string("pull"), key);
    std::cout << s2 << std::endl;
    check_result(s2);
    std::vector<std::string> key_lst = {"p[0,:]", "p[1,:]", "q[:,0]"};
    auto s3 = paste(std::string("pull_multi"), key_lst);
    //check_result(s3);
    auto result = paracel::str_split(s3, paracel::seperator);
    std::cout << "--------------------------------" << std::endl;
    paracel::packer<paracel::str_type> pk1;
    paracel::packer<paracel::list_type<paracel::str_type> > pk2;
    std::cout << "#" << pk1.unpack(result[0]) << "#" << std::endl;
    auto tmp = pk2.unpack(result[1]);
    for(auto & v : tmp)
      std::cout << "#" << v << "#" << std::endl;
    std::cout << "--------------------------------" << std::endl;
    auto s4 = paste(std::string("pullall"));
    check_result(s4);
  }
  {
    std::string key = "q[:,0]";
    std::string v1 = "val";
    auto s1 = paste(std::string("push"), key, v1);
    check_result(s1);

    double v2 = 3.14;
    auto s2 = paste(std::string("push"), key, v2);
    auto result = paracel::str_split(s2, paracel::seperator);
    std::cout << "--------------------------------" << std::endl;
    paracel::packer<paracel::str_type> pk1;
    paracel::packer<double> pk2;
    std::cout << "#" << pk1.unpack(result[0]) << "#" << std::endl;
    std::cout << "#" << pk1.unpack(result[1]) << "#" << std::endl;
    std::cout << "#" << pk2.unpack(result[2]) << "#" << std::endl;
    std::cout << "--------------------------------" << std::endl;
    
    std::vector<double> v3 = {1.1, 2.2, 3.3, 4.4, 5.5};
    auto s3 = paste(std::string("push"), key + "_0", v3);
    auto result2 = paracel::str_split(s3, paracel::seperator);
    std::cout << "--------------------------------" << std::endl;
    paracel::packer<std::vector<double> > pk3;
    std::cout << "#" << pk1.unpack(result2[0]) << "#" << std::endl;
    std::cout << "#" << pk1.unpack(result2[1]) << "#" << std::endl;
    auto tmp = pk3.unpack(result2[2]);
    for(auto & v : tmp) 
      std::cout << "#" << v << "#" << std::endl;
    std::cout << "--------------------------------" << std::endl;

    std::tr1::unordered_map<paracel::str_type, paracel::list_type<double> > d;
    paracel::list_type<double> t1 = {1., 2., 3., 4., 5.};
    paracel::list_type<double> t2 = {5., 4., 3., 2., 1.};
    d["p[0,:]_0"] = t1;
    d["q[:,0]_0"] = t2;
    auto s4 = paste(std::string("push_multi"), d);
    auto result3 = paracel::str_split(s4, paracel::seperator);

    std::cout << "--------------------------------" << std::endl;
    paracel::packer<std::tr1::unordered_map<paracel::str_type, paracel::list_type<double> > > pk4;
    std::cout << "#" << pk1.unpack(result3[0]) << "#" << std::endl;
    auto tmp2 = pk4.unpack(result3[1]);
    for(auto & v : tmp2) {
      std::cout << "#" << v.first << " : ";
      for(auto & va : v.second) {
        std::cout << va << ",";
      }
      std::cout << "#" << std::endl;
    }
    std::cout << "--------------------------------" << std::endl;
    
  }
  {
    std::string key = "q[:,0]";
    std::string v1 = "val";
    auto s1 = paste(std::string("remove"));
    check_result(s1);
  }
  {
    std::string key = "q[:,0]";
    std::string v1 = "val";
    auto s1 = paste(std::string("clear"));
    check_result(s1);
  }
  return 0;
}
