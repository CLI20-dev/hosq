#pragma once

namespace hosq {

/**
 * @brief Pipeline value representing a structured object body.
 *
 * `ObjectView` is a lightweight vocabulary type produced by `scan::object`
 * after the current token has been verified as an object begin token.
 *
 * It does not own reader state, iterator state, or object contents. The active
 * reader and its state remain in `DecodeContext`; subsequent decode steps such
 * as `decode::each<EntrySchema>` use this type only as a dispatch marker to
 * decode the members of the current object.
 */
struct ObjectView {};

/**
 * @brief Pipeline value representing a structured array body.
 *
 * `ArrayView` is a lightweight vocabulary type produced by `scan::array`
 * after the current token has been verified as an array begin token.
 *
 * It does not own reader state, iterator state, or array contents. The active
 * reader and its state remain in `DecodeContext`; subsequent decode steps such
 * as `decode::each<ElemSchema>` use this type only as a dispatch marker to
 * decode the elements of the current array.
 */
struct ArrayView {};

}  // namespace hosq
