#include <string_view>
#include <type_traits>
#include <vector>

#include "hosq/context.hh"
#include "hosq/detail/pipeline_analysis.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/view.hh"

namespace {

using ProbeReader = hosq::detail::PipelineProbeReader;

template <class T>
using ProbeContext =
    hosq::DecodeContext<T, ProbeReader, hosq::ScanPhase::normal>;

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
                     hosq::decode::custom<[](int) constexpr { return true; }>>);

using ArrayCollectInfo = hosq::detail::pipeline_analysis_t<
    hosq::detail::PipelineVerificationMode::no_storage,
    hosq::detail::AnyDecodeContext, void, ArrayCollectPipeline>;
static_assert(std::same_as<hosq::detail::pipeline_terminal_t<ArrayCollectInfo>,
                           std::vector<int>>);

using ArrayEachInfo = hosq::detail::pipeline_analysis_t<
    hosq::detail::PipelineVerificationMode::exactly_one_storage,
    hosq::detail::AnyDecodeContext, void, ArrayEachPipeline>;
static_assert(std::same_as<hosq::detail::pipeline_storage_t<ArrayEachInfo>,
                           std::vector<int>>);

using ObjectCollectInfo = hosq::detail::pipeline_analysis_t<
    hosq::detail::PipelineVerificationMode::no_storage,
    hosq::detail::AnyDecodeContext, void, ObjectCollectPipeline>;
static_assert(std::same_as<hosq::detail::pipeline_terminal_t<ObjectCollectInfo>,
                           hosq::decode::object_items<std::string_view, int>>);

using ObjectEachInfo = hosq::detail::pipeline_analysis_t<
    hosq::detail::PipelineVerificationMode::exactly_one_storage,
    hosq::detail::AnyDecodeContext, void, ObjectEachPipeline>;
static_assert(std::same_as<hosq::detail::pipeline_storage_t<ObjectEachInfo>,
                           std::vector<std::string_view>>);

using ArrayEachStep = hosq::decode::detail::EachImpl<
    hosq::scan::literal | hosq::decode::number<int> | hosq::store::push>;
using ObjectCollectStep = hosq::decode::detail::CollectImpl<
    hosq::scan::string, hosq::scan::literal | hosq::decode::number<int>>;

static_assert(
    !StepCallable<ArrayEachStep,
                  hosq::ObjectView<ProbeReader, hosq::ScanPhase::normal>,
                  ProbeContext<std::vector<int>>&>);
static_assert(
    !StepCallable<ObjectCollectStep,
                  hosq::ArrayView<ProbeReader, hosq::ScanPhase::normal>>);

}  // namespace
