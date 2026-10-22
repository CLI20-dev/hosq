#pragma once

#include <cstddef>

namespace hosq {

template <std::size_t N>
struct StringLiteral;

template <StringLiteral K>
using Key = std::integral_constant<decltype(K), K>;

template <auto... Fns>
  requires(sizeof...(Fns) > 0)
struct Pipeline;

struct BytesView;

template <class Key, class Value>
struct Entry;

template <auto Decoder, auto Encoder>
struct Elem;

template <class T>
struct Try;

enum class TryPhase;

template <class T>
struct MatchResult;

struct PipelineAnalysisStorage;

struct PipelineAnalysisReader;

struct PipelineAnalysisReaderState;

template <class Keys, class Values>
struct Map;

template <class Key, class Value>
class MappingValue;

template <class T, class R, TryPhase Phase>
struct DecodeContext;

template <class T, class W>
struct EncodeContext;

template <class R>
class Decoder;

template <class W>
class Encoder;

}  // namespace hosq
