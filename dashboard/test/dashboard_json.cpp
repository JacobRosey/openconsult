#include "dashboard/src/dashboard_json.h"

#include "openconsult/src/consult_interface.h"

#include <chrono>

#include <gtest/gtest.h>

using namespace openconsult;

TEST(DashboardJsonTest, engineParametersFrameToJSON) {
    std::vector<EngineParameter> params {
        EngineParameter::ENGINE_RPM,
        EngineParameter::VEHICLE_SPEED,
        EngineParameter::BATTERY_VOLTAGE,
    };
    std::vector<uint8_t> data {0x01, 0x59, 0x32, 0x97};
    EngineParameters parameters(params, data);

    EXPECT_EQ("{\n"
              "  \"timestamp_ms\": 123456,\n"
              "  \"parameters\": {\n"
              "    \"engine_speed_rpm\": {\n"
              "      \"name\": \"Engine speed (RPM)\",\n"
              "      \"value\": 4312.50\n"
              "    },\n"
              "    \"vehicle_speed_mph\": {\n"
              "      \"name\": \"Vehicle speed (mph)\",\n"
              "      \"value\": 62.14\n"
              "    },\n"
              "    \"battery_v\": {\n"
              "      \"name\": \"Battery voltage (V)\",\n"
              "      \"value\": 12.08\n"
              "    }\n"
              "  }\n"
              "}",
              dashboard::engineParametersFrameToJSON(
                  parameters, std::chrono::milliseconds(123456)));
}
