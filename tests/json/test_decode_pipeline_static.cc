#include <string_view>
#include <vector>

#include "hosq/context.hh"
#include "hosq/detail/pipeline_analysis.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/map.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/view.hh"

namespace {

struct DecodePipelineStaticFixture {
  using reader_type = hosq::json::SimpleReader;

  template <class T>
  using context_type =
      hosq::DecodeContext<T, reader_type, hosq::TryPhase::normal>;
};

template <class T>
using ProbeContext = DecodePipelineStaticFixture::context_type<T>;

template <class Step, class View, class... Args>
concept StepCallable =
    requires(Step step, View view, Args... args) { step(view, args...); };

using ArrayCollectPipeline =
    decltype(hosq::scan::array |
             hosq::decode::collect<hosq::scan::literal |
                                   hosq::decode::number<int>>);

using ArrayEachPipeline =
    decltype(hosq::scan::array |
             hosq::decode::each<hosq::scan::literal | hosq::decode::number<int> |
                                hosq::store::push>);

using ObjectCollectPipeline =
    decltype(hosq::scan::object |
             hosq::decode::collect<hosq::scan::string,
                                   hosq::scan::literal |
                                       hosq::decode::number<int>>);

using ObjectEachPipeline =
    decltype(hosq::scan::object |
             hosq::decode::each<
                 hosq::scan::string | hosq::store::push,
                 hosq::scan::literal | hosq::decode::number<int> |
                     hosq::decode::custom<[](int) constexpr -> bool {
                       return true;
                     }>>);

using ArrayCollectInfo = hosq::detail::pipeline_analysis_t<
    hosq::detail::PipelineVerificationMode::no_storage, ProbeContext<void>, void,
    ArrayCollectPipeline>;

static_assert(std::same_as<hosq::detail::pipeline_terminal_t<ArrayCollectInfo>,
                           std::vector<int>>);

using ArrayEachInfo = hosq::detail::pipeline_analysis_t<
    hosq::detail::PipelineVerificationMode::at_most_one_storage,
    ProbeContext<void>, void, ArrayEachPipeline>;

static_assert(std::same_as<hosq::detail::pipeline_storage_t<ArrayEachInfo>,
                           std::vector<int>>);

using ObjectCollectInfo = hosq::detail::pipeline_analysis_t<
    hosq::detail::PipelineVerificationMode::no_storage, ProbeContext<void>, void,
    ObjectCollectPipeline>;

static_assert(
    std::same_as<hosq::detail::pipeline_terminal_t<ObjectCollectInfo>,
                 hosq::Map<std::vector<std::string_view>, std::vector<int>>>);

using ObjectEachInfo = hosq::detail::pipeline_analysis_t<
    hosq::detail::PipelineVerificationMode::at_most_one_storage,
    ProbeContext<void>, void, ObjectEachPipeline>;

static_assert(std::same_as<hosq::detail::pipeline_storage_t<ObjectEachInfo>,
                           hosq::Map<std::vector<std::string_view>,
                                     hosq::detail::NoStorage>>);

using ArrayEachStep = hosq::decode::detail::EachImpl<
    hosq::scan::literal | hosq::decode::number<int> | hosq::store::push>;

using ObjectCollectStep = hosq::decode::detail::CollectImpl<
    hosq::scan::string, hosq::scan::literal | hosq::decode::number<int>>;

static_assert(!StepCallable<ArrayEachStep, hosq::ObjectView,
                            ProbeContext<std::vector<int>>&>);

static_assert(
    !StepCallable<ObjectCollectStep, hosq::ArrayView, ProbeContext<void>&>);

}  // namespace
