namespace X {
class Base {
 public:
  ~Base() {}
};
}  // namespace X

namespace Y {
class DerivedA : public X::Base {};
class DerivedB : public X::Base {};
}  // namespace Y
