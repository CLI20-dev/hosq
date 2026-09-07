#include "hosq/json/fast_reader.hh"
using R = hosq::json::FastReader<>;
using S = R::state_type;
extern "C" bool adapter(const R& r, S& s) {
  return hosq::next_token<hosq::Expect::member_value>(r, s);
}
extern "C" bool direct(const R& r, S& s) {
  return r.next_token_impl<hosq::Expect::member_value>(s);
}
extern "C" bool generic(const R& r, S& s) {
  return r.next_token_impl(s);
}
extern "C" bool proven_member(const R& r, S& s) {
  return hosq::next_token<hosq::ReadPosition::member_value>(r, s);
}
extern "C" uint64_t fused_pipeline(R& r, S& s) {
  hosq::DecodeContext<void, R> ctx{r, s};
  return (hosq::scan::scalar | hosq::decode::number<uint64_t>).invoke(ctx);
}
extern "C" uint64_t fused_direct(R& r, S& s) {
  uint64_t value{};
  r.read_number_impl<uint64_t, false>(s, value);
  return value;
}
