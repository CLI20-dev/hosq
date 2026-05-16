#include <benchmark/benchmark.h>

#include <cstdint>
#include <string_view>
#include <tuple>

#include "ser20/matcher.hh"
#include "ser20/string_literal.hh"
#ifdef __ARM_NEON
#include "ser20/matcher_neon.hh"
#endif

namespace {

// ── Simple: plain if-chain over tuple ────────────────────────────────────────

struct MatcherSimple {
  template <class Callback, class... Args>
  static auto find(std::string_view str, std::tuple<Args...>& args,
                   Callback&& cb) -> bool {
    return find_impl<0>(str, args, std::forward<Callback>(cb));
  }

 private:
  template <std::size_t I, class Callback, class... Args>
  static auto find_impl(std::string_view str, std::tuple<Args...>& args,
                        Callback&& cb) -> bool {
    if constexpr (I < sizeof...(Args)) {
      using Arg =
          std::remove_cvref_t<std::tuple_element_t<I, std::tuple<Args...>>>;
      if (str == Arg::name) {
        std::invoke(std::forward<Callback>(cb), std::get<I>(args));
        return true;
      }
      return find_impl<I + 1>(str, args, std::forward<Callback>(cb));
    } else {
      return false;
    }
  }
};

// ── Test field descriptors ────────────────────────────────────────────────────

template <ser20::StringLiteral N>
struct Field {
  static constexpr std::string_view name = {N.data(), N.size()};
  uint64_t value{0};
};

// 8-field object — representative of a typical JSON struct.
using Fields8 = std::tuple<Field<"id">, Field<"name">, Field<"email">,
                           Field<"created_at">, Field<"updated_at">,
                           Field<"status">, Field<"score">, Field<"active">>;

// 32-field object — realistic JSON-style identifiers of varying length.
using Fields32 = std::tuple<
    Field<"user_id">,            //  0
    Field<"session_token">,      //  1
    Field<"created_at">,         //  2
    Field<"updated_at">,         //  3
    Field<"display_name">,       //  4
    Field<"email_address">,      //  5
    Field<"phone_number">,       //  6
    Field<"profile_image_url">,  //  7
    Field<"is_active">,          //  8
    Field<"is_verified">,        //  9
    Field<"subscription_tier">,  // 10
    Field<"billing_cycle_start">,// 11
    Field<"payment_method_id">,  // 12
    Field<"last_login_at">,      // 13
    Field<"failed_login_count">, // 14
    Field<"password_hash">,      // 15
    Field<"two_factor_enabled">, // 16
    Field<"preferred_language">, // 17
    Field<"timezone_offset">,    // 18
    Field<"notification_prefs">, // 19
    Field<"referral_code">,      // 20
    Field<"account_balance">,    // 21
    Field<"credit_limit">,       // 22
    Field<"kyc_status">,         // 23
    Field<"country_code">,       // 24
    Field<"postal_code">,        // 25
    Field<"street_address">,     // 26
    Field<"organization_id">,    // 27
    Field<"role_flags">,         // 28
    Field<"api_key_hash">,       // 29
    Field<"rate_limit_tier">,    // 30
    Field<"deleted_at">>;        // 31

// 64-field object — large struct stress test.
using Fields64 = std::tuple<
    Field<"user_id">,                    //  0
    Field<"session_token">,              //  1
    Field<"created_at">,                 //  2
    Field<"updated_at">,                 //  3
    Field<"display_name">,               //  4
    Field<"email_address">,              //  5
    Field<"phone_number">,               //  6
    Field<"profile_image_url">,          //  7
    Field<"is_active">,                  //  8
    Field<"is_verified">,                //  9
    Field<"subscription_tier">,          // 10
    Field<"billing_cycle_start">,        // 11
    Field<"payment_method_id">,          // 12
    Field<"last_login_at">,              // 13
    Field<"failed_login_count">,         // 14
    Field<"password_hash">,              // 15
    Field<"two_factor_enabled">,         // 16
    Field<"preferred_language">,         // 17
    Field<"timezone_offset">,            // 18
    Field<"notification_prefs">,         // 19
    Field<"referral_code">,              // 20
    Field<"account_balance">,            // 21
    Field<"credit_limit">,               // 22
    Field<"kyc_status">,                 // 23
    Field<"country_code">,               // 24
    Field<"postal_code">,                // 25
    Field<"street_address">,             // 26
    Field<"organization_id">,            // 27
    Field<"role_flags">,                 // 28
    Field<"api_key_hash">,               // 29
    Field<"rate_limit_tier">,            // 30
    Field<"deleted_at">,                 // 31
    Field<"external_user_id">,           // 32
    Field<"oauth_provider">,             // 33
    Field<"oauth_token_expiry">,         // 34
    Field<"last_password_change_at">,    // 35
    Field<"security_question_hash">,     // 36
    Field<"backup_email_address">,       // 37
    Field<"marketing_opt_in">,           // 38
    Field<"analytics_user_id">,          // 39
    Field<"feature_flag_overrides">,     // 40
    Field<"experiment_cohort_id">,       // 41
    Field<"device_fingerprint">,         // 42
    Field<"last_seen_ip_address">,       // 43
    Field<"geo_lat">,                    // 44
    Field<"geo_lng">,                    // 45
    Field<"push_notification_token">,    // 46
    Field<"webhook_endpoint_url">,       // 47
    Field<"webhook_secret_hash">,        // 48
    Field<"invoice_currency_code">,      // 49
    Field<"tax_id_number">,              // 50
    Field<"vat_registration_id">,        // 51
    Field<"bank_account_last4">,         // 52
    Field<"stripe_customer_id">,         // 53
    Field<"paypal_payer_id">,            // 54
    Field<"chargeback_count">,           // 55
    Field<"fraud_score">,                // 56
    Field<"support_tier">,               // 57
    Field<"open_ticket_count">,          // 58
    Field<"last_support_contact_at">,    // 59
    Field<"data_export_requested_at">,   // 60
    Field<"gdpr_consent_version">,       // 61
    Field<"terms_accepted_version">,     // 62
    Field<"account_closure_reason">>;    // 63

// Keys hit at various positions to exercise early / middle / late hits and miss.
static constexpr std::string_view k8_keys[] = {
    "id", "email", "score", "active", "missing",
};

static constexpr std::string_view k32_first[] = {
    "user_id", "session_token", "created_at"};
static constexpr std::string_view k32_mid[] = {
    "two_factor_enabled", "preferred_language", "timezone_offset"};
static constexpr std::string_view k32_last[] = {
    "api_key_hash", "rate_limit_tier", "deleted_at"};
static constexpr std::string_view k32_miss[] = {
    "nonexistent_field", "unknown_key", "bogus_attr"};

static constexpr std::string_view k64_first[] = {
    "user_id", "session_token", "created_at"};
static constexpr std::string_view k64_mid[] = {
    "experiment_cohort_id", "device_fingerprint", "last_seen_ip_address"};
static constexpr std::string_view k64_last[] = {
    "gdpr_consent_version", "terms_accepted_version", "account_closure_reason"};
static constexpr std::string_view k64_miss[] = {
    "nonexistent_field", "unknown_key", "bogus_attr"};

// 128-field object — very large struct stress test.
using Fields128 = std::tuple<
    Field<"user_id">,                       //   0
    Field<"session_token">,                 //   1
    Field<"created_at">,                    //   2
    Field<"updated_at">,                    //   3
    Field<"display_name">,                  //   4
    Field<"email_address">,                 //   5
    Field<"phone_number">,                  //   6
    Field<"profile_image_url">,             //   7
    Field<"is_active">,                     //   8
    Field<"is_verified">,                   //   9
    Field<"subscription_tier">,             //  10
    Field<"billing_cycle_start">,           //  11
    Field<"payment_method_id">,             //  12
    Field<"last_login_at">,                 //  13
    Field<"failed_login_count">,            //  14
    Field<"password_hash">,                 //  15
    Field<"two_factor_enabled">,            //  16
    Field<"preferred_language">,            //  17
    Field<"timezone_offset">,               //  18
    Field<"notification_prefs">,            //  19
    Field<"referral_code">,                 //  20
    Field<"account_balance">,               //  21
    Field<"credit_limit">,                  //  22
    Field<"kyc_status">,                    //  23
    Field<"country_code">,                  //  24
    Field<"postal_code">,                   //  25
    Field<"street_address">,                //  26
    Field<"organization_id">,               //  27
    Field<"role_flags">,                    //  28
    Field<"api_key_hash">,                  //  29
    Field<"rate_limit_tier">,               //  30
    Field<"deleted_at">,                    //  31
    Field<"external_user_id">,              //  32
    Field<"oauth_provider">,                //  33
    Field<"oauth_token_expiry">,            //  34
    Field<"last_password_change_at">,       //  35
    Field<"security_question_hash">,        //  36
    Field<"backup_email_address">,          //  37
    Field<"marketing_opt_in">,              //  38
    Field<"analytics_user_id">,             //  39
    Field<"feature_flag_overrides">,        //  40
    Field<"experiment_cohort_id">,          //  41
    Field<"device_fingerprint">,            //  42
    Field<"last_seen_ip_address">,          //  43
    Field<"geo_lat">,                       //  44
    Field<"geo_lng">,                       //  45
    Field<"push_notification_token">,       //  46
    Field<"webhook_endpoint_url">,          //  47
    Field<"webhook_secret_hash">,           //  48
    Field<"invoice_currency_code">,         //  49
    Field<"tax_id_number">,                 //  50
    Field<"vat_registration_id">,           //  51
    Field<"bank_account_last4">,            //  52
    Field<"stripe_customer_id">,            //  53
    Field<"paypal_payer_id">,               //  54
    Field<"chargeback_count">,              //  55
    Field<"fraud_score">,                   //  56
    Field<"support_tier">,                  //  57
    Field<"open_ticket_count">,             //  58
    Field<"last_support_contact_at">,       //  59
    Field<"data_export_requested_at">,      //  60
    Field<"gdpr_consent_version">,          //  61
    Field<"terms_accepted_version">,        //  62
    Field<"account_closure_reason">,        //  63
    Field<"primary_currency">,              //  64
    Field<"secondary_currency">,            //  65
    Field<"wallet_address">,                //  66
    Field<"nft_collection_id">,             //  67
    Field<"staking_pool_id">,               //  68
    Field<"reward_points_balance">,         //  69
    Field<"loyalty_tier">,                  //  70
    Field<"last_purchase_at">,              //  71
    Field<"lifetime_order_count">,          //  72
    Field<"lifetime_spend_usd">,            //  73
    Field<"avg_order_value_usd">,           //  74
    Field<"cart_abandonment_count">,        //  75
    Field<"wishlist_item_count">,           //  76
    Field<"product_review_count">,          //  77
    Field<"seller_rating">,                 //  78
    Field<"return_request_count">,          //  79
    Field<"shipping_preference">,           //  80
    Field<"default_address_id">,            //  81
    Field<"gift_card_balance">,             //  82
    Field<"promo_code_used">,               //  83
    Field<"affiliate_partner_id">,          //  84
    Field<"influencer_tier">,               //  85
    Field<"content_moderation_flag">,       //  86
    Field<"reported_at">,                   //  87
    Field<"suspension_reason">,             //  88
    Field<"suspension_lifted_at">,          //  89
    Field<"appeal_status">,                 //  90
    Field<"internal_note">,                 //  91
    Field<"assigned_agent_id">,             //  92
    Field<"escalation_level">,              //  93
    Field<"sla_due_at">,                    //  94
    Field<"resolution_at">,                 //  95
    Field<"csat_score">,                    //  96
    Field<"nps_score">,                     //  97
    Field<"feedback_text_hash">,            //  98
    Field<"ab_test_variant">,               //  99
    Field<"cohort_assignment_date">,        // 100
    Field<"first_conversion_source">,       // 101
    Field<"last_conversion_source">,        // 102
    Field<"utm_campaign">,                  // 103
    Field<"utm_medium">,                    // 104
    Field<"utm_source">,                    // 105
    Field<"landing_page_url">,              // 106
    Field<"referrer_domain">,               // 107
    Field<"browser_user_agent_hash">,       // 108
    Field<"os_platform">,                   // 109
    Field<"app_version">,                   // 110
    Field<"sdk_version">,                   // 111
    Field<"screen_resolution">,             // 112
    Field<"connection_type">,               // 113
    Field<"isp_name">,                      // 114
    Field<"autonomous_system_number">,      // 115
    Field<"datacenter_region">,             // 116
    Field<"cdn_pop_id">,                    // 117
    Field<"request_latency_p99_ms">,        // 118
    Field<"error_rate_7d">,                 // 119
    Field<"uptime_percentage_30d">,         // 120
    Field<"storage_used_bytes">,            // 121
    Field<"bandwidth_used_bytes">,          // 122
    Field<"api_call_count_30d">,            // 123
    Field<"webhook_delivery_success_rate">, // 124
    Field<"last_audit_log_id">,             // 125
    Field<"compliance_check_passed_at">,    // 126
    Field<"data_residency_region">>;        // 127

static constexpr std::string_view k128_first[] = {
    "user_id", "session_token", "created_at"};
static constexpr std::string_view k128_mid[] = {
    "ab_test_variant", "cohort_assignment_date", "first_conversion_source"};
static constexpr std::string_view k128_last[] = {
    "last_audit_log_id", "compliance_check_passed_at", "data_residency_region"};
static constexpr std::string_view k128_miss[] = {
    "nonexistent_field", "unknown_key", "bogus_attr"};

// 32-field object — all names exactly 100 characters long.
// Common 90-char prefix, distinguished only by the last 3 chars (_NN).
// This is the worst case for memcmp: the comparison reaches byte 97 before
// the strings diverge, so string equality dominates lookup cost.
// clang-format off
#define PFX "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
using Fields32L = std::tuple<
    Field<PFX "_00">, Field<PFX "_01">, Field<PFX "_02">, Field<PFX "_03">,
    Field<PFX "_04">, Field<PFX "_05">, Field<PFX "_06">, Field<PFX "_07">,
    Field<PFX "_08">, Field<PFX "_09">, Field<PFX "_10">, Field<PFX "_11">,
    Field<PFX "_12">, Field<PFX "_13">, Field<PFX "_14">, Field<PFX "_15">,
    Field<PFX "_16">, Field<PFX "_17">, Field<PFX "_18">, Field<PFX "_19">,
    Field<PFX "_20">, Field<PFX "_21">, Field<PFX "_22">, Field<PFX "_23">,
    Field<PFX "_24">, Field<PFX "_25">, Field<PFX "_26">, Field<PFX "_27">,
    Field<PFX "_28">, Field<PFX "_29">, Field<PFX "_30">, Field<PFX "_31">>;
// clang-format on

static constexpr std::string_view k32L_first[] = {
    PFX "_00", PFX "_01", PFX "_02"};
static constexpr std::string_view k32L_mid[] = {
    PFX "_15", PFX "_16", PFX "_17"};
static constexpr std::string_view k32L_last[] = {
    PFX "_29", PFX "_30", PFX "_31"};
static constexpr std::string_view k32L_miss[] = {
    PFX "_99", PFX "_zz", PFX "_XX"};
#undef PFX

// ── Benchmark helpers ─────────────────────────────────────────────────────────

template <class M, class Tuple>
static uint64_t lookup_all(Tuple& fields, const std::string_view* keys,
                           size_t n) {
  uint64_t acc = 0;
  for (size_t i = 0; i < n; ++i) {
    M::find(keys[i], fields, [&](auto& f) {
      f.value += static_cast<uint64_t>(i) + 1;
      benchmark::DoNotOptimize(f.value);
      acc += f.value;
    });
  }
  return acc;
}

// ── 8-field benchmarks ────────────────────────────────────────────────────────

template <class M>
static void BM_Match8_Mixed(benchmark::State& state) {
  Fields8 fields{};
  for (auto _ : state) {
    uint64_t acc = lookup_all<M>(fields, k8_keys, std::size(k8_keys));
    benchmark::DoNotOptimize(acc);
  }
}

// ── Benchmark body macro ──────────────────────────────────────────────────────

#define DEFINE_MATCH_BMS(N)                                                \
  template <class M>                                                        \
  static void BM_Match##N##_First(benchmark::State& state) {               \
    Fields##N fields{};                                                     \
    for (auto _ : state) {                                                  \
      benchmark::DoNotOptimize(                                             \
          lookup_all<M>(fields, k##N##_first, std::size(k##N##_first)));   \
    }                                                                       \
  }                                                                         \
  template <class M>                                                        \
  static void BM_Match##N##_Mid(benchmark::State& state) {                 \
    Fields##N fields{};                                                     \
    for (auto _ : state) {                                                  \
      benchmark::DoNotOptimize(                                             \
          lookup_all<M>(fields, k##N##_mid, std::size(k##N##_mid)));       \
    }                                                                       \
  }                                                                         \
  template <class M>                                                        \
  static void BM_Match##N##_Last(benchmark::State& state) {                \
    Fields##N fields{};                                                     \
    for (auto _ : state) {                                                  \
      benchmark::DoNotOptimize(                                             \
          lookup_all<M>(fields, k##N##_last, std::size(k##N##_last)));     \
    }                                                                       \
  }                                                                         \
  template <class M>                                                        \
  static void BM_Match##N##_Miss(benchmark::State& state) {                \
    Fields##N fields{};                                                     \
    for (auto _ : state) {                                                  \
      benchmark::DoNotOptimize(                                             \
          lookup_all<M>(fields, k##N##_miss, std::size(k##N##_miss)));     \
    }                                                                       \
  }

DEFINE_MATCH_BMS(32)
DEFINE_MATCH_BMS(64)
DEFINE_MATCH_BMS(128)
DEFINE_MATCH_BMS(32L)

#undef DEFINE_MATCH_BMS

// ── Registration ──────────────────────────────────────────────────────────────

#ifdef __ARM_NEON
#define REGISTER(BM)                      \
  BENCHMARK_TEMPLATE(BM, MatcherSimple);  \
  BENCHMARK_TEMPLATE(BM, ser20::Matcher); \
  BENCHMARK_TEMPLATE(BM, ser20::MatcherNeon)
#else
#define REGISTER(BM)                     \
  BENCHMARK_TEMPLATE(BM, MatcherSimple); \
  BENCHMARK_TEMPLATE(BM, ser20::Matcher)
#endif

REGISTER(BM_Match8_Mixed);
REGISTER(BM_Match32_First);
REGISTER(BM_Match32_Mid);
REGISTER(BM_Match32_Last);
REGISTER(BM_Match32_Miss);
REGISTER(BM_Match64_First);
REGISTER(BM_Match64_Mid);
REGISTER(BM_Match64_Last);
REGISTER(BM_Match64_Miss);
REGISTER(BM_Match128_First);
REGISTER(BM_Match128_Mid);
REGISTER(BM_Match128_Last);
REGISTER(BM_Match128_Miss);
REGISTER(BM_Match32L_First);
REGISTER(BM_Match32L_Mid);
REGISTER(BM_Match32L_Last);
REGISTER(BM_Match32L_Miss);

#undef REGISTER

}  // namespace
