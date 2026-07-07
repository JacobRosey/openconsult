#include "openconsult/src/common.h"
#include "openconsult/src/consult_interface.h"
#include "openconsult/src/log_recorder.h"
#include "openconsult/src/log_replay.h"
#include "openconsult/src/serial.h"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/flags/usage_config.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>


using namespace openconsult;

#define APP_NAME "openconsult_cli"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Command line utility for reading from a Consult device."
// Keep USAGE to < 100 characters per line, including the newline.
#define APP_USAGE "usage: " APP_NAME " [--help] [--version] [--log path] [--replay]\n"\
              "           [--replay_wrap] [--print_ecu] [--print_faults]\n"\
              "           [--print_engine] [--stream_engine] [--stream_frames count] device"

ABSL_FLAG(std::string, log, "",
          "Path to log all Consult transactions to. This log may be subsequently "
          "'replayed' using the --replay flag.");
ABSL_FLAG(bool, replay, false,
          "Interpret the passed device as a log to replay transactions from.");
ABSL_FLAG(bool, replay_wrap, false,
          "When replaying a log, wrap at the end of the log.");
ABSL_FLAG(bool, print_ecu, false,
          "Print metadata about the ECU.");
ABSL_FLAG(bool, print_faults, false,
          "Print any recently observed fault codes.");
ABSL_FLAG(bool, print_engine, false,
          "Print one snapshot of common engine parameters.");
ABSL_FLAG(bool, stream_engine, false,
          "Stream common engine parameters.");
ABSL_FLAG(int, stream_frames, 0,
          "Number of engine parameter frames to stream. A value of 0 streams "
          "until the program is interrupted.");

void reportUsageError(std::string error) {
    std::cerr << APP_USAGE << "\n";
    std::cerr << "ERROR: " << error << "\n";
    std::exit(2);
}

std::vector<EngineParameter> commonEngineParameters() {
    return std::vector<EngineParameter>{

        // tps doesn't work for my vehicle, should try to figure that out
        // gets an "unexpected response" error

        EngineParameter::ENGINE_RPM,
        EngineParameter::LH_MAF_VOLTAGE,
        EngineParameter::COOLANT_TEMPERATURE,
        EngineParameter::VEHICLE_SPEED,
        EngineParameter::BATTERY_VOLTAGE,
        //EngineParameter::THROTTLE_POSITION,
        //EngineParameter::ABSOLUTE_THROTTLE_POSITION,
        EngineParameter::IGNITION_TIMING,
        EngineParameter::AAC_VALVE,
    };
}

int main(int argc, char** argv) {
    // Configure Abseil flags.
    absl::FlagsUsageConfig flag_config;
    flag_config.version_string = [](){ return APP_NAME " " APP_VERSION "\n"; };
    absl::SetFlagsUsageConfig(flag_config);
    absl::SetProgramUsageMessage(APP_DESCRIPTION "\n" APP_USAGE);

    // Parse command line.
    auto positional_args = absl::ParseCommandLine(argc, argv);
    bool replay = absl::GetFlag(FLAGS_replay);
    bool wrap = absl::GetFlag(FLAGS_replay_wrap);
    std::string log_path = absl::GetFlag(FLAGS_log);
    bool print_ecu = absl::GetFlag(FLAGS_print_ecu);
    bool print_faults = absl::GetFlag(FLAGS_print_faults);
    bool print_engine = absl::GetFlag(FLAGS_print_engine);
    bool stream_engine = absl::GetFlag(FLAGS_stream_engine);
    int stream_frames = absl::GetFlag(FLAGS_stream_frames);

    // Validate command line.
    if (positional_args.size() < 2) {
        reportUsageError("The following arguments are required: device");
    } else if (positional_args.size() > 2) {
        reportUsageError("Too many positional arguments supplied");
    } else if (stream_frames < 0) {
        reportUsageError("--stream_frames must be 0 or greater");
    }

    std::string device_id = positional_args[1];

    // Construct the device to perform Consult transactions with.
    std::unique_ptr<ByteInterface> device;
    std::ifstream replay_file;
    std::ofstream log_file;
    if (replay) {
        replay_file = std::ifstream(device_id, std::ios_base::in);
        if (!replay_file.good()) {
            reportUsageError(cmn::pformat("Failed to open %s", device_id.c_str()));
        }
        device = std::unique_ptr<ByteInterface>(new LogReplay(replay_file, wrap));
    } else {
        device = std::unique_ptr<ByteInterface>(new SerialPort(device_id, 9600));
        if (!log_path.empty()) {
            log_file = std::ofstream(log_path, std::ios_base::out);
            if (!log_file.good()) {
                reportUsageError(cmn::pformat("Failed to open %s", log_path.c_str()));
            }
            device = std::unique_ptr<ByteInterface>(new LogRecorder(std::move(device), log_file));
        }
    }

    // Construct the ConsultInterface.
    ConsultInterface consult(std::move(device));

    if (print_ecu) {
        auto metadata = consult.readECUMetadata();
        std::cout << "\n";
        std::cout << "ECU METADATA\n";
        std::cout << "============\n";
        std::cout << metadata.toJSON();
        std::cout << "\n";
    }
    if (print_faults) {
        auto faults = consult.readFaultCodes();
        std::cout << "\n";
        std::cout << "FAULT CODES\n";
        std::cout << "===========\n";
        std::cout << faults.toJSON();
        std::cout << "\n";
    }
    if (print_engine) {
        auto parameters = consult.readEngineParameters(commonEngineParameters());
        std::cout << "\n";
        std::cout << "ENGINE PARAMETERS\n";
        std::cout << "=================\n";
        std::cout << parameters.toJSON();
        std::cout << "\n";
    }
    if (stream_engine) {
        std::cout << "\n";
        std::cout << "ENGINE PARAMETERS STREAM\n";
        std::cout << "========================\n";
        auto stream = consult.streamEngineParameters(commonEngineParameters());
        for (int i = 0; stream_frames == 0 || i < stream_frames; ++i) {
            auto parameters = stream.getFrame();
            std::cout << parameters.toJSON();
            std::cout << "\n";
        }
    }

    return 0;
}
