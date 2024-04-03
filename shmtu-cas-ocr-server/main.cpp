// SHMTU CAS OCR Server
// Created by Haomin Kong on 2024/2/22.
// Python Version:
// Train/SHMTU_CAS_OCR_RESNET/src/classify/predict/predict_server_torch.py

#include <iostream>
#include <thread>
#include <csignal>

#define FMT_HEADER_ONLY 1
#include <fmt/core.h>

#include <tclap/CmdLine.h>

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/StreamCopier.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs/imgcodecs.hpp>

#include "CAS_OCR.h"

#ifndef SHMTU_CAS_SERVER_VERSION
#define SHMTU_CAS_SERVER_VERSION "0.0.1"
#endif

// Define global variable
std::string ip_addr = "0.0.0.0";
int port = 21601;
bool use_gpu = false;
std::string checkpoint_path =
        "../../Checkpoint";
bool use_fp16 = false;

// Global variable to indicate whether to exit the program
volatile sig_atomic_t g_running = true;

// Signal handler function
void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout
                << "Received SIGINT signal. Exiting..."
                << std::endl;
        // Set the exit flag to false
        g_running = false;
    }
}

std::string convert_bool_to_std_string(const bool value) {
    if (value) {
        return "true";
    }
    return "false";
}

bool decode_image(const std::string &image_data, cv::Mat &image) {
    try {
        const std::vector<uchar> data(
            image_data.begin(),
            image_data.end()
        );
        image =
                cv::imdecode(
                    data,
                    cv::IMREAD_COLOR
                );
        cv::resize(
            image, image,
            cv::Size(400, 140)
        );
    } catch (cv::Exception &ex) {
        std::cerr
                << "Error decoding image: "
                << ex.what()
                << std::endl;
        return false;
    }

    if (image.empty()) {
        std::cerr << "Error decoding image" << std::endl;
        return false;
    }

    return true;
}

void handle_client(Poco::Net::StreamSocket client, const std::string &peerAddress) {
    const std::string end_marker = "<END>";
    std::string image_data;
    bool receive_error = false;

    while (true) {
        try {
            char buffer[1024];
            const int bytes_received =
                    client.receiveBytes(
                        buffer,
                        sizeof(buffer)
                    );
            if (bytes_received <= 0) {
                break;
            }
            image_data.append(buffer, bytes_received);
        } catch (Poco::Exception &) {
            receive_error = true;
            break;
        }

        if (
            image_data.length() >= end_marker.length() &&
            image_data.substr(
                image_data.length() - end_marker.length()
            ) == end_marker
        ) {
            image_data.erase(image_data.length() - end_marker.length());
            break;
        }
    }

    // Calc Size
    std::cout
            << "[" << peerAddress << "] " <<
            "Image Size: " << image_data.length() << " B"
            << std::endl;

    if (receive_error) {
        std::cerr
                << "[" << peerAddress << "] "
                << "Error receiving data!"
                << std::endl;
        return;
    }

    std::cout
            << "[" << peerAddress << "] "
            << "Received image data"
            << std::endl;

    cv::Mat image;
    if (!decode_image(image_data, image)) {
        std::cerr
                << "[" << peerAddress << "] "
                << "Error decoding image"
                << std::endl;
        return;
    }

    std::cout << "[" << peerAddress << "] "
            << "Image Decode Success"
            << std::endl;

    // Add image processing and prediction logic here
    const auto predict_result =
            CAS_OCR::predict_validate_code(image, use_gpu);

    const std::string result =
            std::get<1>(predict_result);
    std::cout
            << "[" << peerAddress << "] "
            << result
            << std::endl;
    try {
        client.sendBytes(
            result.c_str(),
            static_cast<int>(result.length())
        );
        std::cout
                << "[" << peerAddress << "] "
                << "Send Successed!"
                << std::endl;
    } catch (Poco::Exception &ex) {
        std::cerr
                << "[" << peerAddress << "] "
                << "Error sending result: "
                << ex.displayText()
                << std::endl;
    }

    client.close();
    std::cout
            << "[" << peerAddress << "] "
            << "Connection closed"
            << std::endl;
}

void monitor_in(Poco::Net::ServerSocket &srv) {
    while (g_running) {
        Poco::Net::StreamSocket client;
        try {
            client = srv.acceptConnection();
        } catch (...) {
            continue;
        }

        if (!g_running) {
            break;
        }

        const std::string peerAddress =
                client.peerAddress().toString();
        std::cout
                << "Connection from: "
                << peerAddress
                << std::endl;

        // Create a new thread to handle the client connection
        std::thread client_thread(
            handle_client,
            std::move(client),
            peerAddress
        );
        // Detach the thread to execute independently in the background
        client_thread.detach();
    }
}

