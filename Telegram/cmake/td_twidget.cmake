# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

include(cmake/external_twidget.cmake)

add_library(td_twidget OBJECT)
init_non_host_target(td_twidget)
add_library(tdesktop::td_twidget ALIAS td_twidget)

target_precompile_headers(td_twidget PRIVATE ${src_loc}/twidget/twidget_pch.h)
nice_target_sources(td_twidget ${src_loc}
PRIVATE
    twidget/twidget_api.cpp
    twidget/twidget_api.h
    twidget/twidget_conformance.cpp
    twidget/twidget_conformance.h
    twidget/twidget_layout.cpp
    twidget/twidget_layout.h
    twidget/twidget_model.cpp
    twidget/twidget_model.h
    twidget/twidget_pch.h
)

target_include_directories(td_twidget
PUBLIC
    ${src_loc}
)

target_link_libraries(td_twidget
PUBLIC
    desktop-app::lib_base
PRIVATE
    desktop-app::lib_ui
    tdesktop::external_twidget
)
