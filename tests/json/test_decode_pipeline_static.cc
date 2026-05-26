#include <hosq/hosq.hh>
#include <string_view>
#include <vector>

#include "hosq/detail/pipeline_analysis.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"

namespace {

using namespace hosq;

template <class Reader>
struct DecodePipelineStaticFixture {
  template <class T>
  using context_type = DecodeContext<T, Reader, TryPhase::normal>;
};

template <class Reader, class T>
using ProbeContext =
    DecodePipelineStaticFixture<Reader>::template context_type<T>;

template <class Step, class View, class... Args>
concept StepCallable =
    requires(Step step, View view, Args... args) { step(view, args...); };

using ArrayCollectPipeline =
    decltype(scan::array | decode::collect<scan::scalar | decode::number<int>>);

using ArrayEachPipeline =
    decltype(scan::array |
             decode::each<scan::scalar | decode::number_into<int>>);

using ObjectCollectPipeline =
    decltype(scan::object | decode::collect<scan::scalar | decode::string_view,
                                            scan::scalar | decode::number<int>>);

using ObjectEachPipeline =
    decltype(scan::object |
             decode::each<scan::scalar | decode::string_view | store::assign,
                          scan::scalar | decode::number<int> |
                              decode::custom<[](int) constexpr -> bool {
                                return true;
                              }>>);

using ArrayEachStep =
    decode::detail::EachImpl<scan::scalar | decode::number_into<int>>;

using ObjectCollectStep =
    decode::detail::CollectImpl<scan::scalar | decode::string_view,
                                scan::scalar | decode::number<int>>;

template <class Reader>
struct StaticChecks {
  using ArrayCollectInfo =
      detail::pipeline_analysis_t<detail::PipelineVerificationMode::no_storage,
                                  ProbeContext<Reader, void>, void,
                                  ArrayCollectPipeline>;

  static_assert(std::same_as<detail::pipeline_terminal_t<ArrayCollectInfo>,
                             std::vector<int>>);

  using ArrayEachInfo = detail::pipeline_analysis_t<
      detail::PipelineVerificationMode::at_most_one_storage,
      ProbeContext<Reader, void>, void, ArrayEachPipeline>;

  static_assert(
      std::same_as<detail::pipeline_storage_t<ArrayEachInfo>, std::vector<int>>);

  using ObjectCollectInfo =
      detail::pipeline_analysis_t<detail::PipelineVerificationMode::no_storage,
                                  ProbeContext<Reader, void>, void,
                                  ObjectCollectPipeline>;

  static_assert(
      std::same_as<detail::pipeline_terminal_t<ObjectCollectInfo>,
                   Map<std::vector<std::string_view>, std::vector<int>>>);

  using ObjectEachInfo = detail::pipeline_analysis_t<
      detail::PipelineVerificationMode::at_most_one_storage,
      ProbeContext<Reader, void>, void, ObjectEachPipeline>;

  static_assert(std::same_as<detail::pipeline_storage_t<ObjectEachInfo>,
                             Map<std::vector<std::string_view>,
                                 std::vector<detail::NoStorage>>>);

  static_assert(!StepCallable<ArrayEachStep, ObjectView,
                              ProbeContext<Reader, std::vector<int>>&>);

  static_assert(
      !StepCallable<ObjectCollectStep, ArrayView, ProbeContext<Reader, void>&>);
};

[[maybe_unused]] inline constexpr StaticChecks<json::SimpleReader>
    simple_reader_checks{};
[[maybe_unused]] inline constexpr StaticChecks<json::FastReader<>>
    fast_reader_checks{};

}  // namespace
