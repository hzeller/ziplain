load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//visibility:public"],
)

# Machine-readable license specification.
license(
    name = "license",
    package_name = "ziplain",
    license_kind = "@rules_license//licenses/spdx:Apache-2.0",
    license_text = "LICENSE",
)

cc_library(
    name = "ziplain",
    srcs = ["ziplain.cc"],
    hdrs = ["ziplain.h"],
    deps = [
        "//third_party:portable_endian",
        "@zlib",
    ],
)
