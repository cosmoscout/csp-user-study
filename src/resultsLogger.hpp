////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR                               //
////////////////////////////////////////////////////////////////////////////////////////////////////

// SPDX-FileCopyrightText: German Aerospace Center (DLR) <cosmoscout@dlr.de>
// SPDX-License-Identifier: MIT

#ifndef CSP_USER_STUDY_RESULTSLOGGER_HPP
#define CSP_USER_STUDY_RESULTSLOGGER_HPP

#include <spdlog/spdlog.h>

namespace csp::userstudy {

/// This creates the default singleton logger for "csp-user-study" when called for the first time
/// and returns it. See cs-utils/logger.hpp for more logging details.
spdlog::logger& resultsLogger();

} // namespace csp::userstudy

#endif // CSP_USER_STUDY_RESULTSLOGGER_HPP
