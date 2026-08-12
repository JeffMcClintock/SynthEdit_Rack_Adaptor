# SPDX-License-Identifier: GPL-3.0-or-later
#
# SynthEdit Rack Adaptor — header-only.
#
# Consumers link the INTERFACE target:
#     target_link_libraries(MyModule PRIVATE SynthEditRackAdaptor)
#
# GMPI's Core headers (Processor.h) and, for the editor, gmpi_ui's helpers are
# expected on the consumer's include path already — gmpi_plugin() provides
# both.
#
# NOTE ON LICENSING: this library is GPL-3.0-or-later, and so is anything you
# build with it. See LICENSE and README.md.

cmake_minimum_required(VERSION 3.20)

project(SynthEdit_Rack_Adaptor LANGUAGES CXX)

add_library(SynthEditRackAdaptor INTERFACE)

# rack/ is on the path so a module's own `#include "plugin.hpp"` resolves to
# the mock without the module being edited.
target_include_directories(SynthEditRackAdaptor INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/rack
)

target_compile_features(SynthEditRackAdaptor INTERFACE cxx_std_20)

# Shown in IDEs even though nothing compiles here.
target_sources(SynthEditRackAdaptor INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/RackModule.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackAdaptor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackPanelLayout.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackAutoRegister.h
    ${CMAKE_CURRENT_SOURCE_DIR}/rack/plugin.hpp
)
