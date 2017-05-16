const auto lambda = [] (auto) noexcept {
  bool done = true;
  flip: done = !done;
  if (!done) goto flip;
};
