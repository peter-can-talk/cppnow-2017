class X {
 public:
  int x;
};

int f() {
  int y = X().x;
  y += 1;
  return y;
}
