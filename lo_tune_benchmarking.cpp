//
// Copyright 2010-2012,2014 Ettus Research LLC
// Copyright 2018 Ettus Research, a National Instruments Company
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <complex>
#include <iostream>
#include <csignal>
#include <thread>

namespace po = boost::program_options;

static bool stop_signal_called = false;
void sig_int_handler(int)
{
    stop_signal_called = true;
}

int UHD_SAFE_MAIN(int argc, char* argv[])
{
    uhd::set_thread_priority_safe();

    // variables to be set by po
    std::string args, file, ant, subdev, ref;
    double rate, freq, gain, bw, resolution;
    double offset = 0.000500; // figure out worst case scenario for USRPs, user doesn't need to know about this

    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
        ("freq", po::value<double>(&freq)->default_value(500000000), "rf center frequency in Hz")
        ("ant", po::value<std::string>(&ant), "antenna selection")
        ("subdev", po::value<std::string>(&subdev), "subdevice specification")
        ("resolution", po::value<double>(&resolution)->default_value(0.00001), "desired resolution (in seconds) for LO tuning time")
        ("ref", po::value<std::string>(&ref)->default_value("internal"), "reference source (internal, external, mimo)")
        ("int-n", "tune USRP with integer-N tuning")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help")) {
        std::cout << boost::format("UHD frequency hop benchmarking %s") % desc << std::endl;
        return ~0;
    }

    // create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);
    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    // Lock mboard clocks
    usrp->set_clock_source(ref);

    // always select the subdevice first, the channel mapping affects the other settings
    if (vm.count("subdev")) {
        usrp->set_rx_subdev_spec(subdev);
    }

    // set USRP time to 0.00 seconds
    std::cout << boost::format("Setting device timestamp to 0...") << std::endl;
    usrp->set_time_now(uhd::time_spec_t(0.0));

    // set the rx center frequency
    uhd::tune_request_t tune_request(freq);
    tune_request.dsp_freq_policy = uhd::tune_request_t::POLICY_NONE; //this policy removes all DSP tuning eleminating the need to balance streaming rate with hop rate
    tune_request.rf_freq_policy = uhd::tune_request_t::POLICY_MANUAL;
    tune_request.rf_freq = freq;
    if (vm.count("int-n"))
        tune_request.args = uhd::device_addr_t("mode_n=integer");
    usrp->set_rx_freq(tune_request);

    // set the antenna
    if (vm.count("ant"))
        usrp->set_rx_antenna(ant);

    std::this_thread::sleep_for(std::chrono::seconds(1)); // allow for some setup time

    // check Ref and LO Lock detect
    std::vector<std::string> sensor_names;
    sensor_names = usrp->get_rx_sensor_names(0);
    if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked")
        != sensor_names.end()) {
        uhd::sensor_value_t lo_locked = usrp->get_rx_sensor("lo_locked", 0);
        std::cout << boost::format("Checking RX: %s ...") % lo_locked.to_pp_string()
                  << std::endl;
        UHD_ASSERT_THROW(lo_locked.to_bool());
    }
    sensor_names = usrp->get_mboard_sensor_names(0);
    if ((ref == "mimo")
        and (std::find(sensor_names.begin(), sensor_names.end(), "mimo_locked")
                != sensor_names.end())) {
        uhd::sensor_value_t mimo_locked = usrp->get_mboard_sensor("mimo_locked", 0);
        std::cout << boost::format("Checking RX: %s ...") % mimo_locked.to_pp_string()
                  << std::endl;
        UHD_ASSERT_THROW(mimo_locked.to_bool());
    }
    if ((ref == "external")
        and (std::find(sensor_names.begin(), sensor_names.end(), "ref_locked")
                != sensor_names.end())) {
        uhd::sensor_value_t ref_locked = usrp->get_mboard_sensor("ref_locked", 0);
        std::cout << boost::format("Checking RX: %s ...") % ref_locked.to_pp_string()
                  << std::endl;
        UHD_ASSERT_THROW(ref_locked.to_bool());
    }

    // make sure the ctrl+c interupt is handled. Do I need this?
    std::signal(SIGINT, &sig_int_handler);

    // constants to be used in the loop below. 
    bool lock = true;
    uhd::time_spec_t tune_time;
    double last_freq = freq;
    double next_freq = 5000000000;

    while (lock) {
        
        if (stop_signal_called) {
            break;
        }

        // call for a retune from last_freq -> next_freq with a timed command
        tune_time = usrp->get_time_now() + uhd::time_spec_t(0.010);
        usrp->set_command_time(tune_time);
        tune_request.rf_freq = next_freq;
        usrp->set_rx_freq(tune_request);
        std::cout << "Setting frequency to: " << usrp->get_rx_freq() << " at "<< tune_time.get_frac_secs() << "\n";

        // query the lo_locked sensor 'offset' seconds after the retune occurs
        usrp->set_command_time(tune_time + offset);
        // usrp->clear_command_time();
        lock = usrp->get_rx_sensor("lo_locked", 0).to_bool();
        std::cout << "LO-lock status: " << lock << " after " << offset << " seconds \n";

        // reset to last_freq once the lo_locked sensor has been queried
        usrp->clear_command_time();
        tune_request.rf_freq = last_freq;
        usrp->set_rx_freq(tune_request);
        std::cout << "Resetting frequency to: " << usrp->get_rx_freq() << " at "<< usrp->get_time_now().get_frac_secs() << "\n";

        // loop to allow an unlocked LO to settle
        while (!lock) {
            lock = usrp->get_rx_sensor("lo_locked", 0).to_bool();
        };

        usleep(10);

        // handle the error codes. Does streaming have to exist to get these?
        /*switch (md.error_code) {
            case uhd::rx_metadata_t::ERROR_CODE_NONE:
                break;

            default:
                break;
        }*/

        // once we get the failure we're looking for, report the last successful lock time as the result
        if(!lock){
            std::cout << "Hop from " << last_freq << " to " << next_freq << " took " << offset + resolution << " seconds!\n";
        }

        // decrement resolution each itteration
        offset -= resolution;

        if (offset < 0){
            std::cout << "\n\nDevice did not report failure to lock LO.\n\n";
            goto done_loop;
        }

        std::cout << "\n\n"; // debug output formatting
    }   

done_loop:

    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;
    return EXIT_SUCCESS;
}
