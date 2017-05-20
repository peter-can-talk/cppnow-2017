#include <string>

namespace X {
struct BaseA {
  ~BaseA() {}
};

struct BaseB {
  std::string s;
};
}  // namespace X

namespace Y {
struct DerivedA : public X::BaseA {};
struct DerivedB : public X::BaseA {};
struct DerivedC : public X::BaseB {};
}  // namespace Y
