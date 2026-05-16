
#include <ser20/element.hh>

struct Json {
  ser20::detail::ElemImpl<"name", ser20::pack::set_once> name;
  ser20::detail::ElemImpl<"active", ser20::pack::set_once> active;
  ser20::detail::ElemImpl<"deleted", ser20::pack::set_once> deleted;

  ser20::detail::ElemImpl<"", ser20::pack::set_once> remainings;
};

auto main() -> int {}
