# Pure, and depends on nothing -- not even ot_secrets. That is what lets test/test_json put
# a hostile body against it on the host, which is the only place the cases it exists for can be
# written at all: a lone surrogate, an escaped NUL, a document that ends in the middle of a
# string. Every one of them is a body somebody can send to the open setup access point.
#
# NOT listed, deliberately: esp_http_server, and anything that could reach it. This component is
# handed a buffer and returns what it decoded; the moment it can send a response it can answer a
# request, and then the access policy in ot_policy has a second door. Same rule, and same
# wording, as components/ot_captive and components/ot_auth.
#
# Four sources, not one: ot_json.cpp had grown over the 350-line ceiling (CLAUDE.md).
# The seam follows the division of responsibility that already existed in the file:
#
#   ot_json_scan.{h,cpp}  what a TOKEN is -- one string walk, one number grammar. Shared by all
#                         three: three walks would mean three sets of escaping rules.
#   ot_json_check.cpp     whether this is a document AS A WHOLE -- duplicates, trailing garbage,
#                         nesting. Called once, before the body is trusted at all.
#   ot_json.cpp           what lies at a KEY and which key is n-th. Called once per field, after.
#   ot_json_escape.cpp    the writer. Shares nothing with the reader, deliberately: what gets escaped
#                         is a string the firmware produced itself, what gets parsed is one a
#                         stranger chose.
#
# INCLUDE_DIRS stays just "include": ot_json_scan.h is the component's private header, it sits next
# to the sources and is visible only to them. DO NOT move it into include/: the outward contract is
# ot_json.h, and it did not change by a single byte in the split.
idf_component_register(
    SRCS "ot_json.cpp" "ot_json_scan.cpp" "ot_json_check.cpp" "ot_json_escape.cpp"
    INCLUDE_DIRS "include"
)
