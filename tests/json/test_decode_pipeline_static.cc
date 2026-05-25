#include <string_view>
#include <vector>

#include "hosq/detail/pipeline_analysis.hh"
#include "hosq/hosq.hh"
#include "hosq/json/simple_reader.hh"

namespace {

using namespace hosq;

struct DecodePipelineStaticFixture {
  using reader_type = json::SimpleReader;

  template <class T>
  using context_type = DecodeContext<T, reader_type, TryPhase::normal>;
};

template <class T>
using ProbeContext = DecodePipelineStaticFixture::context_type<T>;

template <class Step, class View, class... Args>
concept StepCallable =
    requires(Step step, View view, Args... args) { step(view, args...); };

using ArrayCollectPipeline =
    decltype(scan::array | decode::collect<scan::scalar | decode::number<int>>);

using ArrayEachPipeline =
    decltype(scan::array |
             decode::each<scan::scalar | decode::number<int> | store::push>);

using ObjectCollectPipeline =
    decltype(scan::object | decode::collect<scan::scalar | decode::string_view,
                                            scan::scalar | decode::number<int>>);

using ObjectEachPipeline =
    decltype(scan::object |
             decode::each<scan::scalar | decode::string_view | store::push,
                          scan::scalar | decode::number<int> |
                              decode::custom<[](int) constexpr -> bool {
                                return true;
                              }>>);

using ArrayCollectInfo =
    detail::pipeline_analysis_t<detail::PipelineVerificationMode::no_storage,
                                ProbeContext<void>, void, ArrayCollectPipeline>;

static_assert(std::same_as<detail::pipeline_terminal_t<ArrayCollectInfo>,
                           std::vector<int>>);

using ArrayEachInfo = detail::pipeline_analysis_t<
    detail::PipelineVerificationMode::at_most_one_storage, ProbeContext<void>,
    void, ArrayEachPipeline>;

static_assert(
    std::same_as<detail::pipeline_storage_t<ArrayEachInfo>, std::vector<int>>);

using ObjectCollectInfo =
    detail::pipeline_analysis_t<detail::PipelineVerificationMode::no_storage,
                                ProbeContext<void>, void, ObjectCollectPipeline>;

static_assert(
    std::same_as<detail::pipeline_terminal_t<ObjectCollectInfo>,
                 Map<std::vector<std::string_view>, std::vector<int>>>);

using ObjectEachInfo = detail::pipeline_analysis_t<
    detail::PipelineVerificationMode::at_most_one_storage, ProbeContext<void>,
    void, ObjectEachPipeline>;

static_assert(
    std::same_as<detail::pipeline_storage_t<ObjectEachInfo>,
                 Map<std::vector<std::string_view>, detail::NoStorage>>);

using ArrayEachStep =
    decode::detail::EachImpl<scan::scalar | decode::number<int> | store::push>;

using ObjectCollectStep =
    decode::detail::CollectImpl<scan::scalar | decode::string_view,
                                scan::scalar | decode::number<int>>;

static_assert(
    !StepCallable<ArrayEachStep, ObjectView, ProbeContext<std::vector<int>>&>);

static_assert(!StepCallable<ObjectCollectStep, ArrayView, ProbeContext<void>&>);

}  // namespace
