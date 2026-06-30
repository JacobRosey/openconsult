#ifndef OPENCONSULT_DASHBOARD_DASHBOARD_JSON
#define OPENCONSULT_DASHBOARD_DASHBOARD_JSON

#include "openconsult/src/consult_interface.h"

#include <chrono>
#include <string>
#include <vector>

namespace openconsult {
namespace dashboard {

std::vector<EngineParameter> commonDashboardParameters();

std::string engineParametersFrameToJSON(
        const EngineParameters& frame,
        std::chrono::milliseconds timestamp);

}
}

#endif
