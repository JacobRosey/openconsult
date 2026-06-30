cc_library(
    name = "dashboard_json",
    hdrs = ["dashboard_json.h"],
    srcs = ["dashboard_json.cpp"],
    deps = [
        "//openconsult/src:openconsult",
    ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "openconsult_dashboard",
    srcs = ["main.cpp"],
    deps = [
        ":dashboard_json",
        "@com_google_absl//absl/flags:parse",
        "@com_github_yhirose_cpp_httplib//:httplib",
        "//openconsult/src:openconsult",
    ],
)
