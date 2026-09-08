#include <hosq/json/fast_reader.hh>
using B = hosq::json::FastReader<>;
using P = hosq::json::FastReader<128, hosq::json::InputPadding::zero_padded_4>;
extern "C" bool bounded_resolve(B& r, B::state_type& s) {
  return r.resolve_skip(s);
}
extern "C" bool padded_resolve(P& r, P::state_type& s) {
  return r.resolve_skip(s);
}
extern "C" std::errc bounded_integer(const char* p, const char* end,
                                     uint64_t& v) {
  return B::parse_number(p, end, v).ec;
}
extern "C" std::errc padded_integer(const char* p, const char* end,
                                    uint64_t& v) {
  return P::parse_number(p, end, v).ec;
}
using P8 = hosq::json::FastReader<128, hosq::json::InputPadding::zero_padded_8>;
extern "C" std::errc padded8_integer(const char* p, const char* end,
                                     uint64_t& v) {
  return P8::parse_number(p, end, v).ec;
}
