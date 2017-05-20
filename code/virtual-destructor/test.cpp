namespace X {
class Foo {
 public:
  ~Foo() {}

  virtual void foo() {}
};
}  // namespace X

namespace Y {
class Bar : public X::Foo {
  void foo() override {}
};

class Baz : public X::Foo {};
}  // namespace Y
