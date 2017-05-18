class Foo {
 public:
  ~Foo() {
  }

  virtual void foo() {
  }
};


class Bar : public Foo {
  void foo() override {
  }
};
