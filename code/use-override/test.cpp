
struct A {
  virtual void foo() { }
  virtual void g(int x
	) const;
  void h();
};

struct B : public A {
  void foo() { }
  void g(int x = int()
) const;
  virtual void h();
};

auto main() -> int { }
