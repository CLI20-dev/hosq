#pragma once

#include <gtest/gtest.h>

#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"

namespace hosq::test {

using RuntimeReaderTypes =
    ::testing::Types<json::SimpleReader, json::FastReader<>>;

template <class Reader>
class RuntimeReaderTest : public ::testing::Test {};

}  // namespace hosq::test
