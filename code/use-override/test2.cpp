struct A {
  virtual void f();
};

struct B : public A {
  void f() override;
};
auto main() -> int { }
