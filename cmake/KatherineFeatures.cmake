# Copyright (c) 2018 Petr Mánek.
# This software is distributed under the terms of the MIT License, copied verbatim in the file "LICENSE".
#
# SPDX-License-Identifier: MIT

#[=======================================================================[
KatherineFeatures
-----------------

Feature flag facility for the Katherine project.

katherine_declare_feature(<name> <description>
                          DEFAULT <ON|OFF>
                          [AUTO_DETECTED]
                          [LEGACY <old_option_name>])

  Declares a boolean feature flag as the cache option KATHERINE_<name>.
  Without AUTO_DETECTED, the default value is the hard-coded DEFAULT.
  With AUTO_DETECTED, _katherine_feature_default() probes the host
  environment and may raise the default to ON; DEFAULT is the fallback
  used when the probe finds nothing. Either way, a value set by the
  user overrides the default.

  When LEGACY names a deprecated pre-1.0 option and that option is set
  while KATHERINE_<name> is not, the legacy value is adopted and a
  deprecation warning is issued. If both are set, KATHERINE_<name> wins.

katherine_feature_summary()

  Prints a table of all declared features and their configured state.
#]=======================================================================]

include_guard(GLOBAL)

set_property(GLOBAL PROPERTY KATHERINE_FEATURES "")

# Normalize any CMake boolean spelling (1/0, TRUE/FALSE, YES/NO, ...) to ON/OFF.
function(_katherine_bool value out_var)
    if(value)
        set(${out_var} ON PARENT_SCOPE)
    else()
        set(${out_var} OFF PARENT_SCOPE)
    endif()
endfunction()

# Resolve the default of an AUTO_DETECTED feature by probing the host
# environment; the declared DEFAULT is the fallback when the probe finds
# nothing. New detection rules are added here, next to the existing ones.
function(_katherine_feature_default name fallback out_var)
    set(default "${fallback}")
    if(name STREQUAL "BUILD_DOXYGEN")
        find_package(Doxygen "${KATHERINE_DOXYGEN_MINIMUM_VERSION}" QUIET)
        if(DOXYGEN_FOUND)
            set(default ON)
        endif()
    elseif(name STREQUAL "BUILD_PYTHON")
        find_package(Python3 "${KATHERINE_PYTHON_MINIMUM_VERSION}" QUIET
                     COMPONENTS Interpreter Development)
        if(Python3_FOUND)
            list(JOIN KATHERINE_PYTHON_REQUIRED_MODULES ", " imports)
            execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import ${imports}"
                            RESULT_VARIABLE import_result
                            OUTPUT_QUIET ERROR_QUIET)
            if(import_result EQUAL 0)
                set(default ON)
            endif()
        endif()
    else()
        message(FATAL_ERROR "katherine_declare_feature(${name}): declared "
                            "AUTO_DETECTED, but no detection rule exists in "
                            "_katherine_feature_default()")
    endif()
    set(${out_var} "${default}" PARENT_SCOPE)
endfunction()

function(katherine_declare_feature name description)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "AUTO_DETECTED" "DEFAULT;LEGACY" "")
    if(NOT DEFINED ARG_DEFAULT)
        message(FATAL_ERROR "katherine_declare_feature(${name}): DEFAULT is required")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "katherine_declare_feature(${name}): unknown arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()
    set(flag "KATHERINE_${name}")

    if(ARG_AUTO_DETECTED)
        _katherine_feature_default("${name}" "${ARG_DEFAULT}" default)
    else()
        set(default "${ARG_DEFAULT}")
    endif()

    # Backward compatibility with the deprecated pre-1.0 option name.
    if(ARG_LEGACY AND DEFINED CACHE{${ARG_LEGACY}})
        _katherine_bool("${${ARG_LEGACY}}" legacy_value)
        if(DEFINED CACHE{${flag}})
            _katherine_bool("${${flag}}" new_value)
            if(NOT legacy_value STREQUAL new_value)
                message(WARNING "Both ${ARG_LEGACY}=${legacy_value} and ${flag}=${new_value} "
                                "are set; ${flag} takes precedence.")
            endif()
        else()
            message(DEPRECATION "Option ${ARG_LEGACY} is deprecated; use ${flag} instead.")
            set(default "${legacy_value}")
        endif()
    endif()

    option(${flag} "${description}" "${default}")

    set_property(GLOBAL APPEND PROPERTY KATHERINE_FEATURES "${name}")
    set_property(GLOBAL PROPERTY "KATHERINE_FEATURE_${name}_DESC" "${description}")
endfunction()

function(katherine_feature_summary)
    get_property(features GLOBAL PROPERTY KATHERINE_FEATURES)

    set(name_width 7)   # "Feature"
    set(desc_width 11)  # "Description"
    foreach(name IN LISTS features)
        string(LENGTH "KATHERINE_${name}" len)
        if(len GREATER name_width)
            set(name_width ${len})
        endif()
        get_property(desc GLOBAL PROPERTY "KATHERINE_FEATURE_${name}_DESC")
        string(LENGTH "${desc}" len)
        if(len GREATER desc_width)
            set(desc_width ${len})
        endif()
    endforeach()

    string(REPEAT "-" ${name_width} name_rule)
    string(REPEAT "-" ${desc_width} desc_rule)
    set(rule "+-${name_rule}-+-------+-${desc_rule}-+")

    macro(_katherine_row name state desc)
        string(LENGTH "${name}" len)
        math(EXPR pad "${name_width} - ${len}")
        string(REPEAT " " ${pad} name_pad)
        string(LENGTH "${desc}" len)
        math(EXPR pad "${desc_width} - ${len}")
        string(REPEAT " " ${pad} desc_pad)
        string(LENGTH "${state}" len)
        math(EXPR pad "5 - ${len}")
        string(REPEAT " " ${pad} state_pad)
        message(STATUS "| ${name}${name_pad} | ${state}${state_pad} | ${desc}${desc_pad} |")
    endmacro()

    message(STATUS "Katherine features:")
    message(STATUS "${rule}")
    _katherine_row("Feature" "State" "Description")
    message(STATUS "${rule}")
    foreach(name IN LISTS features)
        _katherine_bool("${KATHERINE_${name}}" state)
        get_property(desc GLOBAL PROPERTY "KATHERINE_FEATURE_${name}_DESC")
        _katherine_row("KATHERINE_${name}" "${state}" "${desc}")
    endforeach()
    message(STATUS "${rule}")
endfunction()
