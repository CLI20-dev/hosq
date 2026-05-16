
#include <ser20/parser.hh>

struct Json {
  ser20::ElemImpl<"name", ser20::scan::string | ser20::decode::string_view |
                              ser20::pack::once>
      name;
  ser20::ElemImpl<"active", ser20::scan::scalar | ser20::decode::boolean |
                                ser20::pack::once>
      active;
  ser20::ElemImpl<"deleted", ser20::scan::scalar | ser20::decode::boolean |
                                 ser20::pack::once>
      deleted;

  ser20::ElemImpl<"", ser20::unknown::ignore> remainings;
};

auto main() -> int {}
