#pragma once

#include <utility>

struct NoCtx {};

template <class T>
struct ElemCtx {
  T storage{};
  auto elem() -> T& { return storage; }
};

template <class P, class Input>
auto pinvoke(Input input) {
  NoCtx ctx;
  return P::invoke(std::move(input), ctx);
}

template <class P, class Input, class Ctx>
auto pinvoke_with(Input input, Ctx& ctx) {
  return P::invoke(std::move(input), ctx);
}
