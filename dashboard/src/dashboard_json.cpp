#include "dashboard_json.h"

#include "openconsult/src/consult_engine_parameters.h"

#include <iomanip>
#include <sstream>

namespace openconsult {
namespace dashboard {

namespace {

constexpr double kKilometersPerHourToMilesPerHour = 0.6213711922;

std::string jsonEscape(const std::string& value) {
    std::stringstream stream;
    for (char c : value) {
        switch (c) {
            case '"':
                stream << "\\\"";
                break;
            case '\\':
                stream << "\\\\";
                break;
            case '\b':
                stream << "\\b";
                break;
            case '\f':
                stream << "\\f";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    stream << "\\u"
                           << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(static_cast<unsigned char>(c))
                           << std::dec << std::setfill(' ');
                } else {
                    stream << c;
                }
                break;
        }
    }
    return stream.str();
}

std::string dashboardParameterId(EngineParameter parameter) {
    if (parameter == EngineParameter::VEHICLE_SPEED) {
        return "vehicle_speed_mph";
    }
    return engineParameterId(parameter);
}

std::string dashboardParameterName(EngineParameter parameter) {
    if (parameter == EngineParameter::VEHICLE_SPEED) {
        return "Vehicle speed (mph)";
    }
    return engineParameterName(parameter);
}

double dashboardParameterValue(EngineParameter parameter, double value) {
    if (parameter == EngineParameter::VEHICLE_SPEED) {
        return value * kKilometersPerHourToMilesPerHour;
    }
    return value;
}

}

std::vector<EngineParameter> commonDashboardParameters() {
    return std::vector<EngineParameter>{
        EngineParameter::ENGINE_RPM,
        EngineParameter::LH_MAF_VOLTAGE,
        EngineParameter::COOLANT_TEMPERATURE,
        EngineParameter::LH_O2_SENSOR_VOLTAGE,
        EngineParameter::VEHICLE_SPEED,
        EngineParameter::BATTERY_VOLTAGE,
        //EngineParameter::THROTTLE_POSITION,
        //EngineParameter::ABSOLUTE_THROTTLE_POSITION,
        EngineParameter::IGNITION_TIMING,
        EngineParameter::AAC_VALVE,
    };
}

std::string engineParametersFrameToJSON(
        const EngineParameters& frame,
        std::chrono::milliseconds timestamp) {
    std::stringstream stream;
    std::string separator = "\n";
    stream << "{\n"
           << "  \"timestamp_ms\": " << timestamp.count() << ",\n"
           << "  \"parameters\": {";

    for (const auto& parameter : frame.parameters) {
        const std::string id = dashboardParameterId(parameter.first);
        const std::string name = dashboardParameterName(parameter.first);
        const double value = dashboardParameterValue(parameter.first, parameter.second);
        stream << separator
               << "    \"" << jsonEscape(id) << "\": {\n"
               << "      \"name\": \"" << jsonEscape(name) << "\",\n"
               << "      \"value\": " << std::fixed << std::setprecision(2)
               << value << "\n"
               << "    }";
        separator = ",\n";
    }

    stream << "\n"
           << "  }\n"
           << "}";
    return stream.str();
}

}
}