void print_hello() {
    std::cout
            << "ShangHai Maritime Uninversity"
            << "\n";
    std::cout
            << "\tSHMTU CAS OCR Server"
            << " V" << SHMTU_CAS_SERVER_VERSION
            << "\n";
    std::cout
            << "\tAuthor:Haomin Kong"
            << "\n";
    std::cout
            << "\tDate:2024/2/22"
            << "\n";
    std::cout
            << std::endl;
}

int command_line(int argc, char *argv[]) {
    // Define command line arguments
    TCLAP::ValueArg<std::string> ipArg(
        "i", "ip",
        fmt::format(
            "Listen IP Address(default value: {})",
            ip_addr
        ),
        false, ip_addr,
        "IP Address"
    );

    TCLAP::ValueArg<int> portArg(
        "p", "port",
        fmt::format(
            "The port number to use(default value: {})",
            port
        ),
        false, port,
        "port_number"
    );

    std::ostringstream oss_gpu;
    oss_gpu << "Use GPU (default is "
            << (use_gpu ? "true" : "false")
            << ")";
    TCLAP::SwitchArg gpuArg(
        "g", "gpu",
        oss_gpu.str(),
        use_gpu
    );

    TCLAP::ValueArg<std::string> checkpointArg(
        "c", "checkpoint",
        fmt::format(
            "Checkpoint directory path (default: {})",
            checkpoint_path
        ),
        false, checkpoint_path,
        "Directory Path"
    );

    std::string desc_fp16 = "Use FP16 Model (default is ";
    desc_fp16 += convert_bool_to_std_string(use_fp16);
    desc_fp16 += ")";
    TCLAP::SwitchArg fp16Arg(
        "f", "fp16",
        desc_fp16,
        use_fp16
    );

    // Add to CmdLine
    TCLAP::CmdLine cmd(
        "SHMTU CAS OCR Server",
        ' ',
        SHMTU_CAS_SERVER_VERSION
    );
    cmd.add(ipArg);
    cmd.add(portArg);
    cmd.add(gpuArg);
    cmd.add(checkpointArg);
    cmd.add(fp16Arg);

    // Try to parse the command line arguments
    try {
        cmd.parse(argc, argv);
    } catch (TCLAP::ArgException &e) {
        std::cerr << "Error: " << e.error()
                << " for arg " << e.argId()
                << std::endl;
        return 1;
    }

    // Get the value parsed by each arg.
    const auto command_line_ip =
            ipArg.getValue();
    const auto command_line_port =
            portArg.getValue();
    const auto command_line_use_gpu =
            gpuArg.getValue();
    const auto command_line_checkpoint =
            checkpointArg.getValue();
    const auto command_line_use_fp16 =
            fp16Arg.getValue();

    // Set global variables
    port = command_line_port;
    if (command_line_use_gpu) use_gpu = !use_gpu;
    checkpoint_path = command_line_checkpoint;
    if (command_line_use_fp16) use_fp16 = !use_fp16;
    ip_addr = command_line_ip;

    // Print command line arguments values
    std::cout
            << "Command Line Arguments:"
            << "\n";
    std::cout
            << "\tIP Address: " << ip_addr
            << "\n";
    std::cout
            << "\tPort: " << command_line_port
            << "\n";
    std::cout
            << "\tUse GPU: "
            << (command_line_use_gpu ? "true" : "false")
            << "\n";
    std::cout
            << "\tCheckpoint Path: \n\t"
            << command_line_checkpoint
            << "\n";
    std::cout
            << "\tUse FP16: "
            << (use_fp16 ? "true" : "false")
            << "\n";
    std::cout << std::endl;

    return 0;
}

int main(const int argc, char *argv[]) {
    // Set up signal handling
    std::signal(SIGINT, signal_handler);

    print_hello();

    const auto command_ret = command_line(argc, argv);
    if (command_ret != 0) {
        return command_ret;
    }

    if (use_gpu) {
        CAS_OCR::set_model_gpu_support(use_gpu);
    }
    if (
        !CAS_OCR::init_model(
            checkpoint_path,
            (use_fp16 ? "fp16" : "fp32")
        )
    ) {
        std::cerr << "Error initializing model" << std::endl;
        return -1;
    }

    std::cout << "Load Model Success" << std::endl;

    Poco::Net::ServerSocket srv;

    try {
        const auto address =
                Poco::Net::SocketAddress(ip_addr, port);
        srv.bind(address);
        srv.listen();
        std::cout
                << "Server started, listening on "
                << address.toString()
                << std::endl;
    } catch (Poco::Exception &e) {
        std::cerr
                << "Failed to bind to port " << port
                << "\n";
        std::cout << e.displayText() << std::endl;

        CAS_OCR::release_model();

        exit(-1);
    }

    // Create a new thread to monitor client connect
    std::thread monitor_thread(monitor_in, std::ref(srv));
    // Detach the thread to execute independently in the background
    monitor_thread.detach();

    while (g_running) {
    }

    // Stop listening
    srv.close();

    // Must release model especially using GPU.
    CAS_OCR::release_model();

    return 0;
}
