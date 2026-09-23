load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_test(
    name = "disruptor_queue_test",
    srcs = [
        "bit_utils_tests.cpp",
        "disruptor_queue_tests.cpp",
    ],
    deps = [
        "//src:disruptor_queue",
        "@googletest//:gtest_main",
    ],
)
