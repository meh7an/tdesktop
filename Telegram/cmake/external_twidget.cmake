# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# tlottie and twidget ship in one umbrella library, built by the tdesktop_rust
# stage of prepare.py, because two Rust static libraries can't share a binary.
# So cmake_helpers' tlottie target is pointed at it too.
set(tdesktop_rust_library ${libs_loc}/local/lib/libtdesktop_rust.a)

set_target_properties(external_tlottie_native PROPERTIES
    IMPORTED_LOCATION ${tdesktop_rust_library}
)

add_library(external_twidget_native STATIC IMPORTED GLOBAL)
add_library(external_twidget INTERFACE IMPORTED GLOBAL)
add_library(tdesktop::external_twidget ALIAS external_twidget)

set_target_properties(external_twidget_native PROPERTIES
    IMPORTED_LOCATION ${tdesktop_rust_library}
)

target_include_directories(external_twidget SYSTEM
INTERFACE
    ${third_party_loc}/twidget/include
)

target_link_libraries(external_twidget
INTERFACE
    external_twidget_native
)
